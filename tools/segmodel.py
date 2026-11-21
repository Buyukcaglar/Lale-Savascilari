#!/usr/bin/env python3
"""
segmodel.py - segment model + far entry point discovery for the flat image
              produced by exeload.py.

Every far pointer in the program passes through a relocation (MZ table for the
resident image, fixup table for overlays). A relocation at seg:off whose
preceding byte is 0x9A (CALL FAR) or 0xEA (JMP FAR) therefore gives an exact
far code target.  Far "push seg / push off" style references (function
pointers) are reported separately as candidates.

Writes build/<exe>_segments.json:
  segments: [{name, seg, start, size, kind}]  contents at [seg:start, seg:size)
            kind: code | ovl | stub | asm (code in a data-class segment)
  entries:  {"SSSS:OOOO": name}
  dgroup:   "16E0"
"""
import json
import os
import struct
import sys


def p(s):
    seg, off = s.split(':')
    return int(seg, 16), int(off, 16)


def build(mapfile, flatfile):
    info = json.load(open(mapfile))
    mem = open(flatfile, 'rb').read()
    base = os.path.basename(flatfile).split('_')[0]
    segs = []
    st = info['segtable']
    if st:
        for e in st:
            seg = int(e['seg'], 16)
            # TLINK packs segments at byte alignment: contents live at
            # [seg:minoff, seg:maxoff), minoff being the paragraph remainder.
            if e['flags'] == 1 and e['maxoff']:
                segs.append(dict(seg=seg, start=e['minoff'], size=e['maxoff'], kind='code'))
            elif e['flags'] == 3:
                segs.append(dict(seg=seg, start=e['minoff'], size=e['maxoff'], kind='stub'))
        # data: DGROUP from entry stub (mov dx, imm16 at entry)
        ecs, eip = p(info['entry'])
        a = ecs * 16 + eip
        assert mem[a] == 0xBA
        dgroup = struct.unpack_from('<H', mem, a + 1)[0]
        for o in info['overlays']:
            if not o.get('empty'):
                segs.append(dict(seg=o['seg'], start=0, size=o['code_size'], kind='ovl',
                                 stub=o['stub_seg']))
    else:
        dgroup = None
    segs.sort(key=lambda s: s['seg'])
    for i, s in enumerate(segs):
        s['name'] = {'code': 'seg', 'stub': 'stub', 'ovl': 'ovl'}[s['kind']] + f"{s['seg']:04X}"
        s['seg_hex'] = f"{s['seg']:04X}"

    entries = {info['entry']: 'start'}

    def ranges(kinds):
        return [(s['seg'] * 16 + s['start'], s['seg'] * 16 + s['size'])
                for s in segs if s['kind'] in kinds]

    def inside(lin, rs):
        return any(a <= lin < b for a, b in rs)

    # far refs: MZ relocations + overlay fixups
    refs = [p(r) for r in info['relocations']]
    for o in info['overlays']:
        for f in o.get('fixups', []):
            refs.append((o['seg'], f))

    def far_targets():
        src = ranges(('code', 'ovl', 'asm'))
        for seg, off in refs:
            lin = seg * 16 + off
            if lin >= 3 and mem[lin - 3] in (0x9A, 0xEA) and inside(lin - 3, src):
                yield (struct.unpack_from('<H', mem, lin)[0],
                       struct.unpack_from('<H', mem, lin - 2)[0])

    # Data-class segments (segtable flags 0) other than DGROUP that receive
    # far calls hold hand-written assembly (e.g. 2599: 386 code placed in a
    # FAR_DATA class segment).  Promote them to kind 'asm'.
    if st:
        datasegs = {int(e['seg'], 16): e for e in st
                    if e['flags'] == 0 and e['maxoff'] and int(e['seg'], 16) != dgroup}
        hit = {tseg for tseg, toff in far_targets()
               if tseg in datasegs and datasegs[tseg]['minoff'] <= toff < datasegs[tseg]['maxoff']}
        for tseg in sorted(hit):
            e = datasegs[tseg]
            segs.append(dict(seg=tseg, start=e['minoff'], size=e['maxoff'], kind='asm',
                             name=f'asm{tseg:04X}', seg_hex=f'{tseg:04X}'))
        segs.sort(key=lambda s: s['seg'])

    # only accept targets that land inside code, addressed through the
    # canonical segment base (real far calls are relocated seg:off pairs, so a
    # non-canonical segment means the 9A/EA byte was really a displacement or
    # immediate, e.g. C7 46 9A = mov word [bp-66h], ...)
    dst = ranges(('code', 'ovl', 'asm', 'stub'))
    bases = {s['seg'] for s in segs}
    farcalls = rejected = 0
    for tseg, toff in far_targets():
        if tseg not in bases or not inside(tseg * 16 + toff, dst):
            rejected += 1
            continue
        key = f'{tseg:04X}:{toff:04X}'
        # resolve calls through overlay stubs to the overlay body
        key = info['thunks'].get(key, key)
        entries.setdefault(key, None)
        farcalls += 1
    for stub, tgt in info['thunks'].items():
        entries.setdefault(tgt, None)
    for k in list(entries):
        if entries[k] is None:
            seg, off = p(k)
            entries[k] = f'F_{seg:04X}_{off:04X}'
    return dict(exe=base, dgroup=f'{dgroup:04X}' if dgroup is not None else None,
                segments=segs, thunks=info['thunks'], entries=dict(sorted(entries.items())),
                stats=dict(far_calls=farcalls, rejected=rejected, entries=len(entries)))


def main(argv):
    exe = argv[1] if len(argv) > 1 else 'ist'
    res = build(f'build/{exe}_map.json', f'build/{exe}_flat.bin')
    json.dump(res, open(f'build/{exe}_segments.json', 'w'), indent=1)
    print(exe, res['stats'], 'segments', len(res['segments']), 'dgroup', res['dgroup'])


if __name__ == '__main__':
    sys.exit(main(sys.argv))
