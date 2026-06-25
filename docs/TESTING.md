# Nano_D++ Firmware — Test Strategy

This document describes how to validate the Nano_D++ firmware **before** (and after) flashing. The approach is a three-layer pyramid: fast logic tests that run on any laptop, a protocol harness that exercises the JSON API over a live device, and a hardware checklist for things that cannot be emulated.

---

## Why not full emulation?

ESP32-S3 QEMU images exist, but a full-system boot of this firmware in QEMU is not viable. Every significant I/O path depends on hardware that QEMU does not emulate:

| Peripheral | Driver | Notes |
|---|---|---|
| SimpleFOC motor controller | FOC task, `haptic.cpp` | Requires real encoder + BLDC motor |
| STUSB4500 USB-PD controller | `hmi_thread.cpp init_pd()` | I2C-attached ASIC, no QEMU model |
| GC9A01 SPI display | `lcd_thread.cpp`, TFT_eSPI | Custom SPI panel, framebuffer-only in QEMU |
| WS2811 / FastLED ring | `hmi_thread.cpp` | Bit-banged RMT peripheral |
| I2S audio amplifier | `audio/` (NANO_AUDIO) | Requires populated amp IC |
| TinyUSB (HID + CDC) | `hmi_thread.cpp`, `com_thread.cpp` | Needs USB D+/D- DP-pullup hardware |

The native unit tests and the protocol harness together cover all pure-logic and protocol-layer behaviour. Everything that touches the peripherals above is covered by the hardware checklist.

---

## Layer 1 — Native unit tests (no hardware required)

### What they cover

The `test/` directory (PlatformIO native env) hosts host-compiled tests that run entirely on a laptop. They exercise the following logic boundaries:

| Test suite | What it proves |
|---|---|
| **CRC-32 firmware/app parity** | The CRC-32 implementation used in `DeviceSettings` and `sprite_store.cpp` matches a reference implementation (zlib polynomial, final XOR). A settings file written on device and verified on host — or vice-versa — must agree. |
| **USB-PD voltage clamp** | `setPdVoltage()` clamps any input to the board-safe range [5.0 V, 9.0 V]. Values below 5.0 clamp to 5.0; values above 9.0 clamp to 9.0; values exactly at the boundaries pass through unchanged. |
| **Value mapping** | The `updateValue()` angle-to-value linear map: given `angleMin`, `angleMax`, `valueMin`, `valueMax`, the output is proportional and bounded. Edge cases: angle at min, angle at max, angle below min (clamp), angle above max (clamp), zero-width range. |
| **Profile-type symmetry** | The `HapticProfileManager` serialiser round-trips a profile with every action type through JSON encode → decode and comes back with identical fields. Catches `"type":"profiles"` vs `"type":"profile"` canonicalisation. |
| **Settings integrity** | `DeviceSettings` serialise → corrupt one byte → deserialise triggers the fallback-to-defaults path. A clean round-trip produces a CRC field that validates. `schemaVersion` mismatch also triggers fallback. |

### How to run

No board, no cable, no USB. Any macOS or Linux host with PlatformIO CLI installed:

```bash
# From the fw/ directory
pio test -e native
```

Expected output: all test suites `PASSED`, total elapsed time under 5 s.

If a `native` environment is not yet defined in `platformio.ini`, add:

```ini
[env:native]
platform = native
test_framework = unity
build_flags = -DNATIVE_TEST
```

and place test source files under `test/native/`.

### When to run

Run `pio test -e native` after **every code change** before building for a target board. These are the first gate. If native tests fail, do not proceed to hardware.

---

## Layer 2 — Protocol E2E harness

### What it is

`fw/test/integration/nanod_e2e.mjs` is a Node.js script that connects to a running device (WiFi build over TCP:3333, or any build over USB serial) and drives a scripted sequence of JSON commands. It verifies that the device responds with the correct ACK shapes, that settings survive a round-trip, and that sprite upload CRC validation works.

The harness requires a flashed device but **no manual interaction** — everything is automated.

### Connection modes

| Mode | Build env | Connection string |
|---|---|---|
| TCP (WiFi) | `nanofoc_d_wifi` | `--host <device-ip> --port 3333` |
| USB serial | any env | `--serial /dev/tty.usbmodem*` |

The harness auto-detects JSON line framing and handles the variable startup delay after boot.

### What each scripted step proves

