
#include "./DeviceSettings.h"
#include <Arduino.h>
#include "nanofoc_d.h"
#include <Preferences.h>
#include <common/foc_utils.h>

// Use LittleFS for atomic rename support.
// SPIFFS does not implement rename() reliably on ESP32 Arduino SDK;
// LittleFS does.  The integrator must add LittleFS to lib_deps and
// set BOARD_HAS_PSRAM / partition scheme accordingly.
// See integrationHooks in the FW5 manifest for details.
#include <LittleFS.h>
#define FS_HANDLE LittleFS

#define DEVICE_SETTINGS_FILE     "/device_settings.json"
#define DEVICE_SETTINGS_TMP      "/device_settings.tmp"
#define SCHEMA_VERSION           2          // bump when persisted shape changes
#define SETTINGS_NVS_NAMESPACE   "nano_D"
#define WIFI_NVS_NAMESPACE       "nano_wifi" // separate NS so creds never appear in main JSON file

// Firmware version string from platform header
#ifndef NANO_FIRMWARE_VERSION
#  define NANO_FIRMWARE_VERSION "unknown"
#endif

// -----------------------------------------------------------------------
// CRC-32 (PKZIP/ISO-3309 table-based, no external lib needed)
// -----------------------------------------------------------------------
static const uint32_t CRC32_TABLE[256] = {
#define P(x) x
    P(0x00000000), P(0x77073096), P(0xEE0E612C), P(0x990951BA),
    P(0x076DC419), P(0x706AF48F), P(0xE963A535), P(0x9E6495A3),
    P(0x0EDB8832), P(0x79DCB8A4), P(0xE0D5E91B), P(0x97D2D988),
    P(0x09B64C2B), P(0x7EB17CBF), P(0xE7B82D08), P(0x90BF1D9C),
    P(0x1DB71064), P(0x6AB020F2), P(0xF3B97148), P(0x84BE41DE),
    P(0x1ADAD47D), P(0x6DDDE4EB), P(0xF4D4B551), P(0x83D385C7),
    P(0x136C9856), P(0x646BA8C0), P(0xFD62F97A), P(0x8A65C9EC),
    P(0x14015C4F), P(0x63066CD9), P(0xFA0F3D63), P(0x8D080DF5),
    P(0x3B6E20C8), P(0x4C69105E), P(0xD56041E4), P(0xA2677172),
    P(0x3C03E4D1), P(0x4B04D447), P(0xD20D85FD), P(0xA50AB56B),
    P(0x35B5A8FA), P(0x42B2986C), P(0xDBBBC9D6), P(0xACBCB9A0),
    P(0x32D86CE3), P(0x45DF5C75), P(0xDCD60DCF), P(0xABD13D59),
    P(0x26D930AC), P(0x51DE003A), P(0xC8D75180), P(0xBFD06116),
    P(0x21B4F927), P(0x56B3C9B1), P(0xCFBA9860), P(0xB8BDA50F),
    P(0x2802B89E), P(0x5F058808), P(0xC60CD9B2), P(0xB10BE924),
    P(0x2F6F7C87), P(0x58684C11), P(0xC1611DAB), P(0xB6662D3D),
    P(0x76DC4190), P(0x01DB7106), P(0x98D220BC), P(0xEFD5102A),
    P(0x71B18589), P(0x06B6B51F), P(0x9FBFE4A5), P(0xE8B8D433),
    P(0x7807C9A2), P(0x0F00F934), P(0x9609A88E), P(0xE10E9818),
    P(0x7F6AD2BB), P(0x086D3D2D), P(0x91646C97), P(0xE6635C01),
    P(0x6B6B51F4), P(0x1C6C6162), P(0x856530D8), P(0xF262004E),
    P(0x6C0695ED), P(0x1B01A57B), P(0x8208F4C1), P(0xF50FC457),
    P(0x65B0D9C6), P(0x12B7E950), P(0x8BBEB8EA), P(0xFCB9887C),
    P(0x62DD1D7F), P(0x15DA2D49), P(0x8CD37CF3), P(0xFBD44C65),
    P(0x4DB26158), P(0x3AB551CE), P(0xA3BC0074), P(0xD4BB30E2),
    P(0x4ADFA541), P(0x3DD895D7), P(0xA4D1C46D), P(0xD3D6F4FB),
    P(0x4369E96A), P(0x346ED9FC), P(0xAD678846), P(0xDA60B8D0),
    P(0x44042D73), P(0x33031DE5), P(0xAA0A4C5F), P(0xDD0D7CC9),
    P(0x5005713C), P(0x270241AA), P(0xBE0B1010), P(0xC90C2086),
    P(0x5768B525), P(0x206F85B3), P(0xB966D409), P(0xCE61E49F),
    P(0x5EDEF90E), P(0x29D9C998), P(0xB0D09822), P(0xC7D7A8B4),
    P(0x59B33D17), P(0x2EB40D81), P(0xB7BD5C3B), P(0xC0BA6CAD),
    P(0xEDB88320), P(0x9ABFB3B6), P(0x03B6E20C), P(0x74B1D29A),
    P(0xEAD54739), P(0x9DD277AF), P(0x04DB2615), P(0x73DC1683),
    P(0xE3630B12), P(0x94643B84), P(0x0D6D6A3E), P(0x7A6A5AA8),
    P(0xE40ECF0B), P(0x9309FF9D), P(0x0A00AE27), P(0x7D079EB1),
    P(0xF00F9344), P(0x8708A3D2), P(0x1E01F268), P(0x6906C2FE),
    P(0xF762575D), P(0x806567CB), P(0x196C3671), P(0x6E6B06E7),
    P(0xFED41B76), P(0x89D32BE0), P(0x10DA7A5A), P(0x67DD4ACC),
    P(0xF9B9DF6F), P(0x8EBEEFF9), P(0x17B7BE43), P(0x60B08ED5),
    P(0xD6D6A3E8), P(0xA1D1937E), P(0x38D8C2C4), P(0x4FDFF252),
    P(0xD1BB67F1), P(0xA6BC5767), P(0x3FB506DD), P(0x48B2364B),
    P(0xD80D2BDA), P(0xAF0A1B4C), P(0x36034AF6), P(0x41047A60),
    P(0xDF60EFC3), P(0xA8670955), P(0x316658EF), P(0x4669E879),
    P(0xCB61B38C), P(0xBC66831A), P(0x256FD2A0), P(0x5268E236),
    P(0xCC0C7795), P(0xBB0B4703), P(0x220216B9), P(0x5505262F),
    P(0xC5BA3BBE), P(0xB2BD0B28), P(0x2BB45A92), P(0x5CB36A04),
    P(0xC2D7FFA7), P(0xB5D0CF31), P(0x2CD99E8B), P(0x5BDEAE1D),
    P(0x9B64C2B0), P(0xEC63F226), P(0x756AA39C), P(0x026D930A),
    P(0x9C0906A9), P(0xEB0E363F), P(0x72076785), P(0x05005713),
    P(0x95BF4A82), P(0xE2B87A14), P(0x7BB12BAE), P(0x0CB61B38),
    P(0x92D28E9B), P(0xE5D5BE0D), P(0x7CDCEFB7), P(0x0BDBDF21),
    P(0x86D3D2D4), P(0xF1D4E242), P(0x68DDB3F8), P(0x1FDA836E),
    P(0x81BE16CD), P(0xF6B9265B), P(0x6FB077E1), P(0x18B74777),
    P(0x88085AE6), P(0xFF0F6A70), P(0x66063BCA), P(0x11010B5C),
    P(0x8F659EFF), P(0xF862AE69), P(0x616BFFD3), P(0x166CCF45),
    P(0xA00AE278), P(0xD70DD2EE), P(0x4E048354), P(0x3903B3C2),
    P(0xA7672661), P(0xD06016F7), P(0x4969474D), P(0x3E6E77DB),
    P(0xAED16A4A), P(0xD9D65ADC), P(0x40DF0B66), P(0x37D83BF0),
    P(0xA9BCAE53), P(0xDEBB9EC5), P(0x47B2CF7F), P(0x30B5FFE9),
    P(0xBDBDF21C), P(0xCABAC28A), P(0x53B39330), P(0x24B4A3A6),
    P(0xBAD03605), P(0xCDD70693), P(0x54DE5729), P(0x23D967BF),
    P(0xB3667A2E), P(0xC4614AB8), P(0x5D681B02), P(0x2A6F2B94),
    P(0xB40BBE37), P(0xC30C8EA1), P(0x5A05DF1B), P(0x2D02EF8D),
#undef P
};

