# Changelog — Binaris Nano_D++ Firmware + App

All changes cover the merge commit `79ca593` (branch `runger`) for the firmware
(`fw/`) and the corresponding app commit `2332693` for the ZERO/ONE Electron
desktop application (`app/`).

---

## [1.2.0] — 2026-06-28 — Album artwork & music profile

### Added — Firmware

#### Album artwork display (`src/lcd_thread.cpp`, `src/sprite_store.cpp`)

- **RGB565 artwork streaming**: the host pre-renders album art as a 240×240
  little-endian RGB565 frame and uploads it as a `.rgb565` sprite (115200 bytes).
  The firmware streams the frame from flash to the GC9A01 in 16-row strips via
  TFT_eSPI — the full image never resides in RAM, so artwork works on this
  PSRAM-less board.
- **Music profile idle/active behaviour**: when a `.rgb565` sprite is active the
  LCD shows the album cover while the knob is idle, then switches to the native
  value screen (big volume number + arc) while the knob turns, and returns to
  the cover ~1.2 s after it stops. LVGL is paused while the raw frame is on
  screen.

#### Binary fast-path sprite upload (`src/sprite_store.cpp`, `src/com_thread.cpp`)

- **`binbegin` / `binchunk` / `end` upload path**: ~10× faster than the base64
  `data` path (~2.5 s vs ~13 s for a 115 KB RGB565 frame). After
  `{"sprite":{"op":"binbegin",...}}`, each chunk is a JSON header
  `{"sprite":{"op":"binchunk","len":N}}` followed immediately by N raw bytes.
  The device reads the bytes into RAM, commits them to flash in one write, and
  responds `{"binack":{"rd":<total>,"ok":true}}`. The host must wait for the
  `binack` before sending the next chunk (chunk-ack barrier prevents overlapping
  reads and flash writes). Recommended chunk size: 4096 bytes; the USB-CDC RX
  queue is enlarged to 8 KB so a full chunk cannot overflow it.
- **Motor parks during upload**: the FOC thread applies zero torque while a
  binary upload is active, eliminating knob buzz from flash-write stalls on the
  shared bus. Haptic detents resume automatically when the upload ends.

#### New serial commands

- **`{"ring":{"primary":<0xRRGGBB>,"secondary":<0xRRGGBB>,"mode":<0-3>}}`**:
  pushes a transient LED config to the HMI thread in the current album palette
  without modifying the saved profile. Also stores `primary` as the album color
  for LCD music overlays (seek arc, cover-to-value transitions).
- **`{"seek":{"pos":<0.0..1.0>}}`**: sets the song-progress arc on the LED ring
  (album color, same geometry as the volume pointer). `pos < 0` hides the arc.
- **`{"reboot":true}`**: ACK then `esp_restart()`.
- **`{"reboot":"bootloader"}`**: ACK then `usb_persist_restart(RESTART_BOOTLOADER)` —
  restarts the ESP32-S3 into ROM USB download mode with native USB kept enumerated
  for buttonless firmware flashing via `esptool`. A manual EN-button tap boots
  the new image after flashing.

---

## [1.1.0] — 2024-05-28

### Upgrade notes

1. **LittleFS replaces SPIFFS.** The first flash after this update will find no
   settings file on LittleFS; the firmware will boot with factory defaults and
   save a fresh file on next `{"save":true}`. All previously saved SPIFFS data
   (profiles, device settings) is lost. Re-configure via the app or serial JSON
   after flashing.
2. **Re-flash the filesystem partition.** PlatformIO must now be told
   `board_build.filesystem = littlefs` (already set in `platformio.ini`). Run
   *Upload Filesystem Image* in addition to *Upload* on a clean board.
3. **Profile JSON format: `type: "profiles"` key action renamed to `"profile"`.** Files
   persisted by older firmware that contain `"type":"profiles"` on a key action
   are accepted (the parser aliases it) but will be rewritten as `"type":"profile"` on
   the next `save`. The knob-type field `"profiles"` (for `KV_DEVICE_PROFILES`)
   is unchanged.
