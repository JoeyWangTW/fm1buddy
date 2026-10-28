# Hosyond ESP32-S3 recovery integration

This experimental profile tests fm1buddy's recovery UI and backup workflow with the RMT/native-IDF USB backend used by a successful standalone recovery app. It is **read-only**: normal firmware installation is refused, `uboot::writeSector` is compiled to refuse every request, and the recovery USB boundary rejects erase/write commands. Removing only the read-only build flag fails compilation. It does not yet integrate the standalone app's guarded stock-V15 writer.

## Hardware and build

- Hosyond / LCDWiki ES3C28P, ESP32-S3 N16R8, ILI9341 display and FT6336 touch.
- FAT32 microSD. The profile uses SDMMC one-bit CLK38 / CMD40 / D039. It does not format the card.
- A compatible protected 1S battery, charged separately as required. Both boards run on battery during recovery; connect only a direct USB-C data cable between them. No hub or external USB power during the ROM-key test.
- Native USB D− is GPIO19; D+ is GPIO20. Other S3 boards need independently checked pin, memory and power configurations.

```sh
sh tools/unit-test.sh
sh tools/recovery-test.sh
pio run -e hosyond-diagnostic
```

Preserve the ESP32's existing flash before installing. Flash only with the FM-1 disconnected. The build uses the same default 16 MiB partition layout as `cyd-s3`; do not erase flash or overwrite a different partition layout blindly. Keep the original working recovery app and private FM-1 backups available.

Place `wl82loader.bin` in `/fm1buddy/` on SD. Use the URL and SHA-256 in `src/app/uboot.h`; the helper remains external and must pass its hash check. No stock firmware package or restore bundle is needed for this diagnostic. Existing `/firmware` and `/transporter` files are not removed.

## Read-only hardware test

1. While connected to the Mac, `recoveryinfo` on the serial console must report `diagnostic-v5`, `backend=RMT-IDF`, `writes=DISABLED`, `sd=1`, `helper=1`, and `computer=1`. The recovery Start action must refuse to run on a computer.
2. Turn the FM-1 off. Disconnect the display from the Mac and RESET it on battery. Connect the direct cable with the FM-1 still off.
3. Open **Settings → Recover the FM-1**, check the read-only diagnostic label, and tap Start. The display restarts into recovery. Switch the FM-1 on only when the screen asks for it.
4. On success, the app checks the chip identity, reads the whole flash twice, requires identical reads, and verifies the saved SD backup's SHA-256. The result says **Backup verified. Nothing written to FM-1 flash.** A live dry run of a restore is not performed by this build.
5. Disconnect the FM-1, reconnect the display to the Mac and RESET. Retrieve the persistent log with `tools/s3.py cat fm1buddy/recovery.txt`. Preserve the named `/backups/fm1-N.bin` and metadata privately via USB disk mode after recovery has ended. Do not publish device dumps.

Key transmission has a 30-second limit; calibration and enumeration are separately bounded. The job also has a 65-second key/host wait limit and reports failure rather than waiting indefinitely. A failed IDF transport stays stopped until reset; no automatic USB or flash retry is attempted by that backend. Cancel while waiting restarts the display, as in upstream; it is safe only because this profile cannot erase/write flash.

## Integration boundary

Normal MIDI, audio, hubs, and computer/disk modes continue to use fm1buddy's TinyUSB implementation. Recovery boots reserve the USB controller for Espressif's host library. The two stacks never run together: recovery-mode dispatch routes only storage operations to IDF, and ordinary host startup refuses that mode.

The recovery backend preinitializes the host with its root port inactive and PHY pads released, sends paired RMT key waveforms and open-drain calibration pulses, then hands the pads to the prepared host. The GPIO matrix is reconnected after changing pin direction, and an on-chip capture must see all 16 key bits. This is not an oscilloscope measurement or proof of cable continuity. Bulk transfers check lengths and CSW tags/status/residue; RAM commands are bounded to the pinned helper, and flash erase/write commands are never allowed.

Recovery logs append to `/fm1buddy/recovery.txt` and survive ordinary host/serial resets. The Mac is detected before honoring a recovery-mode boot request. A failed key/host preparation enters an explicit error state.

## Evidence and remaining work

The source standalone app (v0.2.2) obtained two matching full reads, verified SD/Mac backups, passed a stock-V15 dry run, then wrote and verified 143 sectors plus the full 1 MiB flash. The owner confirmed stock firmware boots; the one-shot write token was consumed. That result belongs to the standalone app, not this integration.

Native tests cover the actual fm1buddy reader, full 1 MiB transfer, short-read rejection, key bit order/timing, packet-size rounding, ROM identity, CSW checks, command allowlist, and write refusal. The v3 integrated on-device test failed during RMT initialization, before transmitting the recovery key. V4 then passed signal capture and detected USB, but enumerated `1209:0001` instead of the required ROM `4C4A:8057`; no helper was sent and no backup was read. V5 subsequently completed one on-device recovery backup on the same FM-1 running SLOOP: ROM identification, two matching full reads, SD verification and a matching private Mac copy. A separate change can bring in the hash-pinned V15 policy, verified fresh backups, bundle-bound dry run and single-use write approval. Any real FM-1 write requires a new explicit approval. The earlier approval was already consumed by the successful standalone restore.

