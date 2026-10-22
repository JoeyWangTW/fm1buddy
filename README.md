# fm1buddy

A companion box for the M-VAVE FM-1: switch its firmware, and plug cheap USB MIDI controllers into it, with no
computer.

**The main one is the [CYD-S3](#the-cyd-s3)**, a ready-made Freenove ESP32-S3 2.8" touch screen with an SD card slot,
Wi-Fi, Bluetooth and a USB-C port that can be a USB host. There are also [RP2040 boxes](#the-rp2040-boxes) for MIDI
alone (one with a small OLED screen).

## The CYD-S3

A [Freenove ESP32-S3 Display, 2.8", with touch](https://github.com/Freenove/Freenove_ESP32_S3_Display) (FNK0104B; the
same board as LCDwiki's ES3C28P): an ILI9341 320x240 screen, capacitive touch, a microSD slot, and an ESP32-S3 with
8 MB of PSRAM.

It has two modes, on the left, and the settings:

- **Firmware** (the download icon) has two lists. **SD card** lists the `.fwsc` packages in the card's `fm1` folder.
  **Hub** lists the [FM-1 Firmware Hub](https://thefirmwarehub.com)'s firmwares: filter them by kind (synth, groove,
  drums, ...), and sort them by stars, date or name. Tap one to see what it is (its description, author, version,
  stars, installs, size, and the device it's for). **Save to SD** downloads it into `fm1`; **Install** (tap it twice)
  installs it on the FM-1 on the hub, downloading it first if it isn't on the card. Each download is checked (its size,
  sha256, and the device in the package's own header) before it's kept. Keep the FM-1 plugged in until it says
  _Installed_; if an install is cut off, the FM-1 waits in update mode, and **Finish install** writes it. The Hub's
  list is kept on the card (`fm1buddy/hub.json`), so it works offline; it's fetched again the first time you open
  **Hub** after power-on (with its ETag, so only when it changed), or when you tap the refresh button.
- **MIDI** (the note icon): what's on the hub, each with a light when it plays and the last thing it sent. Whatever any
  of them plays goes to all the others, so a controller plays the FM-1 (except while installing). **Bluetooth MIDI** is
  off until you turn it on here (then it stays on after power-off): phones, tablets and computers connect to
  `fm1buddy`, and Bluetooth MIDI keyboards and synths around are listed: tap one to connect it (it's remembered, and
  connected again whenever it's around; tap it again to forget it). Up to 3 Bluetooth connections.
- **Settings** (the gear): Wi-Fi (scan, pick a network, type its password), the theme (the same color themes as
  [FOMAJAM](https://github.com/konsumer/fomajam): one for each FM-1 color, plus WHITE and NEON), the screen's brightness,
  whether to update the Hub's list on its own, and what's on the SD card.

Wi-Fi is only on while it's needed: while the Hub's list or a firmware downloads (and while you set it up). Bluetooth
stops while Wi-Fi is on (both don't fit in the internal RAM) and starts again after. The icons on the left show what's
on; **FM-1** lights up when an FM-1 on the hub has said who it is (yellow: it's in update mode).

### Plugging it in

The USB-C port is the USB host: through a **USB-C OTG adapter with a charging port** (look for one sold for the
Chromecast with Google TV: it passes charging and data at the same time), to a hub, with a charger or power bank on
the adapter's charging port. The board can't power its USB-C port itself, so the hub needs that charger (or a powered
hub).

```
controller ─┐
            ├─ USB hub ── OTG adapter ── CYD-S3
FM-1 ───────┘                 │
                  charger ────┘ (its charging port)
```

When it starts plugged into a **computer** instead, it stays a USB serial port, for flashing and its log (no BOOT
button needed). On the hub, its log goes to `fm1buddy/log.txt` on the SD card.

The S3's USB has 8 channels: the hub takes 2, and each device 3, so **two devices on the hub**: the FM-1 and one
controller. The RGB LED on the back flashes with USB MIDI.

### Flashing

With [PlatformIO](https://platformio.org), the board on the computer:

```sh
pio run -e cyd-s3 -t upload
```

Put a FAT32 microSD card in. Firmwares go in its `fm1` folder (the Hub's downloads land there too, or copy `.fwsc` files
there on a computer).

### Development

The app (`src/app`) runs on the desktop too, in an SDL window (macOS, with SDL2: `brew install sdl2`), so the
interface can be worked on without flashing:

```sh
pio run -e sim && .pio/build/sim/program
```

The mouse is the finger. Its SD card is the `sim/sd` folder (or `$FM1_SD`), and its settings are in `sim/`. Wi-Fi is
pretend (any password works, but "wrong"); the internet is real. `--fm1` puts a pretend FM-1 on the hub, which goes
through a whole install (its update loader and all), and `--keys` a keyboard that plays now and then; Bluetooth is
pretend too. For screenshots and tests it also runs headless, from
a script:

```sh
.pio/build/sim/program --headless --script "tap 24 116; wait 300; shot settings.bmp; quit"
```

On the board, `tools/s3.py` does the same over its USB serial port (with PlatformIO's Python, which has pyserial):

```sh
~/.platformio/penv/bin/python tools/s3.py shot screen.bmp
~/.platformio/penv/bin/python tools/s3.py tap 24 116
~/.platformio/penv/bin/python tools/s3.py host   # the USB host for 20 s, then back
~/.platformio/penv/bin/python tools/s3.py log    # what it logged meanwhile
```

`pio run -e sim -t compiledb` makes `compile_commands.json` for editors (clangd).

## The RP2040 boxes

USB MIDI host boxes without the touch screen: plug controllers into the FM-1 with no computer. Three ways, all from
ready-made parts and with no soldering:

|                      | A: Feather (recommended)                             | B: Raspberry Pi                                                                                         | C: ultra-light                                                               |
| -------------------- | ---------------------------------------------------- | ------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------- |
| Board                | Adafruit Feather RP2040 with USB Type A Host (#5723) | Raspberry Pi Zero 2 W (or any Raspberry Pi with USB-A)                                                  | Waveshare RP2040-Zero                                                        |
| Also needed          | a USB hub; a USB-C charger or power bank             | a USB OTG adapter (micro-USB to USB-A), a powered USB hub, a 5 V 2.5 A micro-USB supply, a microSD card | a USB-C OTG adapter with a charging port, a USB hub, a charger or power bank |
| Ports on the box     | 2 (USB-C power, USB-A hub)                           | 2                                                                                                       | 1 (USB-C, the adapter splits it)                                             |
| Battery inside       | optional (a LiPo)                                    | no                                                                                                      | no: use a power bank                                                         |
| Setup                | drag one file onto it                                | install Raspberry Pi OS, run one command                                                                | drag one file onto it                                                        |
| Ready after power-on | under a second                                       | about 30 seconds                                                                                        | under a second                                                               |
| Devices              | up to 4 on the hub                                   | as many as the hub has ports                                                                            | up to 4 on the hub, and a USB stick                                          |
| Firmware switcher    | yes: with an OLED FeatherWing                        | no                                                                                                      | no                                                                           |
| MIDI mapping         | yes                                                  | no                                                                                                      | yes: `midimap.txt` on a USB stick                                            |

Either way you also need the cables you already have: one for the FM-1 (USB-A to USB-C) and one for each controller.

### A: [Adafruit Feather RP2040 with USB Type A Host](https://www.adafruit.com/product/5723)

The board has a USB-A socket for devices and powers it (up to 500 mA). Its USB-C socket is only for power and for
loading the firmware.

1. Get `fm1buddy-feather.uf2` from the [latest release](https://github.com/konsumer/fm1buddy/releases/latest), or
   build it (`pio run -e feather`, it's `.pio/build/feather/firmware.uf2`).
2. Hold the board's **BOOT** button while you plug its USB-C into a computer. A drive called `RPI-RP2` appears.
3. Drag `fm1buddy-feather.uf2` onto it. The drive disappears; the box is ready. (You only do this once.)
4. Plug a USB hub into the USB-A socket (`HUB` on the case), and the FM-1 and your controllers into the hub. Power the
   board's USB-C (`POWER`) from a charger, a power bank or a computer. The FM-1 can't go into the USB-C: it's a USB
   device, like the Feather's USB-C, so it needs the hub too.

   ```
   keyboard ─┐
             ├─ USB hub ── HUB (USB-A)   Feather   POWER (USB-C) ── charger
   FM-1 ─────┘
   ```

The red LED blinks when MIDI passes through. If a device keeps dropping out, it needs more power than the board gives:
use a hub with its own power supply.

### B: Raspberry Pi

1. Use Raspberry Pi Imager to put **Raspberry Pi OS Lite** on the microSD card. In its settings, set a user name and
   password, your Wi-Fi, and turn on SSH.
2. Start the Pi, connect to it with `ssh`, and run:

   ```sh
   curl -fsSL https://raw.githubusercontent.com/konsumer/fm1buddy/main/tools/pi-setup.sh | sudo sh
   ```

3. Plug the OTG adapter into the Zero's **USB** port (not PWR), the powered hub into the adapter, and the FM-1 and
   your controllers into the hub. Power the Pi from its PWR port.

From then on it starts by itself whenever the Pi is powered. It checks every 2 seconds for devices that came or went
(`tools/pi-setup.sh` is the whole thing). Wi-Fi is only needed for setup.

### C: ultra-light: [Waveshare RP2040-Zero](https://www.waveshare.com/wiki/RP2040-Zero)

The smallest and cheapest box: a thumbnail-sized board whose only USB-C port is the USB host. A **USB-C OTG adapter
with a charging port** (a Y-adapter: USB-C plug, a USB-A socket, a USB-C charging socket) splits that port into the
hub and the power, which feeds the board, the hub and everything on it.

The adapter must pass charging and data at the same time, to a device that is the USB host while it is powered: look
for one sold for the **Chromecast with Google TV** (the same situation). Many 3-port "splitters" can't (they say
"does not support charging and data at the same time"). Hubs with a power input made for the Chromecast or Fire TV
(USB-C plug, USB-A ports, a charging port) are the adapter and the hub in one.

1. Get `fm1buddy-pico.uf2` from the [latest release](https://github.com/konsumer/fm1buddy/releases/latest), or build
   it (`pio run -e pico`).
2. Hold the board's **BOOT** button while you plug it into a computer (straight in, not through the adapter), and drag
   the file onto the `RPI-RP2` drive.
3. Plug in the adapter, a hub into its USB-A socket, a charger or power bank into its charging socket, and the FM-1
   and your controllers into the hub. The LED flashes white once when it starts, and when MIDI passes through.

   ```
   keyboard ─┐
             ├─ USB hub ── OTG adapter ── RP2040-Zero
   FM-1 ─────┘                 │
                   charger ────┘ (its charging port)
   ```

What it can't do, next to the Feather:

- **No USB drive** (its USB port is the host), so no firmwares on the box, and no screen of its own. To install FM-1
  firmware, plug the FM-1 straight into a computer and use the [FM-1 Firmware Hub](https://thefirmwarehub.com).
- **MIDI mapping:** put `midimap.txt` (the same rules as below) on a USB stick (FAT32), and plug the stick into the hub:
  the box reads it when the stick goes in, and keeps the rules until it's switched off.
- **No battery inside:** a power bank is the battery. Some switch off when little is drawn; a hub's LED, or the FM-1
  charging, usually keeps them on.
- MIDI thru (everything to everything) is always on.
- To update the box itself, open the lid for BOOT.

**Avoid loops:** if a controller sends back everything it receives ("MIDI thru" or "echo" in its settings), turn
that off, or messages go round in circles.

## The firmware switcher on the box: OLED and buttons

Add an [Adafruit 128x64 OLED FeatherWing](https://www.adafruit.com/product/4650) to the Feather box (A): solder
headers on, and stack it. Its three buttons are **up**, **OK** and **down**; hold **OK** to go back.

When a computer is on the Feather's USB-C, the Feather shows up there as a drive called `fm1buddy` (most of its 8 MB
flash): copy firmware packages (`.fwsc`) into its `fm1` folder. Then plug the Feather into a charger instead (or leave
it on the computer) and pick **Install firmware**.

- **Install firmware** lists `fm1/`. Pick one, and **OK** installs it on the FM-1 on the hub (it checks that it's for the
  FM-1 first). If an install was cut off, the FM-1 waits in update mode, and **OK** finishes it.
- **Devices** shows what's on the hub.
- **MIDI map** is the [mapping](#midi-mapping-rp2040-boxes): pick a rule (or "add a rule"), move to a field and press **OK** to change
  it with up/down, **OK** again when it's right. "learn" fills in what comes in from the next thing you play.
- **MIDI thru** turns the box's everything-to-everything on or off.

The rules are in `midimap.txt` on the drive, so you can also write them on the computer.

Another 128x64 I2C OLED (SSD1306 or SH1106, on the Feather's STEMMA QT / SDA SCL) works too, with your own buttons
(each to GND): set `OLED_SSD1306` or `OLED_SH1106`, and the button pins, in `platformio.ini` (`BTN_BACK` adds a
separate back button). An SD card (an [Adalogger FeatherWing](https://www.adafruit.com/product/2922), CS on 10) adds
its `fm1` folder to the list.

## MIDI mapping (RP2040 boxes)

Everything passes through as it is, until a rule says otherwise. The rules are in a text file, `midimap.txt`, one per
line: on the Feather's `fm1buddy` drive (plug its USB-C into a computer, open the file, write your rules, save, and
eject: the box uses them a second after you save, and every time it starts), or on a USB stick on the RP2040-Zero's
hub. With the OLED FeatherWing, **MIDI map** edits them on the box.

```
from nanokontrol2 ch 1 cc 10 -> ch 2 cc 5
from "launchkey mini" note 36 -> to fm-1 note 38
cc 1 -> pc
pb -> drop
```

A name matches any device whose name contains it (ignoring case). The first rule that matches a message decides; a
message no rule matches passes as it is. The value (velocity, CC value, pressure) carries over: a CC turned into a note
plays it at the CC's value (0 is note off), and a CC turned into a program change picks the program from the value,
unless the rule names one.

Installing firmware is at your own risk. If the FM-1 won't start, hold **OCT-** while switching it on (the USB rescue)
and install again.

## Cases

`case/fm1buddy-case.scad` (OpenSCAD) is a snap-fit case for each setup: `s3` (the CYD-S3: the board on posts, its
USB-C out one end, the SD card at the side, paperclip holes in the bottom for BOOT and RESET), `bare` (the box), `oled` (with the OLED
FeatherWing; tabs in the lid press its buttons), `sd` and `sd_oled` (with an Adalogger FeatherWing), `cyd` (an
original CYD in front, the Feather behind it), and `lite` (the RP2040-Zero: one USB-C hole, and a lid thin enough for
its LED to shine through). Each Feather case, and the CYD-S3's, also comes with room for a flat LiPo (`battery`, 500 mAh by default)
and a hole for a switch. Print the base open side up and the lid flat side down, without supports.

The STLs are on the [installer page](https://konsumer.github.io/fm1buddy/) and in each release; `tools/cases.sh` makes
them (with OpenSCAD 2025 or later, or its Docker image). The board sizes come from Adafruit's CAD, the CYD's drawing and LCDwiki's ES3C28P outline drawing (the CYD-S3);
how far apart the stacked boards are depends on your headers: measure, and set `wing_gap` if yours differ.

## How it works

```
platformio.ini       the builds: cyd-s3, sim, feather, pico
src/app/             the CYD-S3's app, on LVGL 9: the same code on the board and in the simulator
  ui.cpp             the frame: the theme, the rail on the left, pages, watching the jobs
  page_*.cpp         the pages: firmware, MIDI, settings
  catalog.cpp        the Hub's list (catalog.json) and the SD card's /fm1
  jobs.cpp           the slow things (Wi-Fi, downloads, installing), on a thread of their own
  midi.cpp           the devices, and MIDI between them
  fm1.cpp            the FM-1's update protocol, and asking devices who they are
  hal.h              what the app needs from the board: SD, settings, Wi-Fi, HTTPS, USB, Bluetooth, threads
  themes.h           the color themes (FOMAJAM's)
src/s3/              the CYD-S3: the screen and touch for LVGL, and hal.h (SD_MMC, NVS, Wi-Fi, esp_http_client),
                     the USB host (usb_s3.cpp) and Bluetooth MIDI (ble_s3.cpp, NimBLE)
src/sim/             the simulator: SDL2, hal.h with a folder for the SD card and libcurl, pretend devices
src/common/          the RP2040 boxes' routing and mapping, their link, their FM-1 update protocol
src/feather/         the Feather box (host.cpp), its USB drive and OLED screen
src/pico/            the ultra-light box (RP2040-Zero): its main, the USB stick
lib/Pico-PIO-USB/    Pico-PIO-USB 0.7.2 (MIT), here so PlatformIO doesn't pull a second TinyUSB in with it
tools/s3.py          screenshots and taps on the CYD-S3, over its USB serial
tools/pi-setup.sh    the Raspberry Pi box
case/                the 3D-printed cases (OpenSCAD); tools/cases.sh makes their STLs
```

On the CYD-S3, LVGL's memory, the app's lists, MIDI's buffers and TLS's are in PSRAM: the internal RAM is kept for
Wi-Fi, the USB host and Bluetooth. HTTPS uses ESP-IDF's certificate bundle.

`src/app/fm1.cpp` (and `src/common/fm1ota.cpp`, for the RP2040 boxes) is the FM-1's update protocol (M-UPGRADE compatible), ported from foma's `web/fm1ota.js`: the
FM-1 asks for parts of the package with SysEx read requests, and the box answers each one straight from storage. First
the running firmware checks the package and starts the update loader; then the loader (a new USB device, `ota-...`)
reads the whole image and writes it.

## The installer page

`site/` is the installer page. `.github/workflows/pages.yml` publishes it on GitHub Pages (Settings → Pages → Source:
GitHub Actions), with the latest release's firmware copied next to it, after every build. To try it here, put builds
in `site/fw/` and run `npm run site`.

## License

GPL-3.0-only. Credits and third-party parts: [LICENSING.md](LICENSING.md).
