
#pragma once

#include <ArduinoJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

typedef struct {
    bool in = true;
    bool out = true;
    bool thru = false;
    bool route = false;
    bool nano = true;
} midiSettings;


typedef struct {
    int8_t direction;
    float zero_angle;
} MotorCalibration;



typedef struct {
    uint8_t ledMaxBrightness;
    uint16_t deviceOrientation;
    midiSettings midiUsb;
    midiSettings midi2;
    uint8_t midi_sysex_id;
} HmiDeviceSettings;



/**
 * Device settings — thread-safe singleton.
 *
 * All public getters/setters acquire the internal recursive mutex so callers
 * from FOC, COM, and WiFi threads are safe.  The legacy public fields are
 * still accessible for code that was written before the mutex was added, but
 * new code should prefer the accessor methods.
 *
 * Persistence uses an atomic write-then-rename pattern to avoid truncation
 * corruption: data is written to a .tmp file first, then renamed over the
 * real file only on success.
 *
 * A CRC32 field guards the persisted JSON; a schema version field allows
 * future forward-migration.  On CRC mismatch / parse failure the code falls
 * back to safe defaults and does NOT overwrite the corrupt file.
 *
 * WiFi credentials are stored in NVS Preferences (separate namespace) and
 * are NEVER emitted in the serial settings response.
 */
class DeviceSettings {
    friend class HmiThread;
    friend class ComThread;
public:
    static DeviceSettings& getInstance();

    // -----------------------------------------------------------------------
    // Mutex helpers — callers may wrap multi-field operations
    // -----------------------------------------------------------------------
    bool take(TickType_t wait = portMAX_DELAY);
    void give();

    // -----------------------------------------------------------------------
    // Serialisation / persistence
    // -----------------------------------------------------------------------
    DeviceSettings& operator=(JsonObject& obj);

    /**
     * Serialise to JSON.  Password is ALWAYS redacted — never emitted over
     * serial.  Pass redactWifi=false only in internal code that needs the
     * real value (e.g. wifi_thread reading credentials at connect time).
     */
    void toJSON(JsonObject& obj, bool redactWifi = true);

    /**
     * Atomic save: write to tmp file, flush, rename.  Thread-safe.
     * Returns true on success.
     */
    bool saveAtomic();

    /** Legacy name — delegates to saveAtomic(). */
    bool toSPIFFS() { return saveAtomic(); }

    /**
     * Load from filesystem.  On CRC/parse failure falls back to defaults
     * and does NOT overwrite the bad file.
     */
    bool fromSPIFFS();

    // -----------------------------------------------------------------------
    // NVS helpers (all guarded by _nvsMutex)
    // -----------------------------------------------------------------------
    void storeCurrentProfile(String profile);
    String loadCurrentProfile();
    MotorCalibration loadCalibration();
    void storeCalibration(MotorCalibration& cal);

    // -----------------------------------------------------------------------
    // WiFi credential accessors (stored in NVS, never serialised plaintext)
    // -----------------------------------------------------------------------
    String getWifiSsid();
    void   setWifiSsid(const String& v);
    String getWifiPassword();   // returns plaintext — for wifi_thread only
    void   setWifiPassword(const String& v);
    bool   getWifiEnabled();
    void   setWifiEnabled(bool v);

    // -----------------------------------------------------------------------
    // Network PSK — pre-shared key for TCP mutual auth handshake.
    // Stored in NVS alongside WiFi creds.  NEVER emitted over any transport
    // (redacted as "***" in toJSON()).  Set ONLY via the USB serial settings
    // command: {"settings":{"netPsk":"<key>"}}.
    // getNetPsk() returns plaintext for internal auth use only.
    // -----------------------------------------------------------------------
    String getNetPsk();
    void   setNetPsk(const String& v);

    // -----------------------------------------------------------------------
    // PD voltage accessor (clamped to [5.0, 9.0] V, board-safe)
    // -----------------------------------------------------------------------
    float  getPdVoltage();
    void   setPdVoltage(float v);

    // -----------------------------------------------------------------------
    // Active sprite name
    // -----------------------------------------------------------------------
    String getActiveSprite();
    void   setActiveSprite(const String& v);

    // -----------------------------------------------------------------------
    // Factory reset — wipes settings + profiles to safe defaults.
    // Triggered by FW4/integrator on button-combo / brick-recovery path.
    // -----------------------------------------------------------------------
    void factoryReset();

    // -----------------------------------------------------------------------
    // Init — mount FS, open Preferences.  Call once from setup().
    // -----------------------------------------------------------------------
    bool init();

    // -----------------------------------------------------------------------
    // Public fields (legacy — new code should use accessors where available)
    // -----------------------------------------------------------------------
    bool dirty;

    bool debug;
    uint8_t ledMaxBrightness;
    float maxVelocity;
    float maxVoltage;
    String deviceName;
    uint16_t deviceOrientation;
    midiSettings midiUsb;
    midiSettings midi2;
    uint8_t midi_sysex_id;
    uint32_t idleTimeout;

    // Contract fields — also accessible via get/set above
    String wifiSsid;
    String wifiPassword;
    bool wifiEnabled;
    float pdVoltage;      // [5.0, 9.0] V
    String activeSprite;

    // Transient music-profile overlay state — LCD thread reads, com thread writes.
    // Plain 32-bit fields: aligned loads/stores are atomic on the ESP32, and a
    // one-frame stale read would only blip a cosmetic overlay. No mutex needed.
    uint32_t musicColor   = 0x08596C;   // album color for volume/seek arcs (0xRRGGBB)
    int32_t  seekPermille = -1;         // song progress 0..1000; -1 = hide seek arc

    // Network PSK — internal only; never serialise plaintext; use getNetPsk()
    String netPsk;

    // read-only device identity
    String serialNumber;
    String firmwareVersion;

protected:
    DeviceSettings();
    ~DeviceSettings();

    static DeviceSettings instance;

    // Recursive mutex — used for both field access and Preferences access
    SemaphoreHandle_t _mutex;

    // Separate mutex for NVS Preferences (also recursive, shared with _mutex)
    SemaphoreHandle_t _nvsMutex;

    // Internal helpers
    uint32_t _computeCrc(const String& json) const;
    bool     _loadFromJson(const String& json);
    void     _applyDefaults();

    // Load/save WiFi creds from NVS (separate from JSON file)
    void _loadWifiFromNvs();
    void _saveWifiToNvs();
};
