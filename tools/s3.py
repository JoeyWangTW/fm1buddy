#!/usr/bin/env python3
# Drive the CYD-S3 over its USB serial port (the console in src/s3/main.cpp), for development:
#
#   tools/s3.py shot out.bmp     the screen, as a .bmp
#   tools/s3.py tap X Y          touch it at X, Y (320x240, landscape)
#   tools/s3.py drag X1 Y1 X2 Y2 slide a finger from X1, Y1 to X2, Y2 (to scroll)
#   tools/s3.py log              print its log on the SD card (from when it was the USB host)
#   tools/s3.py host [SCRIPT]    restart it as the USB host for 40 s (then it's back here): to test that. SCRIPT runs
#                                meanwhile, as the console can't: "tap X Y" and "wait MS" between ';'
#   tools/s3.py usbmidi          restart it as a USB MIDI device for this computer (Settings > On a computer)
#   tools/s3.py serial           and back: sends it the SysEx that brings the serial port back (needs python-rtmidi)
#
# Needs pyserial (PlatformIO has it: ~/.platformio/penv/bin/python tools/s3.py ...). The port: $PORT, or the first
# /dev/cu.usbmodem* or /dev/ttyACM*.
import glob, os, struct, sys, time
import serial

def port():
    p = os.environ.get('PORT') or (glob.glob('/dev/cu.usbmodem*') + glob.glob('/dev/ttyACM*') + [None])[0]
    if not p:
        sys.exit('no serial port: plug the CYD-S3 in, or set PORT')
    return serial.Serial(p, 115200, timeout=3)

def bmp(path, w, h, px):
    row = w * 3
    with open(path, 'wb') as f:
        f.write(b'BM' + struct.pack('<IHHIIiiHHIIiiII', 54 + row * h, 0, 0, 54, 40, w, h, 1, 24, 0, row * h, 0, 0, 0, 0))
        for y in range(h - 1, -1, -1):
            for x in range(w):
                c = px[(y * w + x) * 2] | px[(y * w + x) * 2 + 1] << 8
                f.write(bytes(((c & 0x1F) << 3, ((c >> 5) & 0x3F) << 2, (c >> 11) << 3)))

def shot(path):
    s = port()
    s.reset_input_buffer()
    s.write(b'shot\n')
    end = time.time() + 5
    while time.time() < end:
        line = s.readline().decode('utf-8', 'replace').strip()
        if line.startswith('SHOT '):
            w, h = map(int, line.split()[1:3])
            px = s.read(w * h * 2)
            if len(px) != w * h * 2:
                sys.exit('the screen came part way')
            bmp(path, w, h, px)
            print(path)
            return
    sys.exit('no answer')

def drag(x1, y1, x2, y2):
    s = port()
    s.write(b'drag %d %d %d %d\n' % (x1, y1, x2, y2))
    time.sleep(0.6)

def tap(x, y):
    s = port()
    s.write(b'tap %d %d\n' % (x, y))
    s.flush()
    time.sleep(0.3)  # it goes out before the port closes

def command(line, secs):
    s = port()
    s.write(line.encode() + b'\n')
    s.flush()
    end = time.time() + secs
    while time.time() < end:
        try:
            d = s.read(4096)
        except serial.SerialException:
            break
        if d:
            sys.stdout.write(d.decode('utf-8', 'replace'))

TO_SERIAL = [0xF0, 0x7D, 0x66, 0x6D, 0x31, 0x62, 0x01, 0xF7]  # computer_s3.cpp

def to_serial():
    try:
        import rtmidi
    except ImportError:
        sys.exit('needs python-rtmidi: pip install python-rtmidi')
    out = rtmidi.MidiOut()
    ports = out.get_ports()
    i = next((k for k, p in enumerate(ports) if 'fm1buddy' in p.lower() and 'bluetooth' not in p.lower()), None)
    if i is None:
        sys.exit('no fm1buddy USB MIDI port (is it in USB MIDI mode, on this computer?)')
    out.open_port(i)
    out.send_message(TO_SERIAL)
    time.sleep(0.3)
    out.close_port()
    for _ in range(50):  # it restarts: wait for its serial port
        time.sleep(0.2)
        if glob.glob('/dev/cu.usbmodem*') + glob.glob('/dev/ttyACM*'):
            return
    sys.exit('it didn\'t come back as a serial port')

if __name__ == '__main__':
    a = sys.argv[1:]
    if a[:1] == ['shot'] and len(a) == 2:
        shot(a[1])
    elif a[:1] == ['tap'] and len(a) == 3:
        tap(int(a[1]), int(a[2]))
    elif a[:1] == ['drag'] and len(a) == 5:
        drag(*map(int, a[1:]))
    elif a == ['log']:
        command('log', 2)
    elif a[:1] == ['host'] and len(a) <= 2:
        command(' '.join(['host'] + a[1:2]), 0.5)
    elif a == ['usbmidi']:
        command('usbmidi', 0.5)
    elif a == ['serial']:
        to_serial()
    else:
        sys.exit('tools/s3.py shot FILE | tap X Y | drag X1 Y1 X2 Y2 | log | host [SCRIPT] | usbmidi | serial')
