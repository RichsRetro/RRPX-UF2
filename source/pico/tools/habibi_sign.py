"""Prepare a COPY of a homebrew XBE for an ENDGAME Habibi-key session.

Does not install a softmod or patch a running console. Requires Python 3 only.
Reads published numerical compatibility parameters from public_key.json.
Format reference: https://github.com/XboxDev/xbedump
"""
import argparse
import hashlib
import pathlib
import struct


def key_pair():
    import json
    key = json.loads(pathlib.Path(__file__).with_name('public_key.json').read_text())
    n = int.from_bytes(bytes.fromhex(key['modulus_le']), 'little')
    e = key['exponent']
    if n % 3:
        raise ValueError('Unexpected Habibi modulus')
    d = pow(e, -1, 2 * (n // 3 - 1))
    return n, e, d


def xbox_hash(data):
    return hashlib.sha1(struct.pack('<I', len(data)) + data).digest()


def validate_layout(data):
    if len(data) < 0x178 or data[:4] != b'XBEH':
        raise ValueError('Not an XBE')
    base, header_size, image_size = struct.unpack_from('<III', data, 0x104)
    cert, sections, section_table = struct.unpack_from('<III', data, 0x118)
    cert -= base
    section_table -= base
    if base != 0x10000 or not 0x178 <= header_size <= len(data):
        raise ValueError('Invalid XBE header')
    if not 0x178 <= cert <= header_size-0x1d0:
        raise ValueError('Certificate outside header')
    if not 0 < sections <= 256 or not 0 <= section_table <= header_size-sections*56:
        raise ValueError('Section table outside header')
    if struct.unpack_from('<I', data, 0x15c)[0]:
        raise ValueError('Debug/non-kernel imports unsupported by this preparation tool')
    ranges = []
    for index in range(sections):
        entry = section_table + index*56
        _, address, virtual_size, file_offset, file_size = struct.unpack_from('<5I', data, entry)
        if file_offset + file_size > len(data):
            raise ValueError('Section outside file')
        if xbox_hash(data[file_offset:file_offset+file_size]) != data[entry+36:entry+56]:
            raise ValueError(f'Section {index} hash does not match')
        ranges.append((address, address+virtual_size))
    return base, image_size, header_size, cert, ranges


def verify_habibi(data):
    _, _, header_size, _, _ = validate_layout(data)
    n, e, _ = key_pair()
    decoded = pow(int.from_bytes(data[4:260], 'little'), e, n).to_bytes(256, 'little')
    # Xbox accepts several SHA1 padding forms; the essential digest is reversed.
    return decoded[:20] == xbox_hash(data[0x104:header_size])[::-1] and decoded[-2:] == b'\x01\x00'


def populate_empty_hashes(original):
    """Fill cxbe's empty section digests; reject conflicting nonempty digests."""
    data = bytearray(original)
    if len(data) < 0x178 or data[:4] != b'XBEH':
        raise ValueError('Not an XBE')
    base = struct.unpack_from('<I', data, 0x104)[0]
    count, table = struct.unpack_from('<II', data, 0x11c)
    table -= base
    if not 0 < count <= 256 or not 0 <= table <= len(data)-count*56:
        raise ValueError('Invalid section table')
    for index in range(count):
        at = table + index*56
        offset, size = struct.unpack_from('<II', data, at+12)
        if offset + size > len(data):
            raise ValueError('Section outside file')
        digest = xbox_hash(data[offset:offset+size])
        if data[at+36:at+56] not in (bytes(20), digest):
            raise ValueError('Conflicting nonempty section digest')
        data[at+36:at+56] = digest
    return bytes(data)


def sign_xbe(original):
    base, image_size, header_size, cert, ranges = validate_layout(original)
    data = bytearray(original)
    # Choose the source encoding by checking that both decoded pointers are mapped.
    enc_entry = struct.unpack_from('<I', data, 0x128)[0]
    enc_thunk = struct.unpack_from('<I', data, 0x158)[0]
    candidates = []
    for entry_key, thunk_key in ((0xa8fc57ab, 0x5b6d40b6), (0x94859d4b, 0xefb1f152)):
        entry, thunk = enc_entry ^ entry_key, enc_thunk ^ thunk_key
        if any(a <= entry < b for a, b in ranges) and base <= thunk < base+image_size:
            candidates.append((entry, thunk))
    if len(candidates) != 1:
        raise ValueError('Cannot unambiguously identify retail/debug pointer encoding')
    entry, thunk = candidates[0]
    struct.pack_into('<I', data, 0x128, entry ^ 0xa8fc57ab)
    struct.pack_into('<I', data, 0x158, thunk ^ 0x5b6d40b6)
    struct.pack_into('<II', data, cert+0x9c, 0x800000ff, 0x80000007)
    digest = xbox_hash(data[0x104:header_size])
    padding = digest[::-1] + b'\x00' + b'\xff'*233 + b'\x01\x00'
    n, _, d = key_pair()
    data[4:260] = pow(int.from_bytes(padding, 'little'), d, n).to_bytes(256, 'little')
    if not verify_habibi(data):
        raise ValueError('Signature self-check failed')
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    parser.add_argument('--populate-empty-section-hashes', action='store_true',
                        help='Fill empty digests emitted by nxdk cxbe')
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve() or args.output.exists():
        parser.error('Choose a NEW output file; original files are never overwritten')
    original = args.source.read_bytes()
    if args.populate_empty_section_hashes:
        original = populate_empty_hashes(original)
    args.output.write_bytes(sign_xbe(original))
    print(f'Prepared and verified {args.output}')


if __name__ == '__main__':
    main()
