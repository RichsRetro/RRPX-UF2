"""Exercise compiled Cortex-M0+ callbacks with Unicorn (not a USB/Xbox test).

Requires: pip install unicorn pyelftools
Usage: python tools/test_firmware.py firmware.elf generated/card.bin
"""
import argparse
import random
import struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2
from unicorn.arm_const import UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf')
    parser.add_argument('image')
    args = parser.parse_args()
    machine = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    machine.mem_map(0x10000000, 2*1024*1024)
    machine.mem_map(0x20000000, 0x42000)
    stop = 0x08000000
    machine.mem_map(stop, 4096)
    with open(args.elf, 'rb') as file:
        elf = ELFFile(file)
        for segment in elf.iter_segments():
            if segment['p_type'] == 'PT_LOAD' and segment['p_filesz']:
                machine.mem_write(segment['p_vaddr'], segment.data())
        symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}

    # Pico SDK normally dispatches memory operations through the chip's boot ROM.
    # Supply their semantics here; all card logic and puff execute as ARM code.
    hooks = {}
    for name in ('memcpy', '__wrap_memcpy', '__aeabi_memcpy', '__aeabi_memcpy4', '__aeabi_memcpy8'):
        if name in symbols:
            hooks[symbols[name] & ~1] = 'copy'
    for name in ('memset', '__wrap_memset'):
        if name in symbols:
            hooks[symbols[name] & ~1] = 'set'

    def intercept(uc, address, size, data):
        del size, data
        if address == stop:
            uc.emu_stop()
            return
        kind = hooks.get(address)
        if kind:
            dst, src, length = (uc.reg_read(reg) for reg in
                                (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
            content = bytes(uc.mem_read(src, length)) if kind == 'copy' else bytes([src & 255])*length
            uc.mem_write(dst, content)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    for address in (*hooks, stop):
        machine.hook_add(UC_HOOK_CODE, intercept, begin=address, end=address)
    stack = 0x20041000
    output = 0x20030000

    def call(name, *values):
        machine.reg_write(UC_ARM_REG_SP, stack)
        machine.reg_write(UC_ARM_REG_LR, stop | 1)
        regs = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
        for index, value in enumerate(values):
            if index < 4:
                machine.reg_write(regs[index], value)
            else:
                machine.mem_write(stack + (index-4)*4, struct.pack('<I', value))
        machine.emu_start(symbols[name] | 1, stop, count=30000000)
        assert machine.reg_read(UC_ARM_REG_PC) == stop, f'{name}: instruction limit reached'
        result = machine.reg_read(UC_ARM_REG_R0)
        return result if result < 0x80000000 else result - 0x100000000

    image = open(args.image, 'rb').read()
    # Read every compressed block using the actual C decoder and compare exactly.
    for start in range(0, len(image), 16384):
        assert call('tud_msc_read10_cb', 0, start//512, 0, output, 16384) == 16384
        assert bytes(machine.mem_read(output, 16384)) == image[start:start+16384]
    print('PASS: all 512 blocks decoded by compiled ARM puff and storage callbacks')
    rng = random.Random(42)
    for _ in range(50):
        start = rng.randrange(len(image)-1536)
        assert call('tud_msc_read10_cb', 0, start//512, start%512, output, 1536) == 1536
        assert bytes(machine.mem_read(output, 1536)) == image[start:start+1536]
    assert call('tud_msc_read10_cb', 0, len(image)//512, 0, output, 1) == -1
    assert call('tud_msc_read10_cb', 1, 0, 0, output, 1) == -1
    assert call('tud_msc_read10_cb', 0, 0xffffffff, 0xffffffff, output, 512) == -1
    print('PASS: unaligned reads, cross-block reads, invalid LUN and overflow bounds')
    start = 511
    content = b'overlay check' * 75
    machine.mem_write(output, content)
    assert call('tud_msc_write10_cb', 0, 0, start, output, len(content)) == len(content)
    assert call('tud_msc_read10_cb', 0, 0, 0, output, 2048) == 2048
    expected = image[:start] + content + image[start+len(content):2048]
    assert bytes(machine.mem_read(output, 2048)) == expected
    assert call('tud_msc_write10_cb', 0, len(image)//512, 0, output, 1) == -1
    # Exhaust the RAM overlay and confirm a new-sector write is rejected.
    used_address = symbols['overlay_used']
    used = struct.unpack('<I', machine.mem_read(used_address, 4))[0]
    for index in range(192-used):
        assert call('tud_msc_write10_cb', 0, 100+index, 0, output, 512) == 512
    assert call('tud_msc_write10_cb', 0, 1000, 0, output, 512) == -1
    print('PASS: partial writes preserve surrounding bytes; overlay exhaustion rejected')


if __name__ == '__main__':
    main()