// -----------------------------------------------------------------------
// Singleton
// -----------------------------------------------------------------------
DeviceSettings DeviceSettings::instance = DeviceSettings();

// NVS handles — protected by _nvsMutex
static Preferences nano_preferences;  // main namespace (calibration, profile)
static Preferences wifi_preferences;  // separate namespace for credentials

DeviceSettings& DeviceSettings::getInstance() {
    return instance;
}

// -----------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------
DeviceSettings::DeviceSettings() {
    _applyDefaults();

    // Create recursive mutexes so a thread that holds the lock can call
    // internal helpers that also acquire it without deadlocking.
    _mutex    = xSemaphoreCreateRecursiveMutex();
    _nvsMutex = xSemaphoreCreateRecursiveMutex();
    configASSERT(_mutex);
    configASSERT(_nvsMutex);
}

DeviceSettings::~DeviceSettings() {}

// -----------------------------------------------------------------------
// Defaults
// -----------------------------------------------------------------------
void DeviceSettings::_applyDefaults() {
    debug             = false;
    ledMaxBrightness  = DEFAULT_LED_MAX_BRIGHTNESS;
    maxVelocity       = 10.0f;
    maxVoltage        = 5.0f;
    deviceOrientation = 1;
    serialNumber      = String(ESP.getEfuseMac(), HEX);
    deviceName        = "Nano_" + serialNumber;
    firmwareVersion   = String(NANO_FIRMWARE_VERSION);
    midiUsb           = midiSettings();
    midi2             = midiSettings();
    midi_sysex_id     = 0x00;
    idleTimeout       = 10000;
    wifiSsid          = "";
    wifiPassword      = "";
    wifiEnabled       = false;
    pdVoltage         = 5.0f;
    activeSprite      = "";
    dirty             = true;
}

