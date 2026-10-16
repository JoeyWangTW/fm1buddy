#!/usr/bin/env python3
"""Make an OpenSCAD picture's background transparent: tools/png_key.py in.png out.png

The background is the color of the top-left pixel. Pixels near it get some transparency (so edges blend into
any page), and their color is taken back out of the mix. Python's standard library only."""
import struct
import sys
import zlib

SOFT = 48  # how far from the background a color must be to be solid


def read(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', 'not a PNG'
    i, idat, w = 8, b'', 0
    while i < len(data):
        n, kind = struct.unpack('>I4s', data[i : i + 8])
        body = data[i + 8 : i + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', body)
            assert depth == 8 and ctype in (2, 6) and not interlace, 'needs 8-bit RGB or RGBA'
            bpp = 3 if ctype == 2 else 4
        elif kind == b'IDAT':
            idat += body
        i += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1 : (y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def write(path, w, h, rows):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)

    raw = b''.join(b'\x00' + bytes(r) for r in rows)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    open(path, 'wb').write(png)


def main(src, dst):
    w, h, bpp, rows = read(src)
    bg = rows[0][0:3]
    out = []
    for line in rows:
        o = bytearray(w * 4)
        for x in range(w):
            r, g, b = line[x * bpp : x * bpp + 3]
            d = max(abs(r - bg[0]), abs(g - bg[1]), abs(b - bg[2]))
            a = min(1.0, d / SOFT)
            if a > 0:
                # un-mix the background: c = a * color + (1 - a) * bg
                r, g, b = (min(255, max(0, round((v - (1 - a) * k) / a))) for v, k in zip((r, g, b), bg))
            o[x * 4 : x * 4 + 4] = bytes((r, g, b, round(a * 255)))
        out.append(o)
    write(dst, w, h, out)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
