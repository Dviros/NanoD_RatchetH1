# Nano_D++ Hardware Bring-Up Test Checklist

**Status**: Firmware and app compile-verified; NOT yet flashed to hardware.
**Board**: ESP32-S3, 4 MB flash, dual-OTA partition scheme (app0/app1 @ 0x140000, 1.3 MB each).
**Build verified**: `nanofoc_d` (safe core) — Flash 65.6% (840 KB of 1.3 MB slot), RAM 46.8%.

Tests are ordered by risk. Items marked **HIGH RISK** should be reviewed with extra care before proceeding; a failure there can leave the device unflashable without physical UART access.

---

## 1. Initial Flash (Baseline)

**Risk: HIGH — first flash establishes the rollback anchor. Do this before any OTA attempt.**

| Step | Command / Action | Pass | Fail |
|------|-----------------|------|------|
| 1.1 | Flash `nanofoc_d` env via serial: `pio run -e nanofoc_d -t upload` | Serial prints `Welcome to Nano_D++!` and firmware version `1.1.0` | No output or exception decoder trace |
| 1.2 | Confirm LittleFS partition: Serial shows `[DeviceSettings] LittleFS mounted` | Message present | `LittleFS mount failed` |
| 1.3 | Confirm startup ACK: `{"ack":"boot","ok":true}` emitted on first serial line | JSON line received | Bare `println` or no output |
| 1.4 | Confirm threads started: Serial shows `Starting threads...` with no panic/exception | Normal | Exception decoder prints backtrace |
| 1.5 | Confirm OTA valid marker: after the 2-second delay in `setup()`, Serial shows `[WIFI] OTA rollback cancelled — firmware marked valid.` (stub build) or no crash | Device remains running | Reboots within 2 s |

---

## 2. Settings Persistence (LittleFS, Atomic Save, CRC)

Covers `DeviceSettings.cpp` — atomic tmp-then-rename save, CRC-32, schemaVersion, corrupt-file fallback.

| Step | Action | Pass | Fail |
|------|--------|------|------|
| 2.1 | Send `{"settings":{"deviceName":"test-unit"}}` then `{"save":true}` | Serial: `{"ack":"settings","ok":true}`, then `{"saved":true}` and `{"ack":"save","ok":true}`, then `[DeviceSettings] Settings saved OK` | Error or no ACK |
| 2.2 | Hard-reset device (`EN` button). Send `{"settings":""}` query | `deviceName` field in reply equals `"test-unit"` (survived reboot) | Reverted to default name |
| 2.3 | Confirm schemaVersion and CRC in file: connect UART, open `/device_settings.json` via `{"settings":""}` response, verify `schemaVersion:2` and `crc32` field non-zero | Both fields present | Missing or zero |
| 2.4 | **Corrupt-file recovery**: truncate `/device_settings.json` via a SPIFFS write tool or by sending a garbage string to the file path, then reboot | Serial: `[DeviceSettings] Settings corrupt — falling back to defaults`, device boots with defaults, `{"ack":"boot","ok":true}` appears | Device panics or hangs |
| 2.5 | WiFi creds stored in separate NVS namespace: send `{"wifi":{"ssid":"test","password":"pw","enabled":false}}`, reboot, send `{"settings":""}` | `wifiSsid:"test"` present, `wifiPassword:"***"` (redacted in serial output) | Password leaked or missing |

---

## 3. USB-PD Voltage Negotiation

**Risk: HIGH — incorrect negotiated voltage reaches the motor driver; too high risks hardware damage.**

The call chain is: `main.cpp setup()` calls `hmi_thread.init_pd()` before `foc_thread.begin()`, which reads `DeviceSettings::pdVoltage` (clamped to [5.0, 9.0] V) into `driver.voltage_power_supply` and `driver.voltage_limit`.

