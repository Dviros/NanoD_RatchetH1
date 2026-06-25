#pragma once

#include <Arduino.h>   // String — used by both the real class and the no-op stub

// Guard: this entire module is optional. Define WIFI_ENABLED in build flags to activate.
#ifdef WIFI_ENABLED

#include <WiFiServer.h>
#include <WiFiClient.h>
#include <freertos/queue.h>

/**
 * WifiThread — FW6 (lean TCP transport)
 *
 * Manages WiFi station connection (from DeviceSettings), SoftAP provisioning
 * fallback (credentials accepted over TCP on port 3333), ArduinoOTA, and a
 * raw WiFiServer on TCP_PORT that mirrors the serial JSON API.
 *
 * CONTRACT (shared interface — do NOT rename):
 *   void begin()            — call once from setup(), after DeviceSettings is ready.
 *   void loop()             — called by the internal FreeRTOS task; also exposes OTA handle.
 *   void apply_settings()   — re-read DeviceSettings wifi fields and (re)connect.
 *   void mark_ota_valid()   — call after core threads are confirmed healthy (OTA rollback guard).
 *   String ip()             — returns current IPv4 string, "" if disconnected.
 *
 * The FreeRTOS task entry is wifi_thread_task() (static, pinned core 0, priority 1).
 * Integrator starts it by calling wifi_thread.begin() which also creates the task.
 *
 * TCP transport design:
 *   - WiFiServer listens on TCP_PORT (3333).
 *   - Accepts up to MAX_TCP_CLIENTS clients; reads newline-delimited JSON lines.
 *   - Lines > MAX_LINE_BYTES are silently dropped (buffer-overflow guard).
 *   - Inbound lines → com_thread.net_submit(new String(line)) [queued, not parsed here].
 *   - Outbound frames arrive via _q_net_out (String* heap ptrs written by com_thread.emit()).
 *     wifi_thread drains that queue and client.println()s each frame, then deletes the ptr.
 *   - No cross-task socket or parser access anywhere.
 */
class WifiThread {
public:
    void begin();
    void loop();
    void apply_settings();
    void mark_ota_valid();
    String ip();

    // Returns a JSON status object for the {"net":"?"} diagnostic command.
    // {"net":{"rssi":<dBm>,"ip":"<ip>","ps":"NONE|MIN|MAX","heap":<bytes>,"uptime":<ms>,"clients":<n>}}
    String netStatusJson();

    // Watchdog helper — call from any task to register it with the task WDT.
    static void addCurrentTask();

    // TCP port for the raw JSON API
    static constexpr uint16_t TCP_PORT      = 3333;
    // Max clients held open simultaneously
    static constexpr uint8_t  MAX_TCP_CLIENTS = 4;
    // Max inbound line length (bytes); longer lines are dropped
    static constexpr size_t   MAX_LINE_BYTES  = 2048;
    // Handshake must complete within this window or the connection is closed
    static constexpr uint32_t AUTH_TIMEOUT_MS = 5000;

private:
    // Internal task bookkeeping
    static void taskEntry(void* param);
    TaskHandle_t _task_handle = nullptr;

    // Connection state machine
    void _connect();
    void _startSoftAP();
    void _stopSoftAP();
    void _setupOTA();
    void _setupTcpServer();

    // Per-client state: accumulated partial line + HMAC-SHA256 auth state machine.
    //
    // Auth flow (per connection):
    //   1. New accept: if PSK is empty → fail-closed (send error, close).
    //      Else: generate device_nonce, send {"hello":{"nonce":"<hex>","proto":1}},
    //            set state = WAIT_CLIENT_AUTH, record auth_deadline.
    //   2. While WAIT_CLIENT_AUTH: only {"auth":{"hmac":"<hex>","nonce":"<clientHex>"}}
    //      is accepted.  Anything else is silently dropped (no forwarding to com_thread).
    //      Verify hmac == HMAC-SHA256(psk, device_nonce_bytes).
    //      On match: send {"auth":{"ok":true,"hmac":"<deviceProofHex>"}} (device proof
    //                = HMAC-SHA256(psk, client_nonce_bytes)), set state = AUTHED.
    //      On mismatch or timeout: send {"auth":{"ok":false}}, close socket.
    //   3. AUTHED: normal forwarding to com_thread; out-queue frames broadcast only to
    //              AUTHED slots (no bytes leak to unauthenticated sockets).
    //
    // Nonces are single-use per-connection; slot reset on disconnect clears them.
    struct ClientSlot {
        enum class AuthState : uint8_t {
            WAIT_HELLO_SENT,   // device has not sent hello yet (transient, same loop tick)
            WAIT_CLIENT_AUTH,  // hello sent, waiting for client auth message
            AUTHED             // handshake complete; normal data flow active
        };

        WiFiClient client;
        String     buf;           // partial line accumulator

        // Auth state machine fields
        AuthState  authState     = AuthState::WAIT_HELLO_SENT;
        uint8_t    deviceNonce[16];  // raw bytes of the device nonce (sent as hex)
        String     deviceNonceHex;   // cached hex string sent to client
        unsigned long authDeadline = 0; // millis() deadline for handshake (0 = not started)
    };

    ClientSlot _slots[MAX_TCP_CLIENTS];

    // FW6: raw TCP server (replaces AsyncWebServer + g_ws)
    WiFiServer _tcp_server{TCP_PORT};
    bool _tcp_server_started = false;

    // FW6: outbound queue registered with com_thread.
    // Items are String* allocated by com_thread.emit(); we delete after sending.
    QueueHandle_t _q_net_out = nullptr;

    bool _ota_setup_done    = false;
    bool _soft_ap_active    = false;

    // Reconnect backoff (ms)
    unsigned long _last_connect_attempt = 0;
    uint32_t      _backoff_ms           = 0;
    static constexpr uint32_t BACKOFF_MIN_MS  = 5000;
    static constexpr uint32_t BACKOFF_MAX_MS  = 60000;

    // SoftAP provisioning SSID
    static constexpr const char* SOFTAP_SSID = "NanoD-Setup";
};

extern WifiThread wifi_thread;

#else // !WIFI_ENABLED -------------------------------------------------------

// Stub — lets other TUs include this header without a WiFi build.
class WifiThread {
public:
    inline void begin()          {}
    inline void loop()           {}
    inline void apply_settings() {}
    inline void mark_ota_valid() {}
    inline String ip()           { return ""; }
    static inline void addCurrentTask() {}

    // No-WiFi stub: caller gets a minimal object so it can still emit the frame.
    inline String netStatusJson() {
        return "{\"net\":{\"enabled\":false}}";
    }
};

extern WifiThread wifi_thread;

#endif // WIFI_ENABLED
