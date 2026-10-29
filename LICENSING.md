# Licensing

fm1buddy is GPL-3.0-only (see [LICENSE](LICENSE)).

| Part                                   | Author                                                                                                                                                                                                                                                                                                                                                                |
| -------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/app/fm1.cpp`, `src/app/fm1.h`     | A port of [foma](https://github.com/konsumer/foma)'s `web/fm1ota.js` and `web/fm1pkg.js`: Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca))                                                                                                                                                      |
| `src/app/themes.h`                     | [FOMAJAM](https://github.com/konsumer/fomajam)'s color themes                                                                                                                                                                                                                                                                                                         |
| `src/app/uboot.cpp`, `src/app/uboot.h` | The FM-1's boot loader protocol and JieLi's cipher, as [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) (kurogedelic, MIT) and [jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) (kagaimiq, MIT) describe and implement them. JieLi's `wl82loader.bin` isn't included: it's downloaded from jl-uboot-tool to the SD card when it's needed |
| `src/s3/key_s3.cpp`                    | The USB_KEY and the 1 ms pulses that wake the FM-1's boot loader, as [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) (kurogedelic, MIT) sends them (`src/recovery.c`, `pio/usb_key.pio`)                                                                                                                                                          |
| everything else                        | fm1buddy's authors                                                                                                                                                                                                                                                                                                                                                    |

## Third-party parts

PlatformIO downloads these when it builds; their license notices apply to the built firmware (`fm1buddy-cyd-s3.bin`).

| Part                                                                                                | License              |
| --------------------------------------------------------------------------------------------------- | -------------------- |
| [LVGL](https://github.com/lvgl/lvgl), with its Montserrat (OFL-1.1) and FontAwesome (OFL-1.1) fonts | MIT                  |
| [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)                                                      | FreeBSD / MIT        |
| Arduino-ESP32, ESP-IDF                                                                              | LGPL-2.1, Apache-2.0 |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson)                                             | MIT                  |
| [TinyUSB](https://github.com/hathach/tinyusb)                                                       | MIT                  |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino), Apache NimBLE                           | Apache-2.0           |


## Hosyond recovery backend and stock policy

`src/s3/recovery/key_rmt.cpp`, `usb_idf.cpp`, and `policy.h` are adapted from the Hosyond ESP32-S3 Transporter experiment, itself based on kurogedelic/FM-1-transporter commit `a7fb5e8ac692c528f8ed29e325911a3f4b8111d6`. These files retain MIT licensing; the notice is in `src/s3/recovery/LICENSE`. The fm1buddy integration is GPL-3.0-only. No JieLi helper, firmware package or device dump is bundled.

`src/app/stock_restore.c` and `stock_restore.h` retain the MIT-licensed fixed-stock policy from the same ESP32-S3 experiment; its wire protocol follows FM-1-transporter. The GPL-3.0-only `src/s3/recovery/guarded.cpp` and `guarded.h` integrate that policy with fm1buddy's UI, backup and transport layers.

`tools/prepare_v15.py` adapts the package decoder from [charlesvestal/fm1-x0x, tools/fm1_rescue.py](https://github.com/charlesvestal/fm1-x0x/blob/70440e57c2b1390c93569baa55b8e6b7eacbabc2/tools/fm1_rescue.py), commit `70440e57c2b1390c93569baa55b8e6b7eacbabc2`, under GPL-3.0-only. It prepares bundles locally and does not distribute stock firmware or backups.
