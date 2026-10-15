#!/usr/bin/env python3
"""Screenshot of the CYD over its USB serial: tools/shot.py /dev/cu.usbserial-110 shot.png [tap X Y]...

Needs pyserial and pillow. Each "tap X Y" is sent first (with a pause after it), then the screen is read."""
import sys
import time

import serial
from PIL import Image

port, out = sys.argv[1], sys.argv[2]
s = serial.Serial(None, 460800, timeout=5)
s.port = port
s.dtr = False
s.rts = False
s.open()
time.sleep(2)  # macOS resets the board on open anyway: let it boot
s.reset_input_buffer()
args = sys.argv[3:]
while args:
    s.write(f'tap {args[1]} {args[2]}\n'.encode())
    args = args[3:]
    time.sleep(0.6)
    print(s.read(s.in_waiting).decode(errors='replace').strip())
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
print(out)