// -----------------------------------------------------------------------
// Mutex helpers
// -----------------------------------------------------------------------
bool DeviceSettings::take(TickType_t wait) {
    return xSemaphoreTakeRecursive(_mutex, wait) == pdTRUE;
}
void DeviceSettings::give() {
    xSemaphoreGiveRecursive(_mutex);
}

// -----------------------------------------------------------------------
// CRC-32 helper
// -----------------------------------------------------------------------
uint32_t DeviceSettings::_computeCrc(const String& json) const {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < json.length(); i++) {
        uint8_t b = (uint8_t)json[i];
        crc = (crc >> 8) ^ CRC32_TABLE[(crc ^ b) & 0xFF];
    }
    return crc ^ 0xFFFFFFFFu;
}

// -----------------------------------------------------------------------
// WiFi accessors (field + NVS)
// -----------------------------------------------------------------------
String DeviceSettings::getWifiSsid() {
    take(); String v = wifiSsid; give(); return v;
}
void DeviceSettings::setWifiSsid(const String& v) {
    take(); wifiSsid = v; _saveWifiToNvs(); give();
}
String DeviceSettings::getWifiPassword() {
    take(); String v = wifiPassword; give(); return v;
}
void DeviceSettings::setWifiPassword(const String& v) {
    take(); wifiPassword = v; _saveWifiToNvs(); give();
}
bool DeviceSettings::getWifiEnabled() {
    take(); bool v = wifiEnabled; give(); return v;
}
void DeviceSettings::setWifiEnabled(bool v) {
    take(); wifiEnabled = v; _saveWifiToNvs(); give();
}