| Step | Action | Pass | Fail |
|------|--------|------|------|
| 3.1 | Connect device to USB-C PD charger capable of 9 V (e.g., 18 W GaN). Measure VBUS with a multimeter **before** USB enumeration completes | 9.0 V on VBUS | 5.0 V (no PD negotiation) or >9.0 V |
| 3.2 | Confirm STUSB4500 found: Serial shows `STUSB4500 found` | Present | `STUSB4500 not found; defaulting to 5V USB` — check I2C wiring (SDA/SCL per `PIN_NANO_I2C_SDA`/`PIN_NANO_I2C_SCL`) |
| 3.3 | Confirm PDO read-back: Serial shows `PD negotiated: PDO2 = 9.0V` (PDO2 is the 9 V profile per NVM config in `init_pd()`) | `PDO2 = 9.0V` | Reports 5.0V or wrong PDO |
| 3.4 | Confirm `DeviceSettings.pdVoltage` propagated: send `{"settings":""}` | `"pdVoltage":9.0` in response | `5.0` or absent |
| 3.5 | Confirm motor driver supply: at higher PD voltage, max torque should be noticeably higher than on 5 V USB. Rotate knob against a load — compare force to 5 V supply | Increased torque at 9 V | Same torque (voltage not propagated to driver) |
| 3.6 | Out-of-range clamp: if a bench supply is used, force a voltage outside [5, 9] V by spoofing the RDO register response (or by patching a debug print in `init_pd()`). Confirm `setPdVoltage()` clamps to 5.0 V | Serial: `PD voltage X.XV out of range, clamping to 5.0V` | Value outside [5, 9] stored/used |
| 3.7 | NVM write avoidance: if `getPdoNumber()` already returns 2 (NVM already written), Serial must NOT print `Programming STUSB4500 NVM` | Skips NVM write | Writes NVM on every boot (flash wear) |

---

## 4. Thread Synchronisation

Covers `semaphore_guard.h` (RAII recursive mutex), `DeviceSettings._mutex` + `_nvsMutex`, `HmiThread._hmi_mutex`, `global_sleep_flag` atomic read.

| Step | Action | Pass | Fail |
|------|--------|------|------|
| 4.1 | Press all 4 buttons rapidly while rotating the knob and sending serial commands simultaneously for 30 s | No RTOS assert / watchdog reset, no garbled serial JSON | Assert: `xSemaphoreTake` deadlock or corrupted JSON output |
| 4.2 | Send `{"settings":{"ledMaxBrightness":200}}` while the knob is being actively turned | Settings applied (LED dims/brightens) without corrupted telemetry frames | Truncated JSON or LED flicker glitch |
| 4.3 | Trigger idle (leave device idle past `idleTimeout`): `global_sleep_flag` becomes true, idle LED animation plays | `{"idle":…}` JSON frames appear, ring LEDs cycle Red→Green→Blue | No idle frames or LED stays in normal mode |

---

## 5. Haptic/Knob Bug Fixes

### 5.1 LED IdleLeds Out-of-Bounds (Fix 1 in `hmi_thread.cpp`)

Original loop ran to `NANO_LED_A_NUM+8` (68), writing past the end of `leds[60]`. Fixed to use separate bounded loops for ring (`NANO_LED_A_NUM`) and button (`NANO_LED_B_NUM`) arrays.

| Step | Action | Pass | Fail |
|------|--------|------|------|
| Trigger idle mode (no input > idle timeout). Observe all LEDs | All 60 ring LEDs and all 8 button LEDs cycle in idle animation. No crash. | Memory corruption crash / only some LEDs light |

### 5.2 PD / Knob Angle Clamp (Fix 5 in `hmi_thread.cpp updateValue()`)

Original code called `_constrain()` but discarded the return value — angle was never clamped.

| Step | Action | Pass | Fail |
|------|--------|------|------|
| Configure a profile with `angleMin=0`, `angleMax=3.14` (half turn). Rotate past the limit. | Mapped value stops at `value_max`; no MIDI CC sent beyond 127 | Value continues climbing past limit |

### 5.3 Knob Mapping Overwrite (Fix 6 in `hmi_thread.cpp updateValue()`)

Original code overwrote `currentValue` with raw encoder position after the mapped calculation.

