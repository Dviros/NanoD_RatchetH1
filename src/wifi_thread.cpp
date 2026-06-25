
// FW6 — WifiThread (lean raw-TCP transport; replaces ESPAsyncWebServer/AsyncTCP)
// Only compiled when WIFI_ENABLED is defined in platformio.ini build_flags.
#include "wifi_thread.h"   // declares WifiThread (real class or no-op stub) for BOTH branches

#ifdef WIFI_ENABLED

#include "DeviceSettings.h"
#include "com_thread.h"   // net_submit() + net_attach_out()
#include "net_auth.h"     // hmac_sha256, hex_encode/decode, ct_memeq, make_nonce_hex

#include <WiFi.h>         // WiFiGeneric/WiFiEvent types (arduino-esp32 2.x)
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <ArduinoOTA.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <freertos/queue.h>

// ─── global singleton ────────────────────────────────────────────────────────
WifiThread wifi_thread;

// OTA password (pulled from build flag; override in platformio.ini if desired)
#ifndef WIFI_OTA_PASSWORD
#  define WIFI_OTA_PASSWORD "nanod-ota"
#endif

// ─── FreeRTOS task entry ─────────────────────────────────────────────────────

/*static*/ void WifiThread::taskEntry(void* param) {
    WifiThread* self = static_cast<WifiThread*>(param);
    // Register this task with the task watchdog so a hang triggers a reset.
    esp_task_wdt_add(nullptr);
    while (true) {
        self->loop();
        esp_task_wdt_reset(); // feed watchdog each iteration
        // 2ms tick: keeps round-trip latency low (read client + drain out-queue
        // promptly). Low-priority task on core 0, so the extra wakeups are cheap.
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}


// ─── Public API ──────────────────────────────────────────────────────────────

void WifiThread::begin() {
    // Must be called from setup() after DeviceSettings::init() + fromSPIFFS().

    // FW6: create outbound queue (String* pointers, depth 16) and register with com_thread.
    _q_net_out = xQueueCreate(16, sizeof(String*));
    com_thread.net_attach_out(_q_net_out);

    DeviceSettings& ds = DeviceSettings::getInstance();

    if (ds.wifiEnabled && ds.wifiSsid.length() > 0) {
        WiFi.persistent(false);         // we manage persistence ourselves via SPIFFS
        WiFi.setAutoReconnect(true);
        _connect();
    } else if (ds.wifiEnabled) {
        // No credentials yet — fall straight to provisioning AP.
        _startSoftAP();
    }
    // else: wifi disabled — don't touch the radio.

    // Start internal FreeRTOS task, pinned to core 0, low priority.
    xTaskCreatePinnedToCore(
        WifiThread::taskEntry,
        "WIFI",
        6144,       // stack — OTA + TCP server; no async lib overhead anymore
        this,
        1,          // priority: lower than all real-time threads
        &_task_handle,
        0           // core 0
    );
}


void WifiThread::loop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (_soft_ap_active) {
            _stopSoftAP();
        }
        if (!_ota_setup_done) {
            _setupOTA();
        }
        if (!_tcp_server_started) {
            _setupTcpServer();
        }
        ArduinoOTA.handle();

        // Periodic reconnect backoff reset once healthy.
        _backoff_ms = BACKOFF_MIN_MS;
        _last_connect_attempt = 0;
    } else {
        // Not connected — attempt reconnect with exponential backoff.
        unsigned long now = millis();
        if (now - _last_connect_attempt >= _backoff_ms) {
            _last_connect_attempt = now;
            DeviceSettings& ds = DeviceSettings::getInstance();
            if (ds.wifiEnabled && ds.wifiSsid.length() > 0) {
                Serial.printf("[WIFI] Reconnecting to '%s' ...\n", ds.wifiSsid.c_str());
                WiFi.disconnect(false);
                WiFi.begin(ds.wifiSsid.c_str(), ds.wifiPassword.c_str());
            } else if (ds.wifiEnabled && !_soft_ap_active) {
                _startSoftAP();
            }
            // Exponential backoff capped at BACKOFF_MAX_MS.
            _backoff_ms = min(_backoff_ms == 0 ? BACKOFF_MIN_MS : _backoff_ms * 2,
                              (uint32_t)BACKOFF_MAX_MS);
        }
    }

    // ── Accept new TCP clients ──────────────────────────────────────────────
    if (_tcp_server_started) {
        WiFiClient incoming = _tcp_server.available();
        if (incoming) {
            bool placed = false;
            for (auto& slot : _slots) {
                if (!slot.client.connected()) {
                    slot.client    = incoming;
                    slot.client.setNoDelay(true); // disable Nagle — send small JSON frames immediately (low latency)
                    slot.buf       = "";
                    slot.authState = ClientSlot::AuthState::WAIT_HELLO_SENT;
                    slot.deviceNonceHex = "";
                    memset(slot.deviceNonce, 0, sizeof(slot.deviceNonce));
                    slot.authDeadline = 0;
                    placed = true;

                    Serial.printf("[WIFI] TCP client connected from %s\n",
                                  incoming.remoteIP().toString().c_str());

                    // ── Auth gate: fail-closed if no PSK is configured ──────
                    String psk = DeviceSettings::getInstance().getNetPsk();
                    if (psk.length() == 0) {
                        // No PSK configured — refuse immediately.
                        // One-time warning (use a static flag to avoid spamming).
                        static bool s_noPskWarned = false;
                        if (!s_noPskWarned) {
                            Serial.println("[WIFI] AUTH: net PSK not configured — "
                                           "all TCP connections refused. "
                                           "Set via USB: {\"settings\":{\"netPsk\":\"<key>\"}}");
                            s_noPskWarned = true;
                        }
                        slot.client.println("{\"error\":\"net auth not configured\"}");
                        slot.client.stop();
                        // Leave slot.client disconnected; the slot is free again.
                        break;
                    }

                    // ── Send hello with device nonce ────────────────────────
                    slot.deviceNonceHex = make_nonce_hex(slot.deviceNonce);
                    // {"hello":{"nonce":"<32hex>","proto":1}}
                    String helloMsg = "{\"hello\":{\"nonce\":\"" +
                                       slot.deviceNonceHex +
                                       "\",\"proto\":1}}";
                    slot.client.println(helloMsg);

                    slot.authState    = ClientSlot::AuthState::WAIT_CLIENT_AUTH;
                    slot.authDeadline = millis() + AUTH_TIMEOUT_MS;
                    break;
                }
            }
            if (!placed) {
                incoming.stop(); // no room — refuse gracefully
                Serial.println("[WIFI] TCP: max clients reached, refused connection");
            }
        }
    }

    // ── Handshake timeout enforcement ──────────────────────────────────────
    // Check before reading so a stalled client is evicted even if it sends
    // nothing (denial-of-service / slot exhaustion guard).
    {
        unsigned long now = millis();
        for (auto& slot : _slots) {
            if (!slot.client.connected()) continue;
            if (slot.authState == ClientSlot::AuthState::WAIT_CLIENT_AUTH &&
                slot.authDeadline != 0 && now > slot.authDeadline)
            {
                Serial.println("[WIFI] AUTH: handshake timeout — closing connection");
                slot.client.println("{\"auth\":{\"ok\":false}}");
                slot.client.stop();
                // slot fields reset on next accept
            }
        }
    }

    // ── Read inbound data from connected clients ────────────────────────────
    for (auto& slot : _slots) {
        if (!slot.client.connected()) continue;

        while (slot.client.available()) {
            char c = static_cast<char>(slot.client.read());
            if (c == '\n' || c == '\r') {
                if (slot.buf.length() == 0) continue;

                // ── Line too long: drop regardless of auth state ──────────
                if (slot.buf.length() > MAX_LINE_BYTES) {
                    Serial.printf("[WIFI] TCP: line too long (%u bytes), dropped\n",
                                  (unsigned)slot.buf.length());
                    slot.buf = "";
                    continue;
                }

                String line = slot.buf;
                slot.buf = "";

                if (slot.authState == ClientSlot::AuthState::AUTHED) {
                    // Normal path — forward to com_thread
                    com_thread.net_submit(new String(line));

                } else if (slot.authState == ClientSlot::AuthState::WAIT_CLIENT_AUTH) {
                    // ── Auth message expected: {"auth":{"hmac":"<hex>","nonce":"<hex>"}} ──
                    // Parse with ArduinoJson (stack-allocated, small doc).
                    // We only accept the "auth" key; anything else is silently dropped
                    // (no data forwarded, no response — starve unknown frames).
                    JsonDocument authDoc;
                    DeserializationError err = deserializeJson(authDoc, line);
                    if (err || !authDoc["auth"].is<JsonObject>()) {
                        // Not a valid auth frame — drop silently, keep waiting
                        Serial.println("[WIFI] AUTH: non-auth frame dropped (not authed)");
                        continue;
                    }

                    JsonObject authObj  = authDoc["auth"].as<JsonObject>();
                    const char* hmacHex  = authObj["hmac"]  | "";
                    const char* cnHex    = authObj["nonce"]  | "";

                    // Both fields must be present
                    if (strlen(hmacHex) == 0 || strlen(cnHex) == 0) {
                        Serial.println("[WIFI] AUTH: malformed auth frame — closing");
                        slot.client.println("{\"auth\":{\"ok\":false}}");
                        slot.client.stop();
                        continue;
                    }

                    // Decode client-provided HMAC (must be exactly 32 bytes = 64 hex chars)
                    uint8_t clientHmac[32];
                    if (!hex_decode(String(hmacHex), clientHmac, 32)) {
                        Serial.println("[WIFI] AUTH: bad hmac hex — closing");
                        slot.client.println("{\"auth\":{\"ok\":false}}");
                        slot.client.stop();
                        continue;
                    }

                    // Compute expected HMAC: HMAC-SHA256(psk, device_nonce_bytes)
                    String psk = DeviceSettings::getInstance().getNetPsk();
                    uint8_t expected[32];
                    int rc = hmac_sha256(
                        reinterpret_cast<const uint8_t*>(psk.c_str()), psk.length(),
                        slot.deviceNonce, sizeof(slot.deviceNonce),
                        expected);
                    if (rc != 0) {
                        Serial.printf("[WIFI] AUTH: HMAC computation failed (%d) — closing\n", rc);
                        slot.client.println("{\"auth\":{\"ok\":false}}");
                        slot.client.stop();
                        continue;
                    }

                    // Constant-time compare to avoid timing oracle on the HMAC value
                    if (!ct_memeq(clientHmac, expected, 32)) {
                        Serial.println("[WIFI] AUTH: HMAC mismatch — closing");
                        slot.client.println("{\"auth\":{\"ok\":false}}");
                        slot.client.stop();
                        continue;
                    }

                    // ── HMAC verified: compute device proof and send ────────
                    // Device proof = HMAC-SHA256(psk, client_nonce_bytes)
                    // This proves the device holds the same PSK → mutual auth.
                    uint8_t clientNonce[16];
                    if (!hex_decode(String(cnHex), clientNonce, 16)) {
                        // Client nonce hex must be exactly 32 chars (16 bytes).
                        // If not, the handshake is malformed; close to be safe.
                        Serial.println("[WIFI] AUTH: bad client nonce hex — closing");
                        slot.client.println("{\"auth\":{\"ok\":false}}");
                        slot.client.stop();
                        continue;
                    }

                    uint8_t deviceProof[32];
                    rc = hmac_sha256(
                        reinterpret_cast<const uint8_t*>(psk.c_str()), psk.length(),
                        clientNonce, sizeof(clientNonce),
                        deviceProof);
                    if (rc != 0) {
                        Serial.printf("[WIFI] AUTH: device proof HMAC failed (%d) — closing\n", rc);
                        slot.client.println("{\"auth\":{\"ok\":false}}");
                        slot.client.stop();
                        continue;
                    }

                    String proofHex = hex_encode(deviceProof, sizeof(deviceProof));
                    String authOk = "{\"auth\":{\"ok\":true,\"hmac\":\"" + proofHex + "\"}}";
                    slot.client.println(authOk);

                    slot.authState = ClientSlot::AuthState::AUTHED;
                    slot.authDeadline = 0; // disarm timeout
                    Serial.printf("[WIFI] AUTH: client authenticated from %s\n",
                                  slot.client.remoteIP().toString().c_str());

                } else {
                    // WAIT_HELLO_SENT — should not have data before hello is sent;
                    // this is a transient state resolved within the same loop tick.
                    // Drop silently.
                }
            } else {
                if (slot.buf.length() < MAX_LINE_BYTES) {
                    slot.buf += c;
                } else {
                    // Already overlong — keep consuming until newline to re-sync
                    slot.buf += c;
                }
            }
        }
    }

    // ── Drain outbound queue → AUTHED clients only ─────────────────────────
    // com_thread.emit() enqueues String* here; we own the pointer after dequeue.
    // SECURITY: frames are broadcast ONLY to slots in the AUTHED state.
    // Unauthenticated sockets receive ZERO bytes from the out-queue.
    if (_q_net_out != nullptr) {
        String* frame = nullptr;
        while (xQueueReceive(_q_net_out, &frame, 0) == pdTRUE && frame != nullptr) {
            for (auto& slot : _slots) {
                if (slot.client.connected() &&
                    slot.authState == ClientSlot::AuthState::AUTHED)
                {
                    slot.client.print(*frame);
                    if (!frame->endsWith("\n")) slot.client.print('\n');
                }
            }
            delete frame;
            frame = nullptr;
        }
    }
}