// -----------------------------------------------------------------------
// PD voltage accessor — clamped to board-safe [5.0, 9.0] V
// -----------------------------------------------------------------------
float DeviceSettings::getPdVoltage() {
    take(); float v = pdVoltage; give(); return v;
}
void DeviceSettings::setPdVoltage(float v) {
    // Clamp to board-safe range as per STUSB4500 config
    if (v < 5.0f) v = 5.0f;
    if (v > 9.0f) v = 9.0f;
    take(); pdVoltage = v; dirty = true; give();
}

// -----------------------------------------------------------------------
// Active sprite accessor
// -----------------------------------------------------------------------
String DeviceSettings::getActiveSprite() {
    take(); String v = activeSprite; give(); return v;
}
void DeviceSettings::setActiveSprite(const String& v) {
    take(); activeSprite = v; dirty = true; give();
}

// -----------------------------------------------------------------------
// NVS WiFi load/save  (caller must hold _mutex)
// -----------------------------------------------------------------------
void DeviceSettings::_loadWifiFromNvs() {
    if (xSemaphoreTakeRecursive(_nvsMutex, portMAX_DELAY) != pdTRUE) return;
    wifiSsid     = wifi_preferences.getString("ssid",     "");
    wifiPassword = wifi_preferences.getString("password", "");
    wifiEnabled  = wifi_preferences.getBool  ("enabled",  false);
    xSemaphoreGiveRecursive(_nvsMutex);
}

void DeviceSettings::_saveWifiToNvs() {
    if (xSemaphoreTakeRecursive(_nvsMutex, portMAX_DELAY) != pdTRUE) return;
    wifi_preferences.putString("ssid",     wifiSsid);
    wifi_preferences.putString("password", wifiPassword);
    wifi_preferences.putBool  ("enabled",  wifiEnabled);
    xSemaphoreGiveRecursive(_nvsMutex);
}

// -----------------------------------------------------------------------
// JSON assignment (from incoming settings command)
// -----------------------------------------------------------------------
DeviceSettings& DeviceSettings::operator=(JsonObject& obj) {
    take();
    if (!obj["debug"].isNull())
        debug = obj["debug"].as<bool>();
    if (!obj["ledMaxBrightness"].isNull())
        ledMaxBrightness = obj["ledMaxBrightness"].as<uint8_t>();
    if (!obj["maxVelocity"].isNull())
        maxVelocity = obj["maxVelocity"].as<float>();
    if (!obj["maxVoltage"].isNull())
        maxVoltage = obj["maxVoltage"].as<float>();
    if (obj["deviceOrientation"].is<uint16_t>())
        deviceOrientation = obj["deviceOrientation"].as<uint16_t>();
    if (!obj["deviceName"].isNull())
        deviceName = obj["deviceName"].as<String>();
    if (obj["idleTimeout"].is<uint32_t>())
        idleTimeout = obj["idleTimeout"].as<uint32_t>();

    // WiFi — update in-memory AND persist to NVS
    bool wifiChanged = false;
    if (!obj["wifiSsid"].isNull()) {
        wifiSsid = obj["wifiSsid"].as<String>();
        wifiChanged = true;
    }
    if (!obj["wifiPassword"].isNull()) {
        wifiPassword = obj["wifiPassword"].as<String>();
        wifiChanged = true;
    }
    if (!obj["wifiEnabled"].isNull()) {
        wifiEnabled = obj["wifiEnabled"].as<bool>();
        wifiChanged = true;
    }
    if (wifiChanged) _saveWifiToNvs();

    if (obj["pdVoltage"].is<float>()) {
        float v = obj["pdVoltage"].as<float>();
        // clamp inside the lock
        if (v < 5.0f) v = 5.0f;
        if (v > 9.0f) v = 9.0f;
        pdVoltage = v;
    }
    if (!obj["activeSprite"].isNull())
        activeSprite = obj["activeSprite"].as<String>();

    if (!obj["midiUsb"].isNull()) {
        JsonObject u = obj["midiUsb"].as<JsonObject>();
        if (!u["in"].isNull()) midiUsb.in    = u["in"].as<bool>();
        if (!u["out"].isNull()) midiUsb.out   = u["out"].as<bool>();
        if (!u["thru"].isNull()) midiUsb.thru  = u["thru"].as<bool>();
        if (!u["route"].isNull()) midiUsb.route = u["route"].as<bool>();
        if (!u["nano"].isNull()) midiUsb.nano  = u["nano"].as<bool>();
    }
    if (!obj["midi2"].isNull()) {
        JsonObject m = obj["midi2"].as<JsonObject>();
        if (!m["in"].isNull()) midi2.in    = m["in"].as<bool>();
        if (!m["out"].isNull()) midi2.out   = m["out"].as<bool>();
        if (!m["thru"].isNull()) midi2.thru  = m["thru"].as<bool>();
        if (!m["route"].isNull()) midi2.route = m["route"].as<bool>();
        if (!m["nano"].isNull()) midi2.nano  = m["nano"].as<bool>();
    }
    if (obj["sysexId"].is<uint8_t>())
        midi_sysex_id = obj["sysexId"].as<uint8_t>();

    dirty = true;
    give();
    return *this;
}

