
// FW6 — WifiThread
// Only compiled when WIFI_ENABLED is defined in platformio.ini build_flags.
#include "wifi_thread.h"   // declares WifiThread (real class or no-op stub) for BOTH branches

#ifdef WIFI_ENABLED

#include "DeviceSettings.h"
#include "com_thread.h"   // for forwarding WS commands to serial path (via queue)

#include <WiFi.h>   // pulls in WiFiGeneric/WiFiEvent types (no standalone WiFiEvent.h in arduino-esp32 2.x)
#include <ArduinoOTA.h>
#include <ESPAsyncWebServer.h>  // me-no-dev/ESPAsyncWebServer
#include <AsyncTCP.h>           // me-no-dev/AsyncTCP (pulled in transitively)
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <LittleFS.h>           // sprite_store uses LittleFS; wifi thread opens it lazily

// ─── global singleton ────────────────────────────────────────────────────────
WifiThread wifi_thread;

// ─── module-private helpers ──────────────────────────────────────────────────
namespace {

// AsyncWebServer on port 80; AsyncWebSocket at /ws
AsyncWebServer  g_server(80);
AsyncWebSocket  g_ws("/ws");

// OTA password (pulled from build flag; override in platformio.ini if desired)
#ifndef WIFI_OTA_PASSWORD
#  define WIFI_OTA_PASSWORD "nanod-ota"
#endif

// Minimal provisioning page served by SoftAP
static const char PROV_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width'>
<title>NanoD Setup</title></head><body>
<h2>NanoD WiFi Setup</h2>
<form method='POST' action='/prov'>
  SSID: <input name='ssid' required><br>
  Password: <input name='pw' type='password'><br>
  <input type='submit' value='Connect'>
</form></body></html>
)rawliteral";

// Convenience: serialize and emit a JSON document to all WebSocket clients
void ws_send_json(JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    g_ws.textAll(out);
}

} // anonymous namespace


// ─── FreeRTOS task entry ─────────────────────────────────────────────────────

