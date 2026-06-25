
# Nano D++ communications protocol

This document describes the configuration and control protocol for the Nano D++ haptic knob device.

The primary transport is USB serial (CDC). An optional WebSocket mirror of the same API is available when the firmware is built with `WIFI_ENABLED`.

This is the protocol used to configure and customize the device via the ZeroOne haptic configuration GUI, or to build your own host-side integration. It is not the output protocol for regular use — that would be MIDI or USB HID, documented in [midi.md](midi.md) and [hid.md](hid.md).

---

## Serial transport

The Nano D++ presents a USB CDC-ACM serial interface alongside its HID and MIDI descriptors. Connection settings: **8N1**, any baud rate (USB CDC auto-negotiates). `Serial.setTimeout(50)` is set on the device side, so partial frames that do not arrive within 50 ms are discarded.

Tools: CoolTerm, TeraTerm, `screen`, or the ZeroOne application (which auto-detects connected devices).

## WebSocket transport (WIFI_ENABLED builds only)

When the firmware is built with `WIFI_ENABLED`, a second transport is available after WiFi connects: an `AsyncWebSocket` server on port 80 at path `/ws`. The JSON message format is identical to the serial protocol. On new client connection the device sends:

```json
{ "connected": true, "ip": "192.168.1.42", "device": "Nano_b826cb7554dc", "fw": "1.0.0" }
```