// -----------------------------------------------------------------------
// Serialise to JSON
// redactWifi=true (default): password becomes "***", never sent over serial
// redactWifi=false: for wifi_thread internal use only
// -----------------------------------------------------------------------
void DeviceSettings::toJSON(JsonObject& obj, bool redactWifi) {
    take();
    obj["debug"]             = debug;
    obj["ledMaxBrightness"]  = ledMaxBrightness;
    obj["maxVelocity"]       = maxVelocity;
    obj["maxVoltage"]        = maxVoltage;
    obj["deviceOrientation"] = deviceOrientation;
    obj["deviceName"]        = deviceName;
    obj["serialNumber"]      = serialNumber;
    obj["firmwareVersion"]   = firmwareVersion;
    obj["sysexId"]           = midi_sysex_id;
    obj["idleTimeout"]       = idleTimeout;
    obj["pdVoltage"]         = pdVoltage;
    obj["activeSprite"]      = activeSprite;
    obj["wifiEnabled"]       = wifiEnabled;
    if (wifiSsid.length() > 0)
        obj["wifiSsid"] = wifiSsid;
    // Password is always redacted in serial output; wifi_thread reads NVS directly
    if (redactWifi) {
        if (wifiPassword.length() > 0)
            obj["wifiPassword"] = "***";
    } else {
        if (wifiPassword.length() > 0)
            obj["wifiPassword"] = wifiPassword;
    }

    JsonObject midiUsbObj = obj["midiUsb"].to<JsonObject>();
    midiUsbObj["in"]    = midiUsb.in;
    midiUsbObj["out"]   = midiUsb.out;
    midiUsbObj["thru"]  = midiUsb.thru;
    midiUsbObj["route"] = midiUsb.route;
    midiUsbObj["nano"]  = midiUsb.nano;
    JsonObject midi2Obj = obj["midi2"].to<JsonObject>();
    midi2Obj["in"]    = midi2.in;
    midi2Obj["out"]   = midi2.out;
    midi2Obj["thru"]  = midi2.thru;
    midi2Obj["route"] = midi2.route;
    midi2Obj["nano"]  = midi2.nano;
    give();
}

