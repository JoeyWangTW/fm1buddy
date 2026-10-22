# Licensing

fm1buddy is GPL-3.0-only (see [LICENSE](LICENSE)).

| Part                                           | Author                                                                                                                                                                                                           |
| ---------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/common/fm1ota.cpp`, `src/common/fm1ota.h` | A port of [foma](https://github.com/konsumer/foma)'s `web/fm1ota.js` and `web/fm1pkg.js`: Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)) |
| `src/feather/tusb_config_fm1buddy.h`           | Adafruit TinyUSB's RP2040 config (MIT), with 4 MIDI devices                                                                                                                                                      |
| `lib/Pico-PIO-USB/`                            | [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB) 0.7.2, sekigon-gonnoc (MIT, its LICENSE is there)                                                                                                 |
| `src/app/fm1.cpp`, `src/app/fm1.h`             | The same port as `src/common/fm1ota.cpp` (Leo Kuroshita, Hügelton Instruments)                                                                                                                                   |
| `src/app/themes.h`                             | FOMAJAM's color themes                                                                                                                                                                                           |
| `src/feather/host.cpp`, and everything else    | fm1buddy's authors (the host box started in [FOMAJAM](https://github.com/konsumer/fomajam))                                                                                                                      |

## Third-party parts

PlatformIO downloads these when it builds; their license notices apply to the built files.

| Part                                                                                                | License                         | Used by                                     |
| --------------------------------------------------------------------------------------------------- | ------------------------------- | ------------------------------------------- |
| [LVGL](https://github.com/lvgl/lvgl), with its Montserrat (OFL-1.1) and FontAwesome (OFL-1.1) fonts | MIT                             | `fm1buddy-cyd-s3.bin`                       |
| [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)                                                      | FreeBSD / MIT                   | `fm1buddy-cyd-s3.bin`                       |
| Arduino-ESP32, ESP-IDF                                                                              | LGPL-2.1, Apache-2.0            | `fm1buddy-cyd-s3.bin`                       |
| [Arduino-Pico](https://github.com/earlephilhower/arduino-pico), with the Pico SDK and FatFs         | LGPL-2.1, BSD-3-Clause, FatFs's | `fm1buddy-feather.uf2`, `fm1buddy-pico.uf2` |
| [Adafruit TinyUSB](https://github.com/adafruit/Adafruit_TinyUSB_Arduino), TinyUSB                   | MIT                             | `fm1buddy-feather.uf2`, `fm1buddy-pico.uf2` |
| [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB)                                      | MIT                             | `fm1buddy-feather.uf2`                      |
| [U8g2](https://github.com/olikraus/u8g2)                                                            | BSD-2-Clause                    | `fm1buddy-feather.uf2`                      |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson)                                             | MIT                             | `fm1buddy-cyd-s3.bin`                       |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino), Apache NimBLE                           | Apache-2.0                      | `fm1buddy-cyd-s3.bin`                       |
| [SdFat](https://github.com/greiman/SdFat)                                                           | MIT                             | `fm1buddy-pico.uf2`                         |
| [Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel)                                  | LGPL-3.0                        | `fm1buddy-pico.uf2`                         |
