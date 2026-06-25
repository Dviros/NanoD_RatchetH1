# Nano_D++ Firmware

ESP32-S3 SimpleFOC haptic knob firmware for the Binaris Nano_D++.

---

## Quick start

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or IDE)
- A Nano_D++ board connected via USB

### Build and flash

```bash
# Default build — safe core (all bug fixes + PD + sprites + brick-proofing)
pio run -e nanofoc_d -t upload

# With WiFi (adds ~110 KB flash)
pio run -e nanofoc_d_wifi -t upload

# With I2S audio haptic feedback (I2S amp must be populated)
pio run -e nanofoc_d_audio -t upload

# Everything on (WiFi + audio)
pio run -e nanofoc_d_full -t upload

# Upload filesystem (required before first boot and whenever sprites change)
pio run -e nanofoc_d -t uploadfs
```

Open a serial monitor at 115200 baud to observe boot output.

---

## Build environments

| Environment | Extra features | Build flag(s) |
|---|---|---|
| `nanofoc_d` | Core only (default) | — |
| `nanofoc_d_wifi` | WiFi STA + SoftAP provisioning + ArduinoOTA + raw TCP JSON API (port 3333) | `-DWIFI_ENABLED` |
| `nanofoc_d_audio` | I2S audio haptic feedback | `-DNANO_AUDIO=1 -DAUDIO_EN` |
| `nanofoc_d_full` | WiFi + audio | both sets above |

`nanofoc_d` is the `default_envs` target in `platformio.ini`. All four environments share the same core firmware; the optional features are compile-time gated and add no code or overhead to disabled builds.

**Build size (nanofoc_d, 2025-06):** Flash 65.6 % (840 KB of 1.28 MB OTA slot), RAM 46.8 %.

---

## Pinned dependencies (fresh clone now builds)

Previously, several library dependencies used caret (`^`) version ranges that resolved to incompatible versions on a clean install. The following are now pinned to exact versions:

| Library | Pinned version |
|---|---|
| `lvgl/lvgl` | 9.0.0 |
| `askuric/Simple FOC` | 2.3.3 |
| `adafruit/Adafruit TinyUSB Library` | 3.1.0 |
| `fastled/FastLED` | 3.6.0 |

A `pio run` on a fresh clone will reproduce the verified build without manual intervention.

---

## Filesystem (LittleFS)

The firmware uses **LittleFS** (`board_build.filesystem = littlefs` in `platformio.ini`).

- Settings are saved to `/device_settings.json` via an atomic write (temp file + rename) with CRC-32 validation and `schemaVersion` checking. A corrupt or version-mismatched file falls back to built-in defaults.
- WiFi credentials are stored separately in NVS (`nano_wifi` namespace) and never appear in the JSON settings file.
- Sprites are stored under `/sprites/<name>` (up to 16 files, 64 KB each, 512 KB total budget).

The `uploadfs` target uploads the initial LittleFS image. Run it at least once before the first boot, and again after manually placing sprites into the `data/` directory.

---

## Feature overview

### USB-PD power negotiation

At boot, `HmiThread::init_pd()` initialises the STUSB4500 over I2C. If the chip is absent the firmware falls back to 5 V and continues. When present:

1. The NVM is programmed with two PDOs (PDO1: 5 V / 3 A, PDO2: 9 V / 3 A) on first boot.
2. A soft reset forces re-negotiation using the new profile.
3. The selected PDO is read back from register `0x91` (RDO_REG_STATUS).
4. The negotiated voltage is clamped to the board-safe range **[5.0 V, 9.0 V]** and stored in `DeviceSettings.pdVoltage`.
5. `FocThread::run()` reads `pdVoltage` before calling `driver.init()` and sets `driver.voltage_power_supply` and `driver.voltage_limit` from it (was previously hardcoded to 5 V).

### Thread model

Four FreeRTOS tasks run concurrently:

| Thread | Core | Stack | Responsibility |
|---|---|---|---|
| COM | (configurable) | 12 000 B | Serial JSON API, command routing |
| FOC | (configurable) | 8 192 B | SimpleFOC motor loop, haptic |
| HMI | (configurable) | 4 608 B | LED ring, buttons, MIDI, USB HID, PD init |
| LCD | (configurable) | (in lcd_thread) | GC9A01 display, LVGL |
| WIFI (optional) | 0 | 6 144 B | WiFi STA/AP, ArduinoOTA, raw TCP JSON server (port 3333) |

Shared state is protected by FreeRTOS recursive mutexes. The RAII helper `SemaphoreGuard` (`src/semaphore_guard.h`) ensures mutex release on scope exit. A global `atomic` `global_sleep_flag` signals idle state to the LED renderer.