4. **OTA rollback.** The firmware now calls
   `esp_ota_mark_app_valid_cancel_rollback()` 2 seconds after all threads start.
   If the device boots the new image but crashes before that window, the
   bootloader will revert to the previous OTA slot automatically (requires
   bootloader built with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`).
5. **Dependency pins.** `platformio.ini` now pins previously-floating caret
   dependencies (`lvgl@9.0.0`, `askuric/Simple FOC@2.3.3`,
   `Adafruit TinyUSB Library@3.1.0`, `FastLED@3.6.0`). A `pio update` before
   first build is recommended to populate the cache.
6. **WiFi / audio are opt-in build envs.** The default env `nanofoc_d` does not
   define `WIFI_ENABLED` or `NANO_AUDIO`. Use `nanofoc_d_wifi`,
   `nanofoc_d_audio`, or `nanofoc_d_full` to include those subsystems.

---

### Fixed — Firmware

#### Filesystem / persistence (`src/DeviceSettings.cpp`)

- **Atomic settings write** (`saveAtomic()`): settings are now written to
  `/device_settings.tmp` and renamed over `/device_settings.json` only on
  success. A partial write no longer corrupts the live file.
- **CRC-32 validation** (schema version 2): a CRC-32 (PKZIP/ISO-3309
  polynomial) is appended as `"crc32"` on every save. On load, the stored CRC
  is checked; a mismatch or JSON parse error triggers `_applyDefaults()` with
  a log line (`Settings corrupt — falling back to defaults`). The bad file is
  preserved for forensics; WiFi credentials are still recovered from NVS.
- **Schema version forward-migration**: files with `schemaVersion < 1` are
  rejected; files at version 1 (pre-CRC) are accepted without CRC check so
  existing v1 files survive the first boot after upgrade.
- **WiFi credentials separated into NVS** (`nano_wifi` namespace): credentials
  are no longer stored in plaintext in the main JSON file sent over serial.
  `toJSON()` always redacts the password to `"***"` on the serial/WS path;
  `wifi_thread` reads NVS directly via `_loadWifiFromNvs()`.
- **Corrupt-file fallback does not infinite-loop**: the load path calls
  `_applyDefaults()` and sets `dirty = true` so the next `saveAtomic()` writes
  clean defaults, ending the boot normally.
- **NVS Preferences now mutex-guarded** (`_nvsMutex`): all calls to
  `nano_preferences` and `wifi_preferences` are protected by a
  `xSemaphoreCreateRecursiveMutex()`.

#### Thread synchronisation (`src/semaphore_guard.h`, `src/hmi_thread.cpp`,
`src/DeviceSettings.cpp`)

- **`SemaphoreGuard` RAII helper added** (`src/semaphore_guard.h`): wraps
  `xSemaphoreTakeRecursive` / `xSemaphoreGiveRecursive` so the lock is always
  released on scope exit, even under early-return paths.
- **`DeviceSettings` singleton now protected by a recursive mutex** (`_mutex`):
  all public accessors (`take()` / `give()`) use it; `operator=` and `toJSON`
  bracket their field access inside a `take()`/`give()` pair.
- **HMI keyState mutex** (`_hmi_mutex`): created in `HmiThread::run()` before
  ISR callbacks can fire. `HmiThreadButtonHandler::handleEvent()` acquires it
  via `SemaphoreGuard` on every button event (Fix 3 in source).
- **`global_sleep_flag` atomic access**: the flag is set/read only from the COM
  thread loop and read in HMI/LED callbacks; reads are now guarded behind the
  existing per-thread check rather than a bare global read.

#### USB-PD negotiation (`src/hmi_thread.cpp` — `init_pd()`)

- **Fix 2a — STUSB4500 absent guard**: `usb_pd.begin()` is checked; on failure
  `DeviceSettings::setPdVoltage(5.0f)` is called and `POWER_5V_USB` is
  returned. Boot continues safely on 5 V.
- **Fix 2b — NVM write skipped when already correct**: NVM is only programmed
  (and soft-reset triggered) when `usb_pd.getPdoNumber() != 2`, preventing
  unnecessary flash wear on every boot.
- **Fix 2c — soft reset after NVM programming**: after programming the
  STUSB4500 NVM, the code issues a `SOFT_RESET` (`0x51 = 0x0D`) over I2C to
  the default address `0x28` and waits 100 ms for renegotiation to settle.
- **Fix 2d — negotiated PDO voltage read back and stored**: the 32-bit
  `RDO_REG_STATUS` register (`0x91`) is read over I2C; bits `[30:28]` give the
  selected object position (1-indexed). The configured voltage for that PDO is
  looked up via `usb_pd.getVoltage(selected_pdo)` and clamped to `[5.0, 9.0]`
  V before calling `DeviceSettings::getInstance().setPdVoltage(negotiated_v)`.
  Previously this path was missing entirely; the motor driver was hardcoded to
  5 V.

#### Motor voltage supply (`src/foc_thread.cpp` lines 46-52)

- **`driver.voltage_power_supply` now set from `DeviceSettings.pdVoltage`**:
  ```cpp
  float pdV = DeviceSettings::getInstance().pdVoltage;
  if (pdV < 5.0f || pdV > 9.0f) pdV = 5.0f;
  driver.voltage_power_supply = pdV;
  driver.voltage_limit = pdV;
  ```
  Previously both were hardcoded to 5 V regardless of the negotiated PD
  contract.

#### HMI / LED (`src/hmi_thread.cpp`)

- **Fix 1 — idle LED array bounds overrun**: `IdleLeds()` previously looped to
  `NANO_LED_A_NUM + 8` (68), writing past the end of the 60-element `leds[]`
  ring array. The loop is now split into `for (i < NANO_LED_A_NUM)` for
  `leds[]` and `for (i < NANO_LED_B_NUM)` for `ledsp[]`.
- **Fix 4 — long-press (held) key actions never dispatched**: `ButtonConfig`
  now has `kFeatureLongPress` enabled with a 500 ms threshold.
  `kEventLongPressed` events are handled in `HmiThreadButtonHandler::handleEvent()`
  and fire the `num_held_actions` list. Previously, held actions were parsed and
  stored but never triggered.
- **Fix 5 — `_constrain` result not assigned**: `updateValue()` discarded the
  return value of `_constrain()` so angle clamping was a no-op. The result is
  now assigned back to `angle`.
- **Fix 6 — knob mapping overwritten with raw encoder position**: a line that
  reassigned `currentValue` to the raw encoder integer after the mapped value
  was computed has been removed. The mapped `value` is now correctly stored.

#### COM thread (`src/com_thread.cpp`)

- **Serial timeout shortened to 50 ms** (`Serial.setTimeout(50)`): was 1000 ms,
  causing a 1-second stall on any partial frame.
- **Heap leak on motor command when queue full**: `put_motor_command()` now only
  allocates a `String*` when there is space; the previous code allocated
  unconditionally and dropped the pointer if `xQueueSend` failed.
- **Bare `Serial.println()` calls replaced**: all bare string prints injected
  non-JSON lines into the output stream. They are now wrapped in
  `{"debug":"..."}` JSON documents.
- **`message` command fixed**: the handler previously tested
  `v.is<String>()` on the `message` key which always failed because the
  protocol sends an object `{title, text, duration}`. The handler now tests
  `v.is<JsonObject>()` and dispatches `LCD_LAYOUT_MESSAGE` correctly.
- **Profile reorder command — real sort implemented**: `handleProfilesCommand()`
  with a `JsonArray` argument now performs a two-pass operation: pass 1 removes
  profiles not present in the incoming list; pass 2 uses a selection sort to
  reorder `profiles[]` to match the requested order. The `current_profile`
  pointer is re-anchored by name after the sort to prevent dangling.
- **Profile command uses correct JSON key**: response for a profile read now
  uses `doc["profile"]` (not `doc["profiles"]`) matching the protocol.
- **ACKs added for all mutating commands** (see Added section below).
- **`sendAck("boot", true)` on startup**: the initial boot message is now a
  valid JSON document.

#### HapticProfileManager (`src/HapticProfileManager.cpp`)

- **`type == "profiles"` comparison operator bug fixed** (line ~472): the knob
  type comparison used `=` (assignment) instead of `==`, so parsing always fell
  into the `KV_DEVICE_PROFILES` branch regardless of the actual type string.
  Now correctly `== "profiles"`.
- **Key action type `"profiles"` aliased to `"profile"`**: `keyActionFromJSON()`
  accepts both `"profile"` and `"profiles"` for `KA_PROFILE_CHANGE`; any
  profile saved with the old key is marked `dirty = true` so it is rewritten
  on next `toSPIFFS()`.
- **File handle leak on non-JSON files**: `toSPIFFS()` iterated the profiles
  directory but only called `file.close()` when the file was a `.json` file and
  was not in the active set. Non-`.json` files and directories were closed
  before the `if (!found)` branch but the handle for passing entries was not.
  The close is now called unconditionally before the next `openNextFile()`.

---

### Added — Firmware

#### WiFi / OTA (`src/wifi_thread.cpp`, gated by `-DWIFI_ENABLED`)

- **STA auto-connect with exponential backoff**: `WifiThread::_connect()` calls
  `WiFi.begin()` non-blocking; `loop()` polls `WL_CONNECTED` and retries with
  backoff from `BACKOFF_MIN_MS` doubling to `BACKOFF_MAX_MS`.
- **SoftAP provisioning**: if `wifiEnabled` is true but no SSID is stored, the
  chip opens a `WIFI_AP_STA` access point (`SOFTAP_SSID = "NanoD-Setup"`, open,
  no password). The raw TCP server is started immediately in AP mode; connect to
  the AP then send `{"wifi":{"ssid":"...","password":"...","enabled":true}}\n`
  to `192.168.4.1:3333` to provision credentials and trigger `_connect()`.
- **ArduinoOTA**: once STA is connected, `ArduinoOTA` is started with
  password `WIFI_OTA_PASSWORD` (default `nanod-ota`, configurable via build
  flag) and the device hostname set from `DeviceSettings::deviceName`.
- **Raw TCP JSON server on port 3333** (`WifiThread::TCP_PORT`): mirrors the
  serial JSON API. Up to `MAX_TCP_CLIENTS` (4) simultaneous connections.
  Inbound newline-delimited lines are forwarded to `com_thread` via
  `net_submit()`; lines longer than `MAX_LINE_BYTES` (2048 bytes) are dropped.
  All outbound frames pass through `com_thread.emit()`, which enqueues a heap
  copy to the `_q_net_out` FreeRTOS queue; `wifi_thread` drains that queue each
  loop iteration and `client.println()`s each frame to connected clients.
  `ESPAsyncWebServer`, `AsyncTCP`, WebSocket (`/ws`), HTTP `/update`, and HTTP
  `/status` are no longer used or compiled.
- **OTA rollback**: `WifiThread::mark_ota_valid()` calls
  `esp_ota_mark_app_valid_cancel_rollback()`. `main.cpp` calls this 2 seconds
  after all threads start. If the device reboots before this point (e.g. crash
  loop), the bootloader reverts to the previous OTA slot.
- **Task watchdog** in `taskEntry()`: the WiFi task registers with
  `esp_task_wdt_add()` and resets the watchdog each iteration.
- **`apply_settings()` public API**: called by `com_thread` when a `{"wifi":{…}}`
  JSON command arrives. Disconnects (`WiFi.mode(WIFI_OFF)`) if `wifiEnabled`
  is false; starts SoftAP if no SSID; otherwise forces a fresh connect cycle.

#### Sprite store (`src/sprite_store.cpp`, `src/sprite_store.h`)

- **Chunked image upload via the JSON protocol** → LittleFS → LVGL display:
  A state-machine upload context (`UploadCtx`) tracks the active upload, its
  declared size, bytes written, next expected sequence number, and a running
  CRC-32.
- **`begin` sub-command**: validates name (≤ 32 chars), declared size
  (> 0, ≤ 64 KB), per-name replacement accounting against the 512 KB total
  quota and 16-sprite slot cap, then opens the LittleFS file for writing.
- **`data` sub-command**: accepts base64-encoded chunks with a `seq` counter.
  Out-of-order sequence numbers abort the upload and delete the partial file.
  A self-contained base64 decoder is included (no external dependency).
- **`end` sub-command**: flushes and closes the file, verifies `written ==
  expected` and CRC-32 against the `"crc32"` field provided by the sender.
  On mismatch the partial file is deleted.
- **`list` sub-command**: returns a comma-separated list of names stored under
  `/sprites/`.
- **`delete` sub-command**: removes the file; clears `DeviceSettings::activeSprite`
  if the deleted sprite was selected.
- **`select` sub-command**: sets `DeviceSettings::activeSprite`; an empty
  or absent `name` field deselects.
- **LVGL filesystem driver** registered on letter `'L'`: maps
  `lv_fs` paths of the form `L:/sprites/<name>` to LittleFS via
  `open_cb / close_cb / read_cb / seek_cb / tell_cb` (write not registered —
  display is read-only).
- **Storage limits**: `MAX_SPRITE_SIZE = 64 KB` per file,
  `MAX_SPRITES_BYTES = 512 KB` total, `MAX_SPRITES = 16` slots.

#### Audio enable path (`src/audio/`, `src/hmi_thread.cpp`, gated by `-DNANO_AUDIO=1`)

- `audioPlayer.audio_init()` called from `HmiThread::init()` under `#if NANO_AUDIO`.
- `audioPlayer.audio_loop()` called each HMI iteration under `#if NANO_AUDIO`.
- `audio_play(SOUND_CHIME)` emitted at startup (no-op stub if `NANO_AUDIO=0`).
- `audio_click()` emitted on key press (no-op stub if `NANO_AUDIO=0`).
- `ComThread::dispatchAudioConfig()` dispatches audio config to `audioPlayer`
  when the current profile changes (body compiled only under `#if NANO_AUDIO`).

#### ACKs for all mutating commands (`src/com_thread.cpp`)

`sendAck(cmd, ok[, errMsg])` emits:
```json
{"ack":"<cmd>","ok":true}
{"ack":"<cmd>","ok":false,"error":"<message>"}
```
ACKs are now emitted for: `boot`, `current`, `recalibrate`, `save`, `load`,
`profile` (create/update), `profiles` (reorder), `settings`, `message`,
`wifi`, `sprite`.

#### Richer angle telemetry (`src/com_thread.cpp` — `handleEvents()`)

Each encoder position change now emits:
```json
{"p":<uint16>,"a":<float rad>,"t":<int32 turns>,"v":<float rad/s>}
```
- `p`: legacy uint16 position (kept for back-compatibility).
- `a`: `motor.shaft_angle` in radians.
- `t`: integer turn count, `floor(a / 2π)`.
- `v`: `motor.shaft_velocity` in rad/s.

#### Factory reset (`src/DeviceSettings.cpp` — `factoryReset()`)

- Removes `/device_settings.json` and `/device_settings.tmp` from LittleFS.
- Clears both NVS namespaces (`nano_D`, `nano_wifi`).
- Calls `_applyDefaults()` and sets `dirty = true`.

---

### Changed

#### Filesystem standardised on LittleFS (`src/DeviceSettings.cpp`,
`src/HapticProfileManager.cpp`, `src/sprite_store.cpp`)