| Step | Action | Pass | Fail |
|------|--------|------|------|
| Configure MIDI CC mapping (0–127) over one full turn. Rotate slowly and observe `{"a":…,"t":…,"v":…,"p":…}` telemetry + MIDI CC on DAW | Mapped CC 0–127 matches knob position proportionally | CC jumps to raw encoder position (e.g., 0–360 range) |

### 5.4 D-Term Inversion (Fix in `haptic.cpp correct_pid()`)

Original condition was inverted — D-term was applied when there were no detents and zeroed when there were.

| Step | Action | Pass | Fail |
|------|--------|------|------|
| Load a profile with `detentCount=60`. Rotate knob at moderate speed. Compare feel to a profile with `detentCount=0` (smooth) | Detented profile has crisp clicks; smooth profile has no D-term buzz | Inverted: smooth profile has D-term noise; detented profile feels mushy |

### 5.5 Key Long-Press Actions (Fix 4 in `hmi_thread.cpp`)

`kFeatureLongPress` was not enabled; `kEventLongPressed` was never dispatched.

| Step | Action | Pass | Fail |
|------|--------|------|------|
| Configure a held action on a key (e.g., profile switch after 500 ms hold). Hold the key for 600 ms. | Action fires at 500 ms threshold | Nothing happens on hold |

### 5.6 HMI Mutex on ISR Callback (Fix 3)

Key event handler now wraps `keyState` updates with `SemaphoreGuard(hmi_thread._hmi_mutex)`.

| Step | Action | Pass | Fail |
|------|--------|------|------|
| Press two keys simultaneously and hold while monitoring serial | Key state bitmask in `{"ks":…}` JSON is correct (both bits set), no garbled frames | Corrupted `ks` value or assert in RTOS |

---

## 6. Profile Save / Load / Migration

Covers `HapticProfileManager.cpp` — `type=` vs `==` fix, `"profiles"` serialisation fix, file-handle leak fix.

| Step | Action | Pass | Fail |
|------|--------|------|------|
| 6.1 | Send `{"profile":"TestProf","updates":{"knob":[{"type":"midi","cc":7,"channel":1,"valueMin":0,"valueMax":127,"angleMin":0,"angleMax":6.28,"step":1,"haptic":{"mode":0,"detentCount":127,"startPos":0,"endPos":127,"vernier":5,"outputRamp":5000,"detentStrength":3,"kxForce":false}}]}}` then `{"save":true}` | ACKs received; `TestProf.json` written to `/profiles/` | Error or file not created |
| 6.2 | Reboot; send `{"profiles":"#all"}` | `TestProf` in list; `{"current":"…"}` field present | Profile absent or list malformed |
| 6.3 | **`profiles=` bug migration**: load a pre-fix profile JSON containing `"type":"profiles"` for a `KA_PROFILE_CHANGE` action. The code at line 550 of `HapticProfileManager.cpp` accepts both `"profile"` and `"profiles"` as aliases and marks dirty. After a `{"save":true}`, re-read the file. | File now contains `"type":"profile"` (canonical); `dirty` was true | File still contains `"type":"profiles"` |
| 6.4 | Create 3 profiles. Send `{"profiles":["C","A","B"]}` (reorder). Check memory order then `{"save":true}` + reboot. | `{"profiles":"#all"}` returns `["C","A","B"]` | Order unchanged |
| 6.5 | Update a profile using `{"profile":"TestProf","updates":{...}}` with only some fields (partial update). | Only the sent fields change; others retain previous values | Entire profile zeroed on partial update |

---

## 7. Serial JSON API — New Commands and ACKs

All mutating commands now reply with `{"ack":"<cmd>","ok":true}` or `{"ack":"<cmd>","ok":false,"error":"..."}`.

### 7.1 ACK Shape Verification