The Hub Install action in this diagnostic now displays “Read-only test build: installs disabled.” immediately instead of a misleading busy message.

The default upstream writer is not changed or declared validated by this work. This profile cannot restore arbitrary firmware or backups. Other boards, startup repeatability and hardware cancellation remain unvalidated.


## Local validation — 2026-10-28

- Existing MIDI/SLOOP tests and the recovery sanitizer/write-refusal tests pass.
- Both `cyd-s3` and `hosyond-diagnostic` build with pinned pioarduino 55.03.312 / Arduino 3.3.12. This does not establish hardware recovery.
- A private full 16 MiB backup of the display's previous app was saved before installing. The backed-up app bytes match the previously verified standalone v0.2.2 image; the partition table matches this build. Only the application at `0x10000` is updated, with no erase/format of device storage or SD.
- Previous v3 diagnostic application SHA-256: `cc01034954b640e01456e74482ea73f882781eac94a337aef62b2b10e30bcfab`.
- The application-only upload passed esptool hash verification. The installed console reports `backend=RMT-IDF writes=DISABLED sd=1 helper=1 computer=1 psram=8388608 recovery=0 key=0`. The captured recovery page has readable read-only/no-writes labels. Pressing Start while connected to the Mac leaves `recovery=0 key=0` and the console available.
- The v3 trace contains seven consistent `KEY: RMT initialization failed` results, before key transmission, USB enumeration or helper upload. No FM-1 flash writes occurred.
- The Arduino RGB LED driver retains an APB-clocked RMT channel even when writing black. The recovery channels use XTAL; [Espressif requires one clock source per RMT group](https://docs.espressif.com/projects/esp-idf/en/release-v5.3/esp32s3/api-reference/peripherals/rmt.html). Each 64-symbol recovery channel also reserves two of the S3's four 48-symbol TX blocks. The retained LED channel conflicts with both requirements. V4 skips RGB initialization on recovery-requested boots, preserving ordinary LED behavior, and logs the operation, GPIO and error for each RMT allocation/enable failure. The old trace did not capture which allocation returned the error, so this is a source-confirmed conflict, not a measured reproduction of that exact return code.
- V4 application SHA-256: `34fe6bfe9d72c56fdbbe806b0a924da177ded318488c992965dd790889c211f3`. Application-only upload was hash-verified, the console reports `diagnostic-v4` with SD/helper present and writes disabled, and Start on the Mac leaves `recovery=0 key=0` with the console available.
- The V4 hardware trace confirms RMT initialization now succeeds, GPIO capture sees 16 edges and key `16EF`, and the USB host enumerates `1209:0001`. The owner confirms SLOOP was installed after the earlier stock restore; the USB ID alone does not uniquely identify its firmware. The boot-ROM identity filter rejects it. This proves the earlier allocation failure is cleared, not that recovery succeeds.
- Before the first valid pin capture, V4 repeatedly interpreted the powered-off device's clamped D- as a possible ACK and stopped sending for 500 ms. V5 skips acknowledgment sensing until the first complete key capture, so those pauses cannot consume the startup window. The subsequent V5 attempt entered ROM and completed a verified backup on the working SLOOP device; repeatability across further starts remains untested. An unexpected USB identity now ends the attempt immediately with an explicit log instead of a further 15-second wait; the ROM-only filter remains intact.
- V5 application SHA-256: `166fd99380194585bbef08e8890a5a3564450d7f9b8fd898ae905543dc6962d7`. Both builds and native tests passed; application-only upload was hash-verified. The installed console reports `diagnostic-v5`, writes disabled, SD/helper present, and `computer=1 recovery=0 key=0`.
- One integrated V5 read-only hardware test passed on the Hosyond ES3C28P with a working FM-1 running SLOOP, both on battery and joined by a direct USB-C data cable. The trace records:
  - 16 captured key edges, key `16EF`; pull-up after 2,002 packets.
  - 6,000 ms calibration, 5,999 observed rising edges; USB handoff in 184 us.
  - ROM USB `4C4A:8057` and verified `WL82 / UBOOT1.00 / 1.00` INQUIRY.
  - Expected chip key `980F`, flash ID `856014`, size 1,048,576 bytes.
  - Two matching full reads, saved SD backup checksum verified, and `Backup verified. Nothing written to FM-1 flash.` / `job: recover done` at 22.1 seconds after the recovery reboot.
- The new SD backup and metadata were copied privately to the Mac; its exact 1 MiB length and SHA-256 match the SD verification in the trace. The exported SD volume was safely ejected. The backup and raw trace are not published.
- This establishes one successful integrated ROM-entry/read/backup run. Integrated restore writes, other boards, repeated startup reliability and hardware cancellation remain unvalidated. Remote CI needs maintainer approval. No FM-1 write is authorized or enabled by this installation.
