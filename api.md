# Nano D++ API reference

This document summarises the complete JSON command and event API between a host and the Nano D++. It is intended as a quick-reference companion to [communications.md](communications.md), which has full transport and protocol detail.

The API is not for end-user consumption — it is the low-level wire protocol used by the ZeroOne GUI and any custom host integration. End-user bindings (SDK, scripting) may be built on top.

---

## Command summary

| Command key     | Direction     | Mutating | ACK emitted        | Notes |
|-----------------|---------------|----------|--------------------|-------|
| `profiles`:`"#all"` | H→D       | no       | no (data response) | List all profile names |
| `profiles`:`[array]`| H→D       | yes      | `"profiles"`       | Reorder / delete profiles |
| `profile`:`"name"` (no updates) | H→D | no | no (data response) | Fetch single profile |
| `profile` + `updates` | H→D    | yes      | `"profile"`        | Create / update profile |
| `current`:`"name"` | H→D        | yes      | `"current"`        | Set active profile |
| `R`:`"cmd"`     | H→D           | no       | no (motor echo)    | SimpleFOC register command |
| `recalibrate`:`true` | H→D      | yes      | `"recalibrate"`    | Non-blocking motor recal |
| `settings`:`"?"` | H→D          | no       | no (data response) | Fetch all settings |
| `settings`:`{...}` | H→D        | yes      | `"settings"`       | Update one or more settings |
| `save`:`true`   | H→D           | yes      | `"save"` + `saved` | Persist settings + profiles |
| `load`:`true`   | H→D           | yes      | `"load"`           | Reload from storage |
| `message`:`{...}` | H→D         | yes      | `"message"`        | Overlay message on LCD |
| `screen`:`{...}` | H→D          | no       | no                 | Set LCD layout fields |
| `wifi`:`{...}`  | H→D           | yes      | `"wifi"`           | Update WiFi credentials / state |
| `sprite`:`{...}` | H→D          | yes      | `"sprite"`         | Sprite upload / management |

---

## Event summary (Device → Host)

| Event key    | When emitted                                    |
|--------------|-------------------------------------------------|
| `ack`        | After every mutating command (including `boot`) |
| `error`      | On JSON parse errors or internal errors         |
| `debug`      | Informational strings from firmware             |
| `idle`       | Once per second after idle timeout              |
| `saved`      | After successful `{"save":true}` (then also ACK)|
| `p`,`a`,`t`,`v` | On every knob position change              |
| `kd`,`ku`,`ks`  | On key press / release                      |
| `current`    | Device-initiated profile change (key action)    |
| `r`          | SimpleFOC motor command echo                    |

---

## ACK shape

```json
{ "ack": "<command-name>", "ok": true }
{ "ack": "<command-name>", "ok": false, "error": "Human-readable reason." }
```

The `"error"` field is present only when `"ok"` is `false`.

---

## Knob telemetry event

```json
{ "p": 7, "a": 4.16, "t": 0, "v": -7.78 }
```

- `p` — legacy haptic position integer (uint16, back-compat)
- `a` — shaft angle in radians (float, `motor.shaft_angle`)
- `t` — integer turn count, `floor(a / 2π)` (int32)
- `v` — shaft velocity in rad/s (float, `motor.shaft_velocity`)

New integrations should use `a`, `t`, `v`.

---

## Key event shape

```json
{ "kd": 0, "ks": 1 }   // key 0 pressed; bitmask = 0b0001
{ "ku": 2, "ks": 5 }   // key 2 released; bitmask = 0b0101 (keys 0 and 2 still held)
```

- `kd` — key-down index (0–3)
- `ku` — key-up index (0–3)
- `ks` — bitmask of currently held keys (bits 0–3)

Only one of `kd` or `ku` is present per message.

---

## Settings object fields

See [communications.md — Settings](communications.md) for the full table. Key fields:

| Field              | Writeable | Type    |
|--------------------|-----------|---------|
| `debug`            | yes       | bool    |
| `ledMaxBrightness` | yes       | uint8   |
| `maxVelocity`      | yes       | float   |
| `maxVoltage`       | yes       | float   |
| `deviceOrientation`| yes       | uint16  |
| `deviceName`       | yes       | string  |
| `idleTimeout`      | yes       | uint32  |
| `sysexId`          | yes       | uint8   |
| `wifiEnabled`      | yes (via `wifi` cmd) | bool |
| `wifiSsid`         | yes (via `wifi` cmd) | string |
| `pdVoltage`        | read-only at runtime | float (5.0–9.0 V) |
| `activeSprite`     | yes (via `sprite select`) | string |
| `serialNumber`     | read-only | string  |
| `firmwareVersion`  | read-only | string  |

---

## Sprite command ops summary

| `op`     | Required fields          | Effect                                |
|----------|--------------------------|---------------------------------------|
| `begin`  | `name`, `size`           | Start upload session                  |
| `data`   | `seq`, `data` (base64)   | Send one chunk (must be in sequence)  |
| `end`    | `crc32`                  | Finalise and validate upload          |
| `list`   | —                        | Confirms success; names not returned (see communications.md) |
| `select` | `name` (optional)        | Set active sprite; empty = deselect   |
| `delete` | `name`                   | Remove sprite from LittleFS           |

LVGL filesystem path for stored sprites: `L:/sprites/<name>`

---

## WiFi command fields

```json
{ "wifi": { "ssid": "...", "password": "...", "enabled": true } }
```

All fields optional. Password is stored in NVS (`nano_wifi` namespace) and never echoed back over serial. ACK: `{ "ack": "wifi", "ok": true }`.

---

## Build targets and feature gates

| Target             | WiFi | Audio | Notes                              |
|--------------------|------|-------|------------------------------------|
| `nanofoc_d`        | no   | no    | Default; smallest flash footprint  |
| `nanofoc_d_wifi`   | yes  | no    | WiFi + OTA + WebSocket API         |
| `nanofoc_d_audio`  | no   | yes   | Haptic audio feedback              |
| `nanofoc_d_full`   | yes  | yes   | All features                       |

The `wifi` and `sprite` JSON commands are always parsed by the serial path. Without `WIFI_ENABLED`, `wifi_thread.apply_settings()` is a no-op stub. Without `NANO_AUDIO`, audio config dispatch is a no-op.

---

For full protocol detail, message ordering, and transport notes see [communications.md](communications.md).