- All three subsystems use `LittleFS` (not SPIFFS). The `#define FS_HANDLE`
  macro in `DeviceSettings.cpp` points to `LittleFS`. `DeviceSettings::init()`
  calls `LittleFS.begin(true)` (format-on-fail).
- `platformio.ini` sets `board_build.filesystem = littlefs` in the base env.
- The `fromSPIFFS()` / `toSPIFFS()` method names are retained for minimal diff
  but they now operate on LittleFS.

#### Pinned library dependencies (`platformio.ini`)

Previously floating caret specifiers caused non-reproducible builds (a fresh
clone could pull incompatible minor versions). All four critical libraries are
now pinned:

| Library | Pinned version |
|---|---|
| `lvgl/lvgl` | `9.0.0` |
| `askuric/Simple FOC` | `2.3.3` |
| `Adafruit TinyUSB Library` | `3.1.0` |
| `fastled/FastLED` | `3.6.0` |

#### Build environments (`platformio.ini`)

Four named envs replace the single env:

| Env | Feature set | Extra flags / deps |
|---|---|---|
| `nanofoc_d` (default) | Core: bug fixes, PD, sprites, brick-proofing | — |
| `nanofoc_d_wifi` | + WiFi STA, ArduinoOTA, raw TCP JSON server (port 3333) | `-DWIFI_ENABLED` (arduino-esp32 core only; `ESPAsyncWebServer-esphome` removed) |
| `nanofoc_d_audio` | + I2S audio feedback | `-DNANO_AUDIO=1 -DAUDIO_EN` |
| `nanofoc_d_full` | WiFi + audio | all of the above |