void WifiThread::apply_settings() {
    // Re-read DeviceSettings and (re)connect. Safe to call from com_thread.
    DeviceSettings& ds = DeviceSettings::getInstance();
    if (!ds.wifiEnabled) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        _stopSoftAP();
        return;
    }
    if (ds.wifiSsid.length() == 0) {
        _startSoftAP();
        return;
    }
    // Force a fresh connect cycle.
    _backoff_ms = 0;
    _last_connect_attempt = 0;
    _connect();
}


void WifiThread::mark_ota_valid() {
    // Call ONLY after core threads + (optionally) connectivity are healthy.
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        Serial.println("[WIFI] OTA rollback cancelled — firmware marked valid.");
    } else if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
        Serial.println("[WIFI] WARNING: OTA validate failed — already rolled back?");
    }
    // ESP_ERR_INVALID_ARG means no OTA partition scheme; safe to ignore.
}


String WifiThread::ip() {
    if (WiFi.status() == WL_CONNECTED)
        return WiFi.localIP().toString();
    if (_soft_ap_active)
        return WiFi.softAPIP().toString();
    return "";
}


/*static*/ void WifiThread::addCurrentTask() {
    esp_task_wdt_add(nullptr);
}


// ─── Private helpers ─────────────────────────────────────────────────────────

