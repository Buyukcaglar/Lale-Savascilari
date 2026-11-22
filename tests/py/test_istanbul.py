"""ISTANBUL.C main() (seg0546): the start-up, from the first statement up to
the first call into the intro/menu module (CALLF 1682:002A demo_intro_menu
at 0546:057B), compared with the original running in DOSBox-X.

The emulator cannot run the original's main(): the far heap arena of the
main() snapshot (runtime 2EAD:0000 up) covers the flat overlay images that
main calls.  The ground truth is DOSBox-X instead:
tools/dosbox/capture_startup.py breaks at 0546:057B and saves memory, DOS
MCB/EMS state and VGA (build/oracle/menu_*).  The port's ist_main runs over
orig/ (all game modules linked) until demo_intro_menu (redirected to
testhook.c's th_stop by lale.patch), then:
  * DGROUP: equal except the fields listed in startup_ignore() (relocated
    segment words, the stack, the clock, DOS/driver state of the host);
  * far heap: the block chain (seg, size, used), __first/__last/__rover;
  * EMS: the pages of the handles the game allocated (overlay cache +
    MEMMAN), ist_ovr_init_ems included;
  * VGA: DAC, mode registers, the screen (A000).
Without the capture files test_startup_oracle is skipped (says so)."""
import ctypes
import json
import os
import re
import struct

from lale import ROOT, check, fn, lib, DG, LOAD, patch, unpatch
import helpers as H
from test_heap_layout import chain, port_state

C = ctypes
i16, u16, i32 = C.c_int16, C.c_uint16, C.c_int32
ORIG = os.path.join(ROOT, 'orig')
CAP = os.path.join(ROOT, 'build', 'oracle')
TIME = 0x2F1A33C5

c_ovr = fn('ist_ovr_init_ems', i16, u16, u16, u16)
c_ovr_prepare = fn('ist_ovr_prepare', None)
TIME_OVR = i32.in_dll(lib, 'ist_time_override')
c_th_run = fn('th_run', C.c_int, C.c_void_p, C.c_ssize_t, C.c_ssize_t, C.c_ssize_t, C.c_ssize_t)


class Regs(C.Structure):
    _fields_ = [('ax', u16), ('bx', u16), ('cx', u16), ('dx', u16)]


# a private function object: setting argtypes on lib.pc_int67 would break
# emu86's own calls (its Regs is another class)
c_int67 = lib['pc_int67']
c_int67.restype = None
c_int67.argtypes = [C.POINTER(Regs)]
c_handle_mem = fn('pc_ems_handle_mem', C.c_void_p, u16, C.POINTER(C.c_uint32))


