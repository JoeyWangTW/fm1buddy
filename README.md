# fm1buddy

Hardware helpers for the M-VAVE FM-1:

- **The USB MIDI host box:** plug USB MIDI controllers (and synths) straight into the FM-1, no computer.
- **The firmware switcher:** a touch screen with an SD card full of FM-1 firmwares (`.fwsc`). Pick one, and it's
  installed on the FM-1, no computer.

The switcher is a "Cheap Yellow Display" (ESP32-2432S028R) wired to the host box with three wires. The box works on its
own too.

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
| Firmware switcher    | yes                                                  | no                                                                                                      |

Either way you also need the cables you already have: one for the FM-1 (USB-A to USB-C) and one for each controller.

### A: Adafruit Feather RP2040 with USB Type A Host

The board has a USB-A socket for devices and powers it (up to 500 mA). Its USB-C socket is only for power and for
loading the firmware.

1. Get `fm1buddy-usbhost.uf2` from the [latest release](https://github.com/konsumer/fm1buddy/releases/latest), or
   build it with `usbhost/build.sh` (needs Docker).
2. Hold the board's **BOOT** button while you plug its USB-C into a computer. A drive called `RPI-RP2` appears.
3. Drag `fm1buddy-usbhost.uf2` onto it. The drive disappears; the box is ready. (You only do this once.)
4. Plug the hub into the USB-A socket, and the FM-1 and your controllers into the hub. Power the board's USB-C from a
   charger or power bank (or a computer).

The red LED blinks when MIDI passes through. If a device keeps dropping out, it needs more power than the board gives:
use a hub with its own power supply.

### B: Raspberry Pi

1. Use Raspberry Pi Imager to put **Raspberry Pi OS Lite** on the microSD card. In its settings, set a user name and
   password, your Wi-Fi, and turn on SSH.
2. Start the Pi, connect to it with `ssh`, and run:

   ```sh
   curl -fsSL https://raw.githubusercontent.com/konsumer/fm1buddy/main/usbhost/pi-setup.sh | sudo sh
   ```

3. Plug the OTG adapter into the Zero's **USB** port (not PWR), the powered hub into the adapter, and the FM-1 and
   your controllers into the hub. Power the Pi from its PWR port.

From then on it starts by itself whenever the Pi is powered. It checks every 2 seconds for devices that came or went
(`usbhost/pi-setup.sh` is the whole thing). Wi-Fi is only needed for setup.

**Avoid loops:** if a controller sends back everything it receives ("MIDI thru" or "echo" in its settings), turn
that off, or messages go round in circles.

## The firmware switcher

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
[foma](https://github.com/konsumer/foma) build) in a folder called `fx1` on a FAT32 card:

```
fx1/
  felucca-1.0.3.fwsc
  fomajam-0.1.fwsc
  stock-v15.fwsc
```

### Using it

- **FILES** lists `fx1/`, with each package's identity (`FM-1_961`). The top right shows the FM-1 on the hub.
- Tap a package, then **INSTALL**. It checks that the package is for the FM-1 that is plugged in, then installs it.
  Keep the FM-1 plugged in until it says _Installed_.
- If an install was cut off, the FM-1 waits in update mode: tap a package and **FINISH** writes it.
- **DEVICES** shows what's on the hub, and turns **MIDI THRU** (the box's everything-to-everything) on or off. While
  installing, nothing else reaches the FM-1.

Installing firmware is at your own risk. If the FM-1 won't start, hold **OCT-** while switching it on (the USB rescue)
and install again.

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
CYD (screen, touch, SD:/fx1)  <-- UART 1 Mbaud -->  Feather RP2040 (USB host)  -->  hub  -->  FM-1, controllers
```

- `usbhost/` is the box: TinyUSB on Pico-PIO-USB. It routes every device's MIDI to the others, and over its UART it
  tells the CYD what's plugged in, passes on what each device sends, and takes MIDI to send and routing. The link is
  in `common/fm1link.h`.
- `cyd/` is the switcher. `fm1ota.cpp` is the FM-1's update protocol (M-UPGRADE compatible), ported from foma's
  `web/fm1ota.js`: the FM-1 asks for parts of the package with SysEx read requests, and the CYD answers each one
  straight from the SD card. First the running firmware checks the package and starts the update loader; then the
  loader (a new USB device, `ota-...`) reads the whole image and writes it.

```
common/fm1link.h     the UART link between the two
cyd/                 the CYD (PlatformIO, platformio.ini)
usbhost/             the Feather (Pico SDK, usbhost/build.sh), and pi-setup.sh
```

## License

GPL-3.0-only. Credits and third-party parts: [LICENSING.md](LICENSING.md).