### WiFi (`nanofoc_d_wifi` / `nanofoc_d_full`)

Enabled by `-DWIFI_ENABLED`. Features:

- **STA mode**: auto-connects to stored SSID with exponential-backoff reconnect.
- **SoftAP provisioning**: if no credentials are stored, an open access point `NanoD-Setup` is started. Connect to it, then open a raw TCP connection to `192.168.4.1:3333` and send `{"wifi":{"ssid":"...","password":"...","enabled":true}}\n` to provision credentials.
- **ArduinoOTA**: OTA update over the local network (password: `nanod-ota` by default, overridable via `-DWIFI_OTA_PASSWORD`).
- **Raw TCP JSON server** on port 3333: mirrors the full serial JSON API. Inbound lines are queued to `com_thread` for parsing; all outbound frames (ACKs, events, replies) are mirrored back to connected TCP clients via `com_thread.emit()`. Up to 4 simultaneous clients; lines > 2048 bytes are dropped.
- **OTA rollback**: `wifi_thread.mark_ota_valid()` calls `esp_ota_mark_app_valid_cancel_rollback()` after all threads have started; if the device crashes before that call the bootloader rolls back to the previous firmware.

### Sprite store

Images (PNG recommended, up to 64 KB each) are uploaded to LittleFS via the JSON protocol and exposed to LVGL through a custom filesystem driver registered on drive letter `L`. Paths visible to LVGL are `L:/sprites/<name>`.

### Audio (`nanofoc_d_audio` / `nanofoc_d_full`)

Enabled by `-DNANO_AUDIO=1`. Requires the I2S amplifier to be populated. When disabled, `audio_play()` and `audio_click()` compile to no-ops.

---

## Communications protocol

The full serial JSON protocol is documented in [communications.md](communications.md).

New commands added in this release:

**WiFi configuration**
```json
// Host -> device
{ "wifi": { "ssid": "MyNet", "password": "secret", "enabled": true } }

// Device -> host (ACK)
{ "ack": "wifi", "ok": true }
{ "ack": "wifi", "ok": false, "error": "reason" }
```

**Sprite management**
```json
// Begin upload
{ "sprite": { "op": "begin", "name": "foo.png", "size": 12345 } }

// Data chunk (base64, 4 KB recommended)
{ "sprite": { "op": "data", "name": "foo.png", "seq": 0, "data": "<base64>" } }

// End upload (firmware validates CRC-32 and byte count)
{ "sprite": { "op": "end", "name": "foo.png", "crc32": 1234567890 } }

// Other sprite ops
{ "sprite": { "op": "list" } }
{ "sprite": { "op": "select", "name": "foo.png" } }
{ "sprite": { "op": "delete", "name": "foo.png" } }

// ACK shape (same for all sprite ops)
{ "ack": "sprite", "ok": true }
{ "ack": "sprite", "ok": false, "error": "CRC mismatch" }
```

**ACK shape** (all mutating commands)
```json
{ "ack": "<command>", "ok": true }
{ "ack": "<command>", "ok": false, "error": "human-readable reason" }
```

**Richer telemetry**
```json
// Angle event (replaces legacy {"p": N})
{ "p": 42, "a": 4.16, "t": -2, "v": -7.78 }
// p = legacy uint16 position (back-compat)
// a = shaft angle (rad), t = integer turns, v = velocity (rad/s)
```

**Message display**
```json
{ "message": { "title": "Hello", "text": "World" } }
{ "ack": "message", "ok": true }
```

---

## Partition table

`boards/nano_partitions.csv` — dual-OTA layout:

| Name | Type | Offset | Size |
|---|---|---|---|
| nvs | data/nvs | 0x9000 | 20 KB |
| otadata | data/ota | 0xe000 | 8 KB |
| app0 | app/ota_0 | 0x10000 | 1.25 MB |
| app1 | app/ota_1 | 0x150000 | 1.25 MB |
| spiffs | data/spiffs | 0x290000 | 1.375 MB |
| coredump | data/coredump | 0x3F0000 | 64 KB |

Both OTA app slots are 0x140000 (1.3 MB). The `spiffs` partition label is retained for tooling compatibility; the actual filesystem written is LittleFS.

---

## Further reading

- [communications.md](communications.md) — full serial JSON protocol
- [hid.md](hid.md) — USB HID report descriptors
- [midi.md](midi.md) — MIDI protocol details
- [mapping.md](mapping.md) — knob/key mapping reference
- docs/BRICK_PROOFING.md — recovery procedures
- docs/HARDWARE_TEST_CHECKLIST.md — bring-up test checklist
- [CHANGELOG.md](CHANGELOG.md) — release history
