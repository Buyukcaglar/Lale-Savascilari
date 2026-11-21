#!/usr/bin/env python3
"""build_hook.py - assemble tools/verify/hook.S into out/verify/hook.bin (flat, origin 0)."""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN = os.path.join(ROOT, '.tools', 'llvm-mingw', 'llvm-mingw-20261104-ucrt-x86_64', 'bin')
OUT = os.path.join(ROOT, 'out', 'verify')


def build():
    os.makedirs(OUT, exist_ok=True)
    src = os.path.join(ROOT, 'tools', 'verify', 'hook.S')
    obj = os.path.join(OUT, 'hook.o')
    out = os.path.join(OUT, 'hook.bin')
    if os.path.exists(out) and os.path.getmtime(out) > os.path.getmtime(src):
        return out
    subprocess.run([os.path.join(BIN, 'clang.exe'), '--target=i386-none-elf', '-c', src, '-o', obj],
                   check=True)
    subprocess.run([os.path.join(BIN, 'ld.lld.exe'), '-m', 'elf_i386', '-Ttext=0', '--image-base=0', '--oformat=binary',
                    '-e', '_start', obj, '-o', out], check=True)
    return out


if __name__ == '__main__':
    p = build()
    print(p, os.path.getsize(p), 'bytes')
    sys.exit(0)
