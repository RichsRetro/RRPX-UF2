"""Wrap a Pico RP2040 flash binary in UF2; never accesses hardware."""
import argparse
import pathlib
import struct


def make_uf2(binary):
    if not binary or len(binary) > 2 * 1024 * 1024:
        raise ValueError('Binary must fit a standard 2MB RP2040 Pico')
    count = (len(binary) + 255) // 256
    result = bytearray()
    for index in range(count):
        block = bytearray(512)
        struct.pack_into('<8I', block, 0, 0x0a324655, 0x9e5d5157,
                         0x2000, 0x10000000 + index * 256, 256,
                         index, count, 0xe48bff56)
        block[32:288] = binary[index*256:(index+1)*256].ljust(256, b'\x00')
        struct.pack_into('<I', block, 508, 0x0ab16f30)
        result.extend(block)
    return bytes(result)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    args = parser.parse_args()
    args.output.write_bytes(make_uf2(args.binary.read_bytes()))
    print(f'Wrote {args.output}')