// -----------------------------------------------------------------------
// Atomic save: tmp → rename
// On failure the existing good file is preserved.
// -----------------------------------------------------------------------
bool DeviceSettings::saveAtomic() {
    // Fix 4: dirty flag is a public field that may be written by another thread.
    // Move the early-out check INSIDE the lock to eliminate the TOCTOU race.
    take();
    if (!dirty) { give(); return true; }
    Serial.println("[DeviceSettings] Saving settings (atomic)...");

    // Build the JSON payload
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    // Save with plaintext WiFi so we can reload it; WiFi is also in NVS but
    // keep JSON consistent for disaster recovery.
    toJSON(obj, /*redactWifi=*/false);
    obj["schemaVersion"] = SCHEMA_VERSION;

    String payload;
    serializeJson(doc, payload);

    // Compute and append CRC as a top-level field
    uint32_t crc = _computeCrc(payload);
    // Re-build with CRC included
    doc["crc32"] = crc;
    payload = "";
    serializeJson(doc, payload);

    // Write to .tmp
    File f = FS_HANDLE.open(DEVICE_SETTINGS_TMP, "w");
    if (!f) {
        Serial.println("[DeviceSettings] ERROR: cannot open .tmp for write");
        give();
        return false;
    }
    size_t written = f.print(payload);
    f.flush();
    f.close();

    if (written != payload.length()) {
        Serial.println("[DeviceSettings] ERROR: incomplete write to .tmp");
        FS_HANDLE.remove(DEVICE_SETTINGS_TMP);
        give();
        return false;
    }

    // Atomic rename over real file
    if (!FS_HANDLE.rename(DEVICE_SETTINGS_TMP, DEVICE_SETTINGS_FILE)) {
        Serial.println("[DeviceSettings] ERROR: rename failed");
        FS_HANDLE.remove(DEVICE_SETTINGS_TMP);
        give();
        return false;
    }

    dirty = false;
    Serial.println("[DeviceSettings] Settings saved OK");
    give();
    return true;
}

