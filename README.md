# fm1buddy

A companion box for the M-VAVE FM-1: switch its firmware, and play it from cheap USB and Bluetooth MIDI controllers,
with no computer. It's a ready-made Freenove ESP32-S3 2.8" touch screen (the "CYD-S3") with an SD card slot, Wi-Fi,
Bluetooth and a USB-C port that can be a USB host: nothing to put together.

**Install it from the browser:** <https://konsumer.github.io/fm1buddy/>.

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

## Case

`case/fm1buddy-case.scad` (OpenSCAD) is a snap-fit case: the board on posts, its USB-C out one end, the SD card at the
side, paperclip holes in the bottom for BOOT and RESET. With `battery`, it has room for a flat LiPo (500 mAh by default)
under the board, and a hole for a switch. Print the base open side up and the lid flat side down, without supports.

The STLs are on the [installer page](https://konsumer.github.io/fm1buddy/) and in each release; `tools/cases.sh` makes
them (with OpenSCAD 2025 or later, or its Docker image). The board's size is from LCDwiki's ES3C28P outline drawing.

## How it works

```
platformio.ini       the builds: cyd-s3, sim
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
tools/s3.py          screenshots and taps on the CYD-S3, over its USB serial
case/                the 3D-printed case (OpenSCAD); tools/cases.sh makes its STLs
```

On the CYD-S3, LVGL's memory, MIDI's buffers and TLS's are in PSRAM: the internal RAM is kept for Wi-Fi, the USB host
(whose DMA can't reach PSRAM) and Bluetooth. HTTPS uses ESP-IDF's certificate bundle.

`src/app/fm1.cpp` is the FM-1's update protocol (M-UPGRADE compatible), ported from foma's `web/fm1ota.js`: the
FM-1 asks for parts of the package with SysEx read requests, and the box answers each one straight from storage. First
the running firmware checks the package and starts the update loader; then the loader (a new USB device, `ota-...`)
reads the whole image and writes it.

## The installer page

`site/` is the installer page. `.github/workflows/pages.yml` publishes it on GitHub Pages (Settings → Pages → Source:
GitHub Actions), with the latest release's firmware copied next to it, after every build. To try it here, put builds
in `site/fw/` and run `npm run site`.

## License

GPL-3.0-only. Credits and third-party parts: [LICENSING.md](LICENSING.md).
