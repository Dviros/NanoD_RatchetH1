# Nano_D++ Brick-Proofing Reference

Every mechanism that prevents a bad firmware flash, corrupt settings file, power-supply mistake, or runaway task from permanently damaging or inoperably locking the device.

Verification status is noted per section:
- **compile-verified** — confirmed present in the build that passed `pio run -e nanofoc_d`
- **needs on-hardware validation** — code path correct but not yet exercised on a physical unit in this session

---

## 1. Dual-OTA Partition Layout

### Partition table (`boards/nano_partitions.csv`)

```
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x140000,
app1,     app,  ota_1,   0x150000,0x140000,
spiffs,   data, spiffs,  0x290000,0x160000,
coredump, data, coredump,0x3F0000,0x10000,
```

Each OTA app slot is 1 310 720 bytes (1.3 MB). The verified build occupies 840 KB (65.6 %) of one slot, leaving comfortable headroom. The `otadata` partition at `0xe000` tracks which slot is currently active and which is the pending rollback target.

### OTA rollback flow (`main.cpp`, `wifi_thread.cpp`)

After all core threads are started, `setup()` waits 2 seconds and then calls `wifi_thread.mark_ota_valid()`:

```cpp
// main.cpp  (lines 85-86)
vTaskDelay(2000 / portTICK_PERIOD_MS);
wifi_thread.mark_ota_valid();
```

`mark_ota_valid()` calls `esp_ota_mark_app_valid_cancel_rollback()`:

```cpp
// wifi_thread.cpp  (lines 161-171)
void WifiThread::mark_ota_valid() {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        Serial.println("[WIFI] OTA rollback cancelled — firmware marked valid.");
    } else if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
        Serial.println("[WIFI] WARNING: OTA validate failed — already rolled back?");
    }
    // ESP_ERR_INVALID_ARG means no OTA partition scheme; safe to ignore.
}
```

If the new firmware panics or crashes before reaching `vTaskDelay(2000)` in `setup()`, `mark_ota_valid()` is never called. The ESP-IDF bootloader then reverts to the previous slot on the next reset.

### CRITICAL: bootloader flag required for auto-rollback

