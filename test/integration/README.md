# Nano_D++ Integration Test Harness

Protocol-level E2E harness for the Nano_D++ (ESP32-S3 SimpleFOC haptic knob).
Drives a real device — or a compatible TCP emulator — through scripted
command/response round-trips to validate firmware behaviour before and after
flashing.

Zero npm dependencies in TCP mode; Node 22+ required.

---

## How it works

The harness opens a transport (TCP or serial), exchanges newline-delimited JSON
frames with the device per the documented protocol, and asserts the shape and
content of every ACK and response.  Each step has a per-command timeout so a
hung device does not block the run indefinitely.  The exit code is 0 on full
pass and 1 on any failure, making it CI-friendly.

---

## TCP mode (primary)

The `nanofoc_d_wifi` and `nanofoc_d_full` PlatformIO environments expose a raw
TCP JSON server on port 3333.  This is the preferred transport: no drivers
needed, works from any machine on the same network.

```
node fw/test/integration/nanod_e2e.mjs <device-ip>
node fw/test/integration/nanod_e2e.mjs 192.168.1.42
```

On connect the device sends a greeting:

```json
{ "connected": true, "ip": "192.168.1.42", "device": "Nano_3053f07554dc", "fw": "1.0.0" }
```

The harness captures this as the Step 1 boot check, then proceeds through the
remaining steps.

---

## Serial mode (optional)

Works with any PlatformIO build environment over USB-CDC (115200 baud, 8N1).
Requires the `serialport` npm package, which is NOT installed by default so
that TCP mode stays dependency-free.

Install the package once:

```
npm install serialport
```

Then run:

```
node fw/test/integration/nanod_e2e.mjs --serial /dev/tty.usbmodem101
node fw/test/integration/nanod_e2e.mjs --serial COM3
```

In serial mode Step 1 waits for the `{"ack":"boot","ok":true}` frame that the
device emits on power-up, or accepts a settings response if the device is
already running.  Power-cycle or reset the device before running to ensure the
boot ACK is captured.

If `serialport` is not installed you will see a clear error with the install
command; no hard crash.

---

## Test steps

| Step | Command sent | What is asserted |
|------|-------------|-----------------|
| 1 | *(connect)* | TCP greeting `connected:true` or serial `ack:boot ok:true` |
| 2 | `{"settings":"?"}` | Response has `pdVoltage`, `wifiPassword:"***"` (redacted), `deviceName`, `firmwareVersion`, `serialNumber`, and other required fields; `pdVoltage` in [5,9] V |
| 3 | `{"settings":{"debug":false}}` | ACK `{"ack":"settings","ok":true}` |
| 4 | `{"profiles":"#all"}` | Response has `profiles` array (non-empty) and `current` string |
| 5a | sprite begin/data/end (correct CRC-32) | End ACK `ok:true` |
| 5b | sprite begin/data/end (wrong CRC-32) | End ACK `ok:false` — CRC mismatch caught by firmware |
| 6 | `{"wifi":{"enabled":false}}` | ACK `{"ack":"wifi","ok":true}` — set `SKIP_WIFI=1` to skip |

Step 5 uploads a small 32-byte synthetic payload and validates the firmware's
CRC-32 check in both the passing and failing direction.  The test sprite is
deleted after the step completes.  The harness includes its own CRC-32
implementation (IEEE 802.3, poly `0xEDB88320`) and verifies it against the
known vector `crc32("123456789") === 0xCBF43926` before any network I/O.

---

## Environment variables

| Variable | Effect |
|----------|--------|
| `SKIP_WIFI=1` | Skip Step 6 (useful when testing a non-WiFi build) |

---

## Relation to existing tests

The PlatformIO `test/` directory contains native unit tests that run on the
MCU via the PlatformIO Test Runner.  Those tests exercise individual C++
compilation units in isolation.  This harness is complementary:

- Native unit tests: fast, no hardware required, test internal logic.
- This harness: requires a running device, tests the full protocol stack
  end-to-end including JSON serialisation, ACK routing, CRC validation,
  and the filesystem-backed sprite store.

Both should pass before shipping a firmware release.

---

## Hardware checklist (manual, before running the harness)

- [ ] Device is powered and enumerated (USB or external supply).
- [ ] For TCP mode: device has joined the local WiFi network; confirm with
      `ping <device-ip>` before running.
- [ ] For serial mode: USB-CDC port is not open in another application
      (the desktop app, Arduino IDE, etc.).
- [ ] Firmware build matches the transport: `nanofoc_d_wifi` / `nanofoc_d_full`
      for TCP; any build for serial.
- [ ] Power-cycle the device before a serial run to capture the boot ACK.

---

## Syntax check (no device needed)

```
node --check fw/test/integration/nanod_e2e.mjs
```

This parses the file as ESM and exits 0 if there are no syntax errors.
