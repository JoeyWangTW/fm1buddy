#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare a private, stock-V15-only restore bundle. Does not access any device.

Package decoder adapted from charlesvestal/fm1-x0x tools/fm1_rescue.py,
commit 70440e57c2b1390c93569baa55b8e6b7eacbabc2 (GPL-3.0-only).
See repository LICENSE. Neither firmware nor device dumps are distributed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

PACKAGE_SHA = 'db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a'
IMAGE_SHA = 'c1068a403c04a6378a626afb66cc217f72d13f10aa200e983b7a9817cc1dac03'
APP_END, FLASH_SIZE, PROTECTED, SECTOR = 0x93000, 0x100000, 0x4000, 0x1000

def enc(buf):
    out, key = bytearray(buf), 0xffff
    for i in range(len(out)):
        out[i] ^= key & 255
        key = ((key << 1) ^ (0x1021 if key & 0x8000 else 0)) & 0xffff
    return out

def flash_image(raw):
    if hashlib.sha256(raw).hexdigest() != PACKAGE_SHA:
        raise ValueError('Not the exact approved stock V15 package')
    logical = b''.join(raw[i*48:i*48+47] for i in range(20)) + raw[960:]
    header = enc(logical[:64])
    count = struct.unpack_from('<H', header, 8)[0]
    if header[16:22] != b'AC791N' or not 0 < count < 16:
        raise ValueError('Invalid package header')
    images = []
    for i in range(count):
        entry = enc(logical[64+i*80:64+(i+1)*80])
        typ, _, _, _, offset, size = struct.unpack_from('<HHHHII', entry)
        if typ == 0:
            images.append(logical[offset:offset+size])
    if len(images) != 1 or len(images[0]) != APP_END or hashlib.sha256(images[0]).hexdigest() != IMAGE_SHA:
        raise ValueError('Flash image does not match pinned stock V15')
    return images[0]

def prepare(package, reference):
    image = flash_image(package)
    if len(reference) != FLASH_SIZE:
        raise ValueError('Reference must be a complete 1 MiB backup')
    if image[:PROTECTED] != reference[:PROTECTED]:
        raise ValueError('Protected boot area differs: refusing to prepare restoration')
    ref_hash = hashlib.sha256(reference).digest()
    bundle = (b'FM1V15R1' + bytes.fromhex(PACKAGE_SHA) + bytes.fromhex(IMAGE_SHA) + ref_hash
              + struct.pack('<II', APP_END, FLASH_SIZE) + image + reference)
    changed = [a for a in range(PROTECTED, APP_END, SECTOR)
               if image[a:a+SECTOR] != reference[a:a+SECTOR]]
    return bundle, {'mode': 'offline preparation; no device accessed', 'package_sha256': PACKAGE_SHA,
        'image_sha256': IMAGE_SHA, 'reference_sha256': ref_hash.hex(),
        'bundle_sha256': hashlib.sha256(bundle).hexdigest(), 'different_sectors': len(changed),
        'write_range': '[0x4000,0x93000)', 'protected_head_matches': True,
        'device_data': 'preserve the fresh live data at [0x93000,0x100000)'}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('package', type=Path)
    p.add_argument('backup', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    bundle, report = prepare(a.package.read_bytes(), a.backup.read_bytes())
    if a.output.exists() and a.output.read_bytes() != bundle:
        raise SystemExit('Output exists with different contents; choose a new path')
    a.output.write_bytes(bundle)
    a.output.with_suffix(a.output.suffix+'.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
