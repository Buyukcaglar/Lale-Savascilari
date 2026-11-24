#!/usr/bin/env python3
"""
exediff.py - compare two builds of the game executable (Borland MZ + FBOV overlays)
             module by module, telling real code changes from shifted addresses.

A relinked build moves DGROUP data and segment start offsets, so nearly every
routine differs byte-wise although its source did not change.  The comparison
therefore works in three layers:

1. DGROUP of both builds is aligned (unique 8-byte anchors, longest increasing
   chain) into runs with a constant offset delta: the data address map A -> B.
2. Code units (resident code segments and overlays, paired by __SEGTABLE__
   index) of equal size are compared byte-wise.  A difference is explained when
   it is a relocated segment word on both sides, a 16-bit DGROUP offset that
   follows the data map, or a 16-bit code offset that follows the start-offset
   delta of a resident segment (TLINK packs at byte alignment).
3. Units of different size - and any unit named with --asm - are disassembled
   (linear sweep, Borland 8087-emulator interrupts decoded as x87) and aligned
   instruction by instruction on their shape (mnemonic + operand kinds); only
   the blocks that really differ are printed, with call targets and strings
   named from analysis/names.tsv and the DGROUP strings.

usage:
    exediff.py A.EXE B.EXE [--asm UNIT]... [--all-asm] [--data] [--names FILE]
               [--context N]
    exediff.py A.EXE B.EXE --list-a UNIT | --list-b UNIT     full listing of one side

    UNIT   A-side name: seg0546, ovl3C2D, ...
    --data print the DGROUP alignment (changed and inserted data)
    --own  list the near pointers accepted by the unit's own start-offset delta

Addresses are the flat-image addresses of tools/exeload.py for each build.
"""
import bisect
import difflib
import os
import re
import struct
import sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exeload  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DGROUP_INDEX_FLAGS = 0


# --------------------------------------------------------------------------
def align(a, b, k=8):
    """Equal runs [(ia, ib, n)] increasing in both: unique k-gram anchors -> LIS."""
    def uniq(x):
        seen = {}
        for i in range(len(x) - k + 1):
            g = bytes(x[i:i + k])
            seen[g] = i if g not in seen else -1
        return {g: i for g, i in seen.items() if i >= 0}
    ua, ub = uniq(a), uniq(b)
    pairs = sorted((i, ub[g]) for g, i in ua.items() if g in ub)
    tails, idx, prev = [], [], [-1] * len(pairs)
    for n, (i, j) in enumerate(pairs):
        p = bisect.bisect_left(tails, j)
        if p == len(tails):
            tails.append(j)
            idx.append(n)
        else:
            tails[p] = j
            idx[p] = n
        prev[n] = idx[p - 1] if p else -1
    chain, n = [], idx[-1] if idx else -1
    while n >= 0:
        chain.append(pairs[n])
        n = prev[n]
    chain.reverse()
    runs = []
    for i, j in chain:
        lo_a = runs[-1][0] + runs[-1][2] if runs else 0
        lo_b = runs[-1][1] + runs[-1][2] if runs else 0
        if i < lo_a or j < lo_b:
            continue
        s = 0
        while i - s - 1 >= lo_a and j - s - 1 >= lo_b and a[i - s - 1] == b[j - s - 1]:
            s += 1
        e = 0
        while i + e < len(a) and j + e < len(b) and a[i + e] == b[j + e]:
            e += 1
        runs.append((i - s, j - s, s + e))
    return runs


