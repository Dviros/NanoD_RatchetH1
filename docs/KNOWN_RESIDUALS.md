# Known Residuals — flagged for on-hardware validation

This firmware update was **compile-verified** (PlatformIO, ESP32-S3, arduino-esp32
2.0.x) but **not flashed to hardware** in this work. The items below were found by
adversarial review and **deliberately not auto-fixed**, because fixing them blind —
without a board to test on — carried more risk than the symptom. Each is safe in its
current state; validate and finish on real hardware.

## 1. 4 MB flash ceiling (hardware constraint → next-rev recommendation)
Measured build sizes (app slot = `app0`/`app1` = `0x140000` = 1,310,720 B each, dual-OTA):

| Env | Flash | Headroom | Status |
|-----|-------|----------|--------|
| `nanofoc_d` (default, safe core: all fixes + PD + sprites + brick-proofing) | **65.6%** (840 KB) | ~470 KB | comfortable |
| `nanofoc_d_wifi` (+ WiFi/OTA/AsyncWebServer/WS) | **99.7%** (1,307 KB) | **~3.5 KB** | builds, feature-frozen |
| `nanofoc_d_audio` (+ I2S audio) | not built | — | expected to fit (core has room) |
| `nanofoc_d_full` (WiFi + audio) | — | **cannot fit** | WiFi alone is already 99.7% |

**Implication:** on this 4 MB board you cannot run WiFi **and** audio together, and the
WiFi build has no room to grow. **Recommendation (Q2 hardware):** next board rev → **8 MB
or 16 MB flash** (keep the existing PSRAM). That single change unlocks WiFi + audio + a
real sprite library + comfortable dual-OTA simultaneously. Until then, prefer the slim
WiFi transport option (drop `ESPAsyncWebServer`, keep `ArduinoOTA` + a raw TCP socket
mirroring the serial JSON API) if the web UI/WebSocket isn't required — that reclaims
~200 KB.

## 2. PD selected-PDO bit extraction (hmi_thread.cpp `init_pd`, ~line 596)
Review flagged that `selected_pdo = (b3 >> 4) & 0x07` may need to read the **first**
big-endian byte (`b0`) of `RDO_REG_STATUS` (0x91) instead of `b3`. **Not changed** — it
can't be confirmed without the STUSB4500 datasheet + a board. **This is safe:** the
negotiated voltage is clamped to `[5.0, 9.0] V` before it ever reaches the motor driver,
so a misread can only cause the device to fall back to 5 V (losing the extra torque),
**never** an over-voltage. Validate with a USB-PD analyzer or by measuring VBUS, then
confirm/adjust the bit offset.

## 3. FOC `haptic_state` cross-core reads (foc_thread `pass_*` accessors)
`haptic.haptic_state` scalars are written in the FOC loop (core 1) and read by the HMI/LCD
thread (core 0) without a lock. **Not fixed with a mutex on purpose** — taking a blocking
mutex inside the real-time FOC loop risks stalling motor control, which is *worse* than the
symptom. Impact today: at worst a transient LED/arc-position glitch; never a crash or
brick (single-word `uint16` reads). **Proper fix:** mirror the 3 values the HMI actually
needs (`current_pos`, `start_pos`, `end_pos`) into `std::atomic<uint16_t>` updated from the
FOC loop. Low-risk to add; verify visually on hardware that LED/arc tracking is smooth.

## 4. WebSocket relay reply path (wifi_thread `_handleWsCommand`)
In the `WIFI_ENABLED` build, WS commands other than `wifi`/`sprite` are relayed to the
command parser via `Serial.println(json)`, but `com_thread`'s replies go only to the serial
TX — they are **not** mirrored back to the WS client. **The serial/USB transport is fully
functional;** the WebSocket is a bonus transport. Documented in `integ/SERIAL_API.md`.
**Fix (deferred):** a shared `QueueHandle_t` from `wifi_thread` into `com_thread` plus a WS
broadcast of replies. Also gated by the 4 MB flash ceiling above.

## 5. Task watchdog scope
The WiFi task registers with `esp_task_wdt`; the core real-time threads are **not**
auto-added (conservative — an untested aggressive WDT can boot-loop). The strongest
brick-protection here is the **dual-OTA + `esp_ota_mark_app_valid_cancel_rollback()`** path
and the **atomic + CRC settings** — both compile-verified. To widen WDT coverage, call
`WifiThread::addCurrentTask()` from each thread's loop and bench-test timeout margins
(motor `initFOC()` during recalibration can be slow — see `HapticCommander`).

---
See also: [`BRICK_PROOFING.md`](BRICK_PROOFING.md), [`HARDWARE_TEST_CHECKLIST.md`](HARDWARE_TEST_CHECKLIST.md), [`../CHANGELOG.md`](../CHANGELOG.md).
