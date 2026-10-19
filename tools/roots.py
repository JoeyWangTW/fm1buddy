#!/usr/bin/env python3
"""Makes src/cyd/roots.h: the root certificates the CYD trusts for thefirmwarehub.com, from Mozilla's list (curl's
cacert.pem). The site is behind Cloudflare, which signs with Let's Encrypt, Google Trust Services or SSL.com, so it
has all of their current roots. Run it now and then: tools/roots.py [cacert.pem]"""
import os
import sys
import urllib.request

NAMES = [
    'ISRG Root X1',
    'ISRG Root X2',
    'GTS Root R1',
    'GTS Root R3',
    'GTS Root R4',
    'SSL.com TLS RSA Root CA 2022',
    'SSL.com TLS ECC Root CA 2022',
]


def main():
    if len(sys.argv) > 1:
        text = open(sys.argv[1]).read()
    else:
        text = urllib.request.urlopen('https://curl.se/ca/cacert.pem').read().decode()
    lines = text.splitlines()
    out = []
    for name in NAMES:
        i = lines.index(name)
        j = lines.index('-----END CERTIFICATE-----', i)
        pem = lines[lines.index('-----BEGIN CERTIFICATE-----', i) : j + 1]
        out.append('// ' + name + '\n' + '\n'.join('"' + line + '\\n"' for line in pem))
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    with open('src/cyd/roots.h', 'w') as f:
        f.write('// made by tools/roots.py: the roots thefirmwarehub.com may be signed with (Mozilla\'s list)\n')
        f.write('#pragma once\n\nstatic const char ROOTS[] =\n' + '\n'.join(out) + ';\n')


if __name__ == '__main__':
    main()
