import hashlib
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import prepare_v15 as p

class Preparation(unittest.TestCase):
    def test_unknown_package_refused(self):
        for raw in (b'',b'V15',bytes(699956)):
            with self.assertRaisesRegex(ValueError,'exact approved'):
                p.flash_image(raw)

    def test_reference_and_bundle(self):
        image=bytes(p.APP_END)
        ref=bytearray(p.FLASH_SIZE)
        ref[0x5000]=1
        ref[0x95000]=2
        with patch.object(p,'flash_image',return_value=image):
            bundle,report=p.prepare(b'unused',bytes(ref))
            self.assertEqual(len(bundle),1650800)
            self.assertEqual(bundle[:8],b'FM1V15R1')
            self.assertEqual(struct.unpack_from('<II',bundle,104),(p.APP_END,p.FLASH_SIZE))
            self.assertEqual(bundle[112:112+p.APP_END],image)
            self.assertEqual(bundle[112+p.APP_END:],ref)
            self.assertEqual(bundle[72:104],hashlib.sha256(ref).digest())
            self.assertEqual(report['different_sectors'],1)
            self.assertEqual(report['bundle_sha256'],hashlib.sha256(bundle).hexdigest())
            with self.assertRaisesRegex(ValueError,'complete 1 MiB'):
                p.prepare(b'unused',bytes(ref[:-1]))
            ref[0x3000]=1
            with self.assertRaisesRegex(ValueError,'Protected boot area'):
                p.prepare(b'unused',bytes(ref))

if __name__=='__main__': unittest.main()
