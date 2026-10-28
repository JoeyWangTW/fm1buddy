# Hosyond: Hub installs and guarded stock restoration

`hosyond-recovery` enables the normal Hub firmware installer and adds a guarded **stock V15** recovery action to the RMT/IDF backend. `hosyond-diagnostic` remains a separate read-only build. This profile is for the Hosyond/LCDWiki ES3C28P ESP32-S3 N16R8, its one-bit SD wiring and battery ADC9 circuit; other boards need checked pin and power configurations.

Flashing can leave an FM-1 unable to start if power or communication fails. This integration's read-only ROM entry and backup have passed one hardware test. The guarded writer has automated failure tests and is adapted from the standalone app that restored this owner's FM-1; **actual restore writes and Hub installs in this new build still need hardware validation**.

## Build and SD preparation

```sh
sh tools/unit-test.sh
sh tools/recovery-test.sh
sh tools/stock-test.sh
pio run -e hosyond-recovery
```

Preserve the existing display flash and partition layout. Upload the application with the FM-1 disconnected; do not format the SD card. See [hardware notes](hosyond-recovery.md) for pins and the hash-pinned `/fm1buddy/wl82loader.bin` RAM helper. The display's console command `recoveryinfo` reports `guarded-v6`, `hub=ENABLED`, battery voltage and whether stock preparation is ready. It does not start a recovery or write.

For recovery, prepare a private stock bundle from the exact stock V15 `.fwsc` package and a complete backup of this FM-1:

```sh
python3 tools/prepare_v15.py /path/to/stock-v15.fwsc /path/to/fm1-backup.bin /path/to/stock-v15.bundle
```

The offline tool refuses any package except SHA-256 `db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a` and checks the extracted image against `c1068a403c04a6378a626afb66cc217f72d13f10aa200e983b7a9817cc1dac03`. It also checks the backup's protected boot bytes. Put the bundle at `/transporter/stock-v15.bundle` on SD using USB disk mode and safely eject the disk before leaving that mode. The owner's existing verified bundle can be reused. No firmware or device backup is included in this repository.

The bundle's old reference is not used as the live device snapshot in this integration. Every attempt takes two new matching reads, saves and checksum-verifies a new SD backup, binds the operation to that snapshot, and reads the live flash again before writing. This allows returning to stock after changing firmware while preserving the current bytes outside the stock firmware region.

## Recovery actions

Use charged batteries on both devices and one direct USB data cable, without a hub or external USB power. Turn FM-1 off. Disconnect the display from the computer and RESET it on battery; connect the FM-1 while off. Open **Settings → Recover the FM-1** and select:

- **Read and back up**: two matching full reads and a verified SD backup; no flash writes.
- **Check V15 restore**: verify the bundle, identity, protected boot region, fresh backup, and live comparison; no flash writes. Run this before the first restore in a new build.
- **Restore stock V15**: tap Start, then **Confirm V15 restore**. This replaces firmware with stock V15. The saved action is consumed before device access; a new selection and confirmation are required for each attempt.

Switch the FM-1 on only when prompted. Recovery enforces an ESP32 battery reading between 3.90 and 4.35 V before preparation and before every sector. It cannot measure the FM-1 battery. The voltage threshold is a precaution, not a runtime guarantee or calibrated battery percentage.

Stock writes are limited to `[0x4000, 0x93000)`. The boot region below `0x4000` and the bytes from `0x93000` to `0x100000` are preserved. Preserving those bytes does not guarantee compatibility of saved data across firmwares. Differing sectors are written from highest to lowest address, checked individually, then the entire 1 MiB flash is compared. The USB transport authorizes only the current sector's erase and eight exact, CRC-checked 512-byte writes. It does not enable arbitrary erase/write commands or arbitrary backup restoration.

A failed erase/write is never retried automatically. After a write has started, any failure leaves a **KEEP POWER ON** screen with restart/retry/cancel controls unavailable; the worker attempts to keep the RAM helper alive if the transport still works. Keep both devices connected and powered and record the exact message. A failed USB connection can prevent that heartbeat; the UI cannot guarantee the device remains in the loader.

Backups are unique `fm1-stock-*.bin` files in `/backups` with `.bin.txt` metadata. Incomplete writes remain `.partial`. The file is verified before it becomes a completed backup. Keep copies privately on the Mac; do not publish device dumps.

## Hub firmware installs

In normal mode, select a firmware in the Hub, download it if necessary, and use **Install** with the existing second-tap confirmation. The read-only build's install block is absent. Package SHA-256 checks and device identity checks remain in the upstream installer. This uses the instrument's normal update protocol, not the stock recovery sector writer.

The ESP32 battery must read at least 3.90 V immediately before an install starts. Keep the FM-1 charged too. Install only when the FM-1 is detected through a suitable USB host connection; the Mac setup connection cannot simultaneously host the FM-1. After an update has started, this profile does not cut power or cancel it if battery voltage falls.

## Validation boundary

The native stock tests exercise the actual fm1buddy sector writer through the stock guard: dry runs produce zero writes; invalid authorization, protected-region mismatches and changed live flash are rejected; short reads, write failures and corrupt readback stop the operation; writes occur in descending order; protected/tail bytes survive; and write commands are rejected once the window closes. ASan/UBSan are enabled.

One V5 read-only recovery succeeded on this display and a working FM-1 running SLOOP, and the owner confirmed SLOOP still boots. That result is recorded in [the diagnostic history](hosyond-recovery.md). It does not validate V6 restore writes. No FM-1 flash mutation is part of building, uploading or checking this app on the Mac.

## V6 setup validation — 2026-10-28

All three profiles built successfully, and the native/sanitizer and offline preparation tests passed. The guarded image was uploaded application-only and hash-verified; its SHA-256 is `e2eb9d5c91119e34e66f129de248ab8185874f070d4dd7e30be33dfa512eec7f`. The display UI shows the three actions, battery voltage and scroll-accessible Start button. The computer-connected Start guard was checked without initiating recovery. No FM-1 write was performed; the integrated V6 dry run and actual write paths still need physical validation.