void WifiThread::_connect() {
    DeviceSettings& ds = DeviceSettings::getInstance();
    Serial.printf("[WIFI] Connecting to '%s' ...\n", ds.wifiSsid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ds.wifiSsid.c_str(), ds.wifiPassword.c_str());
    WiFi.setAutoReconnect(true);
    // Non-blocking — result polled in loop() via WL_CONNECTED check.
}


void WifiThread::_startSoftAP() {
    if (_soft_ap_active) return;
    Serial.printf("[WIFI] Starting SoftAP '%s' — connect and send JSON to port %u\n",
                  SOFTAP_SSID, TCP_PORT);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(SOFTAP_SSID); // open AP — user connects, then uses TCP port

    // Start TCP server so provisioning commands can arrive over TCP even in AP mode.
    if (!_tcp_server_started) {
        _tcp_server.begin();
        _tcp_server_started = true;
        Serial.printf("[WIFI] TCP server started on port %u (SoftAP mode)\n", TCP_PORT);
    }
    _soft_ap_active = true;
}


void WifiThread::_stopSoftAP() {
    if (!_soft_ap_active) return;
    WiFi.softAPdisconnect(true);
    _soft_ap_active = false;
    Serial.println("[WIFI] SoftAP stopped");
}


void WifiThread::_setupOTA() {
    ArduinoOTA.setPassword(WIFI_OTA_PASSWORD);
    ArduinoOTA.setHostname(DeviceSettings::getInstance().deviceName.c_str());

    ArduinoOTA.onStart([]() {
        Serial.println("[OTA] Start");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("[OTA] End — resetting");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA] %u%%\r", progress * 100 / total);
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA] Error[%u]\n", error);
    });

    ArduinoOTA.begin();
    _ota_setup_done = true;
    Serial.println("[WIFI] ArduinoOTA ready");
}


void WifiThread::_setupTcpServer() {
    _tcp_server.begin();
    _tcp_server_started = true;
    Serial.printf("[WIFI] TCP JSON server ready on %s:%u\n", ip().c_str(), TCP_PORT);
}


#else // !WIFI_ENABLED -------------------------------------------------------

// Stub instance so the `wifi_thread` symbol exists in no-WiFi builds.
WifiThread wifi_thread;

#endif // WIFI_ENABLED