class Build:
    def __init__(self, path):
        self.path = path
        self.mem, self.info = exeload.load(path)
        self.mem = bytes(self.mem)
        self.rel = set()
        for r in self.info['relocations']:
            s, o = r.split(':')
            self.rel.add(int(s, 16) * 16 + int(o, 16))
        ov = {o['segtab_index']: o for o in self.info['overlays'] if not o.get('empty')}
        for o in ov.values():
            for fx in o['fixups']:
                self.rel.add(o['seg'] * 16 + fx)
        self.st = self.info['segtable']
        self.segidx = {}                  # paragraph -> first segtable index
        for e in self.st:
            self.segidx.setdefault(int(e['seg'], 16), e['index'])
        self.units = []                   # (index, kind, seg, start, end)
        for e in self.st:
            seg = int(e['seg'], 16)
            if e['flags'] == 1 and e['maxoff'] > e['minoff']:
                self.units.append((e['index'], 'seg', seg, e['minoff'], e['maxoff']))
            elif e['flags'] == 3 and e['index'] in ov:
                o = ov[e['index']]
                self.units.append((e['index'], 'ovl', o['seg'], 0, o['code_size']))
                self.segidx[o['seg']] = ('ovl', e['index'])
        # DGROUP: the largest flags-0 segment
        dg = max((e for e in self.st if e['flags'] == 0), key=lambda e: e['maxoff'])
        self.dgseg = int(dg['seg'], 16)
        self.dgidx = dg['index']
        self.dgsize = dg['maxoff']
        self.dg = self.mem[self.dgseg * 16: self.dgseg * 16 + self.dgsize]
        self.dginit = len(self.dg.rstrip(b'\0'))
        self.thunks = {}
        for k, v in self.info['thunks'].items():
            s, o = k.split(':')
            t, u = v.split(':')
            self.thunks[(int(s, 16), int(o, 16))] = (int(t, 16), int(u, 16))

    def dg_masked(self):
        d = bytearray(self.dg)
        base = self.dgseg * 16
        for a in self.rel:
            if base <= a < base + len(d) - 1:
                d[a - base:a - base + 2] = b'\xAA\xAA'
        return d

    def string_at(self, off):
        """cp857 string starting exactly at DGROUP:off (after a NUL), or None."""
        if not (0 < off < self.dginit) or self.dg[off - 1] != 0:
            return None
        e = self.dg.find(b'\0', off)
        s = self.dg[off:e]
        if len(s) < 3 or any(c < 32 and c not in (10, 13) for c in s):
            return None
        return s.decode('cp857', 'replace').replace('\n', '\\n').replace('\r', '\\r')


class DataMap:
    def __init__(self, A, B):
        self.A, self.B = A, B
        self.runs = align(A.dg_masked()[:A.dginit], B.dg_masked()[:B.dginit])
        self.bss = B.dgsize - A.dgsize
        self.deltas = {j - i for (i, j, n) in self.runs} | {self.bss}
        self.first_change = next((i for (i, j, n) in self.runs if j != i), A.dginit)

    def maps(self, va, vb):
        """'dg' when A offset va strictly maps to vb, 'dgl' when the delta is one of the
        map's deltas (array bases addressed with a constant index, ambiguous zero runs)."""
        for (i, j, n) in self.runs:
            if i <= va <= i + n and vb == va + j - i:
                return 'dg'
        if va >= self.A.dginit - 16 and vb == va + self.bss:
            return 'dg'
        if va >= self.first_change and (vb - va) in self.deltas:
            return 'dgl'
        return None

    def report(self, out):
        A, B = self.A, self.B
        a, b = A.dg_masked(), B.dg_masked()
        out(f'DGROUP  A {A.dgseg:04X} size {A.dgsize:#x} (initialised to {A.dginit:#x})   '
            f'B {B.dgseg:04X} size {B.dgsize:#x} (initialised to {B.dginit:#x})')

        def txt(x):
            return ''.join(chr(c) if 32 <= c < 127 else
                           (bytes([c]).decode('cp857', 'replace') if c >= 128 else '.') for c in x)
        pa = pb = 0
        for (i, j, n) in self.runs + [(A.dginit, B.dginit, 0)]:
            if i > pa or j > pb:
                ra, rb = bytes(a[pa:i]), bytes(b[pb:j])
                # pointer tables whose only change is the relocated/shifted target are noise
                kind = 'changed ' if ra and rb else ('only in A' if ra else 'only in B')
                out(f'  {kind} A {pa:04X}-{i:04X} ({i - pa:3d})  B {pb:04X}-{j:04X} ({j - pb:3d})')
                for tag, r in (('A', ra), ('B', rb)):
                    for o in range(0, len(r), 64):
                        out(f'        {tag}: {txt(r[o:o + 64])}')
            if n:
                out(f'  equal     A {i:04X}-{i + n:04X}  B {j:04X}-{j + n:04X}  '
                    f'len {n:5d}  delta {j - i:+d}')
            pa, pb = i + n, j + n


