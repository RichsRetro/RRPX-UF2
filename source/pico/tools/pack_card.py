"""Build a small FATX memory-unit image and independently compressed flash blocks.

Python 3 standard library only. No physical disks are opened or modified.
"""
import argparse
import hashlib
import json
import pathlib
import struct
import zlib

# USB transport blocks remain 512 bytes. The Xbox MU filesystem layer uses
# 4096-byte logical sectors, independently of that transport block size.
# See BAD-AL/xbox_memory_unit_tool's MU geometry and libfatx with sector_size=4096.
SECTOR = 4096
SECTORS_PER_CLUSTER = 4
CLUSTER = SECTOR * SECTORS_PER_CLUSTER
DISK_SIZE = 8 * 1024 * 1024
BLOCK_SIZE = 16384
HEADER_SIZE = 4096
# Include the reserved FAT entry and round up to a 4KiB page.
# 8MiB MU: FAT=0x1000, root/data begin at 0x2000, clusters are 0x4000 bytes.
FAT_ENTRIES = DISK_SIZE // CLUSTER + 1
FAT_SIZE = ((FAT_ENTRIES * 2 + 4095) // 4096) * 4096
DATA_START = HEADER_SIZE + FAT_SIZE
CLUSTER_COUNT = (DISK_SIZE - DATA_START) // CLUSTER


def build_image(source):
    image = bytearray(DISK_SIZE)
    image[:HEADER_SIZE] = b'\xff' * HEADER_SIZE
    struct.pack_into('<4sIII', image, 0, b'FATX', 0x50494332, SECTORS_PER_CLUSTER, 1)
    image[16:80] = 'RRPX RETAIL 0714'.encode('utf-16le').ljust(64, b'\x00')
    struct.pack_into('<H', image, HEADER_SIZE, 0xfff8)
    next_cluster = 1

    def allocate(count):
        nonlocal next_cluster
        first = next_cluster
        if first + count - 1 > CLUSTER_COUNT:
            raise ValueError('Files do not fit the virtual memory card')
        for cluster in range(first, first + count):
            struct.pack_into('<H', image, HEADER_SIZE + cluster * 2,
                             cluster + 1 if cluster + 1 < first + count else 0xffff)
        next_cluster += count
        return first

    def location(cluster):
        return DATA_START + (cluster - 1) * CLUSTER

    def add_directory(path):
        children = sorted(path.iterdir(), key=lambda p: p.name.lower())
        count = max(1, (len(children) * 64 + CLUSTER - 1) // CLUSTER)
        first = allocate(count)
        start = location(first)
        image[start:start + count * CLUSTER] = b'\xff' * (count * CLUSTER)
        for index, child in enumerate(children):
            name = child.name.encode('ascii')
            if not 1 <= len(name) <= 42:
                raise ValueError(f'Invalid FATX name: {child.name}')
            if child.is_dir():
                cluster, size, flags = add_directory(child), 0, 0x10
            elif child.is_file():
                content = child.read_bytes()
                size = len(content)
                flags = 0x04 if child.suffix.lower() == '.xbx' else 0x20
                cluster = allocate((size + CLUSTER - 1) // CLUSTER) if size else 0
                if size:
                    pos = location(cluster)
                    image[pos:pos + size] = content
            else:
                raise ValueError(f'Unsupported entry: {child}')
            # Original Xbox FATX uses time/date pairs with year relative to 2000.
            date = ((2026 - 2000) << 9) | (1 << 5) | 1
            entry = struct.pack('<BB42sII6H', len(name), flags, name,
                                cluster, size, 0, date, 0, date, 0, date)
            image[start + index * 64:start + (index + 1) * 64] = entry
        return first

    assert add_directory(source) == 1
    return bytes(image)


def pack_image(image):
    offsets = [0]
    packed = bytearray()
    for start in range(0, len(image), BLOCK_SIZE):
        compressor = zlib.compressobj(9, zlib.DEFLATED, -15)
        packed.extend(compressor.compress(image[start:start + BLOCK_SIZE]))
        packed.extend(compressor.flush())
        offsets.append(len(packed))
    return bytes(packed), offsets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=pathlib.Path,
                        help='Directory containing helper/, trigger/, payload.xbe')
    parser.add_argument('--output', type=pathlib.Path, required=True)
    args = parser.parse_args()
    for name in ('helper', 'trigger', 'payload.xbe'):
        if not (args.source / name).exists():
            parser.error(f'Missing {name}')
    image = build_image(args.source)
    packed, offsets = pack_image(image)
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    (out / 'card.bin').write_bytes(image)
    (out / 'card.deflate').write_bytes(packed)
    (out / 'card_index.h').write_text(
        '#pragma once\n#include <stdint.h>\n'
        f'#define CARD_SIZE {len(image)}u\n#define CARD_BLOCK_SIZE {BLOCK_SIZE}u\n'
        f'#define CARD_PACKED_SIZE {len(packed)}u\n'
        'static const uint32_t card_offsets[] = {\n' +
        ',\n'.join('  ' + ', '.join(map(str, offsets[i:i+12]))
                    for i in range(0, len(offsets), 12)) + '\n};\n', encoding='ascii')
    (out / 'card_blob.S').write_text(
        '.section .rodata.card_blob, "a", %progbits\n.balign 4\n'
        '.global card_blob\ncard_blob:\n.incbin "' +
        (out / 'card.deflate').resolve().as_posix() + '"\n', encoding='ascii')
    manifest = {'disk_bytes': len(image), 'packed_bytes': len(packed),
                'filesystem_sector_bytes': SECTOR, 'usb_block_bytes': 512,
                'cluster_bytes': CLUSTER, 'fat_bytes': FAT_SIZE,
                'data_offset': DATA_START, 'label': 'RRPX RETAIL 0714',
                'block_bytes': BLOCK_SIZE, 'image_sha256': hashlib.sha256(image).hexdigest(),
                'files': {p.relative_to(args.source).as_posix():
                          {'bytes': p.stat().st_size,
                           'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
                          for p in sorted(args.source.rglob('*')) if p.is_file()}}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(f'Virtual FATX card: {len(image):,} bytes; compressed: {len(packed):,} bytes')


if __name__ == '__main__':
    main()
