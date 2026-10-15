# Licensing

fm1buddy is GPL-3.0-only (see [LICENSE](LICENSE)).

| Part                             | Author                                                                                                                                                                                                           |
| -------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `cyd/fm1ota.cpp`, `cyd/fm1ota.h` | A port of [foma](https://github.com/konsumer/foma)'s `web/fm1ota.js` and `web/fm1pkg.js`: Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)) |
| `cyd/touch.cpp`                  | After [pntr_app](https://github.com/RobLoach/pntr_app)'s ESP32 platform                                                                                                                                          |
| `usbhost/`, and everything else  | fm1buddy's authors (the host box started in [FOMAJAM](https://github.com/konsumer/fomajam))                                                                                                                      |

## Third-party parts

| Part                                                             | License              | Used by                                                  |
| ---------------------------------------------------------------- | -------------------- | -------------------------------------------------------- |
| [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)                   | FreeBSD / MIT        | the CYD build (PlatformIO downloads it)                  |
| Arduino-ESP32, ESP-IDF                                           | LGPL-2.1, Apache-2.0 | the CYD build                                            |
| [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) | BSD-3-Clause         | `fm1buddy-usbhost.uf2` (`usbhost/build.sh` downloads it) |
| [TinyUSB](https://github.com/hathach/tinyusb)                    | MIT                  | `fm1buddy-usbhost.uf2`                                   |
| [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB)   | MIT                  | `fm1buddy-usbhost.uf2`                                   |

Their license notices apply to the built files.
