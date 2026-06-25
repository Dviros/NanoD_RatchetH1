#pragma once

#include <Arduino.h>   // String — used by both the real class and the no-op stub

// Guard: this entire module is optional. Define WIFI_ENABLED in build flags to activate.
#ifdef WIFI_ENABLED

#include <ArduinoJson.h>

/**
 * WifiThread — FW6
 *
 * Manages WiFi station connection (from DeviceSettings), SoftAP provisioning
 * fallback, ArduinoOTA, and an AsyncWebSocket that mirrors the serial JSON API.
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
 */
class WifiThread {
public:
    void begin();
    void loop();
    void apply_settings();
    void mark_ota_valid();
    String ip();

    // Watchdog helper — call from any task to register it with the task WDT.
    static void addCurrentTask();

private:
    // Internal task bookkeeping
    static void taskEntry(void* param);
    TaskHandle_t _task_handle = nullptr;

    // Connection state machine
    void _connect();
    void _startSoftAP();
    void _stopSoftAP();
    void _setupOTA();
    void _setupWebServer();

    // Broadcast a JSON string to all connected WebSocket clients
    // (mirrors serial output so the app can connect over WiFi).
    void _wsBroadcast(const String& json);

    // WebSocket JSON command handler (same contract as com_thread serial path)
    void _handleWsCommand(const String& json, uint32_t client_id);

    bool _ota_setup_done     = false;
    bool _server_setup_done  = false;
    bool _soft_ap_active     = false;

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
};

extern WifiThread wifi_thread;

#endif // WIFI_ENABLED