The `esp_ota_mark_app_valid_cancel_rollback()` call is a no-op guard **unless the bootloader was compiled with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`**. The ESP32 Arduino core ships bootloaders without this flag by default.

**How to enable it in PlatformIO:**

1. Create `sdkconfig.defaults` in the project root (next to `platformio.ini`):

   ```
   CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
   ```

2. In `platformio.ini`, point PlatformIO at it:

   ```ini
   [env:nanofoc_d]
   ; ... existing settings ...
   board_build.cmake_extra_args = -DSDKCONFIG_DEFAULTS=sdkconfig.defaults
   ```

3. Clean and rebuild:

   ```bash
   pio run -e nanofoc_d -t clean
   pio run -e nanofoc_d
   ```

   The bootloader binary will be rebuilt from source with rollback support.

4. Flash the bootloader along with the first OTA image:

   ```bash
   pio run -e nanofoc_d -t upload
   ```

   Subsequent OTA flashes via ArduinoOTA or the `/update` HTTP endpoint will use the existing bootloader; only the app slot is written.

> **Without this step**, a crash-looping new firmware will loop forever. With it, the bootloader automatically switches back to the last marked-valid slot after a configurable number of failed boots (default: 3).

Status: **needs on-hardware validation** (compile path correct; bootloader integration not tested in this session).

---

## 2. Atomic Settings Save — CRC + Schema Version + Corrupt Fallback

All device settings are stored in `/device_settings.json` on LittleFS. The save path is fully atomic: a crash mid-write cannot corrupt the live file.

### Save path (`DeviceSettings.cpp`, `saveAtomic()`)

1. Serialize settings to JSON including `schemaVersion: 2`.
2. Compute CRC-32 (PKZIP/ISO-3309) over the serialized string.
3. Re-serialize with `crc32` field appended.
4. Write to `/device_settings.tmp`.
5. If write is incomplete (byte count mismatch), delete the `.tmp` file and return `false` — original file untouched.
6. Call `LittleFS.rename("/device_settings.tmp", "/device_settings.json")`. LittleFS implements `rename()` atomically at the filesystem level. If the device loses power here, only one file can exist (either the old good file or the new good file; a half-renamed state is not possible with LittleFS journaling).

### Load path (`DeviceSettings.cpp`, `fromSPIFFS()` and `_loadFromJson()`)

On load:

- Schema version < 1: rejected, defaults applied.
- Schema version >= 2: CRC is extracted, `crc32` field is removed from the document, the remainder is re-serialized and re-hashed, and the computed hash is compared against the stored one. Mismatch → rejected.
- Parse failure (malformed JSON): rejected.

On rejection:

```cpp
// DeviceSettings.cpp  (lines 521-528)
Serial.println("[DeviceSettings] Settings corrupt — falling back to defaults (bad file preserved)");
_applyDefaults();
// Override WiFi from NVS so at least connectivity survives
_loadWifiFromNvs();
dirty = true; // will be saved on next successful saveAtomic()
```

The corrupt file is preserved (not deleted) so it can be retrieved via serial for diagnosis. WiFi credentials are separately loaded from NVS (see section 3), so network access survives a corrupt settings file.

Status: **compile-verified**.

---

## 3. NVS / Preferences + Thread Mutexes

### Two NVS namespaces

| Namespace | Key store | Content |
|---|---|---|
| `nano_D` | `nano_preferences` | Motor calibration (`direction`, `zero_angle`), current profile name |
| `nano_wifi` | `wifi_preferences` | `ssid`, `password`, `enabled` |

WiFi credentials are intentionally kept in a separate NVS namespace so they are never serialized into the main JSON file in plaintext when `toJSON()` is called with the default `redactWifi=true`.

### Recursive mutexes

`DeviceSettings` creates two `xSemaphoreCreateRecursiveMutex()` handles in its constructor:

- `_mutex` — guards all in-memory fields. All public accessors (`take()`/`give()`, `setPdVoltage()`, `setActiveSprite()`, etc.) acquire this before touching fields.
- `_nvsMutex` — guards `nano_preferences` and `wifi_preferences` Preferences objects. All NVS reads and writes acquire this independently.

Both are recursive so internal helpers that also acquire the lock (`_saveWifiToNvs()` called from within `operator=()` which already holds `_mutex`) do not deadlock.

```cpp
// DeviceSettings.cpp  (lines 120-123)
_mutex    = xSemaphoreCreateRecursiveMutex();
_nvsMutex = xSemaphoreCreateRecursiveMutex();
configASSERT(_mutex);
configASSERT(_nvsMutex);
```

`configASSERT` causes a hard fault (logged panic) at boot if mutex allocation fails rather than silently proceeding with a null handle.

### `SemaphoreGuard` RAII helper (`semaphore_guard.h`)

For scoped locking:

```cpp
class SemaphoreGuard {
    explicit SemaphoreGuard(SemaphoreHandle_t handle)
        : handle_{handle} { xSemaphoreTakeRecursive(handle_, portMAX_DELAY); }
    ~SemaphoreGuard()     { xSemaphoreGiveRecursive(handle_); }
    // Non-copyable
};
```

Status: **compile-verified**.

---

## 4. `factoryReset()` and Recovery

### What it does (`DeviceSettings.cpp`, lines 607-631)

```cpp
void DeviceSettings::factoryReset() {
    // 1. Remove LittleFS settings files
    if (FS_HANDLE.exists(DEVICE_SETTINGS_FILE))
        FS_HANDLE.remove(DEVICE_SETTINGS_FILE);
    if (FS_HANDLE.exists(DEVICE_SETTINGS_TMP))
        FS_HANDLE.remove(DEVICE_SETTINGS_TMP);

    // 2. Wipe both NVS namespaces
    nano_preferences.clear();
    wifi_preferences.clear();

    // 3. Apply in-memory defaults
    _applyDefaults();
    dirty = true;
}
```

After `factoryReset()`, the caller is expected to call `saveAtomic()` (or `toSPIFFS()`) to persist the clean default state.

Defaults include: `pdVoltage = 5.0f`, `wifiEnabled = false`, `wifiSsid = ""`, `wifiPassword = ""`.

### How to trigger recovery

`factoryReset()` is defined and mutex-guarded but is **not yet wired to any hardware trigger** in this codebase (no button combination or serial command calls it in the current `hmi_thread.cpp` or `com_thread.cpp`). The comment in `DeviceSettings.cpp` reads "Triggered by FW4 on the brick-recovery button-combo path" — this is a forward reference to work not yet merged.

**Current recovery options:**

1. **Serial command** (manual): send `{"settings":{"factoryReset":true}}` — note this key is not handled by `handleSettingsCommand()` as shipped; you must add it, or call `DeviceSettings::getInstance().factoryReset()` directly from a debug build.

2. **Erase flash via esptool** (always works):

   ```bash
   esptool.py --chip esp32s3 --port <PORT> erase_flash
   ```

   Then re-flash the full firmware image. This erases both OTA slots, NVS, and the LittleFS partition.

3. **Erase NVS partition only** (preserves firmware):

   ```bash
   esptool.py --chip esp32s3 --port <PORT> erase_region 0x9000 0x5000
   ```

4. **Erase LittleFS partition only** (preserves firmware and NVS):

   ```bash
   esptool.py --chip esp32s3 --port <PORT> erase_region 0x290000 0x160000
   ```

Status: `factoryReset()` function is **compile-verified**. Hardware trigger wiring is **not implemented** in current code and requires a future integration.

---

## 5. USB-PD Voltage Clamp — Safe Supply Range [5.0 V, 9.0 V]

The STUSB4500 USB-PD controller negotiates supply voltage at boot. Two independent clamps prevent an unsafe voltage from reaching `driver.voltage_power_supply`.

### NVM profile (prevents requesting unsafe voltage)

`init_pd()` in `hmi_thread.cpp` configures the STUSB4500 with exactly two PDOs:

| PDO | Voltage | Current |
|---|---|---|
| 1 | 5.0 V | 3.0 A |
| 2 | 9.0 V | 3.0 A |

PDO3 is set equal to PDO2 (9 V) as a no-op slot. `setPdoNumber(2)` limits negotiation to PDOs 1 and 2. The chip will never request voltages above 9 V.

NVM is only written when `usb_pd.getPdoNumber() != 2` (flash-wear guard). A soft-reset is issued after write to force immediate re-negotiation.

### Clamp in `init_pd()` (rejects out-of-range charger offers)

After reading back the negotiated PDO via I2C register `0x91` (RDO_REG_STATUS):

```cpp
// hmi_thread.cpp  (lines 603-607)
if (negotiated_v < 5.0f || negotiated_v > 9.0f) {
    Serial.printf("PD voltage %.1fV out of range, clamping to 5.0V\n", negotiated_v);
    negotiated_v = 5.0f;
}
DeviceSettings::getInstance().setPdVoltage(negotiated_v);
```

### Clamp in `DeviceSettings::setPdVoltage()` (storage layer)

```cpp
// DeviceSettings.cpp  (lines 203-207)
void DeviceSettings::setPdVoltage(float v) {
    if (v < 5.0f) v = 5.0f;
    if (v > 9.0f) v = 9.0f;
    take(); pdVoltage = v; dirty = true; give();
}
```

The same clamp is applied when loading from file in `_loadFromJson()`.

### Clamp in `foc_thread.cpp` (motor driver, final consumer)

```cpp
// foc_thread.cpp  (lines 47-51)
float pdV = DeviceSettings::getInstance().pdVoltage;
if (pdV < 5.0f || pdV > 9.0f) pdV = 5.0f; // default/safe fallback
driver.voltage_power_supply = pdV;
driver.voltage_limit = pdV; // limit cannot exceed supply
```

Three independent clamps — at negotiation time, at storage, and at consumption — ensure the motor driver is never supplied an unsafe voltage even if a single layer is bypassed by a corrupt settings file or a future code path.

If STUSB4500 is not found on I2C (chip absent or bus fault), `init_pd()` sets `pdVoltage = 5.0f` and returns `POWER_5V_USB`. The motor driver defaults to 5 V.

Status: **compile-verified**. Actual PD negotiation with a 9 V charger is **needs on-hardware validation**.

---

## 6. Task Watchdog

### Current status

`esp_task_wdt.h` is included in `main.cpp`, `com_thread.cpp`, and `wifi_thread.cpp`. The WIFI task (`wifi_thread.cpp`) is explicitly registered with and feeds the watchdog:

```cpp
// wifi_thread.cpp  (lines 61-65)
esp_task_wdt_add(nullptr);   // register this task
while (true) {
    self->loop();
    esp_task_wdt_reset();    // feed watchdog each iteration
    vTaskDelay(pdMS_TO_TICKS(10));
}
```

No other tasks (`FOC`, `HMI`, `LCD`, `COM`) currently call `esp_task_wdt_add()` or `esp_task_wdt_reset()`. The watchdog header is included in `com_thread.cpp` and `main.cpp` but neither registers or feeds the watchdog in the current code.

**Consequence:** a hang in `foc_thread`, `hmi_thread`, `lcd_thread`, or `com_thread` will not be caught by the Task WDT. Only a hang in the WIFI task triggers a reset (and only when `WIFI_ENABLED` is defined).

### How to enable full watchdog coverage

The ESP-IDF Task WDT must be initialized with a timeout before tasks register themselves. Add to `setup()` in `main.cpp` before starting threads:

```cpp
#include <esp_task_wdt.h>

