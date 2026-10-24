# fm1buddy

All without a computer/phone/etc:

- Switch the M-VAVE FM-1's firmware, on the fly (from the SD card, or the [FM-1 Firmware Hub](https://thefirmwarehub.com) and download over Wi-Fi, from [the firmware hub](https://thefirmwarehub.com/))
- Play it from cheap USB-only (up to 4, on a hub) and Bluetooth MIDI devices, even if FM-1 firmware only supports USB MIDI
- touchscreen
- nice menus (same themes as [fomajam](https://github.com/konsumer/fomajam))
- Backup flash/settings/presets/etc (from stock V15, or firmwares like fomajam)
- Send/record sounds (.syx, like DX7 banks)
- Sample-management for [fomajam](https://github.com/konsumer/fomajam) (folders of WAV to its sample-banks, and back)
- Extra features that work with [fomajam](https://github.com/konsumer/fomajam), like load/save its song & sample-banks quickly (for swapping sets at a live show), and export them as MIDI/WAV
- MIDI-routing: Bluetooth & USB devices pass notes, clock & controls to each other, or map them with rules in `midimap.txt` on the SD card (say, several controllers each onto their own FM-1 channel). Bluetooth MIDI and Wi-Fi take turns: they don't both fit
- Act as USB disk (to manage files on SD)
- FM1 Recover: write a firmware or backup to an FM-1 that won't start, like [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) with no wiring needed (when people say "use RP2040"). Not tested on a real FM-1 yet

No soldering or assembly needed, just get a cheap [Freenove ESP32-S3 2.8"](https://store.freenove.com/products/fnk0104?variant=45040270213318) (FNK0104B.) It comes in other sizes too, but the firmware is made for this exact model (its screen, touch and SD pins). You might additionally need a USB power-splitter and/or a small USB hub, depending on what you are doing.

**Install it, and how to set it up:** <https://konsumer.github.io/fm1buddy/>

<table>
  <tr>
    <td><img src="docs/screens/firmware-hub.png" width="260" alt="The Firmware Hub's list" /></td>
    <td><img src="docs/screens/firmware-details.png" width="260" alt="A firmware's details" /></td>
    <td><img src="docs/screens/firmware-install.png" width="260" alt="Installing it on the FM-1" /></td>
  </tr>
  <tr>
    <td><img src="docs/screens/midi.png" width="260" alt="The MIDI devices on the hub" /></td>
    <td><img src="docs/screens/midi-bluetooth.png" width="260" alt="Bluetooth MIDI" /></td>
    <td><img src="docs/screens/themes.png" width="260" alt="The themes" /></td>
  </tr>
  <tr>
    <td><img src="docs/screens/sounds.png" width="260" alt="Sounds: what the FM-1 speaks, recording, .syx files" /></td>
    <td><img src="docs/screens/sounds-bank.png" width="260" alt="A DX7 bank on the SD card" /></td>
    <td><img src="docs/screens/settings.png" width="260" alt="Settings" /></td>
  </tr>
</table>

## Development

With [PlatformIO](https://platformio.org), the board on the computer (it's a USB serial port there; on a hub, the USB
host):

```sh
pio run -e cyd-s3 -t upload
```

The app also runs on the desktop (SDL2: `brew install sdl2`), with pretend Wi-Fi, an FM-1 and a keyboard. It runs
headless from a script too, for screenshots:

```sh
pio run -e sim && .pio/build/sim/program --fm1 --keys
.pio/build/sim/program --headless --script "tap 24 142; wait 300; shot settings.bmp; quit"
```

`tools/install-test.sh` installs a package on the pretend FM-1, cleanly and with faults (lost and repeated requests,
garbled answers, unplugged while writing), and checks it ends up exactly as the package.

Each install on the board writes what went between it and the FM-1 to `fm1buddy/installs/N.txt` on the SD card (the
last 10 are kept): send one along with a report of an install gone wrong.

`tools/s3.py` (with `~/.platformio/penv/bin/python`) does the same on the board: `shot FILE`, `tap X Y`, `log` (what it
logged on the hub, also in `fm1buddy/log.txt` on its SD card), and `host` (the USB host for 20 s).

```
src/app/   the app (LVGL 9): pages, the Hub's list, MIDI between devices, the FM-1's update protocol, sounds (SysEx),
           setups and fomajam's kits and MIDI files (sounds, kits, songmidi), backups (uboot);
           hal.h is what it needs from the board
src/s3/    the board: screen, touch, SD, Wi-Fi, USB host (TinyUSB), Bluetooth MIDI (NimBLE)
src/sim/   the simulator
site/      the installer page (GitHub Pages, with the latest release)
case/      the 3D-printed case (OpenSCAD); tools/cases.sh makes its STLs
```

## License

GPL-3.0-only. Credits and third-party parts: [LICENSING.md](LICENSING.md).