Build size for `nanofoc_d`: Flash 65.6% (840 KB of 1.28 MB OTA app slot),
RAM 46.8%.

#### Firmware version string

`-DNANO_FIRMWARE_VERSION=\"1.1.0\"` set in `platformio.ini`. Exposed over
serial as `settings.firmwareVersion` and in the TCP `connected` greeting.

---

### Breaking changes

1. **SPIFFS → LittleFS**: existing SPIFFS data is not migrated. On first flash,
   the device will boot with factory defaults. All profiles and settings must
   be re-applied.
2. **Profile `type:"profiles"` key-action field renamed `type:"profile"`**:
   files using the old spelling are auto-migrated on next `save`, but host
   software that hard-codes `"profiles"` for key-action type will emit a
   warning. The knob-level `type:"profiles"` (for `KV_DEVICE_PROFILES`) is
   **not** renamed.
3. **Settings file now CRC-checked (schema v2)**: a settings file written by
   older firmware (schema v1, no `crc32` field) is accepted on first boot and
   then rewritten as schema v2. Any file at schema v0 (missing
   `schemaVersion`) is rejected and defaults are used.
4. **`driver.voltage_power_supply` is now dynamic**: code that assumes 5 V
   motor supply (e.g. hardcoded current limits or external torque calculators)
   must be updated to read `DeviceSettings::pdVoltage`.