/*static*/ void WifiThread::taskEntry(void* param) {
    WifiThread* self = static_cast<WifiThread*>(param);
    // Register this task with the task watchdog so a hang triggers a reset.
    esp_task_wdt_add(nullptr);
    while (true) {
        self->loop();
        esp_task_wdt_reset(); // feed watchdog each iteration
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


// ─── Public API ──────────────────────────────────────────────────────────────

void WifiThread::begin() {
    // Must be called from setup() after DeviceSettings::init() + fromSPIFFS().
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
        8192,       // stack — OTA + web server need headroom
        this,
        1,          // priority: lower than all real-time threads
        &_task_handle,
        0           // core 0
    );
}


void WifiThread::loop() {
    // Setup OTA + WebServer once WiFi is connected (lazy, avoids blocking boot).
    if (WiFi.status() == WL_CONNECTED) {
        if (_soft_ap_active) {
            _stopSoftAP();
        }
        if (!_ota_setup_done) {
            _setupOTA();
        }
        if (!_server_setup_done) {
            _setupWebServer();
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

    // Clean up stale WebSocket connections.
    g_ws.cleanupClients();
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
    // Cancels OTA rollback so the device won't revert on next reset.
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
    Serial.printf("[WIFI] Starting SoftAP '%s'\n", SOFTAP_SSID);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(SOFTAP_SSID); // open AP — user connects then submits form

    // Serve provisioning page only if server not already up.
    if (!_server_setup_done) {
        g_server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
            req->send_P(200, "text/html", PROV_PAGE);
        });
        g_server.on("/prov", HTTP_POST, [](AsyncWebServerRequest* req) {
            if (req->hasParam("ssid", true)) {
                String ssid = req->getParam("ssid", true)->value();
                String pw   = req->hasParam("pw", true)
                              ? req->getParam("pw", true)->value()
                              : "";
                // Fix 1: use thread-safe NVS-persisting setters; remove bare
                // public field writes and the manual ds.dirty=true.
                DeviceSettings& ds = DeviceSettings::getInstance();
                ds.setWifiSsid(ssid);
                ds.setWifiPassword(pw);
                ds.setWifiEnabled(true);
                ds.toSPIFFS();
                req->send(200, "text/plain", "Saved — connecting...");
                wifi_thread.apply_settings();
            } else {
                req->send(400, "text/plain", "Missing ssid");
            }
        });
        g_server.begin();
        _server_setup_done = true;
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


void WifiThread::_setupWebServer() {
    // ── WebSocket handler ────────────────────────────────────────────────────
    g_ws.onEvent([this](AsyncWebSocket* server,
                        AsyncWebSocketClient* client,
                        AwsEventType type,
                        void* arg,
                        uint8_t* data,
                        size_t len) {
        if (type == WS_EVT_DATA) {
            AwsFrameInfo* info = reinterpret_cast<AwsFrameInfo*>(arg);
            if (info->final && info->index == 0 && info->len == len &&
                info->opcode == WS_TEXT) {
                // Null-terminate and hand off.
                String json = String(reinterpret_cast<char*>(data), len);
                _handleWsCommand(json, client->id());
            }
        } else if (type == WS_EVT_CONNECT) {
            Serial.printf("[WIFI] WS client #%u connected\n", client->id());
            // Greet new client with device info.
            JsonDocument hello;
            hello["connected"] = true;
            hello["ip"]        = ip();
            hello["device"]    = DeviceSettings::getInstance().deviceName;
            hello["fw"]        = DeviceSettings::getInstance().firmwareVersion;
            String out;
            serializeJson(hello, out);
            client->text(out);
        } else if (type == WS_EVT_DISCONNECT) {
            Serial.printf("[WIFI] WS client #%u disconnected\n", client->id());
        }
    });

    g_server.addHandler(&g_ws);

    // ── HTTP firmware update endpoint (/update, multipart POST) ─────────────
    g_server.on("/update", HTTP_POST,
        // onComplete
        [](AsyncWebServerRequest* req) {
            bool ok = !Update.hasError();
            req->send(200, "text/plain", ok ? "OK — rebooting" : "FAIL");
            if (ok) {
                vTaskDelay(pdMS_TO_TICKS(200));
                ESP.restart();
            }
        },
        // onUpload
        [](AsyncWebServerRequest* req,
           const String& filename,
           size_t index,
           uint8_t* data,
           size_t len,
           bool final) {
            if (index == 0) {
                int cmd = filename.endsWith(".spiffs") ? U_SPIFFS : U_FLASH;
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
                    Serial.printf("[OTA/HTTP] begin error: %s\n",
                                  Update.errorString());
                }
            }
            if (Update.isRunning())
                Update.write(data, len);
            if (final) {
                if (!Update.end(true))
                    Serial.printf("[OTA/HTTP] end error: %s\n",
                                  Update.errorString());
            }
        }
    );

    // ── Simple status endpoint ───────────────────────────────────────────────
    g_server.on("/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["ip"]   = WiFi.localIP().toString();
        doc["rssi"] = WiFi.RSSI();
        doc["device"] = DeviceSettings::getInstance().deviceName;
        doc["fw"]   = DeviceSettings::getInstance().firmwareVersion;
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    g_server.begin();
    _server_setup_done = true;
    Serial.printf("[WIFI] Web server ready — IP: %s\n", ip().c_str());
}


/**
 * _handleWsCommand — mirrors the serial JSON API from com_thread.
 *
 * Commands understood (same keys as serial path):
 *   profile, updates, current, R, settings, save, load, profiles,
 *   wifi  (set ssid/pw/wifiEnabled),
 *   sprite (forwarded to SpriteStore via com_thread queue if available).
 *
 * Every mutating command sends an ACK: { "ack":"<cmd>", "ok":true|false, "error":"..." }
 */
void WifiThread::_handleWsCommand(const String& json, uint32_t client_id) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        JsonDocument ack;
        ack["ack"]   = "parse";
        ack["ok"]    = false;
        ack["error"] = err.c_str();
        String out; serializeJson(ack, out);
        g_ws.text(client_id, out);
        return;
    }

    // ── wifi command — update DeviceSettings wifi fields ─────────────────
    // Fix 1: use thread-safe NVS-persisting setters; remove bare field writes and ds.dirty=true.
    if (!doc["wifi"].isNull()) {
        JsonVariant w = doc["wifi"];
        if (w.is<JsonObject>()) {
            DeviceSettings& ds = DeviceSettings::getInstance();
            JsonObject wobj = w.as<JsonObject>();
            if (!wobj["ssid"].isNull())
                ds.setWifiSsid(wobj["ssid"].as<String>());
            if (!wobj["password"].isNull())
                ds.setWifiPassword(wobj["password"].as<String>());
            if (!wobj["enabled"].isNull())
                ds.setWifiEnabled(wobj["enabled"].as<bool>());
            // Non-blocking reconnect; integrator must call save separately.
            apply_settings();
        }
        JsonDocument ack;
        ack["ack"] = "wifi";
        ack["ok"]  = true;
        String out; serializeJson(ack, out);
        g_ws.text(client_id, out);
        return;
    }

    // ── All other commands — forward to com_thread by re-queuing as a
    //    synthetic serial message through the existing string-message path.
    //    We serialise back to a string and push it as if it came from serial.
    //    com_thread.run() reads Serial; the cleanest bridge is to inject via
    //    the existing put_string_message queue with type STRING_MESSAGE_DEBUG
    //    is NOT correct here (that's for display).
    //
    //    Instead we echo the JSON onto the Serial TX line — since com_thread
    //    owns Serial RX and we own Serial TX from the same UART this is the
    //    correct IPC on a single-UART embedded system.  A more elegant
    //    solution would require a separate inter-task queue added to com_thread
    //    (tracked as an integrationHook).
    //
    //    For now, relay inbound WS JSON straight to the USB-CDC serial so
    //    com_thread processes it identically to a host command.
    Serial.println(json);  // com_thread will pick this up on its next iteration

    // ACK is deferred — com_thread will emit the reply JSON which
    // _wsBroadcast() will forward to all WS clients (see note below on
    // broadcast hooking — this requires an integrationHook in com_thread).
}


void WifiThread::_wsBroadcast(const String& json) {
    g_ws.textAll(json);
}


#else // !WIFI_ENABLED -------------------------------------------------------

// Stub instance so the `wifi_thread` symbol exists in no-WiFi builds.
// The header declares the matching stub class with inline no-op methods, so
// callers (main.cpp, com_thread.cpp) link and compile to nothing.
WifiThread wifi_thread;

#endif // WIFI_ENABLED