| Command sent | Expected ACK | Pass | Fail |
|---|---|---|---|
| `{"save":true}` | `{"saved":true}` then `{"ack":"save","ok":true}` | Both frames received | Missing or bare println |
| `{"load":true}` | `{"ack":"load","ok":true}` | Received | Missing |
| `{"current":"Default Profile"}` | `{"ack":"current","ok":true}` | Received | Missing |
| `{"recalibrate":true}` | `{"ack":"recalibrate","ok":true}` | Received | Missing |
| `{"settings":{"debug":true}}` | `{"ack":"settings","ok":true}` | Received | Missing |
| `{"profile":"P","updates":{...}}` | `{"ack":"profile","ok":true}` | Received | Missing |
| `{"profiles":["P"]}` | `{"ack":"profiles","ok":true}` | Received | Missing |
| Invalid JSON `{bad}` | `{"error":"JSON parse error","msg":"…"}` | Received | Silent drop or crash |

### 7.2 Telemetry Shape (`{a, t, v}`)

Rotate knob slowly by 1 full turn, then 2 full turns.

| Field | Expected | Pass | Fail |
|---|---|---|---|
| `"a"` (shaft_angle, radians) | Increases monotonically approx 0 → 6.28 → 12.57 | Monotonic float | Stuck at 0 or same as `"p"` |
| `"t"` (integer turns) | 0, then 1 after first full revolution | Increments correctly | Stays 0 |
| `"v"` (velocity rad/s) | Non-zero while rotating; ~0 when still | Non-zero during motion | Always 0 |
| `"p"` (legacy uint16 position) | 0–end_pos as before | Present alongside a/t/v | Missing |

### 7.3 `message` Command (Screen Display)

```json
{"message":{"title":"Hello","text":"World","duration":2000}}
```

| Step | Pass | Fail |
|---|---|---|
| Send above command | LCD shows "Hello" / "World"; `{"ack":"message","ok":true}` returned | No display change or no ACK |

### 7.4 `screen` Command

```json
{"screen":{"title":"T","data1":"D1","data2":"D2","data3":"D3","data4":"D4"}}
```

| Step | Pass | Fail |
|---|---|---|
| Send above | LCD updates with all five fields; no ACK expected (read-only style command) | LCD unchanged |

---

## 8. WiFi Environment (`nanofoc_d_wifi`)

**Risk: HIGH — OTA rollback is the most dangerous test. Read the OTA rollback sub-section carefully.**

### 8.1 Build and Flash

| Step | Action | Pass | Fail |
|---|---|---|---|
| 8.1.1 | `pio run -e nanofoc_d_wifi -t upload` | Builds and flashes; `[WIFI] ...` messages appear | Build error (likely missing `ESPAsyncWebServer-esphome`) |
| 8.1.2 | Configure WiFi: `{"wifi":{"ssid":"MyNet","password":"pw","enabled":true}}` | `{"ack":"wifi","ok":true}` returned; `[WIFI] Connecting to 'MyNet'...` in Serial | No ACK; no connect attempt |
| 8.1.3 | Confirm STA connect | Serial: `[WIFI] Web server ready — IP: x.x.x.x` | `[WIFI] Reconnecting` repeated without success |
| 8.1.4 | GET `http://<device-ip>/status` | JSON with `ip`, `rssi`, `device`, `fw` fields | Connection refused or 404 |

### 8.2 SoftAP Provisioning

| Step | Action | Pass | Fail |
|---|---|---|---|
| 8.2.1 | Set `{"wifi":{"enabled":true,"ssid":""}}` and reboot | SoftAP `NanoD-Setup` visible in WiFi scanner; navigating to `192.168.4.1` shows provisioning form | AP not visible |
| 8.2.2 | Submit form with valid SSID/password | Device transitions to STA mode; SoftAP stops; Serial shows `[WIFI] SoftAP stopped` | Device stays in AP mode |

### 8.3 WebSocket API

| Step | Action | Pass | Fail |
|---|---|---|---|
| 8.3.1 | Connect WebSocket client to `ws://<ip>/ws` | Receives `{"connected":true,"ip":"...","device":"...","fw":"..."}` | Connection refused |
| 8.3.2 | Send `{"settings":""}` over WebSocket | Settings JSON echoed back | No response |
| 8.3.3 | Send `{"wifi":{"ssid":"X","enabled":true}}` over WebSocket | `{"ack":"wifi","ok":true}` returned to this client only | Missing ACK |