---

### Fixed — App (ZERO/ONE, `app/`)

#### `app/src/main/nanoSerialApi.ts`

- **Fix 1 / FIX #2 — `disconnect()` called on wrong object**: the handler
  previously closed the `PortInfo` struct rather than the open `SerialPort`.
  Now `conn.port.close()` is called on the live connection.
- **FIX #3 — double JSON serialisation**: the preload's `send()` already calls
  `JSON.stringify(obj)`; the main-process `ipcMain.handle` was also calling
  `JSON.stringify`, producing double-encoded strings. The handler now passes the
  string through unchanged.
- **FIX #4 — `_list()` never resolves on success**: the `resolve()` call inside
  `.then()` was missing; the promise hung forever. Added `resolve()` after
  populating `found_serials`. Also added detach detection: serials absent from
  the current scan are emitted as `device-detached` and removed from state.
- **FIX #5 — serial write errors swallowed**: `port.write()` callback now
  rejects the returned promise when `err` is non-null.
- **FIX #6 — wrong error event name**: `port.on('error', ...)` now emits
  `nanoSerialApi:device-error` (matching the listener in `index.ts`); previously
  it emitted a different name that was never caught.
- **FIX #7 — double-open on reconnect**: `connect()` short-circuits with
  `Promise.resolve(deviceid)` if `connected_nano_devices[deviceid]` already
  exists.
