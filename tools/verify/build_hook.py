#!/usr/bin/env python3
"""build_hook.py - assemble tools/verify/hook.S into out/verify/hook.bin (flat, origin 0)."""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN = os.path.join(ROOT, '.tools', 'llvm-mingw', 'llvm-mingw-20261104-ucrt-x86_64', 'bin')
OUT = os.path.join(ROOT, 'out', 'verify')


def link_flat(obj):
    """What `ld.lld -m elf_i386 -Ttext=0 --image-base=0 --oformat=binary`
    does for hook.o, without a linker (macOS has clang but no ELF linker):
    the object is one .text section whose relocations are all R_386_16
    against symbols of that section, so the flat image at origin 0 is the
    section with each relocated word += the symbol's value.  Anything else in
    the object is refused."""
    import struct
    d = open(obj, 'rb').read()
    assert d[:6] == b'\x7fELF\x01\x01', 'not a little-endian ELF32 object'
    shoff, = struct.unpack_from('<I', d, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', d, 0x2E)
    sh = [struct.unpack_from('<10I', d, shoff + i * shentsize) for i in range(shnum)]
    names = sh[shstrndx]

    def name(off):
        e = d.index(b'\0', names[4] + off)
        return d[names[4] + off:e].decode()
    by = {name(x[0]): (i, x) for i, x in enumerate(sh)}
    ti, t = by['.text']
    text = bytearray(d[t[4]:t[4] + t[5]])
    for nm, (i, x) in by.items():
        if x[1] in (4, 9) and x[7] != ti:              # SHT_RELA / SHT_REL
            raise ValueError(f'relocations for another section: {nm}')
        if x[1] == 1 and i != ti and x[5]:              # SHT_PROGBITS
            raise ValueError(f'unexpected section with contents: {nm}')
    if '.rel.text' in by:
        _, r = by['.rel.text']
        _, st = by['.symtab']
        for k in range(r[5] // 8):
            off, info = struct.unpack_from('<II', d, r[4] + 8 * k)
            if info & 0xFF != 20:                       # R_386_16
                raise ValueError(f'relocation type {info & 0xFF} at {off:#x}')
            _, value, _, _, _, shndx = struct.unpack_from('<IIIBBH', d, st[4] + 16 * (info >> 8))
            if shndx != ti:
                raise ValueError(f'relocation at {off:#x} against a symbol outside .text')
            w, = struct.unpack_from('<H', text, off)
            struct.pack_into('<H', text, off, (w + value) & 0xFFFF)
    return bytes(text)


def build():
    os.makedirs(OUT, exist_ok=True)
    src = os.path.join(ROOT, 'tools', 'verify', 'hook.S')
    obj = os.path.join(OUT, 'hook.o')
    out = os.path.join(OUT, 'hook.bin')
    if os.path.exists(out) and os.path.getmtime(out) > os.path.getmtime(src):
        return out
    if os.name == 'nt':
        subprocess.run([os.path.join(BIN, 'clang.exe'), '--target=i386-none-elf', '-c', src, '-o', obj],
                       check=True)
        subprocess.run([os.path.join(BIN, 'ld.lld.exe'), '-m', 'elf_i386', '-Ttext=0', '--image-base=0',
                        '--oformat=binary', '-e', '_start', obj, '-o', out], check=True)
    else:
        # the system clang assembles for any target; link_flat stands in for ld.lld
        subprocess.run([os.environ.get('CC', 'clang'), '--target=i386-none-elf', '-c', src, '-o', obj],
                       check=True)
        open(out, 'wb').write(link_flat(obj))
    return out


if __name__ == '__main__':
    p = build()
    print(p, os.path.getsize(p), 'bytes')
    sys.exit(0)