| Step | Assertion |
|---|---|
| Boot ACK | First JSON line is `{"ack":"boot","ok":true}` within 5 s of connect. |
| Settings set | `{"settings":{"deviceName":"e2e-test"}}` → `{"ack":"settings","ok":true}` returned. |
| Settings save | `{"save":true}` → `{"saved":true}` followed by `{"ack":"save","ok":true}`. |
| Settings round-trip | `{"settings":""}` query returns `deviceName:"e2e-test"`. Proves LittleFS write + read with no reboot. |
| Settings persist across reboot | Harness sends a soft-reset command (or prompts manual `EN` press), reconnects, re-queries settings. `deviceName` is still `"e2e-test"`. Proves atomic-write + CRC survived a power cycle. |
| Profile create | Sends a minimal profile via `{"profile":"e2e-prof","updates":{...}}` + `{"save":true}`. `{"profiles":"#all"}` includes `"e2e-prof"`. |
| Profile delete / reorder | Sends `{"profiles":["e2e-prof"]}` to reorder. Re-queries and confirms new order. |
| Sprite upload CRC pass | Uploads a 512-byte test sprite with correct CRC-32. Expects `{"ack":"sprite","ok":true}` on `end`. |
| Sprite upload CRC fail | Uploads the same sprite with `crc32` flipped by one bit. Expects `{"ack":"sprite","ok":false,"error":"CRC mismatch"}`. |
| Invalid JSON | Sends `{bad json}`. Expects `{"error":"JSON parse error",...}`. |
| Telemetry shape | Verifies at least one `{a:...,t:...,v:...,p:...}` frame is received within 10 s (device reports positions even at rest due to encoder noise). |

### How to run

```bash
# Prerequisites: Node.js 18+, device flashed and reachable
cd fw/test/integration
node nanod_e2e.mjs --host 192.168.1.42 --port 3333

# Or over USB serial
node nanod_e2e.mjs --serial /dev/tty.usbmodem1101

# Verbose (prints every JSON line exchanged)
node nanod_e2e.mjs --host 192.168.1.42 --verbose
```

The harness exits 0 on full pass, non-zero with a summary of failed assertions on any failure.

> **Note:** The harness file `fw/test/integration/nanod_e2e.mjs` is the planned integration test entrypoint. If it does not yet exist in the repository, the steps above describe exactly what it must implement — the scripted sequence can be run manually against a serial monitor as an interim measure (see `HARDWARE_TEST_CHECKLIST.md` § 7).

---

## Layer 3 — On-hardware checklist (final gate)

Once layers 1 and 2 pass, proceed to the full hardware bring-up sequence documented in:

**[docs/HARDWARE_TEST_CHECKLIST.md](HARDWARE_TEST_CHECKLIST.md)**

The checklist is ordered by risk. Two items are marked HIGH RISK and must be signed off before the device is considered field-ready:

1. **Section 3 — USB-PD voltage negotiation.** Incorrect PDO negotiation can deliver the wrong voltage to the motor driver. Measure VBUS with a multimeter before trusting serial output. The firmware clamps PD voltage to [5.0, 9.0] V so a mis-read falls back to 5 V rather than over-volting, but the correct PDO byte (`PD_RDO_ALT_BYTE` A/B test) must be confirmed on hardware.

2. **Section 8.5 — OTA rollback.** Flash a deliberately bad firmware over OTA and confirm the bootloader reverts to the last good slot. This test must be done before any field deployment. Requires `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` in the ESP-IDF sdkconfig (see [BRICK_PROOFING.md](BRICK_PROOFING.md)).

Refer also to [docs/KNOWN_RESIDUALS.md](KNOWN_RESIDUALS.md) for the list of items that are compile-verified but have not yet been validated on physical hardware. The residuals that most directly affect test sequencing are:

- **PD voltage path** — highest risk; A/B byte selection still unresolved.
- **OTA rollback** — requires a bootloader built with rollback support.
- **Raw-TCP transport** — compile-verified; validate under real sockets before shipping the WiFi build.
- **Sprite display decode** — chunked upload + CRC is verified; on-display LVGL image rendering needs hardware.
- **`nanofoc_d_full` flash overflow** — WiFi + audio combined overflows the 1.3 MB OTA slot by ~1.4 KB on the current 4 MB board. Do not flash `nanofoc_d_full` until the board moves to 8 MB or 16 MB flash.

---

## Test pyramid summary

```
                  ┌───────────────────────────┐
                  │   Layer 3: Hardware        │  ← real board, ordered by risk
                  │   HARDWARE_TEST_CHECKLIST  │    PD voltage → OTA rollback last
                  └───────────────────────────┘
                ┌─────────────────────────────────┐
                │   Layer 2: Protocol harness     │  ← device + TCP or USB serial
                │   test/integration/nanod_e2e.mjs│    no manual steps
                └─────────────────────────────────┘
          ┌────────────────────────────────────────────┐
          │   Layer 1: Native unit tests               │  ← laptop only, no hardware
          │   pio test -e native                       │    first gate after every change
          └────────────────────────────────────────────┘
```

**Rule:** do not advance to a higher layer until the layer below is green.
