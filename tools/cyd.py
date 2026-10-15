#!/usr/bin/env python3
"""Drive the CYD over its USB serial console, for testing without hands (needs pyserial and pillow):

  tools/cyd.py /dev/cu.usbserial-110 STEP...

  shot:FILE.png   save the screen
  tap:X,Y         tap the screen
  send:TEXT       a console line: "mount 0 nanoKONTROL2" and "umount 0" fake a device on the host box,
                  "midi 0 b0 0a 40" fakes MIDI from it, "ls /" lists the SD card, "cat /midimap.txt" shows a file
  wait:MS         wait

Opening the port resets the board (macOS), so all the steps run in one go, after it has started."""
import sys
import time

import serial
from PIL import Image


def shot(s, out):
    s.reset_input_buffer()
    s.write(b'shot\n')
    while True:
        line = s.readline()
        if not line:
            sys.exit('no answer')
        if line.startswith(b'SHOT'):
            break
    w, h = map(int, line.split()[1:3])
    data = s.read(w * h * 2)
    if len(data) != w * h * 2:
        sys.exit(f'short read: {len(data)}')
    img = Image.new('RGB', (w, h))
    px = img.load()
    for i in range(w * h):
        v = data[2 * i] << 8 | data[2 * i + 1]  # TFT_eSPI's readRect: byte-swapped RGB565
        px[i % w, i // w] = ((v >> 11) << 3, (v >> 5 & 63) << 2, (v & 31) << 3)
    img.save(out)
    print('shot', out)


def main(port, steps):
    s = serial.Serial(None, 460800, timeout=5)
    s.port = port
    s.dtr = False
    s.rts = False
    s.open()
    time.sleep(2.5)  # it restarted: let it boot
    s.reset_input_buffer()
    for step in steps:
        kind, _, arg = step.partition(':')
        if kind == 'shot':
            shot(s, arg)
            continue
        if kind == 'wait':
            time.sleep(int(arg) / 1000)
        elif kind == 'tap':
            s.write(f'tap {arg.replace(",", " ")}\n'.encode())
        elif kind == 'send':
            s.write(arg.encode() + b'\n')
        else:
            sys.exit(f'what is {step}?')
        time.sleep(0.4)
        out = s.read(s.in_waiting).decode(errors='replace').strip()
        if out:
            print(out)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2:])
