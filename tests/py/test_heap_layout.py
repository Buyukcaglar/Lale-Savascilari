"""The far heap through the whole stack: a start-up-like sequence (vfs
tables, archives mounted with buffered streams, DDS, MEMMAN with the EMS
descriptor table, conventional surfaces, a buffered real file opened and
closed, fm_change_ext, frees and reuse) run by the original code and by
the port.  The original's allocator is the real one (helpers.FarHeap, from
the main() heap); after every step the block chain (segment, size, used or
free), the CS variables and farcoreleft() must be the same - i.e. every
block lands at the same seg:0004 as in the original."""
import ctypes
import os
import struct

from lale import NAMES, ROOT, check, fn
import helpers as H

C = ctypes
u8, i16, u32, vp, cp = C.c_uint8, C.c_int16, C.c_uint32, C.c_void_p, C.c_char_p
ORIG = os.path.join(ROOT, 'orig')

c_vfs_open = fn('vfs_open', i16, cp, i16)
c_vfs_close = fn('vfs_close', i16, i16)
c_unmount = fn('vfs_unmount_archive', i16, i16)
c_mm_init = fn('mm_init', i16, u32, u32)
c_surface = fn('mm_surface_alloc', vp, u8, i16, i16, u8)
c_mm_free = fn('mm_free', u8, vp, i16, cp)
c_change_ext = fn('fm_change_ext', vp, cp, cp)
c_DDSInit = fn('DDSInit', i16)


def chain(word, first, last):
    """[(seg, paragraphs, used)] from the first block to the last one"""
    out, seg = [], first
    while seg and len(out) < 10000:
        out.append((seg, word(seg, 0), word(seg, 2) != 0))
        if seg == last:
            break
        seg += word(seg, 0)
    return out


def orig_state(heap):
    h = heap.h

    def word(seg, off):
        return struct.unpack('<H', h.u.mem_read(seg * 16 + off, 2))[0]
    first, last, rover = H.emu_cvars(h)
    return chain(word, first, last), (first, last, rover), heap.coreleft()


def port_state():
    a = H.arena()

    def word(seg, off):
        i = (seg - H.ARENA_SEG) * 16 + off
        return a[i] | a[i + 1] << 8
    first, last, rover = H.cvars()
    return chain(word, first, last), (first, last, rover), H.c_coreleft()


STEPS = ['vfs_init', 'mount 3D', 'mount IVIR', 'DDSInit', 'mm_init', 'surfaces',
         'open/close real file', 'fm_change_ext', 'free surface 3', 'surface 6',
         'unmount IVIR', 'open member, surface 7']


def run_orig():
    H.pc_reset()
    H.dg_reset()
    H.c_heap_reset()                       # the seed: the heap at main()
    e = H.make_emu(emu=H.DosEmu(ORIG, snapshot='main'), dds=False)
    heap, out = e.heap, []
    s = e.alloc(0x200)

    def string(slot, text):
        e.wr(s, slot * 0x20, text.encode() + b'\0', runtime=True)
        return e.far(s, slot * 0x20)

    def pcall(name, *args):                # pascal: first argument pushed first
        words = []
        for a in reversed(args):
            words.extend(a if isinstance(a, list) else [a])
        return e.call(NAMES[name], *words)
    recs = {}
    for step in STEPS:
        if step == 'vfs_init':
            pcall('o569A_init', 20)
        elif step.startswith('mount'):
            pcall('o569A_mount_archive', string(0, step.split()[1] + '.CKD'), [0, 0])
        elif step == 'DDSInit':
            e.call(NAMES['DDSInit'])
        elif step == 'mm_init':
            e.call(NAMES['mm_init'], *H.L(0x240000), *H.L(0x440000))
        elif step == 'surfaces':
            for slot, w, hgt in ((1, 320, 204), (9, 15, 15), (3, 320, 200), (4, 100, 50)):
                recs[slot] = e.call(NAMES['mm_surface_alloc'], slot, w, hgt, 0)[0]
        elif step == 'open/close real file':
            hd = pcall('o569A_open', string(1, 'SONPAL.PAL'), 0)[0]
            pcall('o569A_close', hd)
        elif step == 'fm_change_ext':
            e.call(NAMES['fm_change_ext'], *string(2, 'PIC.CMP'), *string(3, 'PAL'))
        elif step == 'free surface 3':
            e.call(NAMES['mm_free'], recs[3], 0x1F04, 0x77, *string(4, 'T:X.L'))
        elif step == 'surface 6':
            e.call(NAMES['mm_surface_alloc'], 6, 200, 100, 0)
        elif step == 'unmount IVIR':
            pcall('o569A_unmount_archive', 1)
        else:
            pcall('o569A_open', string(5, 'KAPI4.CMP'), 0)
            e.call(NAMES['mm_surface_alloc'], 7, 64, 64, 0)
        out.append(orig_state(heap))
    e.close_all()
    return out, len(e.fatals)


def run_port():
    H.pc_reset()
    H.dg_reset()                           # + bc_farheap_reset (lale.py)
    H.c_set_dir(ORIG.encode())
    H.c_stdio_reset()
    H.c_vfile_reset()
    H.c_vfs_reset()
    H.c_setupio()                          # stdin gets the main() block 4BB3h
    H.FATALS.value = 0
    out, recs = [], {}
    for step in STEPS:
        if step == 'vfs_init':
            H.c_vfs_init(20)
        elif step.startswith('mount'):
            H.c_vfs_mount((step.split()[1] + '.CKD').encode(), None)
        elif step == 'DDSInit':
            c_DDSInit()
        elif step == 'mm_init':
            c_mm_init(0x240000, 0x440000)
        elif step == 'surfaces':
            for slot, w, hgt in ((1, 320, 204), (9, 15, 15), (3, 320, 200), (4, 100, 50)):
                recs[slot] = c_surface(slot, w, hgt, 0)
        elif step == 'open/close real file':
            c_vfs_close(c_vfs_open(b'SONPAL.PAL', 0))
        elif step == 'fm_change_ext':
            c_change_ext(b'PIC.CMP', b'PAL')
        elif step == 'free surface 3':
            c_mm_free(recs[3], 0x77, b'T:X.L')
        elif step == 'surface 6':
            c_surface(6, 200, 100, 0)
        elif step == 'unmount IVIR':
            c_unmount(1)
        else:
            c_vfs_open(b'KAPI4.CMP', 0)
            c_surface(7, 64, 64, 0)
        out.append(port_state())
    return out, H.FATALS.value


def test_startup_sequence_layout():
    o, fo = run_orig()
    c, fc = run_port()
    check(fo == fc == 0, f'fatals {fo} / {fc}')
    for step, (co, vo, lo), (cc, vc, lc) in zip(STEPS, o, c):
        if co != cc:
            i = next((k for k in range(min(len(co), len(cc))) if co[k] != cc[k]), min(len(co), len(cc)))
            check(False, f'{step}: block {i}: orig {co[i:i + 2]} port {cc[i:i + 2]} '
                         f'({len(co)} / {len(cc)} blocks)')
            break
        if not check(vo == vc and lo == lc, f'{step}: CS vars {vo} / {vc}, farcoreleft {lo:#x} / {lc:#x}'):
            break
    # the sequence did something: blocks were added, one freed and reused
    check(len(c[-1][0]) > len(c[0][0]) + 10, f'only {len(c[-1][0])} blocks at the end')
    check(any(not used for _, _, used in c[STEPS.index('free surface 3')][0]), 'no free block after the free')
    check(c[-1][2] < c[0][2] - 0x20000, f'farcoreleft only went from {c[0][2]:#x} to {c[-1][2]:#x}')
