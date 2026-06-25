# Known Residuals — flagged for on-hardware validation

This update was **compile + link verified** (PlatformIO, ESP32-S3, arduino-esp32 2.0.x)
but **not flashed to hardware**. Below: the verified build matrix, what the latest pass
**resolved**, and what still needs a board.

## Verified build matrix (4 MB flash, dual-OTA app slot = 1,310,720 B)

| Env | Flash | RAM | Status |
|-----|-------|-----|--------|
| `nanofoc_d` (default safe core) | **65.6%** (860 KB) | 46.8% | ✅ builds, comfortable |
| `nanofoc_d_wifi` (+ WiFi/OTA/raw-TCP) | **94.7%** (1,241 KB) | 54.6% | ✅ builds, ~70 KB headroom |
| `nanofoc_d_audio` (+ I2S audio) | **71.1%** (932 KB) | 63.3% | ✅ builds |
| `nanofoc_d_full` (WiFi **and** audio) | **100.1%** (1,312,121 B) | 71.1% | ❌ **overflows by ~1.4 KB** |

**The 4 MB ceiling (Q2 hardware finding):** WiFi **or** audio fits; **both together overflow
the app slot by ~1,401 bytes.** This is a hardware limit, not a software bug — the board is
at its maximum. **Recommendation: next board rev → 8 MB or 16 MB flash** (keep the existing
PSRAM). That unlocks WiFi + audio + a real sprite library + comfortable dual-OTA at once.
Until then, ship `nanofoc_d_wifi` **or** `nanofoc_d_audio`, not `_full`.

## Resolved in the slim-transport + residuals pass
- **WebSocket relay limitation → FIXED.** Transport is now a lean raw-TCP JSON server on
  port 3333 (dropped `ESPAsyncWebServer`/`AsyncTCP`, ~66 KB reclaimed → WiFi 99.7%→94.7%).
  `com_thread` has a single `emit()` path + in/out queues, so replies/events **do** mirror
  to network clients now. All parsing stays in the COM task; sockets stay in the WiFi task.
- **FOC `haptic_state` torn reads → FIXED.** `pass_cur_pos/start/end` now read three
  `std::atomic<uint16_t>` mirrors updated in the FOC loop — no mutex in the real-time path.
- **PD selected-PDO byte offset → now A/B-testable.** Default behaviour unchanged; build
  with `-DPD_RDO_ALT_BYTE` to try the alternate byte. Confirm the correct one on hardware.
- **Feature flags overridable.** `nanofoc_d.h` now `#ifndef`-guards `NANO_AUDIO/DISPLAY/FS/MIDI`
  so `-D` build flags win consistently (fixed an audio ODR link error).

## Still needs on-hardware validation
1. **PD voltage path** — confirm 9 V is negotiated and the motor torque scales; verify which
   RDO byte is correct (`-DPD_RDO_ALT_BYTE` A/B). Safe meanwhile: voltage is clamped to
   `[5,9] V`, so a misread can only fall back to 5 V, never over-volt. (highest-risk test)
2. **OTA rollback** — flash a deliberately-bad build over OTA and confirm revert. Full
   auto-rollback-on-boot-crash also needs a bootloader built with
   `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` (see [BRICK_PROOFING.md](BRICK_PROOFING.md)).
3. **Raw-TCP transport** — the bidirectional queue path is compile-verified; validate under
   real sockets (connect to `<ip>:3333`, send commands, confirm replies/events stream back;
   provision over SoftAP `NanoD-Setup` → TCP).
4. **Sprite display decode** — upload works (chunked + CRC32); on-display PNG rendering needs
   an LVGL image decoder enabled (`LV_USE_LODEPNG`, ~40 KB — fits core/audio, not the wifi
   build) and a hardware check (uses PSRAM).
5. **Task watchdog scope** — only the WiFi task self-registers; widen via
   `WifiThread::addCurrentTask()` after bench-testing timeout margins (motor `initFOC()`
   during recalibration can be slow).

See also: [`BRICK_PROOFING.md`](BRICK_PROOFING.md), [`HARDWARE_TEST_CHECKLIST.md`](HARDWARE_TEST_CHECKLIST.md), [`../CHANGELOG.md`](../CHANGELOG.md).