### 8.4 ArduinoOTA Update

| Step | Action | Pass | Fail |
|---|---|---|---|
| 8.4.1 | With device on network, run `pio run -e nanofoc_d_wifi -t upload --upload-port <device-ip>` (or use Arduino IDE OTA) | OTA upload to app1 slot completes; device reboots into new firmware | Transfer error or reboot to old slot |
| 8.4.2 | Post-OTA: device runs 2+ seconds, calls `mark_ota_valid()` | `[WIFI] OTA rollback cancelled — firmware marked valid.` in Serial | Message absent — OTA valid not marked (risky) |

### 8.5 OTA Rollback Test (HIGHEST RISK)

**This test exercises the bootloader's fail-safe. A deliberately bad build is required. Do not skip.**

Precondition: bootloader built with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` in ESP-IDF sdkconfig (or via Espressif's Arduino core OTA rollback support). Confirm partition table has `otadata` entry (it does: `0xe000, 0x2000`).

| Step | Action | Pass | Fail |
|---|---|---|---|
| 8.5.1 | Build a "bad" firmware: in `main.cpp setup()`, add `while(1);` after USB init so `mark_ota_valid()` is never called. Flash via OTA. | Device reboots, gets stuck in the infinite loop, watchdog fires, device resets. After 1–2 resets the bootloader detects repeated failures and reverts to the previous app slot. Device boots into the last good firmware. Serial: `[WIFI] OTA rollback cancelled — firmware marked valid.` from the recovered slot. | Device stays stuck on bad firmware permanently (rollback not enabled or not working) |
| 8.5.2 | Restore good firmware via OTA after rollback | Normal boot | Requires UART re-flash (rollback consumed the slot) |

### 8.6 HTTP Firmware Update (`/update` endpoint)

| Step | Action | Pass | Fail |
|---|---|---|---|
| 8.6.1 | POST a valid `.bin` to `http://<ip>/update` | Response: `OK — rebooting`; device reboots into new firmware | `FAIL` response or no reboot |
| 8.6.2 | POST a deliberately corrupted binary | ESP32 Update library rejects (wrong magic); Serial: `[OTA/HTTP] end error: …` | Device bricks (corrupted flash) |

---

## 9. Sprite Upload and Display (`nanofoc_d` or any env)

Covers `sprite_store.cpp` — chunked base64 upload, CRC validation, LVGL `L:` driver.

### 9.1 Begin / Data / End Upload

Send these three commands in sequence over serial (or WebSocket):

```json
{"sprite":{"op":"begin","name":"test.bmp","size":1234}}
{"sprite":{"op":"data","seq":0,"data":"<base64-chunk>"}}
{"sprite":{"op":"end","crc32":3735928559}}
```

| Step | Pass | Fail |
|---|---|---|
| `begin` with valid name and size ≤ 65536 | `{"ack":"sprite","ok":true}` | `{"ok":false,"error":"invalid size"}` |
| `data` with correct `seq` (0, 1, 2, ...) | ACK true for each chunk | `{"ok":false,"error":"sequence error"}` aborts upload |
| `end` with correct `crc32` | `{"ack":"sprite","ok":true}`; file appears in `/sprites/test.bmp` | `{"ok":false,"error":"CRC mismatch"}` — file removed |

### 9.2 List and Select

| Command | Expected | Pass | Fail |
|---|---|---|---|
| `{"sprite":{"op":"list"}}` | `{"ack":"sprite","ok":true}` (names list in error field per `handle_list` implementation — verify exact format) | Listing returned | Missing or crash |
| `{"sprite":{"op":"select","name":"test.bmp"}}` | `{"ack":"sprite","ok":true}`; `DeviceSettings.activeSprite == "test.bmp"` | Confirmed via `{"settings":""}` response | `activeSprite` unchanged |
| `{"sprite":{"op":"delete","name":"test.bmp"}}` | `{"ack":"sprite","ok":true}`; file gone from LittleFS | Confirmed | File persists |

### 9.3 LVGL Display

