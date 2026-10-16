# fm1buddy

Hardware helpers for the M-VAVE FM-1:

- **The USB MIDI host box:** plug USB MIDI controllers (and synths) straight into the FM-1, no computer.
- **The firmware switcher:** a touch screen with an SD card full of FM-1 firmwares (`.fwsc`). Pick one, and it's
  installed on the FM-1, no computer. It also edits **MIDI mapping** on the box: "when CC 10 comes in from the
  controller, send CC 5 on channel 2".

The switcher is either a 128x64 OLED and buttons on the box itself (one board, a little soldering), or a "Cheap Yellow
Display" touch screen (ESP32-2432S028R) wired to the box with three wires. The box works on its own too.

**Install it from the browser:** <https://konsumer.github.io/fm1buddy/>: step by step, for each board, with the latest
release.

## Why a host box

The FM-1's USB port only connects to a computer: the FM-1 is a USB _device_, and so is a USB controller, and two
devices can't talk to each other. A small box that is a USB _host_ fixes that. Plug a USB hub into it, and the FM-1
and your controllers into the hub. Everything any of them sends goes to all of the others, so a controller plays and
edits the FM-1, the FM-1's CC OUT reaches the controller's lights, and a USB synth on the hub can play the FM-1's MIDI
tracks. There's nothing to set up: no routing, no commands.