# --------------------------------------------------------------------------
NUM = re.compile(r'0x[0-9a-f]+|\b\d+\b')
MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
MD.detail = True


class Ins:
    __slots__ = ('addr', 'raw', 'mn', 'ops', 'shape', 'imm', 'disp', 'far', 'rel', 'emu')


def disasm(build, seg, start, end):
    """Linear sweep of seg:[start, end) -> [Ins].  addr = offset within seg."""
    mem, base = build.mem, seg * 16
    out, off = [], start
    while off < end:
        code = mem[base + off: base + min(end, off + 16)]
        emu = None
        if len(code) >= 3 and code[0] == 0xCD and 0x34 <= code[1] <= 0x3B:
            # Borland 8087 emulator: CD 34+n modrm.. == (FWAIT) D8+n modrm..
            code2 = bytes([0xD8 + code[1] - 0x34]) + code[2:]
            got = next(MD.disasm(code2, off + 1, 1), None)
            if got is not None:
                emu, ins, size = 1, got, got.size + 1
        elif len(code) >= 2 and code[0] == 0xCD and code[1] == 0x3D:
            emu = 2
        if emu == 2:
            r = Ins()
            r.addr, r.raw, r.mn, r.ops = off, code[:2], 'fwait', ''
            r.imm = r.disp = r.far = r.rel = None
            r.emu = 2
            r.shape = 'fwait'
            out.append(r)
            off += 2
            continue
        if emu is None:
            ins = next(MD.disasm(code, off, 1), None)
            if ins is None:
                r = Ins()
                r.addr, r.raw, r.mn, r.ops = off, code[:1], 'db', f'{code[0]:#04x}'
                r.imm = r.disp = r.far = r.rel = None
                r.emu = 0
                r.shape = 'db'
                out.append(r)
                off += 1
                continue
            size = ins.size
        r = Ins()
        r.addr, r.raw, r.mn, r.ops, r.emu = off, mem[base + off: base + off + size], ins.mnemonic, ins.op_str, emu or 0
        r.imm = r.disp = r.far = r.rel = None
        adj = 1 if emu else 0                 # encoding offsets are in the x87 form
        if ins.disp_size == 2 and ins.disp_offset:
            r.disp = ins.disp_offset + adj
        if ins.imm_size >= 2 and ins.imm_offset:
            r.imm = ins.imm_offset + adj
        b0 = r.raw[0]
        if b0 in (0x9A, 0xEA) and size == 5:
            r.far = struct.unpack_from('<HH', r.raw, 1)
            r.imm = None
        elif b0 in (0xE8, 0xE9) and size == 3:
            r.rel = (off + 3 + struct.unpack_from('<h', r.raw, 1)[0]) & 0xFFFF
            r.imm = None
        elif (b0 == 0xEB or 0x70 <= b0 <= 0x7F or 0xE0 <= b0 <= 0xE3) and size == 2:
            r.rel = (off + 2 + struct.unpack_from('<b', r.raw, 1)[0]) & 0xFFFF
        elif b0 == 0x0F and size == 4 and 0x80 <= r.raw[1] <= 0x8F:
            r.rel = (off + 4 + struct.unpack_from('<h', r.raw, 2)[0]) & 0xFFFF
            r.imm = None
        r.shape = r.mn + ' ' + NUM.sub('#', r.ops)
        out.append(r)
        off += size
    return out