| Step | Action | Pass | Fail |
|---|---|---|---|
| 9.3.1 | Upload a valid 240×240 BMP that LVGL can decode. Select it. | Image appears on the GC9A01 circular display | Display unchanged or shows garbage |
| 9.3.2 | Test LVGL `L:` FS driver: verify path `L:/sprites/test.bmp` resolves (add a debug print if needed) | Image loaded from LittleFS via LVGL `lv_img_set_src` | LVGL reports `LV_FS_RES_NOT_EX` (driver registration failed) |

### 9.4 Quota Enforcement

| Test | Action | Pass | Fail |
|---|---|---|---|
| Overflow single sprite size | `begin` with `size=65537` | `{"ok":false,"error":"invalid size"}` | Accepted |
| Overflow total quota | Upload sprites until >512 KB total | `{"ok":false,"error":"storage quota exceeded"}` | Accepted (filesystem full crash) |
| Max sprite count | Upload 17 sprites | 17th `begin`: `{"ok":false,"error":"sprite slot limit reached"}` | Accepted |

---

## 10. Audio Environment (`nanofoc_d_audio`)

Gated by `#if NANO_AUDIO`. Requires I2S amplifier populated on board.

| Step | Action | Pass | Fail |
|---|---|---|---|
| 10.1 | `pio run -e nanofoc_d_audio -t upload` | Builds; startup chime plays on boot (`audio_play(SOUND_CHIME)`) | Silence or I2S error |
| 10.2 | Press any key | Click sound plays (`audio_click()` in `kEventPressed` handler) | No sound |
| 10.3 | Rotate knob to a detent | Haptic click sound (per profile `audio_config`) | No sound |
| 10.4 | Confirm `NANO_AUDIO=0` (core env) is silent: run `nanofoc_d` env, press key | No sound (stubs are no-ops) | Sound plays (define leaked) |

---

## 11. Full Environment (`nanofoc_d_full`)

WiFi + Audio simultaneously.

| Step | Action | Pass | Fail |
|---|---|---|---|
| 11.1 | `pio run -e nanofoc_d_full -t upload`; confirm build size is within the 1.3 MB OTA slot | Build succeeds; size < 1.3 MB | Linker overflow |
| 11.2 | All WiFi tests from section 8 pass | All pass | Regressions from audio adding to stack |
| 11.3 | All audio tests from section 10 pass simultaneously with WiFi active | Both functional | Task starvation or I2S / WiFi RF interference |

---

## 12. Factory Reset (Brick Recovery)

`DeviceSettings::factoryReset()` wipes LittleFS settings file, both NVS namespaces, and applies defaults.

| Step | Action | Pass | Fail |
|---|---|---|---|
| 12.1 | Trigger factory reset via the button combo (defined in integrator code; check `main.cpp` / `hmi_thread` for the specific combo — currently not explicitly tested in this update batch). | Serial: `[DeviceSettings] FACTORY RESET` → `Factory reset complete — defaults applied`. On next reboot `{"ack":"boot","ok":true}` with default `deviceName`. | Settings persist after reset |
| 12.2 | After factory reset, confirm profiles also cleared | `{"profiles":"#all"}` returns only `["Default Profile"]` | Old profiles remain |

---

## Test Sign-Off Summary

| Section | Description | Passed | Failed | Notes |
|---------|-------------|--------|--------|-------|
| 1 | Initial flash | | | |
| 2 | Settings persistence + CRC | | | |
| 3 | USB-PD voltage negotiation | | | HIGH RISK |
| 4 | Thread synchronisation | | | |
| 5 | Haptic/knob bug fixes (6 items) | | | |
| 6 | Profile save/load/migration | | | |
| 7 | Serial JSON API + ACKs + telemetry | | | |
| 8 | WiFi env (STA, SoftAP, OTA, **rollback**) | | | HIGH RISK |
| 9 | Sprite upload + display | | | |
| 10 | Audio env | | | |
| 11 | Full env | | | |
| 12 | Factory reset | | | |

**Device is not field-ready until sections 3 (PD voltage confirmed safe) and 8.5 (OTA rollback confirmed working) both have a Passed entry.**