Installing firmware needs the box too: the FM-1 only takes updates over USB (its TRS MIDI port can't do it), and the
CYD can't be a USB host.

Two ways to build one, both from ready-made parts and with no soldering:

|                      | A: Feather (recommended)                             | B: Raspberry Pi                                                                                         |
| -------------------- | ---------------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| Board                | Adafruit Feather RP2040 with USB Type A Host (#5723) | Raspberry Pi Zero 2 W (or any Raspberry Pi with USB-A)                                                  |
| Also needed          | a USB hub; a USB-C charger or power bank             | a USB OTG adapter (micro-USB to USB-A), a powered USB hub, a 5 V 2.5 A micro-USB supply, a microSD card |
| Setup                | drag one file onto it                                | install Raspberry Pi OS, run one command                                                                |
| Ready after power-on | under a second                                       | about 30 seconds                                                                                        |
| Devices              | up to 4 on the hub                                   | as many as the hub has ports                                                                            |
| Firmware switcher    | yes: with an OLED FeatherWing, or a CYD              | no                                                                                                      |
| MIDI mapping         | yes                                                  | no                                                                                                      |

Either way you also need the cables you already have: one for the FM-1 (USB-A to USB-C) and one for each controller.

### A: [Adafruit Feather RP2040 with USB Type A Host](https://www.adafruit.com/product/5723)

The board has a USB-A socket for devices and powers it (up to 500 mA). Its USB-C socket is only for power and for
loading the firmware.

1. Get `fm1buddy-feather.uf2` from the [latest release](https://github.com/konsumer/fm1buddy/releases/latest), or
   build it (`pio run -e feather`, it's `.pio/build/feather/firmware.uf2`).
2. Hold the board's **BOOT** button while you plug its USB-C into a computer. A drive called `RPI-RP2` appears.
3. Drag `fm1buddy-feather.uf2` onto it. The drive disappears; the box is ready. (You only do this once.)
4. Plug the hub into the USB-A socket, and the FM-1 and your controllers into the hub. Power the board's USB-C from a
   charger or power bank (or a computer).

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
- **MIDI map** is the [mapping](#midi-mapping): pick a rule (or "add a rule"), move to a field and press **OK** to change
  it with up/down, **OK** again when it's right. "learn" fills in what comes in from the next thing you play.
- **MIDI thru** turns the box's everything-to-everything on or off.

The rules are in `midimap.txt` on the drive, so you can also write them on the computer.

Another 128x64 I2C OLED (SSD1306 or SH1106, on the Feather's STEMMA QT / SDA SCL) works too, with your own buttons
(each to GND): set `OLED_SSD1306` or `OLED_SH1106`, and the button pins, in `platformio.ini` (`BTN_BACK` adds a
separate back button). An SD card (an [Adalogger FeatherWing](https://www.adafruit.com/product/2922), CS on 10) adds
its `fm1` folder to the list.

## The firmware switcher on a touch screen: the CYD

You need the Feather box (A) and an ESP32-2432S028R "Cheap Yellow Display" with a microSD card.

### Wiring

The CYD's **CN1** connector (the 4-pin one marked `IO22 IO27`, usually comes with a cable) to the Feather:

| CYD CN1 | Feather    |
| ------- | ---------- |
| GND     | GND        |
| IO22    | TX         |
| IO27    | RX         |
| 3.3V    | (not used) |

Power the CYD from its own USB port (a 2-port charger powers both), or from the Feather: its `USB` pin (5 V from its
USB-C) to `VIN` on the CYD's **P1** connector.

### The SD card

Put FM-1 firmware packages (`.fwsc`, from the [FM-1 Firmware Hub](https://fm1.designburgapps.com) or a
[foma](https://github.com/konsumer/foma) build) in a folder called `fm1` on a FAT32 card:

```
fm1/
  felucca-1.0.3.fwsc
  fomajam-0.1.fwsc
  stock-v15.fwsc
```

### Using it

- **FILES** lists `fm1/`, with each package's identity (`FM-1_961`). The top right shows the FM-1 on the hub.
- Tap a package, then **INSTALL**. It checks that the package is for the FM-1 that is plugged in, then installs it.
  Keep the FM-1 plugged in until it says _Installed_.
- If an install was cut off, the FM-1 waits in update mode: tap a package and **FINISH** writes it.
- **DEVICES** shows what's on the hub, and turns **MIDI THRU** (the box's everything-to-everything) on or off. While
  installing, nothing else reaches the FM-1. **MAP** goes to the MIDI mapping.

Installing firmware is at your own risk. If the FM-1 won't start, hold **OCT-** while switching it on (the USB rescue)
and install again.

### MIDI mapping

Everything passes through as it is, until a rule says otherwise. On the CYD: on **DEVICES**, tap **MAP**, then **ADD**:

- The left column is what comes in: **FROM** a device (or any), its **CHANNEL**, **TYPE** (note, cc, pc, at for channel
  pressure, pb for pitch bend) and **NUMBER** (the note, the CC, the program).
- The right column is what it becomes: **TO** a device (or wherever MIDI THRU sends it), and the channel, type and
  number. What you leave at "same" stays as it came in. **DROP** throws the message away instead.
- **LEARN**, then move a knob (or play a key): the left column fills in with what it sent.
- Hold **-** or **+** to go faster. **DONE** saves it; tap a rule in the list to change or delete it.

The first rule that matches a message decides; a message no rule matches passes as it is. The value (velocity, CC
value, pressure) carries over: a CC turned into a note plays it at the CC's value (0 is note off), and a CC turned into
a program change picks the program from the value, unless the rule names one.

The rules are saved on the SD card in `midimap.txt`, one per line, so you can also write them on a computer:

```
from nanokontrol2 ch 1 cc 10 -> ch 2 cc 5
from "launchkey mini" note 36 -> to fm-1 note 38
cc 1 -> pc
pb -> drop
```

A name matches any device whose name contains it (ignoring case). The CYD sends the rules to the box whenever it
starts and whenever a device comes or goes; the box keeps them while it's on.

### Building and flashing the CYD

With [PlatformIO](https://platformio.org):

```sh
pio run -e cyd -t upload
```

Or flash `fm1buddy-cyd.bin` from a release (bootloader and all, at 0):
`esptool.py --chip esp32 write_flash 0x0 fm1buddy-cyd.bin`.

If the colors look inverted, your CYD is the two-USB-port revision: uncomment `TFT_INVERSION_ON` in `platformio.ini`.

## How it works

```
CYD (screen, touch, SD:/fm1)  <-- UART 1 Mbaud -->  Feather RP2040 (USB host)  -->  hub  -->  FM-1, controllers
                                                    (OLED, buttons, USB drive)
```

- `src/feather/` is the box: TinyUSB on Pico-PIO-USB, on the RP2040's second core (`host.cpp`). It routes every
  device's MIDI to the others, and tells whoever is listening (its own screen, through a queue between the cores, and
  a CYD on its UART) what's plugged in and what each device sends; it takes MIDI to send, routing and mapping rules.
  The link is in `src/common/fm1link.h`, the mapping in `src/common/fm1map.h`. The first core has the USB drive and the
  OLED screen (`ui.cpp`).
- `src/cyd/` is the touch screen.
- `src/common/` is what both screens use. `fm1ota.cpp` is the FM-1's update protocol (M-UPGRADE compatible), ported from foma's
  `web/fm1ota.js`: the FM-1 asks for parts of the package with SysEx read requests, and the CYD answers each one
  straight from the SD card. First the running firmware checks the package and starts the update loader; then the
  loader (a new USB device, `ota-...`) reads the whole image and writes it.

```
platformio.ini       both boards: pio run -e feather, pio run -e cyd
src/common/          the link, the mapping, the FM-1 update, the rules file
src/feather/         the box (host.cpp), its USB drive and screen
src/cyd/             the touch screen
lib/Pico-PIO-USB/    Pico-PIO-USB 0.7.2 (MIT), here so PlatformIO doesn't pull a second TinyUSB in with it
tools/pi-setup.sh    the Raspberry Pi box
tools/cyd.py         drives the CYD over its USB serial: screenshots, taps, fake devices (for testing)
```

## The installer page

`site/` is the installer page. `.github/workflows/pages.yml` publishes it on GitHub Pages (Settings → Pages → Source:
GitHub Actions), with the latest release's firmware copied next to it, after every build. To try it here, put builds
in `site/fw/` and run `npm run site`.

## License

GPL-3.0-only. Credits and third-party parts: [LICENSING.md](LICENSING.md).