def ems_state():
    """(status, free pages, total pages) and (0, pages) of the allocated
    handles 0..15 ((1, None) for the others)"""
    r = Regs(0x4200, 0, 0, 0)
    c_int67(C.byref(r))
    out = [(r.ax >> 8, r.bx, r.dx)]
    for h in range(16):
        size = C.c_uint32(0)
        p = c_handle_mem(h, C.byref(size))
        out.append((0, size.value // 0x4000) if p else (1, None))
    return out


def run_port(argc, stop='demo_intro_menu'):
    """the port's start-up (as sdl3_main.c does it) and ist_main until its
    first call of `stop`; returns (reached, heap state, DGROUP, EMS, fatals)"""
    H.pc_reset()
    H.dg_reset()
    H.c_set_dir(ORIG.encode())
    H.c_stdio_reset()
    H.c_vfile_reset()
    H.c_vfs_reset()
    H.c_setupio()
    c_ovr_prepare()
    # native far pointers whose DGROUP originals dg_reset clears
    for name in ('gr_work_pal', 'gr_target_pal'):
        C.c_void_p.in_dll(lib, name).value = None
    H.FATALS.value = 0
    TIME_OVR.value = TIME
    patch(stop, 'th_stop')
    try:
        ok = c_th_run(C.cast(lib.ist_main, C.c_void_p), argc, 0, 0, 0)
    finally:
        unpatch(stop)
    return ok, port_state(), bytes(DG[0:0x10000]), ems_state(), H.FATALS.value


def test_startup():
    """internal consistency (no oracle needed)"""
    ok, sc, dgc, emsc, fc = run_port(1)
    check(ok == 1, 'start-up: port main reached demo_intro_menu')
    check(fc == 0, f'start-up: {fc} fatal calls')
    # 14 overlay pages (min(18, overlays' size)) + MEMMAN's 240000h bytes
    check(emsc[0][2] - emsc[0][1] == 14 + 0x240000 // 0x4000, f'start-up: EMS pages {emsc[0]}')
    slots = struct.unpack('<8h', dgc[0xB384:0xB394])
    check(all(v >= 0 for v in slots) and len(set(slots)) == 8, f'start-up: archive slots {slots}')
    check(struct.unpack('<4h', dgc[0x3AFB:0x3B03]) == (200,) * 4, 'start-up: 3D view sizes')
    check(sc[2] > 0, 'start-up: farcoreleft')


# ---- against DOSBox-X ------------------------------------------------------------
def oracle():
    mem = os.path.join(CAP, 'menu_mem.bin')
    if not os.path.exists(mem):
        return None
    m = open(mem, 'rb').read()
    dos = open(os.path.join(CAP, 'menu_dos.txt'), encoding='utf-8', errors='replace').read()
    regs = json.load(open(os.path.join(CAP, 'menu_vga_regs.json')))
    dac = open(os.path.join(CAP, 'menu_vga_dac.bin'), 'rb').read()
    planes = []
    for p in range(4):
        f = os.path.join(CAP, f'MENU_VGA_PLANE{p}.BIN')
        planes.append(open(f, 'rb').read() if os.path.exists(f) else None)
    return m, dos, regs, dac, planes


def orig_heap(m):
    def word(seg, off):
        return struct.unpack_from('<H', m, seg * 16 + off)[0]
    first, last, rover = struct.unpack_from('<HHH', m, LOAD * 16 + 0x2382)
    return chain(word, first, last), (first, last, rover)


def orig_ems(dos):
    """{handle: pages} of DOSBox-X's 'DOS EMS' listing"""
    out = {}
    for mm in re.finditer(r'^\s+(\d+)\s+([0-9a-fA-F]{8})\s+([0-9a-fA-F]{8})', dos, re.M):
        out[int(mm.group(1))] = int(mm.group(3), 16) // 0x4000
    return out


# DGROUP bytes the two runs keep differently on purpose (with the reason)
def startup_ignore(dgo, dgc):
    ign = {}

    def add(lo, hi, why):
        for i in range(lo, hi):
            ign.setdefault(i, why)
    # far pointers into DGROUP / resident code: the original's are relocated
    # (runtime segment = flat + 824h), the port keeps the image's
    for i in range(0, 0xFFFF):
        wo, wc = struct.unpack_from('<H', dgo, i)[0], struct.unpack_from('<H', dgc, i)[0]
        if wo != wc and (wo - wc) & 0xFFFF == LOAD and 0x0800 <= wc < 0x6000:
            add(i, i + 2, 'relocated segment')
    # far pointers the port keeps as native pointers (zero in dg[]): to heap
    # blocks / the loaded driver, the stack segment, resident code (timer
    # ISRs, callbacks)
    for i in range(0, 0xFFFD):
        off, seg = struct.unpack_from('<HH', dgo, i)
        if dgc[i:i + 4] == bytes(4) and LOAD <= seg < 0xA000 and seg != 0x16E0 + LOAD:
            add(i, i + 4, 'far pointer (native in the port)')
    add(0x5B, 0x84, 'c0: PSP/environment/stack/_heapbase far pointers (DOS)')
    add(0x86, 0x94, 'c0/_brklvl & co: DOS memory block addresses')
    add(0x9282, 0x9630, 'RTL: argv/env strings, stdio stream tokens/buffers, 8087 emulator')
    add(0xE74A, 0xE74E, '100 Hz clock')
    add(0xEA74, 0xEB00, 'RTL start-up data before main() (tzname "EST/EDT" ...), not modelled')
    add(0xEB90, 0x10000, "beyond DGROUP's end: the next segments (asm2599 code ...)")
    add(0x8FA0, 0x8FA2, 'RTL sin(): x87 type cached (3 = 387); the port uses the host FPU')
    add(0x918E, 0x9190, 'RTL atexit() count: the port registers no RTL exit handlers')
    add(0xE73E, 0xE74A, 'NKONTROL: saved INT 08h/09h vectors (BIOS/DOS addresses)')
    add(0xE764, 0xE766, 'int86 result flags: CPU bits 12-14 (DOSBox-X 486: 7212h, port 0212h)')
    add(0x85A0, 0x8614, 'DSMI amp/timer service: tick times and rate accumulators (real time)')
    # AMP's private interrupt stack (ampInterrupt, 0EF6:0F64: SS:SP :=
    # DGROUP:7BE4, old SS:SP saved at 7BE4/7BE6): the original leaves the
    # frames of the last timer interrupt there (return addresses into
    # seg0EF6, DS, locals); the port has no 8086 stack (src/dsmi/amp.c)
    add(0x7AE3, 0x7BE8, "AMP's private interrupt stack: frames of the last timer tick (no stack in C)")
    return ign


def test_startup_oracle():
    o = oracle()
    if o is None:
        print('      (no build/oracle/menu_*: run tools/dosbox/capture_startup.py)')
        return
    m, dos, regs, dac, planes = o
    ok, sc, dgc, emsc, fc = run_port(1)
    check(ok == 1 and fc == 0, f'oracle: port reached the menu call ({ok}, {fc} fatals)')
    dgo = m[(0x16E0 + LOAD) * 16:(0x16E0 + LOAD) * 16 + 0x10000]

    # far heap: the chain in runtime segments on both sides
    ch_o, cv_o = orig_heap(m)
    ch_c, cv_c, _ = sc
    # (until 2026-11-21 the DSMI driver load differed here: the driver file
    # is now read through the game's file layer and the native MCP takes
    # its tables (404h paragraphs) and DMA buffer (1F8h) from the far heap)
    split = next((i for i, (a, b) in enumerate(zip(ch_o, ch_c)) if a != b), None)
    check(ch_o == ch_c, f'oracle: heap chain {len(ch_o)}/{len(ch_c)} blocks, first difference at '
                        f'{split}: {ch_o[split:split + 3] if split is not None else ""} vs '
                        f'{ch_c[split:split + 3] if split is not None else ""}')
    check(cv_o == tuple(cv_c), f'oracle: heap first/last/rover {cv_o} vs {cv_c}')

    # EMS: the handles the game allocated (DOSBox-X's handle 0 is its own)
    eo = orig_ems(dos)
    ec = {h: emsc[1 + h][1] for h in range(16) if emsc[1 + h][0] == 0 and emsc[1 + h][1]}
    game_o = {h: p for h, p in eo.items() if h}
    game_c = {h: p for h, p in ec.items() if h}
    check(sorted(game_o.values()) == sorted(game_c.values()),
          f'oracle: EMS handle pages {game_o} vs {game_c}')

    # DGROUP
    ign = startup_ignore(dgo, dgc)
    bad = [i for i in range(0x10000) if dgo[i] != dgc[i] and i not in ign]
    if bad:
        runs = []
        for i in bad:
            if runs and i - runs[-1][1] <= 1:
                runs[-1][1] = i
            else:
                runs.append([i, i])
        check(False, f'oracle: DGROUP differs in {len(runs)} ranges: ' + ', '.join(
            f'{a:04X}-{b:04X} {dgo[a:b + 1][:8].hex()}/{dgc[a:b + 1][:8].hex()}' for a, b in runs[:12]))

    # VGA: DAC, the mode registers, the screen
    vs = H.VgaState.from_address(H.VGA)
    check(bytes(vs.dac) == dac, 'oracle: DAC differs')
    check(list(vs.seq[:5]) == regs['seq'] and list(vs.gc[:9]) == regs['gc'],
          f'oracle: SEQ/GC {list(vs.seq[:5])} {list(vs.gc[:9])} vs {regs["seq"]} {regs["gc"]}')
    check(list(vs.crtc[:25]) == regs['crtc'], f'oracle: CRTC {list(vs.crtc[:25])} vs {regs["crtc"]}')
    check(vs.misc_output == regs['misc'], f'oracle: misc {vs.misc_output:02X} vs {regs["misc"]:02X}')
    if all(planes):
        a000 = C.string_at(C.addressof(C.c_uint8.in_dll(lib, 'pc_a000')), 64000)
        scr = bytes(planes[i & 3][i >> 2] for i in range(64000))
        n = sum(1 for i in range(64000) if a000[i] != scr[i])
        check(n == 0, f'oracle: screen (A000, chained) differs in {n} bytes')