class Namer:
    def __init__(self, path):
        self.names = {}
        if path and os.path.exists(path):
            for line in open(path, encoding='utf-8'):
                w = line.rstrip('\n').split('\t')
                if len(w) >= 2 and ':' in w[0] and w[0] != 'address':
                    s, o = w[0].split(':')
                    self.names[(int(s, 16), int(o, 16))] = w[1]

    def get(self, seg, off):
        return self.names.get((seg, off))


class Differ:
    def __init__(self, A, B, names, out, context=2):
        self.A, self.B, self.names, self.out, self.ctx = A, B, names, out, context
        self.dm = DataMap(A, B)
        self.ua = {u[0]: u for u in A.units}
        self.ub = {u[0]: u for u in B.units}
        # start-offset delta per resident code segment
        self.segdelta = {i: self.ub[i][3] - u[3] for i, u in self.ua.items() if u[1] == 'seg' and i in self.ub}
        self.codedeltas = {d & 0xFFFF for d in self.segdelta.values() if d}
        self.own = []          # near pointers accepted by the own-delta rule (--own lists them)
        self.b2a = {}          # (unit index) -> {B offset: A offset} for disassembled units
        self.a2b = {}

    def near_fixup(self, addr, build=None):
        """Is a relocated segment word within 7 bytes of flat address addr?  A code
        offset only moves together with its segment: CALL/JMP FAR, PUSH seg / PUSH off,
        MOV [p+2],seg / MOV [p],off.  Without this a constant that changed by one of the
        segment deltas (0 -> 1) would pass for a shifted address."""
        b = build or self.A
        for k in range(-7, 8):
            if (addr + k) in b.rel:
                # the fixup of a CALL/JMP FAR belongs to that instruction's own offset
                if b.mem[addr + k - 3] in (0x9A, 0xEA) and k != 2:
                    continue
                return True
        return False

    # ---- layer 2: bytes ---------------------------------------------------
    def bytes_equal(self, ua, ub):
        A, B = self.A, self.B
        a, b, n = ua[2] * 16 + ua[3], ub[2] * 16 + ub[3], ua[4] - ua[3]
        ndg = ncode = nrel = nown = 0
        un = []
        x = 0
        while x < n:
            ra, rb = (a + x) in A.rel, (b + x) in B.rel
            if ra and rb:
                nrel += 1
                x += 2
                continue
            if ra != rb:
                un.append(x)
                x += 2
                continue
            if A.mem[a + x] == B.mem[b + x]:
                x += 1
                continue
            ok = None
            for w in (x, x - 1):
                if w < 0 or w + 2 > n:
                    continue
                va = struct.unpack_from('<H', A.mem, a + w)[0]
                vb = struct.unpack_from('<H', B.mem, b + w)[0]
                if self.dm.maps(va, vb):
                    ok = ('dg', w)
                    break
                if ((vb - va) & 0xFFFF) in self.codedeltas and self.near_fixup(a + w):
                    ok = ('code', w)
                    break
                # a near pointer into this unit itself (switch tables, ISR offsets of
                # the assembly modules): exactly the unit's own delta, value in the unit
                own = self.segdelta.get(ua[0], 0) if ua[1] == 'seg' else 0
                if own and vb - va == own and ua[3] <= va <= ua[4]:
                    ok = ('own', w)
                    self.own.append((ua, w, va, vb))
                    break
            if ok:
                if ok[0] == 'dg':
                    ndg += 1
                elif ok[0] == 'own':
                    nown += 1
                else:
                    ncode += 1
                x = ok[1] + 2
            else:
                un.append(x)
                x += 1
        return ndg, ncode + nown, nrel, un, nown

    # ---- layer 3: instructions ---------------------------------------------
    def target_a(self, build, seg, off):
        """A-space (seg, off) of a far code target of either build, or None."""
        if build is self.A:
            return self.A.thunks.get((seg, off), (seg, off))
        t = self.B.thunks.get((seg, off))
        if t:
            seg, off = t
        idx = self.B.segidx.get(seg)
        if idx is None:
            return None
        if not isinstance(idx, tuple) and idx not in self.ub:
            # code in a data-class segment (the hand-written 386 routines): same offsets
            return (int(self.A.st[idx]['seg'], 16), off) if idx < len(self.A.st) else None
        if isinstance(idx, tuple):                      # overlay code
            i = idx[1]
            m = self.b2a.get(i)
            if m is not None:
                return (self.ua[i][2], m[off]) if off in m else None
            return (self.ua[i][2], off)
        if idx not in self.ua:
            return None
        m = self.b2a.get(idx)
        if m is not None:
            return (self.ua[idx][2], m[off]) if off in m else None
        return (self.ua[idx][2], (off - self.segdelta.get(idx, 0)) & 0xFFFF)

    def fmt(self, build, unit, r, mark):
        txt = f'{r.mn} {r.ops}'.strip()
        note = []
        if r.far:
            t = self.target_a(build, r.far[1], r.far[0])
            nm = self.names.get(*t) if t else None
            note.append(nm or (f'{t[0]:04X}:{t[1]:04X}' if t else '?'))
        elif r.rel is not None and r.raw[0] == 0xE8:
            if build is self.A:
                t = (unit[2], r.rel)
            else:
                m = self.b2a.get(unit[0], {})
                t = (self.ua[unit[0]][2], m[r.rel]) if r.rel in m else None
            nm = self.names.get(*t) if t else None
            if nm:
                note.append(nm)
        for slot in (r.imm, r.disp):
            if slot is not None and slot + 2 <= len(r.raw):
                v = struct.unpack_from('<H', r.raw, slot)[0]
                s = build.string_at(v)
                if s is not None:
                    note.append('"' + s[:70] + '"')
        if r.emu == 1:
            note.append('8087 emulator')
        tail = ('   ; ' + ', '.join(note)) if note else ''
        return f'  {mark} {unit[2]:04X}:{r.addr:04X}  {r.raw.hex():<16s} {txt}{tail}'

    def same(self, ua, ub, ra, rb, strict):
        """Do two same-shape instructions differ only by shifted addresses?"""
        if ra.raw == rb.raw:
            pa, pb = ua[2] * 16 + ra.addr, ub[2] * 16 + rb.addr
            if not any(((pa + k) in self.A.rel) != ((pb + k) in self.B.rel) for k in range(len(ra.raw))):
                if ra.rel is None or not strict:
                    return True
        if len(ra.raw) != len(rb.raw):
            return False
        if ra.rel is not None:
            if rb.rel is None:
                return False
            if not strict:
                return True
            return self.a2b[ua[0]].get(ra.rel) == rb.rel
        if ra.far:
            ta = self.target_a(self.A, ra.far[1], ra.far[0])
            tb = self.target_a(self.B, rb.far[1], rb.far[0]) if strict else ta
            return ra.raw[0] == rb.raw[0] and ta == tb
        slots = [s for s in (ra.disp, ra.imm) if s is not None]
        if [s for s in (rb.disp, rb.imm) if s is not None] != slots:
            return False
        ma, mb = bytearray(ra.raw), bytearray(rb.raw)
        pa, pb = ua[2] * 16 + ra.addr, ub[2] * 16 + rb.addr
        for s in slots:
            rela, relb = (pa + s) in self.A.rel, (pb + s) in self.B.rel
            va = struct.unpack_from('<H', ra.raw, s)[0]
            vb = struct.unpack_from('<H', rb.raw, s)[0]
            if rela != relb:
                return False
            if rela:
                ia, ib = self.A.segidx.get(va), self.B.segidx.get(vb)
                if ia != ib:
                    return False
            elif va != vb:
                # a code offset: beside its segment fixup, or a CS-relative table of
                # this unit (JMP CS:[BX+table])
                cs_tab = 0x2E in ra.raw[:2] and s == ra.disp
                own = self.segdelta.get(ua[0], 0) if ua[1] == 'seg' else 0
                if own and vb - va == own and ua[3] <= va <= ua[4]:
                    ma[s:s + 2] = mb[s:s + 2] = bytes(2)
                    continue
                if not (self.dm.maps(va, vb)
                        or (((vb - va) & 0xFFFF) in self.codedeltas and self.near_fixup(pa + s))
                        or (cs_tab and strict and self.a2b[ua[0]].get(va) == vb)
                        or (cs_tab and ua[1] == 'seg' and vb - va == self.segdelta.get(ua[0], 0))):
                    return False
            ma[s:s + 2] = mb[s:s + 2] = b'\0\0'
        return ma == mb

    def asm_diff(self, idx, quiet=False):
        ua, ub = self.ua[idx], self.ub[idx]
        ia = disasm(self.A, ua[2], ua[3], ua[4])
        ib = disasm(self.B, ub[2], ub[3], ub[4])
        sm = difflib.SequenceMatcher(None, [r.shape for r in ia], [r.shape for r in ib], autojunk=False)
        ops = sm.get_opcodes()
        a2b, b2a = {}, {}
        for tag, i1, i2, j1, j2 in ops:
            if tag == 'equal':
                for k in range(i2 - i1):
                    a2b[ia[i1 + k].addr] = ib[j1 + k].addr
                    b2a[ib[j1 + k].addr] = ia[i1 + k].addr
            elif i2 > i1 and j2 > j1:         # a rewritten routine keeps its entry
                a2b.setdefault(ia[i1].addr, ib[j1].addr)
                b2a.setdefault(ib[j1].addr, ia[i1].addr)
        a2b[ua[4]] = ub[4]
        self.a2b[idx], self.b2a[idx] = a2b, b2a
        if quiet:
            return None
        # blocks: runs of pairs that are not 'same'
        blocks = []                                   # (i1, i2, j1, j2)
        for tag, i1, i2, j1, j2 in ops:
            if tag != 'equal':
                blocks.append([i1, i2, j1, j2])
                continue
            k = 0
            while k < i2 - i1:
                if self.same(ua, ub, ia[i1 + k], ib[j1 + k], True):
                    k += 1
                    continue
                e = k
                while e < i2 - i1 and not self.same(ua, ub, ia[i1 + e], ib[j1 + e], True):
                    e += 1
                blocks.append([i1 + k, i1 + e, j1 + k, j1 + e])
                k = e
        blocks.sort()
        merged = []
        for b in blocks:
            if merged and b[0] - merged[-1][1] <= 2 * self.ctx and b[2] - merged[-1][3] == b[0] - merged[-1][1]:
                merged[-1][1], merged[-1][3] = b[1], b[3]
            else:
                merged.append(b)
        # function of an A offset: the last named routine at or before it
        fn = sorted((o, n) for (s, o), n in self.names.names.items() if s == ua[2])
        offs = [o for o, n in fn]

        def where(off):
            p = bisect.bisect_right(offs, off) - 1
            return f'{fn[p][1]}+{off - fn[p][0]:#x}' if p >= 0 else '?'
        na = sum(b[1] - b[0] for b in merged)
        nb = sum(b[3] - b[2] for b in merged)
        self.out(f'    {len(ia)} / {len(ib)} instructions; {len(merged)} changed blocks '
                 f'({na} instructions of A, {nb} of B)')
        for (i1, i2, j1, j2) in merged:
            at = ia[i1].addr if i1 < len(ia) else ua[4]
            self.out(f'  @@ A {ua[2]:04X}:{at:04X} ({where(at)})   B {ub[2]:04X}:'
                     f'{(ib[j1].addr if j1 < len(ib) else ub[4]):04X}')
            for k in range(max(0, i1 - self.ctx), i1):
                self.out(self.fmt(self.A, ua, ia[k], ' '))
            for k in range(i1, i2):
                self.out(self.fmt(self.A, ua, ia[k], '-'))
            for k in range(j1, j2):
                self.out(self.fmt(self.B, ub, ib[k], '+'))
            for k in range(i2, min(len(ia), i2 + self.ctx)):
                self.out(self.fmt(self.A, ua, ia[k], ' '))
        return merged

    def listing(self, name, side):
        """Whole annotated disassembly of one unit of build A or B."""
        for idx, ua in self.ua.items():
            if f'{ua[1]}{ua[2]:04X}' == name and idx in self.ub:
                build, u = (self.A, ua) if side == 'A' else (self.B, self.ub[idx])
                for r in disasm(build, u[2], u[3], u[4]):
                    self.out(self.fmt(build, u, r, side))

    def run(self, asm_units, all_asm):
        A, B = self.A, self.B
        out = self.out
        changed = []
        # address maps of resized units first: other units call into them
        for idx, ua in self.ua.items():
            ub = self.ub.get(idx)
            if ub and ua[4] - ua[3] != ub[4] - ub[3]:
                self.asm_diff(idx, quiet=True)
        for idx, ua in self.ua.items():
            ub = self.ub.get(idx)
            name = f'{ua[1]}{ua[2]:04X}'
            if ub is None:
                out(f'{name}: only in A')
                continue
            la, lb = ua[4] - ua[3], ub[4] - ub[3]
            head = f'{name:8s} A {ua[2]:04X}:{ua[3]:04X} {la:6d} bytes   B {ub[2]:04X}:{ub[3]:04X} {lb:6d} bytes   '
            if la == lb:
                ndg, ncode, nrel, un, nown = self.bytes_equal(ua, ub)
                if not un:
                    out(head + f'same code ({ndg} data, {ncode} code offsets shifted'
                        + (f' - {nown} of them near pointers into the unit' if nown else '')
                        + f'; {nrel} segment fixups)')
                else:
                    out(head + f'{len(un)} UNEXPLAINED byte differences')
                    changed.append(idx)
            else:
                out(head + f'SIZE {lb - la:+d}')
                changed.append(idx)
            if idx in changed or all_asm or name in asm_units:
                self.asm_diff(idx)
        return changed