// -----------------------------------------------------------------------
// Internal: parse JSON string into settings.
// Returns false if CRC mismatch, parse error, or schema too old.
// -----------------------------------------------------------------------
bool DeviceSettings::_loadFromJson(const String& raw) {
    JsonDocument doc;
    if (deserializeJson(doc, raw) != DeserializationError::Ok) {
        Serial.println("[DeviceSettings] ERROR: JSON parse failed");
        return false;
    }

    // Schema version check — allow forward-migration for v1 (no CRC field)
    uint8_t schema = doc["schemaVersion"] | 0;
    if (schema < 1) {
        Serial.println("[DeviceSettings] WARN: unknown schema version, skipping");
        return false;
    }

    // CRC check — only for schema >=2
    if (schema >= 2) {
        uint32_t storedCrc = doc["crc32"] | 0u;
        // Remove the crc32 field, reserialise, recompute
        doc.remove("crc32");
        String withoutCrc;
        serializeJson(doc, withoutCrc);
        uint32_t computedCrc = _computeCrc(withoutCrc);
        if (computedCrc != storedCrc) {
            Serial.printf("[DeviceSettings] ERROR: CRC mismatch (stored %08X, computed %08X)\n",
                          storedCrc, computedCrc);
            return false;
        }
    }

    JsonObject obj = doc.as<JsonObject>();

    // Apply fields — same logic as operator= but without extra NVS save
    if (!obj["debug"].isNull()) debug            = obj["debug"].as<bool>();
    if (!obj["ledMaxBrightness"].isNull()) ledMaxBrightness = obj["ledMaxBrightness"].as<uint8_t>();
    if (!obj["maxVelocity"].isNull()) maxVelocity      = obj["maxVelocity"].as<float>();
    if (!obj["maxVoltage"].isNull()) maxVoltage       = obj["maxVoltage"].as<float>();
    if (obj["deviceOrientation"].is<uint16_t>()) deviceOrientation = obj["deviceOrientation"].as<uint16_t>();
    if (!obj["deviceName"].isNull()) deviceName       = obj["deviceName"].as<String>();
    if (obj["idleTimeout"].is<uint32_t>())  idleTimeout      = obj["idleTimeout"].as<uint32_t>();
    if (obj["sysexId"].is<uint8_t>())       midi_sysex_id    = obj["sysexId"].as<uint8_t>();
    if (!obj["activeSprite"].isNull()) activeSprite     = obj["activeSprite"].as<String>();

    // PD voltage from file (clamp)
    if (obj["pdVoltage"].is<float>()) {
        float v = obj["pdVoltage"].as<float>();
        if (v < 5.0f) v = 5.0f;
        if (v > 9.0f) v = 9.0f;
        pdVoltage = v;
    }

    if (!obj["midiUsb"].isNull()) {
        JsonObject u = obj["midiUsb"].as<JsonObject>();
        if (!u["in"].isNull()) midiUsb.in    = u["in"].as<bool>();
        if (!u["out"].isNull()) midiUsb.out   = u["out"].as<bool>();
        if (!u["thru"].isNull()) midiUsb.thru  = u["thru"].as<bool>();
        if (!u["route"].isNull()) midiUsb.route = u["route"].as<bool>();
        if (!u["nano"].isNull()) midiUsb.nano  = u["nano"].as<bool>();
    }
    if (!obj["midi2"].isNull()) {
        JsonObject m = obj["midi2"].as<JsonObject>();
        if (!m["in"].isNull()) midi2.in    = m["in"].as<bool>();
        if (!m["out"].isNull()) midi2.out   = m["out"].as<bool>();
        if (!m["thru"].isNull()) midi2.thru  = m["thru"].as<bool>();
        if (!m["route"].isNull()) midi2.route = m["route"].as<bool>();
        if (!m["nano"].isNull()) midi2.nano  = m["nano"].as<bool>();
    }

    // WiFi from JSON (may have been saved pre-NVS) — overridden by NVS below
    if (!obj["wifiSsid"].isNull()) wifiSsid     = obj["wifiSsid"].as<String>();
    if (!obj["wifiPassword"].isNull()) {
        String pw = obj["wifiPassword"].as<String>();
        // Ignore redacted sentinel values written by older firmware
        if (pw != "***") wifiPassword = pw;
    }
    if (!obj["wifiEnabled"].isNull()) wifiEnabled  = obj["wifiEnabled"].as<bool>();

    return true;
}

// -----------------------------------------------------------------------
// Load from filesystem
// -----------------------------------------------------------------------
bool DeviceSettings::fromSPIFFS() {
    take();
    Serial.println("[DeviceSettings] Loading settings...");

    if (!FS_HANDLE.exists(DEVICE_SETTINGS_FILE)) {
        Serial.println("[DeviceSettings] No settings file — using defaults");
        give();
        return true; // not an error; first boot
    }

    File f = FS_HANDLE.open(DEVICE_SETTINGS_FILE, "r");
    if (!f) {
        Serial.println("[DeviceSettings] ERROR: cannot open settings file");
        give();
        return false;
    }
    String raw = f.readString();
    f.close();

    bool ok = _loadFromJson(raw);
    if (!ok) {
        // CRC/parse failure — fall back to defaults but keep bad file intact
        Serial.println("[DeviceSettings] Settings corrupt — falling back to defaults (bad file preserved)");
        _applyDefaults();
        // Override WiFi from NVS so at least connectivity survives
        _loadWifiFromNvs();
        dirty = true; // will be saved on next successful saveAtomic()
        give();
        return false;
    }

    // NVS is authoritative for WiFi (schema >=2 also persists there)
    _loadWifiFromNvs();

    dirty = false;
    Serial.println("[DeviceSettings] Settings loaded OK");
    give();
    return true;
}

