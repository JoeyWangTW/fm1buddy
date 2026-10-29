# Hosyond ESP32-S3 recovery

The opt-in `hosyond-diagnostic` profile provides read-only ROM entry and a verified full-flash backup on the Hosyond / LCDWiki ES3C28P. Normal firmware installs are disabled, `uboot::writeSector` refuses requests, and the recovery USB boundary rejects flash erase/write commands. Removing the read-only flag without selecting the guarded stock policy fails compilation.

For Hub installs and stock-V15 recovery, use the separate [guarded restore profile](hosyond-restore.md). See the [validation record](hosyond-validation.md) for tested results and limits.

## Hardware and build

- Hosyond / LCDWiki ES3C28P, ESP32-S3 N16R8, ILI9341 display and FT6336 touch.
- FAT32 microSD, using SDMMC one-bit CLK38 / CMD40 / D039. The profile does not format the card.
- A compatible protected 1S battery. Both devices run on charged batteries during recovery, connected by one direct USB-C data cable, without a hub or external USB power. Check the board's battery connector polarity before connecting a battery.
- Native USB D− is GPIO19; D+ is GPIO20. Other S3 boards require independently checked pin, memory and power configurations; they are not validated by this work.

```sh
sh tools/unit-test.sh
sh tools/recovery-test.sh
pio run -e hosyond-diagnostic
```

Preserve the ESP32's existing flash before installing. Flash only with the FM-1 disconnected. The build uses the same default 16 MiB partition layout as `cyd-s3`; do not erase flash or overwrite a different partition layout blindly. Keep private FM-1 backups available.

Place `wl82loader.bin` in `/fm1buddy/` on SD, using the URL and SHA-256 in [src/app/uboot.h](../src/app/uboot.h). The helper remains external and must pass its hash check. No stock firmware package or restore bundle is needed for this diagnostic.

## Read-only hardware test

1. While connected to a computer, run `recoveryinfo` on the serial console. It should report `backend=RMT-IDF`, `writes=DISABLED`, `sd=1`, `helper=1` and `computer=1`. Recovery Start refuses to run on a computer.
2. Turn the FM-1 off. Disconnect the display from the computer and RESET it on battery. Connect the direct cable with the FM-1 still off.
3. Open **Settings → Recover the FM-1**, check the read-only label, and tap Start. The display restarts into recovery. Switch the FM-1 on only when prompted.
4. The app checks the chip identity, reads the whole flash twice, requires identical reads, and verifies the saved SD backup's SHA-256. Success reads **Backup verified. Nothing written to FM-1 flash.** This diagnostic does not perform a stock-restore dry run.
5. After recovery ends, disconnect the FM-1, reconnect the display to the computer and RESET. Retrieve the persistent trace with `python3 tools/s3.py cat fm1buddy/recovery.txt`. Copy the named `/backups/fm1-N.bin` and its metadata privately using USB disk mode, then safely eject the disk before leaving that mode. Do not publish device dumps.

Key transmission has a 30-second limit; calibration and enumeration are separately bounded. The job also has a 65-second key/host wait limit. A failed IDF transport stays stopped until reset; it does not retry USB transactions automatically. Cancel while waiting restarts the display; this diagnostic cannot erase or write FM-1 flash.

## Backend and integration boundary

Only recovery-mode storage calls use Espressif's USB host. Ordinary MIDI, audio, hubs and computer/disk modes retain TinyUSB. The two stacks do not own the controller simultaneously.

The backend preinitializes the host with its root port inactive and PHY pads released, sends paired RMT key waveforms and open-drain calibration pulses, then hands the pads to the prepared host. An on-chip capture must see the full 16-bit key before acknowledgment sensing starts, avoiding premature pauses caused by a powered-off device clamping D−. Capture checks the local signal, not cable continuity.

Recovery boots skip the RGB LED's RMT allocation, which otherwise conflicts with the key channels' clock and memory requirements. Normal boots retain the LED. The GPIO matrix is reconnected after changing pin direction.

Only ROM USB `4C4A:8057` with verified `WL82 / UBOOT1.00 / 1.00` INQUIRY is accepted. Bulk transfers validate lengths and CSW tags, status and residue. RAM commands are bounded to the pinned helper. The diagnostic rejects all flash mutations; the guarded profile permits only the stock policy's current sector operation.

Traces append to `/fm1buddy/recovery.txt` across resets. Computer detection runs before honoring a recovery-mode boot request. Failed key/host preparation enters an explicit error state. See [LICENSING.md](../LICENSING.md) for the Transporter lineage and retained notices.
