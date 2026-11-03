"""Validate generated FATX files, block compression, and UF2 flash layout."""
import pathlib
import random
import struct
import unittest
import zlib
from pack_card import build_image, pack_image
from make_uf2 import make_uf2

ROOT = pathlib.Path(__file__).resolve().parents[1]


def extract_files(image):
    # Reader derives geometry from the disk header rather than packer constants.
    magic, serial, spc, root = struct.unpack_from('<4sIII', image)
    assert magic == b'FATX' and serial
    cluster_bytes = spc * 4096  # Xbox memory-unit filesystem sector size.
    fat_bytes = (((len(image) // cluster_bytes + 1) * 2 + 4095) // 4096) * 4096
    data_offset = 4096 + fat_bytes
    seen = set()
    files = {}

    def chain(first):
        output = bytearray()
        while first not in (0, 0xffff):
            assert first not in seen, 'Crosslinked or cyclic FAT chain'
            seen.add(first)
            start = data_offset + (first - 1) * cluster_bytes
            assert data_offset <= start < len(image)
            output.extend(image[start:start+cluster_bytes])
            first = struct.unpack_from('<H', image, 4096 + first * 2)[0]
        return bytes(output)

    def directory(first, parent):
        data = chain(first)
        for pos in range(0, len(data), 64):
            length, flags = data[pos:pos+2]
            if length == 0xff:
                break
            if length in (0, 0xe5):
                continue
            assert length <= 42
            name = data[pos+2:pos+2+length].decode('ascii')
            first, size = struct.unpack_from('<II', data, pos+44)
            path = parent + name
            if flags & 0x10:
                directory(first, path + '/')
            else:
                body = chain(first)
                assert len(body) >= size
                files[path] = body[:size]

    directory(root, '')
    return files


class CardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = ROOT / 'assets/ENDGAME'
        cls.image = build_image(cls.source)
        cls.packed, cls.offsets = pack_image(cls.image)

    def test_memory_unit_geometry_regression(self):
        # Fixed offsets confirmed by libfatx with sector_size=4096 and by the
        # Xbox-specific BAD-AL/xbox_memory_unit_tool layout tests.
        self.assertEqual(struct.unpack_from('<I', self.image, 8)[0], 4)
        self.assertEqual(self.image[0x2000:0x2008], b'\x06\x10helper')
        # Cluster 2 is the helper directory, one 16KiB cluster after the root.
        self.assertEqual(self.image[0x6000:0x6003], b'\x01\x100')
        free_entries = sum(struct.unpack_from('<H', self.image, 4096+i*2)[0] == 0
                           for i in range(1, 512))
        self.assertGreater(free_entries, 0)
        date = struct.unpack_from('<H', self.image, 0x2000+54)[0]
        self.assertEqual((2000+(date >> 9), (date >> 5) & 15, date & 31),
                         (2026, 1, 1))
        # Title metadata file follows directory '0' inside helper.
        self.assertEqual(self.image[0x6000+64+1], 0x04)
        self.assertTrue(self.image[16:80].decode('utf-16le').startswith('RRPX RETAIL 0714'))

    def test_fatx_exact_file_roundtrip(self):
        expected = {p.relative_to(self.source).as_posix(): p.read_bytes()
                    for p in self.source.rglob('*') if p.is_file()}
        self.assertEqual(extract_files(self.image), expected)

    def test_every_deflate_block_and_random_read(self):
        parts = [zlib.decompress(self.packed[a:b], -15)
                 for a, b in zip(self.offsets, self.offsets[1:])]
        rebuilt = b''.join(parts)
        self.assertEqual(rebuilt, self.image)
        self.assertTrue(all(len(p) == 16384 for p in parts))
        rng = random.Random(42)
        for _ in range(1000):
            pos = rng.randrange(len(self.image)-2000)
            self.assertEqual(rebuilt[pos:pos+2000], self.image[pos:pos+2000])
        self.assertLess(len(self.packed) + 128*1024, 2*1024*1024)

    def test_uf2_address_family_and_roundtrip(self):
        binary = bytes(range(256)) * 31 + b'last'
        uf2 = make_uf2(binary)
        rebuilt = bytearray()
        count = len(uf2) // 512
        for i in range(count):
            block = uf2[i*512:(i+1)*512]
            a,b,flags,address,size,index,total,family = struct.unpack_from('<8I', block)
            self.assertEqual((a,b,flags,size,index,total,family),
                             (0x0a324655,0x9e5d5157,0x2000,256,i,count,0xe48bff56))
            self.assertEqual(address, 0x10000000 + i*256)
            self.assertEqual(struct.unpack_from('<I', block, 508)[0], 0x0ab16f30)
            rebuilt.extend(block[32:288])
        self.assertEqual(rebuilt[:len(binary)], binary)
        with self.assertRaises(ValueError):
            make_uf2(bytes(2*1024*1024+1))


if __name__ == '__main__':
    unittest.main(verbosity=2)