// -----------------------------------------------------------------------
// Init — must be called once from setup()
// -----------------------------------------------------------------------
bool DeviceSettings::init() {
    if (!FS_HANDLE.begin(true)) {
        Serial.println("[DeviceSettings] ERROR: LittleFS mount failed");
        return false;
    }
    Serial.println("[DeviceSettings] LittleFS mounted");

    if (xSemaphoreTakeRecursive(_nvsMutex, portMAX_DELAY) != pdTRUE) return false;

    if (!nano_preferences.begin(SETTINGS_NVS_NAMESPACE, false)) {
        Serial.println("[DeviceSettings] ERROR: Preferences (main) open failed");
        xSemaphoreGiveRecursive(_nvsMutex);
        return false;
    }
    if (!wifi_preferences.begin(WIFI_NVS_NAMESPACE, false)) {
        Serial.println("[DeviceSettings] ERROR: Preferences (wifi) open failed");
        xSemaphoreGiveRecursive(_nvsMutex);
        return false;
    }

    xSemaphoreGiveRecursive(_nvsMutex);
    return true;
}

// -----------------------------------------------------------------------
// Calibration (NVS, mutex-guarded)
// -----------------------------------------------------------------------
void DeviceSettings::storeCalibration(MotorCalibration& cal) {
    if (xSemaphoreTakeRecursive(_nvsMutex, portMAX_DELAY) != pdTRUE) return;
    nano_preferences.putUChar("direction",  cal.direction);
    nano_preferences.putFloat("zero_angle", cal.zero_angle);
    xSemaphoreGiveRecursive(_nvsMutex);
}

MotorCalibration DeviceSettings::loadCalibration() {
    MotorCalibration result{0, NOT_SET};
    if (xSemaphoreTakeRecursive(_nvsMutex, portMAX_DELAY) != pdTRUE) return result;
    result.direction  = nano_preferences.getUChar("direction",  0);
    result.zero_angle = nano_preferences.getFloat("zero_angle", NOT_SET);
    xSemaphoreGiveRecursive(_nvsMutex);
    return result;
}

// -----------------------------------------------------------------------
// Current profile (NVS, mutex-guarded)
// -----------------------------------------------------------------------
String DeviceSettings::loadCurrentProfile() {
    String p = "Default Profile";
    if (xSemaphoreTakeRecursive(_nvsMutex, portMAX_DELAY) != pdTRUE) return p;
    p = nano_preferences.getString("current_profile", "Default Profile");
    xSemaphoreGiveRecursive(_nvsMutex);
    return p;
}

void DeviceSettings::storeCurrentProfile(String profile) {
    if (xSemaphoreTakeRecursive(_nvsMutex, portMAX_DELAY) != pdTRUE) return;
    nano_preferences.putString("current_profile", profile);
    xSemaphoreGiveRecursive(_nvsMutex);
}

// -----------------------------------------------------------------------
// Factory reset — wipes FS settings file + NVS, restores safe defaults.
// Triggered by FW4 on the brick-recovery button-combo path.
// -----------------------------------------------------------------------
void DeviceSettings::factoryReset() {
    take();
    Serial.println("[DeviceSettings] FACTORY RESET");

    // Remove persisted files
    if (FS_HANDLE.exists(DEVICE_SETTINGS_FILE))
        FS_HANDLE.remove(DEVICE_SETTINGS_FILE);
    if (FS_HANDLE.exists(DEVICE_SETTINGS_TMP))
        FS_HANDLE.remove(DEVICE_SETTINGS_TMP);

    // Wipe NVS namespaces
    if (xSemaphoreTakeRecursive(_nvsMutex, portMAX_DELAY) == pdTRUE) {
        nano_preferences.clear();
        wifi_preferences.clear();
        xSemaphoreGiveRecursive(_nvsMutex);
    }

    _applyDefaults();
    // Mark dirty so the next saveAtomic() (called by integrator after reset)
    // writes clean default settings.
    dirty = true;

    give();
    Serial.println("[DeviceSettings] Factory reset complete — defaults applied");
}
