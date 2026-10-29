# Hosyond recovery validation

Recorded on 2026-10-28 using one Hosyond / LCDWiki ES3C28P ESP32-S3 N16R8 and one FM-1. Recovery used charged batteries and one direct USB-C data cable, without a hub or external USB power. The FM-1 was running SLOOP before the recovery tests.

The tested guarded build reports `guarded-v6`. Source revision: [`18f11df`](https://github.com/JoeyWangTW/fm1buddy/commit/18f11df8bba7286fb955f2493db2cc78434fa558). Application SHA-256:

```text
e2eb9d5c91119e34e66f129de248ab8185874f070d4dd7e30be33dfa512eec7f
```

The application-only upload was hash-verified, preserving the display's partition layout and SD contents. The subsequent documentation consolidation does not change firmware sources or build configuration.

## Results

| Check | Evidence and result |
| --- | --- |
| Local native tests | MIDI/SLOOP tests, recovery policy and actual reader/write-refusal tests passed. |
| Guarded writer tests | ASan/UBSan tests using fm1buddy's actual sector writer passed: zero-write dry runs; authorization, address, payload and CRC checks; protected/reference drift; changed live flash; short reads; partial writes; corrupt readback; descending order; tail preservation; closed-window refusal. Offline bundle preparation tests passed. |
| Builds | `cyd-s3`, `hosyond-diagnostic` and `hosyond-recovery` built successfully. The final guarded image includes the battery ADC initialization fix. |
| Computer/setup guard | Recovery Start refused while connected to the Mac; console remained available with `recovery=0 key=0`. SD, helper, PSRAM, battery reading and action controls were checked. |
| V5 read-only backup | Entered ROM, read 1 MiB twice with matching results and verified the SD backup. A private Mac copy matched its length and SHA-256. Owner confirmed SLOOP still booted. |
| V6 stock dry run | Captured trace: fresh verified backup, `PASS`, 143 differing sectors, `started=0`, no sector-write records. |
| V6 stock restore | Captured trace: fresh verified backup, all 143 sectors verified in descending order, full 1 MiB comparison passed. Owner reports the restored device works. |
| V6 Hub install | Owner reports a successful Hub install. Target firmware/version and its install trace were not supplied; this is user confirmation, not an independently audited transfer. “Hub” here means the firmware catalog, not a physical USB hub. |
| Upstream CI | Awaiting maintainer approval for the fork's Actions run at the time of this record; local passes do not establish a remote CI pass. |

## Captured recovery evidence

Both V6 attempts verified ROM USB `4C4A:8057`, INQUIRY `WL82 / UBOOT1.00 / 1.00`, chip key `980F`, flash ID `856014` and 1 MiB capacity. Each made a fresh, verified backup before proceeding. Their backup hashes match the previously preserved private V5 SLOOP backup. The new V6 backup files remain on SD; this record does not claim those files were separately exported to the Mac.

The dry-run trace ends:

```text
stock: dryrun-v15 result=PASS sectors=143 started=0 address=000000
stock: V15 dry run passed. Nothing written.
job: recover done
```

The restore trace includes:

```text
KEY: pin self-check edges=16/16 key=16EF/16EF routes=81,83 PASS
PULSE: handoff after 6000 ms, D+=1 rising_edges=5999
USB: preinitialized host handoff in 187 us
USB: WL82 UBOOT1.00 INQUIRY verified
stock: sector 092000 verified 1/143
[141 intermediate sector-verification records omitted]
stock: sector 004000 verified 143/143
stock: restore-v15 result=PASS sectors=143 started=1 address=000000
USB: submit ep=81 error=ESP_ERR_INVALID_STATE
uboot: back to its firmware
stock: Stock V15 restored and full flash verified.
job: recover done
```

A local audit checked all 143 sequence numbers and addresses, exactly `0x92000` down to `0x4000` in `0x1000` increments. The final comparison covers the whole 1 MiB, including the preserved boot region `[0,0x4000)` and tail `[0x93000,0x100000)` against the fresh snapshot. The raw trace and device dumps remain private; the excerpts omit backup filenames and hashes.

The USB error occurred **after** the successful full-flash comparison, during `uboot::leave()` requesting `RUN_APP`. That function already allows for the device restarting before it answers, but this trace does not prove the error's cause. Flash verification succeeded and the owner reports a working restored device; a clean restart acknowledgment is not established. The unconditional “back to its firmware” log alone is not proof of a successful reboot.

## Remaining limits

- These are a small number of successful runs on one board/device pair, not a startup reliability study or validation of other ESP32-S3 boards.
- Power loss, cable removal, cancellation and transport faults during real flash writes have not been deliberately tested on hardware. Automated fault tests cannot establish recovery from every interruption.
- The default `cyd-s3` recovery writer and arbitrary firmware/backup restoration are not validated by these stock-only results.
- The ESP32 battery threshold is a precaution. The app cannot measure the FM-1 battery or guarantee sufficient runtime.
- A transport failure after mutation prevents further writes and enters the keep-power-on state, but can also prevent helper heartbeats.

See [setup and restore controls](hosyond-restore.md) before operating the guarded profile. The repository distributes no firmware package, RAM helper, restore bundle, private device backup or raw private trace.