def main(argv):
    args = argv[1:]
    asm, names, data, all_asm, ctx, own = set(), os.path.join(ROOT, 'analysis', 'names.tsv'), False, False, 2, False
    files, lists = [], []
    while args:
        a = args.pop(0)
        if a == '--asm':
            asm.add(args.pop(0))
        elif a == '--names':
            names = args.pop(0)
        elif a == '--context':
            ctx = int(args.pop(0))
        elif a == '--data':
            data = True
        elif a == '--all-asm':
            all_asm = True
        elif a == '--own':
            own = True
        elif a in ('--list-a', '--list-b'):
            lists.append((args.pop(0), a[-1].upper()))
        else:
            files.append(a)
    if len(files) != 2:
        print(__doc__)
        return 1
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    A, B = Build(files[0]), Build(files[1])
    out = print
    for tag, X in (('A', A), ('B', B)):
        i = X.info
        out(f'{tag}: {X.path}  {os.path.getsize(X.path)} bytes, image {i["image_size"]:#x}, '
            f'{len(i["relocations"])} relocations, {len(X.units)} code units, stack {i["stack"]}')
    d = Differ(A, B, Namer(names), out, ctx)
    if data:
        d.dm.report(out)
    if lists:
        for idx, ua in d.ua.items():                 # address maps for the call names
            ub = d.ub.get(idx)
            if ub and ua[4] - ua[3] != ub[4] - ub[3]:
                d.asm_diff(idx, quiet=True)
        for name, side in lists:
            d.listing(name, side)
        return 0
    changed = d.run(asm, all_asm)
    if own:
        out('near pointers into their own unit (accepted as shifted):')
        for ua, w, va, vb in d.own:
            a = ua[2] * 16 + ua[3] + w
            out(f'  {ua[2]:04X}:{ua[3] + w:04X}  {va:04X} -> {vb:04X}   '
                f'bytes {A.mem[a - 3:a + 4].hex()}')
    out(f'{len(changed)} unit(s) with real code changes: '
        + ', '.join(f'{d.ua[i][1]}{d.ua[i][2]:04X}' for i in changed))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