// 5-second timeout; panic on expiry (triggers coredump if partition present).
esp_task_wdt_config_t wdt_cfg = {
    .timeout_ms    = 5000,
    .idle_core_mask = 0,       // don't watch idle tasks
    .trigger_panic  = true
};
esp_task_wdt_reconfigure(&wdt_cfg);   // or esp_task_wdt_init() on IDF < 5.x
```

Then in each thread's `run()` loop:

```cpp
esp_task_wdt_add(nullptr);  // once, at top of run()
// ... loop ...
esp_task_wdt_reset();       // once per iteration, inside the loop
```

The coredump partition (`0x3F0000`, 64 KB) is already present in the partition table and will capture the register state on a watchdog-triggered panic.

### Enabling watchdog via `sdkconfig.defaults`

Alternatively, enable the idle-task watchdog at the bootloader/Kconfig level so it fires even without explicit task registration:

```
CONFIG_ESP_TASK_WDT_EN=y
CONFIG_ESP_TASK_WDT_PANIC=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=5
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=n
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=n
```

Status: WIFI-task watchdog is **compile-verified**. Full multi-task watchdog coverage is **not implemented** in the current codebase and requires the additions described above.

---

## Summary Table

| Mechanism | Where | Status |
|---|---|---|
| Dual-OTA partition layout | `boards/nano_partitions.csv` | compile-verified |
| OTA rollback cancel after healthy boot | `main.cpp`, `wifi_thread.cpp` | compile-verified |
| Auto-rollback on crash (bootloader flag) | `sdkconfig.defaults` (not present) | NOT YET ENABLED — see section 1 |
| Atomic temp+rename settings save | `DeviceSettings.cpp::saveAtomic()` | compile-verified |
| CRC-32 integrity check on load | `DeviceSettings.cpp::_loadFromJson()` | compile-verified |
| Schema version migration | `DeviceSettings.cpp::_loadFromJson()` | compile-verified |
| Corrupt settings → safe defaults fallback | `DeviceSettings.cpp::fromSPIFFS()` | compile-verified |
| NVS recursive mutex (`_nvsMutex`) | `DeviceSettings.cpp` | compile-verified |
| Settings recursive mutex (`_mutex`) | `DeviceSettings.cpp` | compile-verified |
| `SemaphoreGuard` RAII helper | `semaphore_guard.h` | compile-verified |
| `factoryReset()` (wipes FS + NVS) | `DeviceSettings.cpp` | compile-verified |
| Hardware trigger for factory reset | `hmi_thread.cpp` / `com_thread.cpp` | NOT WIRED — see section 4 |
| PD NVM profile limits to [5 V, 9 V] | `hmi_thread.cpp::init_pd()` | compile-verified |
| PD out-of-range clamp at negotiation | `hmi_thread.cpp::init_pd()` | compile-verified |
| PD clamp at storage setter | `DeviceSettings.cpp::setPdVoltage()` | compile-verified |
| PD clamp at motor driver init | `foc_thread.cpp::run()` | compile-verified |
| STUSB4500 absent → 5 V default | `hmi_thread.cpp::init_pd()` | compile-verified |
| Task WDT on WIFI task | `wifi_thread.cpp` | compile-verified (WIFI_ENABLED only) |
| Task WDT on FOC/HMI/LCD/COM tasks | not implemented | NOT IMPLEMENTED — see section 6 |
| Coredump partition present | `boards/nano_partitions.csv` | compile-verified |