All mutating commands sent over WebSocket also return ACK frames (same shape as serial). See the [WiFi command](#wifi-command) section for provisioning details.

---

## Wire format

Each message — in both directions — is a single JSON object on one line, terminated by `\n`. No newlines are permitted inside a message. Embedded newlines in field values must be escaped as `\n`.

---

## Device → Host messages

### Boot acknowledgement

Sent once at startup before any commands are processed:

```json
{ "ack": "boot", "ok": true }
```

### Error messages

```json
{ "error": "JSON parse error", "msg": "InvalidInput" }
{ "error": "An error occurred." }
{ "error": "Another kind of error.", "msg": "Detail text." }
```

### Debug messages

```json
{ "debug": "Calibration required..." }
```

### Idle messages

Sent once per second when no event messages have been emitted and the idle timeout has elapsed. The value is elapsed milliseconds since the last user interaction.

```json
{ "idle": 16233 }
```

### Saved confirmation

Emitted after `{"save":true}` successfully persists settings and profiles to LittleFS:

```json
{ "saved": true }
{ "ack": "save", "ok": true }
```

Both lines are emitted, in that order.

### ACK frames

Every mutating command emits an ACK after processing:

```json
{ "ack": "<command>", "ok": true }
{ "ack": "<command>", "ok": false, "error": "Reason text." }
```

The `"error"` key is present only when `"ok"` is `false`. Commands that emit ACKs: `boot`, `current`, `recalibrate`, `settings`, `save`, `load`, `profile`, `profiles`, `message`, `wifi`, `sprite`.

Read-only queries (e.g. `{"profile":"Foo"}` without `"updates"`, or `{"settings":"?"}`) do not emit an ACK — they emit only the response data.

### Event messages: knob telemetry

Emitted whenever `haptic_state.current_pos` changes. All four fields are present simultaneously.

| Field | Type    | Meaning                                      |
|-------|---------|----------------------------------------------|
| `p`   | uint16  | Legacy haptic position integer (back-compat) |
| `a`   | float   | Shaft angle in radians (`motor.shaft_angle`) |
| `t`   | int32   | Integer turn count, floored from `a/(2π)`    |
| `v`   | float   | Shaft velocity in rad/s (`motor.shaft_velocity`) |

```json
{ "p": 7, "a": 4.16, "t": 0, "v": -7.78 }
```

Hosts that want richer data should read `a`, `t`, and `v`. The `p` field is kept for backwards compatibility with older hosts.

### Event messages: key events

Emitted on key press, release, and long-press (≥ 500 ms hold):

```json
{ "kd": 0, "ks": 1 }
{ "ku": 0, "ks": 0 }
```

| Field | Type   | Meaning                                    |
|-------|--------|--------------------------------------------|
| `kd`  | uint8  | Key-down: index of pressed key (0–3)       |
| `ku`  | uint8  | Key-up: index of released key (0–3)        |
| `ks`  | uint8  | Bitmask of currently-held keys (bits 0–3)  |

Only one of `kd` or `ku` is present per message. Long-press events also generate a `kd` + `ku` sequence for the key actions; `kEventLongPressed` fires held-actions and is included in the key state bitmask.

### Profile change events (device-initiated)

When a key action changes the current profile, the device emits:

```json
{ "current": "Blender" }
```

### Motor command echo

When a motor command is processed by the FOC thread it is echoed back:

```json
{ "r": "17=2.0 19=7.7" }
```

---

## Host → Device commands

All commands are JSON objects sent as a single newline-terminated line.

### Profile commands

#### List all profiles

```json
{ "profiles": "#all" }
```

Response:

```json
{ "profiles": ["Default Profile", "Fusion", "Blender"], "current": "Blender" }
```

#### Reorder (and optionally delete) profiles

Send an array of profile names in the desired order. Any profile not listed is deleted.

```json
{ "profiles": ["Blender", "Fusion", "Default Profile"] }
```

Response: `{ "ack": "profiles", "ok": true }`

#### Get a single profile

```json
{ "profile": "Blender" }
```

Response (single line, formatted here for readability):

```json
{
  "profile": {
    "version": 2,
    "name": "Blender",
    "desc": "A cool profile",
    "profileTag": "",
    "profileType": 0,
    "position_num": 12,
    "attract_distance": 20,
    "feedback_strength": 6,
    "bounce_strength": 3,
    "haptic_click_strength": 6,
    "output_ramp": 10000,
    "ledEnable": true,
    "ledBrightness": 100,
    "ledMode": 0,
    "pointer": 16777215,
    "primary": 32768,
    "secondary": 16753920,
    "buttonAIdle": 16562691,
    "buttonBIdle": 16562691,
    "buttonCIdle": 16562691,
    "buttonDIdle": 16562691,
    "buttonAPress": 16516075,
    "buttonBPress": 16516075,
    "buttonCPress": 16516075,
    "buttonDPress": 16516075,
    "keys": [
      {
        "pressed": [
          { "type": "key",    "keyCodes": [17] },
          { "type": "midi",   "channel": 5, "cc": 5, "val": 5 }
        ],
        "released": [],
        "held": []
      }
    ],
    "knob": [
      {
        "keyState": 0,
        "angleMin": 0,
        "angleMax": 6.283185307179586,
        "valueMin": 0,
        "valueMax": 127,
        "step": 1,
        "wrap": false,
        "type": "midi",
        "channel": 5,
        "cc": 5,
        "haptic": {
          "mode": 0,
          "startPos": 0,
          "endPos": 6.283185307179586,
          "detentCount": 16,
          "vernier": 0,
          "kxForce": true,
          "outputRamp": 1.4,
          "detentStrength": 17.9
        }
      }
    ],
    "audio": {
      "clickType": "hard",
      "keyClickType": "none",
      "clickLevel": 100
    },
    "guiEnable": false
  }
}
```

Haptic mode values:

| Value | Mode    | Description                           |
|-------|---------|---------------------------------------|
| 0     | REGULAR | Coarse detents only                   |
| 1     | VERNIER | Coarse detents with fine between them |
| 2     | VISCOSE | Resistance while turning              |
| 3     | SPRING  | Snap back to center point             |

No ACK is emitted for read-only profile queries.

#### Create or update a profile

If `"profile"` names a profile that does not exist, it is created. If it exists, the fields in `"updates"` are merged in.

```json
{ "profile": "Blender", "updates": { "profileType": 2, "haptic_click_strength": 13.0, "ledEnable": false } }
```

Rename:

```json
{ "profile": "Blender", "updates": { "name": "Blender & co" } }
```

Response: `{ "ack": "profile", "ok": true }`

On error (name collision, invalid name, profile table full):
```json
{ "ack": "profile", "ok": false, "error": "Profile name already exists" }
```

#### Set the current profile

```json
{ "current": "Fusion" }
```

Response: `{ "ack": "current", "ok": true }`

Changing the current profile dispatches the new haptic, LED, HMI, audio, and LCD configs to the relevant threads immediately.

### Motor commands

Send a SimpleFOC register command string:

```json
{ "R": "17=2.0 19=7.7" }
```

No ACK is emitted for motor commands; the command is echoed back as `{"r":"..."}` after FOC thread processing.

Trigger a non-blocking motor recalibration:

```json
{ "recalibrate": true }
```

Response: `{ "ack": "recalibrate", "ok": true }`

### System commands

#### Get device settings

```json
{ "settings": "?" }
```

Response (single line, formatted for readability). Note: `wifiPassword` is always redacted as `"***"` in serial output; WiFi credentials are stored separately in NVS.

```json
{
  "settings": {
    "debug": false,
    "ledMaxBrightness": 150,
    "maxVelocity": 10,
    "maxVoltage": 5.0,
    "deviceOrientation": 1,
    "deviceName": "Nano_b826cb7554dc",
    "serialNumber": "b826cb7554dc",
    "firmwareVersion": "1.0.0",
    "sysexId": 0,
    "idleTimeout": 10000,
    "pdVoltage": 9.0,
    "activeSprite": "",
    "wifiEnabled": false,
    "wifiSsid": "",
    "wifiPassword": "***",
    "midiUsb": { "in": true, "out": true, "thru": false, "route": false, "nano": true },
    "midi2":   { "in": true, "out": true, "thru": false, "route": false, "nano": true }
  }
}
```

Settings field reference:

| Field              | Type    | Notes                                                        |
|--------------------|---------|--------------------------------------------------------------|
| `debug`            | bool    | Enables verbose debug output                                 |
| `ledMaxBrightness` | uint8   | Global LED brightness ceiling (0–255)                        |
| `maxVelocity`      | float   | Motor velocity limit (rad/s)                                 |
| `maxVoltage`       | float   | Motor voltage limit (V)                                      |
| `deviceOrientation`| uint16  | Knob orientation (0–3 → 0°, 90°, 180°, 270°)                |
| `deviceName`       | string  | Human-readable device name                                   |
| `serialNumber`     | string  | Read-only; derived from ESP32 eFuse MAC                      |
| `firmwareVersion`  | string  | Read-only; set at build time                                 |
| `sysexId`          | uint8   | MIDI SysEx device ID                                         |
| `idleTimeout`      | uint32  | Milliseconds before idle flag is set (0 = never)             |
| `pdVoltage`        | float   | USB-PD negotiated voltage, clamped to [5.0, 9.0] V (r/o after boot) |
| `activeSprite`     | string  | Name of the currently-selected display sprite (or `""`)      |
| `wifiEnabled`      | bool    | Whether WiFi is active                                       |
| `wifiSsid`         | string  | WiFi SSID; omitted in response if empty                      |
| `wifiPassword`     | string  | Always `"***"` in serial output; set via `{"wifi":{...}}`    |
| `midiUsb`          | object  | USB MIDI routing flags (see below)                           |
| `midi2`            | object  | Hardware MIDI jack routing flags (see below)                 |

MIDI settings sub-object fields:

| Field   | Type | Meaning                                     |
|---------|------|---------------------------------------------|
| `in`    | bool | Receive MIDI on this interface              |
| `out`   | bool | Send MIDI on this interface                 |
| `thru`  | bool | Forward incoming to outgoing automatically  |
| `route` | bool | Route to the other MIDI interface           |
| `nano`  | bool | Device generates/sends MIDI on this interface |

#### Set device settings

One or more fields may be set in a single message. Unknown fields are ignored.

```json
{ "settings": { "debug": true, "ledMaxBrightness": 170, "deviceOrientation": 2 } }
```

Response: `{ "ack": "settings", "ok": true }`

#### Save to LittleFS

Atomically saves both device settings and all profiles. Uses write-to-tmp then rename to avoid corruption on power loss.

```json
{ "save": true }
```

Response:
```json
{ "saved": true }
{ "ack": "save", "ok": true }
```

#### Load from LittleFS

Reloads settings and all profiles from storage, then re-dispatches all configs to the running threads.

```json
{ "load": true }
```

Response: `{ "ack": "load", "ok": true }`

#### Show a message on the screen

Displays a transient overlay on the device LCD using the `LCD_LAYOUT_MESSAGE` layout. The `"duration"` field is accepted but currently reserved for future timed-dismiss support — the message persists until the next layout change.

```json
{ "message": { "title": "Hey!", "text": "Get some work done.", "duration": 5.0 } }
```

Response: `{ "ack": "message", "ok": true }`

#### Control the screen display

Sets the device LCD to `LCD_LAYOUT_DEFAULT` (title + up to four data lines). All fields are optional; omitted fields are cleared.

```json
{ "screen": { "title": "The Title", "data1": "Subtitle", "data2": "Line 2", "data3": "Line 3", "data4": "Line 4" } }
```

No ACK is emitted for `"screen"` commands.

---

## WiFi command

Available in all build targets; requires `WIFI_ENABLED` for the radio to respond. Updates `DeviceSettings` WiFi fields (persisted to NVS) and calls `wifi_thread.apply_settings()` immediately.

```json
{ "wifi": { "ssid": "MyNetwork", "password": "hunter2", "enabled": true } }
```

All three fields are optional; only present keys are updated.

Response: `{ "ack": "wifi", "ok": true }`

To disable WiFi:

```json
{ "wifi": { "enabled": false } }
```

WiFi credentials are never emitted over serial (the `{"settings":"?"}` response shows `"***"` for the password). Credentials are stored in the `nano_wifi` NVS namespace, separate from the main settings JSON file.

---

## Sprite command

Chunked binary image upload to LittleFS for display via LVGL. Sprites are stored under `/sprites/<name>` and are accessible to LVGL via the `L:` filesystem driver letter.

Limits: max 64 KB per sprite, max 512 KB total, max 16 sprites.

All sprite sub-commands are sent as `{ "sprite": { "op": "<op>", ... } }`. Each returns `{ "ack": "sprite", "ok": true|false, "error": "..." }`.

### Begin upload

```json
{ "sprite": { "op": "begin", "name": "logo.bmp", "size": 14400 } }
```

| Field  | Type   | Required | Notes                                  |
|--------|--------|----------|----------------------------------------|
| `name` | string | yes      | 1–32 characters, no path separators    |
| `size` | uint   | yes      | Exact total byte count of the image    |

If a sprite with the same name already exists it is replaced. A stale incomplete upload from a previous session is aborted automatically.

### Upload chunk

Chunks must be sent in order starting from `seq` = 0. Each chunk's `data` field is the base64-encoded binary of that chunk. There is no fixed chunk size limit; keep chunks small enough to fit in a single JSON message (< 4 KB of base64 is safe).

```json
{ "sprite": { "op": "data", "seq": 0, "data": "iVBORw0KGgo..." } }
```

| Field  | Type   | Required | Notes                             |
|--------|--------|----------|-----------------------------------|
| `seq`  | int    | yes      | Sequence number, starting at 0    |
| `data` | string | yes      | Base64-encoded binary chunk       |

A sequence error aborts the upload and removes the partial file.

### End upload

Finalises the upload. The device verifies that total bytes written equals the `size` declared in `begin`, and that the CRC-32 of the complete file matches `crc32`.

```json
{ "sprite": { "op": "end", "crc32": 3456789012 } }
```

| Field   | Type   | Required | Notes                                       |
|---------|--------|----------|---------------------------------------------|
| `crc32` | uint32 | yes      | IEEE 802.3 CRC-32 of the complete image data |

On mismatch, the partial file is removed and `"ok": false` is returned.

### List sprites

```json
{ "sprite": { "op": "list" } }
```

Response: `{ "ack": "sprite", "ok": true }`

Note: the internal `handle_list` function stores the comma-separated sprite names in the `spriteErr` string and returns `true`. However, `com_thread` currently passes `nullptr` for the error payload when `ok=true`, so the name list is not included in the ACK. The list command confirms success but does not return sprite names in the current implementation — a host that needs the list should track uploads locally or query the `/status` HTTP endpoint (WIFI_ENABLED builds).

### Select active sprite

Sets `DeviceSettings.activeSprite`. Pass `name` as an empty string or omit it to deselect.

```json
{ "sprite": { "op": "select", "name": "logo.bmp" } }
{ "sprite": { "op": "select" } }
```

Response: `{ "ack": "sprite", "ok": true }`

### Delete sprite

```json
{ "sprite": { "op": "delete", "name": "logo.bmp" } }
```

Response: `{ "ack": "sprite", "ok": true }` or `{ "ack": "sprite", "ok": false, "error": "not found" }`

If the deleted sprite was the active sprite, `DeviceSettings.activeSprite` is cleared.

---

## WiFi provisioning (SoftAP)

When WiFi is enabled but no SSID is configured, the device starts a SoftAP named `NanoD-Setup` (open, no password). A minimal HTML provisioning page is served at `http://192.168.4.1/`. Submitting the form stores credentials to NVS via `DeviceSettings` and initiates a STA connection. The SoftAP is stopped once STA connects.

## OTA update (WIFI_ENABLED)

Two OTA paths are available after WiFi connects:

- **ArduinoOTA** (port 3232, password `nanod-ota` by default; override via `WIFI_OTA_PASSWORD` build flag). Hostname is `DeviceSettings.deviceName`.
- **HTTP multipart POST** to `http://<device-ip>/update`. Send the firmware `.bin` as a multipart upload. Append `.spiffs` suffix to the filename to flash the filesystem partition instead of the application.

After a successful OTA flash, the device marks the new firmware valid (cancels rollback) once core threads and connectivity are healthy. If the new firmware fails to call `mark_ota_valid()` before the watchdog fires, the bootloader rolls back to the previous image.

## HTTP status endpoint (WIFI_ENABLED)

```
GET http://<device-ip>/status
```

Response:

```json
{ "ip": "192.168.1.42", "rssi": -54, "device": "Nano_b826cb7554dc", "fw": "1.0.0" }
```
