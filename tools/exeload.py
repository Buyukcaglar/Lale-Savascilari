#!/usr/bin/env python3
"""
exeload.py - unpack IST.EXE (Borland C++ 3.x, large model, VROOMM overlays)
             into a single flat, fully linked real-mode memory image.

IST.EXE layout
--------------
  MZ header (0x2400 bytes, 2063 relocations)
  load image   0x26010 bytes   (resident code, overlay stubs, DGROUP)
  FBOV block   "FBOV", u32 size, u32 segtable_fileofs, u32 nsegs,
               followed by the overlay code+fixups

Borland __SEGTABLE__ (nsegs * 8 bytes at segtable_fileofs) entries:
  u16 seg, u16 maxoff, u16 flags, u16 minoff
  flags: 1 = resident code, 3 = overlay stub, 0 = data, 4 = misc/bss

Overlay stub segment (flags == 3):
  +0x00 CD 3F       INT 3Fh
  +0x02 u16         (runtime: saved return / state)
  +0x04 u32         offset of the overlay code, relative to the END of the
                    16-byte FBOV header (i.e. file offset = fbov + 0x10 + x)
  +0x08 u16         code size
  +0x0A u16         fixup table size (bytes; u16 offsets into the code).
                    The table follows the code. Each referenced word holds
                    (segtable index * 8), patched to a real paragraph at load.
  +0x0C u16         number of entry thunks
  +0x0E..0x1F       runtime bookkeeping (zero on disk)
  +0x20 thunks[n]   each: CD 3F, u16 target offset, 00
                    (the overlay manager rewrites these as JMP FAR seg:off)

Output
------
We load at segment base 0 (so MZ and overlay segment fixups are identity),
then append every overlay at its own fresh segment above the stack.  Each stub
thunk is rewritten to EA off seg (JMP FAR) - exactly what the overlay manager
does after loading - so static tools can follow calls into overlays.

  build/ist_flat.bin    the linear memory image (address = seg*16+off)
  build/ist_map.json    segment table, overlay map, thunk map, relocations

usage: exeload.py [IST.EXE] [outdir]
"""
import json
import os
import struct
import sys

OVL_BASE_SEG = 0x2700          # first paragraph used for overlay code


def load(path):
    d = open(path, 'rb').read()
    (magic, cblp, cp, crlc, cparhdr, minalloc, maxalloc, ss, sp, csum,
     ip, cs, lfarlc, ovno) = struct.unpack_from('<2s13H', d, 0)
    assert magic == b'MZ'
    img_end = (cp - 1) * 512 + cblp if cblp else cp * 512
    hdr = cparhdr * 16
    img = bytearray(d[hdr:img_end])
    relocs = [struct.unpack_from('<HH', d, lfarlc + 4 * i) for i in range(crlc)]

    fb = img_end
    mem = bytearray(img)
    overlays, thunks = [], {}
    if fb + 16 <= len(d) and d[fb:fb + 4] == b'FBOV':
        sig, ovsize, segtab_ofs, nseg = struct.unpack_from('<4sIII', d, fb)
        segtab = [struct.unpack_from('<4H', d, segtab_ofs + 8 * i) for i in range(nseg)]
        mem += bytes(OVL_BASE_SEG * 16 - len(mem))
    else:                                   # plain MZ, no overlays (KUR.EXE)
        ovsize = segtab_ofs = 0
        segtab = []
    next_seg = OVL_BASE_SEG
    for idx, (seg, maxoff, flags, minoff) in enumerate(segtab):
        if flags != 3:
            continue
        stub = img[seg * 16: seg * 16 + maxoff]
        fofs, csize, fxsize, nent = struct.unpack_from('<IHHH', stub, 4)
        if csize == 0:
            overlays.append(dict(segtab_index=idx, stub_seg=seg, empty=True))
            continue
        # stub offsets are relative to the end of the 16-byte FBOV header
        cbase = fb + 0x10 + fofs
        code = bytearray(d[cbase: cbase + csize])
        fix = [struct.unpack_from("<H", d, cbase + csize + 2 * i)[0]
               for i in range(fxsize // 2)]
        # Segment fixups: each referenced word holds (segtable index * 8); the
        # overlay manager replaces it with that segment's paragraph + load base.
        # Our load base is 0, so we substitute the segtable paragraph directly.
        bad = []
        for f in fix:
            v = struct.unpack_from('<H', code, f)[0] if f + 2 <= len(code) else None
            if v is None or v % 8 or v // 8 >= len(segtab):
                bad.append(f)
                continue
            struct.pack_into('<H', code, f, segtab[v // 8][0])
        oseg = next_seg
        # pad first: a slice assignment past the end of a bytearray would
        # silently append the code at the current end instead of at oseg*16
        if len(mem) < oseg * 16 + len(code):
            mem += bytes(oseg * 16 + len(code) - len(mem))
        mem[oseg * 16: oseg * 16 + len(code)] = code
        assert mem[oseg * 16: oseg * 16 + len(code)] == code
        ents = []
        for e in range(nent):
            eo = 0x20 + 5 * e
            assert stub[eo:eo + 2] == b'\xcd\x3f', (hex(seg), e)
            tgt = struct.unpack_from('<H', stub, eo + 2)[0]
            ents.append(dict(stub=f'{seg:04X}:{eo:04X}', target=f'{oseg:04X}:{tgt:04X}'))
            thunks[f'{seg:04X}:{eo:04X}'] = f'{oseg:04X}:{tgt:04X}'
            a = seg * 16 + eo
            mem[a:a + 5] = b'\xea' + struct.pack('<HH', tgt, oseg)
        overlays.append(dict(segtab_index=idx, stub_seg=seg, seg=oseg,
                             file_offset=cbase, code_size=csize,
                             fixups=fix, bad_fixups=bad, entries=ents))
        next_seg = oseg + (csize + 15) // 16 + 1
    if overlays and next_seg * 16 > len(mem):
        mem += bytes(next_seg * 16 - len(mem))

    info = dict(
        source=os.path.basename(path),
        entry=f'{cs:04X}:{ip:04X}', stack=f'{ss:04X}:{sp:04X}',
        image_size=len(img), minalloc=minalloc, maxalloc=maxalloc,
        relocations=[f'{s:04X}:{o:04X}' for o, s in relocs],
        fbov=dict(file_offset=fb, size=ovsize, segtab_file_offset=segtab_ofs) if segtab else None,
        segtable=[dict(index=i, seg=f'{s:04X}', maxoff=m, flags=fl, minoff=mn)
                  for i, (s, m, fl, mn) in enumerate(segtab)],
        overlays=overlays, thunks=thunks,
    )
    return mem, info


def main(argv):
    path = argv[1] if len(argv) > 1 else 'orig/IST.EXE'
    out = argv[2] if len(argv) > 2 else 'build'
    os.makedirs(out, exist_ok=True)
    mem, info = load(path)
    base = os.path.splitext(os.path.basename(path))[0].lower()
    open(os.path.join(out, f'{base}_flat.bin'), 'wb').write(mem)
    json.dump(info, open(os.path.join(out, f'{base}_map.json'), 'w'), indent=1)
    n_ov = sum(1 for o in info['overlays'] if not o.get('empty'))
    n_bad = sum(len(o.get('bad_fixups', [])) for o in info['overlays'])
    print(f'{path}: image {info["image_size"]:#x}, {len(info["relocations"])} relocs, '
          f'{n_ov} overlays, {len(info["thunks"])} thunks, {n_bad} suspicious fixups, '
          f'flat {len(mem):#x}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