- **FIX #8 — RX buffer unbounded growth**: a `MAX_DATA_BUFFER_BYTES = 65536`
  cap is checked on every `_handle_data()` call; overflow emits
  `device-error` and resets the buffer. Root cause of the `"undefined"`
  prefix (buffer field never initialised to `''`) is fixed at the source:
  `connect()` now writes `{ port, data: '' }` on open.

#### `app/src/main/index.ts`

- **FIX #9 — `openExternal` / `setWindowOpenHandler` URL validation**: both
  `ipcMain.on('electron:openExternal', …)` and `setWindowOpenHandler` now parse
  the URL and only forward `http:` / `https:` schemes to the OS shell; other
  schemes (e.g. `javascript:`, `file:`) are rejected with a console warning.
- **FIX #10 — renderer context isolation**: `webPreferences` now has
  `contextIsolation: true` and `nodeIntegration: false`, enforcing the
  Electron renderer isolation boundary.

#### `app/src/renderer/src/deviceStore.ts`

- **`profiles` field — `_list()` never detaches**: fixed upstream in
  `nanoSerialApi._list()` (see FIX #4 above); the store's `device-detached`
  handler now fires correctly.
- **`update.profiles` event handler**: the incoming `profiles` array now drives
  a delayed profile-fetch loop (`setTimeout(…, i * 30)`), replacing the
  previous call that resolved a never-settling promise.
- **`saved` event from `update` payload**: firmware sends `{"saved":true}`
  inside an `update` payload (not as a separate IPC event). The handler now
  checks `update.saved === true` and calls `setDirtyState(false)` accordingly.
- **Error event name mismatch** (`eventid === 'error'`): the handler also checks
  `eventid === 'update' && dataString.includes('error')` because the firmware
  wraps errors inside `update` events, not a dedicated `error` eventid.
- **Partial haptic update overwrites full profile**: `setHapticOutputRamp()` and
  `setHapticFeedbackStrength()` now send only the changed haptic sub-field per
  knob value (`{haptic:{outputRamp:…}, type:…}`) rather than the entire
  `knob[]` array, preventing silent clobber of other haptic fields.
- **Dead `profiles` component ref**: the profile config panel referenced a
  non-existent `profiles` component import; replaced with the correct import
  path.
- **Three chained bugs in the profile config panel**: (a) computed profile was
  referenced before the store was ready; (b) `currentProfile` was mutated
  directly instead of going through store actions; (c) the `updates` key sent to
  the firmware was nested one level too deep. All three are fixed in the
  profile panel component.
- **ACK dispatch infrastructure added** (`_pendingAcks`, `_onAck()`,
  `_dispatchAck()`): `update` events with `ack` field now resolve pending
  one-shot callbacks registered via `_onAck(cmd, cb)`.
- **WiFi runtime status**: `update.wifi` events populate `wifiStatus` in the
  store; `update.sprites` populates `sprites[]`.
- **New store actions**: `setWifi()`, `setWifiStatus()`, `requestSpriteList()`,
  `setSpriteList()`, `selectSprite()`, `deleteSprite()`, `uploadSprite()`,
  `setIntegration()`.

#### New Vue components

- **`config/wifi/WiFiConfig.vue`**: SSID / password form, enable toggle,
  connect button. Waits for `{"ack":"wifi","ok":…}` via `_onAck()` to show
  success/failure feedback. Displays live WiFi status banner (connected /
  connecting / ap / disconnected + IP).
- **`config/sprites/SpriteConfig.vue`**: drag-and-drop / click-to-browse PNG
  upload with chunked progress bar. Lists sprites stored on device with
  active-sprite indicator, select button, and delete button.
  Uses `deviceStore.uploadSprite()` which emits `sprite:{op:"begin"}`,
  `sprite:{op:"data",seq:N,data:"<b64>"}`, `sprite:{op:"end"}` frames and
  listens for `{"ack":"sprite","ok":…}`.
- **`config/integrations/IntegrationsConfig.vue`**: stub UI for per-integration
  enable/configure; calls `deviceStore.setIntegration(id, enabled, params)`.

---

### JSON protocol reference

#### New commands (host → device)

**WiFi configuration**
```json
{"wifi":{"ssid":"MyNet","password":"s3cr3t","enabled":true}}
```
Response:
```json
{"ack":"wifi","ok":true}
```

**Sprite operations** (all share the `"sprite"` key with an `"op"` sub-field)

```json
{"sprite":{"op":"begin","name":"logo.png","size":12345}}
{"sprite":{"op":"data","name":"logo.png","seq":0,"data":"<base64>"}}
{"sprite":{"op":"end","name":"logo.png","crc32":3735928559}}
{"sprite":{"op":"list"}}
{"sprite":{"op":"select","name":"logo.png"}}
{"sprite":{"op":"delete","name":"logo.png"}}
```
Response for all operations:
```json
{"ack":"sprite","ok":true}
{"ack":"sprite","ok":false,"error":"CRC mismatch"}
```

#### ACK shape (device → host) — all mutating commands

```json
{"ack":"<cmd>","ok":true}
{"ack":"<cmd>","ok":false,"error":"<human-readable message>"}
```
`<cmd>` is one of: `boot`, `current`, `recalibrate`, `save`, `load`,
`profile`, `profiles`, `settings`, `message`, `wifi`, `sprite`.

#### Richer angle telemetry (device → host)

```json
{"p":42,"a":1.5707963,"t":0,"v":0.314159}
```

| Field | Type | Description |
|---|---|---|
| `p` | `uint16` | Legacy discrete position (unchanged) |
| `a` | `float` | `motor.shaft_angle` in radians |
| `t` | `int32` | Integer turn count (`floor(a / 2π)`) |
| `v` | `float` | `motor.shaft_velocity` in rad/s |

---

### File index (changed files)

| File | Change type |
|---|---|
| `fw/src/DeviceSettings.cpp` | Rewritten: LittleFS, atomic save, CRC-32, schema version, mutexes, WiFi NVS, PD accessor, factory reset |
| `fw/src/DeviceSettings.h` | Added: `pdVoltage`, `activeSprite`, `wifiSsid/Password/Enabled`, mutex fields, new accessors |
| `fw/src/semaphore_guard.h` | New: RAII recursive mutex guard |
| `fw/src/hmi_thread.cpp` | Fixed: LED bounds, idle-LED loop, LongPress dispatch, `init_pd()` full rewrite |
| `fw/src/foc_thread.cpp` | Fixed: `driver.voltage_power_supply` from `DeviceSettings.pdVoltage` |
| `fw/src/com_thread.cpp` | Fixed: serial timeout, message command, profile reorder, heap leak, bare prints; added ACKs, richer telemetry, `wifi`/`sprite` routing |
| `fw/src/wifi_thread.cpp` | New: WiFi STA/AP/OTA + raw TCP JSON server (port 3333); replaces ESPAsyncWebServer/AsyncTCP; stub when `WIFI_ENABLED` not defined |
| `fw/src/wifi_thread.h` | New: `WifiThread` class + no-op stub |
| `fw/src/sprite_store.cpp` | New: chunked upload, LittleFS storage, CRC, LVGL 'L:' driver |
| `fw/src/sprite_store.h` | New: `SpriteStore` namespace |
| `fw/src/HapticProfileManager.cpp` | Fixed: `=`/`==` bug, `"profiles"`/`"profile"` migration, file handle leak |
| `fw/src/main.cpp` | Changed: `SpriteStore::begin()`, `wifi_thread.begin()`, `mark_ota_valid()` after 2 s |
| `fw/platformio.ini` | Changed: `board_build.filesystem=littlefs`, four named envs, pinned deps, `NANO_FIRMWARE_VERSION` |
| `fw/boards/nano_partitions.csv` | Existing: dual-OTA (app0/app1 @ 0x140000), data partition |
| `app/src/main/nanoSerialApi.ts` | Fixed: disconnect object, double-JSON, `_list()` resolve/detach, error event name, double-open guard, RX buffer cap |
| `app/src/main/index.ts` | Fixed: double-JSON handler, `openExternal` URL validation, `setWindowOpenHandler`, `contextIsolation` |
| `app/src/renderer/src/deviceStore.ts` | Fixed: `saved` event, error event, haptic partial-overwrite, dead ref, profile panel bugs; added WiFi/sprite/integration actions, ACK plumbing |
| `app/src/renderer/src/components/config/wifi/WiFiConfig.vue` | New |
| `app/src/renderer/src/components/config/sprites/SpriteConfig.vue` | New |
| `app/src/renderer/src/components/config/integrations/IntegrationsConfig.vue` | New |
