# Flashing the Nano_D++ firmware

The release asset `firmware.bin` is the **app image** — it flashes at offset `0x10000`
and updates a board that already has its bootloader + partition table (i.e. any board
that has run this firmware before). For a brand-new board, build/flash with PlatformIO
(`pio run -e nanofoc_d_wifi -t upload`), which writes the bootloader, partitions, and app.

## Quick update (recommended — no button dance)

From `integ/macos` in the [NanoD-Integrations](https://github.com/Dviros/NanoD-Integrations) repo:

```bash
./flash.sh path/to/firmware.bin
```

It sends `{"reboot":"bootloader"}` over serial to drop the running firmware into ROM
download mode (no BOOT+EN), waits for the download port, and flashes. Then **tap `EN`
once** to boot — esptool can't reset this board (native USB has no RTS→reset wiring).

## Manual (esptool)

1. **Enter download mode:** hold **BOOT**, tap **EN**, release **BOOT** (screen goes dark).
2. **Flash the app partition:**

   ```bash
   esptool.py --chip esp32s3 --port /dev/cu.usbmodem* \
     --before no_reset --after no_reset \
     write_flash --flash_mode keep --flash_freq keep --flash_size keep \
     0x10000 firmware.bin
   ```

3. **Tap `EN`** to boot the new firmware.

> The build can't self-reset over native USB, so `--after hard_reset` is a no-op here —
> always boot with a physical `EN` tap after flashing.
