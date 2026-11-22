"""CD audio (ovl5737, src/game/cd5737.c) against the original machine code.

The original runs in Unicorn; its INT 2Fh AX=15xx calls are answered by the
MSCDEX model below (class Mscdex: install check 1500h, drive device list
1501h, read VTOC 1505h, send device request 1510h with IOCTL input 08h/0Ah/
0Bh/0Ch, IOCTL output 02h, play 84h, stop 85h, resume 88h) on a simulated
disc (class Disc).  Two set-ups:

  mode A  the port talks to the real virtual drive (src/platform/cdrom.c) with
          the same disc as a cue/bin image - the build/cd image of
          tools/cdsim.py (redump layout) or small generated images.  Every
          device request the port sends is recorded on its way into cdrom.c.
          This also cross-checks cdrom.c against the model.
  mode B  the port's mscdex_send_request/mscdex_install_check are redirected
          to a second instance of the same model, so the driver's answers can
          be anything (fault injection: arbitrary status words, transfer
          buffers left unwritten, any head position, any track table).

After every call the test compares: the return value, the device requests
issued (drive, the 26 request bytes on entry and on return, the control block
on entry and on return), the module's DGROUP block 16E0:E8CC-E9CC, 8F4E
(active) and 8F50 (start).  On the original's side it also checks that no
other DGROUP byte changed and which INT 2Fh functions were called.

Conventions
  * The port keeps the block in a native mirror (cd5737_test_ptr).  The far
    transfer pointer of an IOCTL request (E8F8-E8FB) has no meaning there and
    is masked while an IOCTL request was the last one built.
  * Uninitialised stack: the original's control blocks are stack memory.  The
    stack below the call frame is zeroed before every call, which is what the
    port's zero-filled buffers stand for.  What one call leaves for the next
    inside o5737_cd_play_track is real and is compared (test_driver_faults).
  * The model's answers follow the MSCDEX 2.x interface (RBIL INT 2Fh 15xxh);
    fields the game never reads are filled like cdrom.c.  Mode A sets
    like_cdrom_c for the one place cdrom.c departs from MSCDEX (a request for
    a drive that is not a CD drive: see Mscdex.request).
"""
import atexit
import ctypes
import os
import random
import shutil
import struct
import tempfile

import lale
from lale import Emu, LOAD, NAMES, ROOT, check, fn, DG
from helpers import DosEmu, run_setupio

C = ctypes
u8, u16, i16, i32, u32, vp = C.c_uint8, C.c_uint16, C.c_int16, C.c_int32, C.c_uint32, C.c_void_p

DGSEG = 0x16E0
BLK, BLK_LEN = 0xE8CC, 0x101            # E8CC..E9CC
REQ = 0xE8EA                            # device request block
OWN = ((0x8F4E, 0x8F54), (BLK, BLK + BLK_LEN))   # what the module may write
CD_DRIVE = 3                            # CDROM_DRIVE_INDEX (D:)

ST_DONE, ST_BUSY, ST_ERROR = 0x0100, 0x0200, 0x8000

# ---- the port ------------------------------------------------------------------
c_drive_info = fn('o5737_cd_drive_info', vp)
c_read_cfg = fn('o5737_cd_read_cfg', i16)
c_nop = fn('o5737_nop', None)
c_ioctl = fn('o5737_cd_ioctl', u8, u16, u16, vp)
c_reset = fn('o5737_cd_reset', None)
c_detect = fn('o5737_cd_detect', None)
c_stop = fn('o5737_cd_stop', i16)
c_init = fn('o5737_cd_init', u8)
c_qchannel = fn('o5737_cd_qchannel', u8, vp)
c_msf = fn('o5737_msf_to_sectors', u32, u32)
c_track_info = fn('o5737_cd_track_info', u32, u8)
c_vtoc_ok = fn('o5737_cd_vtoc_ok', i16)
c_play = fn('o5737_cd_play', u8)
c_status = fn('o5737_cd_status', u32)
c_play_track = fn('o5737_cd_play_track', i16, u8, i32)
c_test_ptr = fn('cd5737_test_ptr', vp, C.c_int)

cdrom_open = fn('cdrom_open', C.c_int, C.c_char_p)
cdrom_close = fn('cdrom_close', None)
cdrom_error = fn('cdrom_error', C.c_char_p)
cdrom_render = fn('cdrom_render', None, vp, C.c_size_t)
cdrom_position = fn('cdrom_position', u32)
cdrom_playing = fn('cdrom_playing', C.c_int)
mscdex_set_present = fn('mscdex_set_present', None, C.c_int)

A = {k: NAMES['o5737_' + k] for k in (
    'cd_drive_info', 'cd_read_cfg', 'nop', 'cd_ioctl', 'cd_reset', 'cd_detect', 'cd_stop',
    'cd_init', 'cd_qchannel', 'msf_to_sectors', 'cd_track_info', 'cd_vtoc_ok', 'cd_play',
    'cd_status', 'cd_play_track')}


# ============================================================================
# the disc and the MSCDEX model
# ============================================================================
class Disc:
    """tracks: [(number, audio, start LBA)], leadout: LBA after the last one."""

    def __init__(self, tracks, leadout):
        self.tracks = list(tracks)
        self.leadout = leadout

    @staticmethod
    def linear(spec):
        """spec: [(audio, sectors)] - tracks 1.. one after the other"""
        tr, lba = [], 0
        for n, (audio, sectors) in enumerate(spec, 1):
            tr.append((n, audio, lba))
            lba += sectors
        return Disc(tr, lba)

    def track(self, number):
        for t in self.tracks:
            if t[0] == number:
                return t
        return None

    def track_at(self, lba):
        cur = self.tracks[0]
        for t in self.tracks:
            if t[2] <= lba:
                cur = t
        return cur

    def end_of(self, t):
        i = self.tracks.index(t)
        return self.tracks[i + 1][2] if i + 1 < len(self.tracks) else self.leadout


def redbook(lba):
    """Red Book address as the driver returns it: frame, second, minute, 0"""
    a = (lba + 150) & 0xFFFFFFFF
    return bytes([a % 75, a // 75 % 60, (a // 4500) & 0xFF, 0])


# bytes of the control block that take part in the comparison, by
# (request command, control code); the game's `count` is one less
XLEN = {(3, 0x08): 5, (3, 0x0A): 7, (3, 0x0B): 7, (3, 0x0C): 11, (0x0C, 0x02): 1}


def log_in(log, drive, blk, xfer):
    req = bytearray(blk.get(26))
    if req[2] in (0x03, 0x0C):
        req[14:18] = b'\xAA' * 4            # far transfer pointer: not comparable
    xin = b''
    if xfer is not None:
        xin = xfer.get(XLEN.get((req[2], xfer.get(1)[0]), 2))
    ent = {'drive': drive, 'req': bytes(req), 'xin': xin}
    log.append(ent)
    return ent


def log_out(ent, blk, xfer):
    req = bytearray(blk.get(26))
    if ent['req'][2] in (0x03, 0x0C):
        req[14:18] = b'\xAA' * 4
    ent['out'] = bytes(req)
    ent['xout'] = xfer.get(len(ent['xin'])) if xfer is not None else b''


def fmt(ent):
    return (f"drive {ent['drive']} req {ent['req'].hex()} x {ent['xin'].hex()} -> "
            f"{ent.get('out', b'').hex()} x {ent.get('xout', b'').hex()}")


class Mscdex:
    """MSCDEX + a one-unit CD-ROM driver with an audio drive.

    present       INT 2Fh AX=15xxh is answered at all
    drive         drive index of the CD (CX of 1500h)
    disc          Disc or None (drive empty: every request -> not ready)
    like_cdrom_c  a request for another drive gets status 8101h written
                  (src/platform/cdrom.c); MSCDEX itself returns CF/AX=000Fh
                  and leaves the request header alone (RBIL INT 2F/AX=1510h)
    faults        one entry is consumed per device request: None = normal, or
                  {'st': status word or None (not written), 'x': (offset, bytes)
                  to write into the control block of an IOCTL input (cut
                  at the block's size) or None, 'run': bool - do the normal
                  work first and only replace the status}
    """

    def __init__(self, disc=None, present=True, drive=CD_DRIVE, like_cdrom_c=False):
        self.disc, self.present, self.drive, self.like_cdrom_c = disc, present, drive, like_cdrom_c
        self.state, self.pos, self.end = 'stopped', 0, 0
        self.log, self.faults = [], []

    # ---- INT 2Fh AX=1500h ----------------------------------------------------
    def install_check(self, bx, cx):
        return (1, self.drive) if self.present else (bx, cx)

    # ---- INT 2Fh AX=1505h ----------------------------------------------------
    def read_vtoc(self, drive, index):
        """(ok, AX): the volume descriptor at sector 16 + index"""
        if drive != self.drive:
            return False, 0x000F
        if self.disc is None:
            return False, 0x0015
        lba = 16 + index
        t = self.disc.track_at(lba)
        if t[2] > lba or lba >= self.disc.end_of(t) or t[1]:
            return False, 0x0015
        return True, 0x0001

    # ---- time ----------------------------------------------------------------
    def advance(self, sectors):
        if self.state == 'playing':
            self.pos = min(self.pos + sectors, self.end)
            if self.pos >= self.end:
                self.state = 'stopped'      # the head stays at the end

    # ---- INT 2Fh AX=1510h ----------------------------------------------------
    def request(self, drive, blk, xfer):
        """True: the driver was called (CF clear); 'invalid': CF set, AX=000Fh;
        False: nobody answered."""
        ent = log_in(self.log, drive, blk, xfer)
        r = self._request(drive, blk, xfer)
        log_out(ent, blk, xfer)
        return r

    def _status(self, blk, st):
        if self.state == 'playing':
            st |= ST_BUSY
        blk.put(3, struct.pack('<H', st))

    def _request(self, drive, blk, xfer):
        if not self.present:
            return False
        fault = self.faults.pop(0) if self.faults else None
        if drive != self.drive:
            if not self.like_cdrom_c:
                return 'invalid'
            self._status(blk, ST_DONE | ST_ERROR | 0x01)
            return True
        if not self.like_cdrom_c:
            blk.put(1, b'\0')               # MSCDEX fills in the subunit
        if fault is not None and not fault.get('run'):
            if fault.get('x') is not None and xfer is not None:
                # never past the control block (it is the caller's stack)
                off, data = fault['x']
                n = XLEN.get((blk.get(3)[2], xfer.get(1)[0]), 0) if blk.get(3)[2] == 0x03 else 0
                xfer.put(off, data[:max(0, n - off)])
            if fault.get('st') is not None:
                blk.put(3, struct.pack('<H', fault['st']))
            return True
        if self.disc is None:
            st = ST_ERROR | 0x02            # drive not ready
        else:
            st = self._command(blk.get(26), xfer)
        if fault is not None:
            if fault.get('st') is not None:
                blk.put(3, struct.pack('<H', fault['st']))
            return True
        self._status(blk, st | ST_DONE)
        return True

    def _command(self, req, xfer):
        d, cmd = self.disc, req[2]
        if cmd == 0x03:                                     # IOCTL input
            code = xfer.get(1)[0]
            if code == 0x08:                                # volume size
                xfer.put(1, struct.pack('<I', d.leadout + 150))
                return 0
            if code == 0x0A:                                # audio disk info
                xfer.put(1, bytes([d.tracks[0][0], d.tracks[-1][0]]) + redbook(d.leadout))
                return 0
            if code == 0x0B:                                # audio track info
                t = d.track(xfer.get(2)[1])
                if t is None:
                    return ST_ERROR | 0x08
                xfer.put(2, redbook(t[2]) + bytes([0x00 if t[1] else 0x40]))
                return 0
            if code == 0x0C:                                # audio Q-channel info
                t = d.track_at(self.pos)
                rel, a = (self.pos - t[2]) & 0xFFFFFFFF, self.pos + 150
                xfer.put(1, bytes([0x01 if t[1] else 0x41, t[0], 1,
                                   (rel // 4500) & 0xFF, rel // 75 % 60, rel % 75, 0,
                                   (a // 4500) & 0xFF, a // 75 % 60, a % 75]))
                return 0
            return ST_ERROR | 0x03
        if cmd == 0x0C:                                     # IOCTL output
            if xfer.get(1)[0] == 0x02:                      # reset drive
                self.state = 'stopped'
                return 0
            return ST_ERROR | 0x03
        if cmd == 0x84:                                     # play audio
            start, count = struct.unpack_from('<II', req, 14)
            if req[13] == 1:                                # Red Book address
                start = (req[16] * 4500 + req[15] * 75 + req[14] - 150) & 0xFFFFFFFF
            if start >= d.leadout:
                return ST_ERROR | 0x08
            if count:
                self.pos, self.end, self.state = start, min(start + count, d.leadout), 'playing'
            return 0
        if cmd == 0x85:                                     # stop: pause, or stop if paused
            self.state = 'paused' if self.state == 'playing' else 'stopped'
            return 0
        if cmd == 0x88:                                     # resume
            if self.state == 'paused':
                self.state = 'playing'
                return 0
            return ST_ERROR | 0x0C
        return ST_ERROR | 0x03


# ============================================================================
# the two sides
# ============================================================================
class EmuMem:
    def __init__(self, e, seg, off):
        self.e, self.seg, self.off = e, seg, off

    def get(self, n):
        return self.e.rd(self.seg, self.off, n, runtime=True)

    def put(self, o, data):
        self.e.wr(self.seg, self.off + o, bytes(data), runtime=True)


class CMem:
    def __init__(self, addr):
        self.addr = addr

    def get(self, n):
        return C.string_at(self.addr, n)

    def put(self, o, data):
        C.memmove(self.addr + o, bytes(data), len(data))


class Orig:
    """The original in Unicorn, INT 2Fh served by self.m (a Mscdex)."""
    DEV_NAME = b'MSCD001 '

    def __init__(self, e=None):
        self.e = e or Emu()
        self.m = None
        self.e.int_hooks = dict(getattr(self.e, 'int_hooks', {}))
        self.e.int_hooks[(0x2F, 0x15)] = self._int2f
        self.buf = self.e.alloc(64)
        self.hdr = self.e.alloc(32)                 # the CD driver's device header
        self.e.wr(self.hdr, 10, self.DEV_NAME, runtime=True)
        self.ints = []
        self.errors = []

    def _int2f(self, e, uc):
        try:
            return self._int2f_body(e, uc)
        except Exception as x:                      # noqa: BLE001
            self.errors.append(repr(x))
            raise

    def _int2f_body(self, e, uc):
        import unicorn.x86_const as X
        ax, bx, cx, dx, es = (e.reg(r) for r in ('AX', 'BX', 'CX', 'DX', 'ES'))
        self.ints.append((ax, cx, dx))
        m, al = self.m, ax & 0xFF
        if al == 0x10:
            blk = EmuMem(e, es, bx)
            req = blk.get(26)
            xfer = None
            if req[2] in (0x03, 0x0C):
                off, seg = struct.unpack_from('<HH', req, 14)
                xfer = EmuMem(e, seg, off)
            r = m.request(cx, blk, xfer)
            if r is True:
                e._cf(uc)
            elif r == 'invalid':
                e._cf(uc, 0x000F)
            return True                             # nobody home: IRET, nothing changed
        if not m.present:
            return True
        if al == 0x00:
            nb, nc = m.install_check(bx, cx)
            uc.reg_write(X.UC_X86_REG_BX, nb)
            uc.reg_write(X.UC_X86_REG_CX, nc)
        elif al == 0x01:                            # 5 bytes per drive: subunit, header far ptr
            e.wr(es, bx, bytes([0]) + struct.pack('<HH', 0, self.hdr), runtime=True)
        elif al == 0x05:
            ok, rax = m.read_vtoc(cx, dx)
            if ok:
                e.wr(es, bx, b'\x01CD001\x01' + bytes(2040), runtime=True)
                uc.reg_write(X.UC_X86_REG_AX, rax)
                e._cf(uc)
            else:
                e._cf(uc, rax)
        else:
            raise AssertionError(f'INT 2Fh AX={ax:04X}h: not an MSCDEX call the module makes')
        return True

    # ---- state ---------------------------------------------------------------
    def set_state(self, blk, active, start):
        self.e.wr(DGSEG, BLK, blk)
        self.e.wr(DGSEG, 0x8F4E, struct.pack('<HI', active, start))
        self.e.wr(DGSEG, 0xE9CD, bytes(4))

    def state(self):
        active, start = struct.unpack('<HI', self.e.dgroup(0x8F4E, 6))
        return self.e.dgroup(BLK, BLK_LEN), active, start

    def poke(self, off, data):
        self.e.wr(DGSEG, off, data)

    # ---- calls ---------------------------------------------------------------
    def op(self, name, *a):
        e = self.e
        e.u.mem_write(0x1000, bytes(0x7000))        # the stack below the frame: zero
        before = e.dgroup(0, 0x10000)
        self.ints = []
        r = self._op(name, *a)
        if self.errors:
            raise AssertionError('INT 2Fh model: ' + self.errors.pop())
        after = e.dgroup(0, 0x10000)
        if name != 'read_cfg':                      # (stdio writes its own variables)
            lo = 0
            for a0, a1 in OWN + ((0x10000, 0x10000),):
                if before[lo:a0] != after[lo:a0]:
                    i = next(i for i in range(lo, a0) if before[i] != after[i])
                    check(False, f'{name}: the original wrote DGROUP {i:04X}')
                lo = a1
        return r

    def _op(self, name, *a):
        e = self.e
        if name == 'msf':
            return e.call(A['msf_to_sectors'], a[0] & 0xFFFF, a[0] >> 16, ret='long')
        if name in ('nop', 'reset', 'detect'):
            e.call(A[name if name == 'nop' else 'cd_' + name])
            return None
        if name in ('init', 'play'):
            return e.call(A['cd_' + name])[0] & 0xFF
        if name in ('stop', 'vtoc_ok', 'read_cfg'):
            return e.call(A['cd_' + name])[0]
        if name in ('status',):
            return e.call(A['cd_status'], ret='long')
        if name == 'track_info':
            return e.call(A['cd_track_info'], a[0], ret='long')
        if name == 'play_track':
            return e.call(A['cd_play_track'], a[0], a[1] & 0xFFFF, (a[1] >> 16) & 0xFFFF)[0]
        if name == 'qchannel':
            e.wr(self.buf, 0, bytes(16), runtime=True)
            al = e.call(A['cd_qchannel'], e.far(self.buf, 0))[0] & 0xFF
            return al, e.rd(self.buf, 0, 16, runtime=True)
        if name == 'ioctl':                         # (fn, count, 16 buffer bytes)
            e.wr(self.buf, 0, a[2], runtime=True)
            al = e.call(A['cd_ioctl'], a[0], a[1], e.far(self.buf, 0))[0] & 0xFF
            return al, e.rd(self.buf, 0, 16, runtime=True)
        if name == 'drive_info':
            ax, dx = e.call(A['cd_drive_info'])
            return (dx, ax) == (DGSEG + LOAD, BLK)  # far pointer to E8CC
        raise KeyError(name)


class Hook:
    """Redirect a C function of the DLL to Python; real() calls the function
    itself (the hook is lifted for the duration)."""

    def __init__(self, name, pyfunc, restype, *argtypes):
        self.addr = lale._fn_addr(name)
        self.cb = C.CFUNCTYPE(restype, *argtypes)(pyfunc)       # kept alive here
        self.orig = C.string_at(self.addr, 12)
        dest = C.cast(self.cb, C.c_void_p).value
        self.jmp = b'\x48\xB8' + dest.to_bytes(8, 'little') + b'\xFF\xE0'
        self.f = fn(name, restype, *argtypes)
        self.active = False

    def on(self):
        lale._write_code(self.addr, self.jmp)
        self.active = True

    def off(self):
        lale._write_code(self.addr, self.orig)
        self.active = False

    def real(self, *a):
        self.off()
        try:
            return self.f(*a)
        finally:
            self.on()


class Port:
    """The C module.  model None: mode A (cdrom.c, requests recorded);
    a Mscdex: mode B (the model answers)."""

    def __init__(self):
        self.model = None
        self.log = []
        self.errors = []
        self.h_send = Hook('mscdex_send_request', self._send, None, u8, vp, vp)
        self.h_check = Hook('mscdex_install_check', self._check, None, C.POINTER(u16), C.POINTER(u16))
        self.blk = c_test_ptr(0)
        self.p_active = C.cast(c_test_ptr(1), C.POINTER(u16))
        self.p_start = C.cast(c_test_ptr(2), C.POINTER(u32))

    def _send(self, drive, req, xfer):
        try:
            blk, x = CMem(req), (CMem(xfer) if xfer else None)
            if self.model is not None:
                self.model.request(drive, blk, x)
                return
            ent = log_in(self.log, drive, blk, x)
            self.h_send.real(drive, req, xfer)
            log_out(ent, blk, x)
        except Exception as x:                      # noqa: BLE001
            self.errors.append(repr(x))

    def _check(self, bx, cx):
        try:
            if self.model is not None:
                bx[0], cx[0] = self.model.install_check(bx[0], cx[0])
            else:
                self.h_check.real(bx, cx)
        except Exception as x:                      # noqa: BLE001
            self.errors.append(repr(x))

    def use(self, model):
        self.model = model
        self.log = model.log if model is not None else []

    def on(self):
        self.h_send.on()
        self.h_check.on()

    def off(self):
        self.h_send.off()
        self.h_check.off()

    # ---- state ---------------------------------------------------------------
    def set_state(self, blk, active, start):
        C.memmove(self.blk, blk, BLK_LEN)
        self.p_active[0] = active
        self.p_start[0] = start

    def state(self):
        return C.string_at(self.blk, BLK_LEN), self.p_active[0], self.p_start[0]

    def poke(self, off, data):
        C.memmove(self.blk + off - BLK, bytes(data), len(data))

    # ---- calls ---------------------------------------------------------------
    def op(self, name, *a):
        r = self._op(name, *a)
        if self.errors:
            raise AssertionError('port hook: ' + self.errors.pop())
        return r

    def _op(self, name, *a):
        if name == 'msf':
            return c_msf(a[0])
        if name in ('nop', 'reset', 'detect'):
            {'nop': c_nop, 'reset': c_reset, 'detect': c_detect}[name]()
            return None
        if name == 'init':
            return c_init()
        if name == 'play':
            return c_play()
        if name == 'stop':
            return c_stop() & 0xFFFF
        if name == 'vtoc_ok':
            return c_vtoc_ok() & 0xFFFF
        if name == 'read_cfg':
            return c_read_cfg() & 0xFFFF
        if name == 'status':
            return c_status()
        if name == 'track_info':
            return c_track_info(a[0])
        if name == 'play_track':
            v = a[1] & 0xFFFFFFFF
            return c_play_track(a[0], v - (1 << 32) if v & 0x80000000 else v) & 0xFFFF
        if name == 'qchannel':
            b = C.create_string_buffer(16)
            al = c_qchannel(b)
            return al, b.raw
        if name == 'ioctl':
            b = C.create_string_buffer(bytes(a[2]), 16)
            al = c_ioctl(a[0], a[1], b)
            return al, b.raw
        if name == 'drive_info':
            return c_drive_info() == self.blk
        raise KeyError(name)


_orig = None
_port = None


def sides():
    global _orig, _port
    if _orig is None:
        _orig, _port = Orig(), Port()
    return _orig, _port


# INT 2Fh functions each routine must call (AX values); None = see compare()
INTS = {'msf': [], 'nop': [], 'reset': [0x1510], 'detect': [0x1500, 0x1510],
        'init': [0x1510, 0x1500, 0x1510], 'qchannel': [0x1510], 'track_info': [0x1510],
        'status': [0x1510], 'ioctl': [0x1510], 'play': [0x1510], 'vtoc_ok': [0x1505],
        'drive_info': [0x1501], 'stop': None, 'play_track': None}


def c_advance(sectors):
    buf = (C.c_int16 * (588 * 2 * 256))()
    while sectors:
        n = min(sectors, 256)
        cdrom_render(buf, 588 * n)
        sectors -= n


def compare(label, ops, mo, mp=None, state=None):
    """Run ops on both sides and compare after each one.  mo: the original's
    Mscdex; mp: the port's (mode B) or None (mode A: cdrom.c as set up by the
    caller).  state: (block bytes, active, start) both sides start from.
    Returns the original's results, None where the comparison stopped."""
    o, p = sides()
    o.m = mo
    p.use(mp)
    state = state or (bytes(BLK_LEN), 0, 0)
    o.set_state(*state)
    p.set_state(*state)
    out = []
    p.on()
    try:
        for i, op in enumerate(ops):
            what = f'{label} #{i} {op[0]}{tuple(op[1:])}'
            if op[0] == 'advance':
                mo.advance(op[1])
                mp.advance(op[1]) if mp is not None else c_advance(op[1])
                out.append(None)
                continue
            if op[0] == 'fault':
                mo.faults.extend(op[1])
                mp.faults.extend(dict(f) if f else f for f in op[1])
                out.append(None)
                continue
            if op[0] == 'nofault':                          # drop what was not consumed
                check(len(mo.faults) == len(mp.faults), f'{what}: fault queues {len(mo.faults)}/{len(mp.faults)}')
                del mo.faults[:], mp.faults[:]
                out.append(None)
                continue
            if op[0] == 'poke':
                o.poke(op[1], op[2])
                p.poke(op[1], op[2])
                out.append(None)
                continue
            if op[0] == 'set':                              # ('set', active, start)
                so = o.state()
                o.set_state(so[0], op[1], op[2])
                p.set_state(p.state()[0], op[1], op[2])
                out.append(None)
                continue
            no, npt = len(mo.log), len(p.log)
            ro = o.op(*op)
            rp = p.op(*op)
            out.append(ro)
            ok = check(ro == rp, f'{what}: result orig {ro!r} port {rp!r}')
            lo, lp = mo.log[no:], p.log[npt:]
            if lo != lp:
                ok = False
                k = next((k for k in range(min(len(lo), len(lp))) if lo[k] != lp[k]), None)
                if k is None:
                    check(False, f'{what}: {len(lo)} requests in the original, {len(lp)} in the port')
                else:
                    check(False, f'{what}: request {k}\n        orig {fmt(lo[k])}\n        port {fmt(lp[k])}')
            ints = [x[0] for x in o.ints]
            want = INTS[op[0]] if INTS.get(op[0]) is not None else [0x1510] * len(lo)
            ok &= check(ints == want, f'{what}: INT 2Fh calls {[hex(x) for x in ints]}')
            (bo, ao, so), (bp, ap, sp) = o.state(), p.state()
            ok &= check((ao, so) == (ap, sp), f'{what}: active/start orig {ao},{so:#x} port {ap},{sp:#x}')
            if mo.log and mo.log[-1]['req'][2] in (0x03, 0x0C):
                lo4 = REQ + 14 - BLK
                bo = bo[:lo4] + bytes(4) + bo[lo4 + 4:]
                bp = bp[:lo4] + bytes(4) + bp[lo4 + 4:]
            if bo != bp:
                ok = False
                k = next(k for k in range(BLK_LEN) if bo[k] != bp[k])
                check(False, f'{what}: DGROUP {BLK + k:04X}: orig {bo[k:k + 8].hex()} port {bp[k:k + 8].hex()}')
            if mp is None and mo.present and mo.disc is not None:
                mine = (mo.pos, mo.state == 'playing')
                theirs = (cdrom_position(), bool(cdrom_playing()))
                ok &= check(mine == theirs, f'{what}: drive at {mine} in the model, {theirs} in cdrom.c')
            if not ok:
                out.extend([None] * (len(ops) - len(out)))
                break
    finally:
        p.off()
    return out


# ============================================================================
# disc images for cdrom.c
# ============================================================================
_tmp = None


def _cleanup():
    cdrom_close()
    if _tmp:
        shutil.rmtree(_tmp, ignore_errors=True)


atexit.register(_cleanup)


def write_image(name, spec):
    """cue + one zero-filled .bin per track; spec: [(audio, sectors)].
    Returns the cue path (data tracks MODE1/2352)."""
    global _tmp
    if _tmp is None:
        _tmp = tempfile.mkdtemp(prefix='lale_cd5737_')
    lines = []
    for n, (audio, sectors) in enumerate(spec, 1):
        f = f'{name}_{n:02d}.bin'
        with open(os.path.join(_tmp, f), 'wb') as fh:
            fh.truncate(sectors * 2352)
        lines.append(f'FILE "{f}" BINARY\n  TRACK {n:02d} {"AUDIO" if audio else "MODE1/2352"}\n'
                     '    INDEX 01 00:00:00\n')
    cue = os.path.join(_tmp, name + '.cue')
    with open(cue, 'w') as fh:
        fh.write(''.join(lines))
    return cue


def cdsim_disc():
    """The disc of tools/cdsim.py (redump 91232 layout): (spec, cue).  Uses
    the built image in build/cd when it is there, else zero-filled files."""
    import cdsim
    spec = [(t['type'] == 'AUDIO', t['sectors']) for t in cdsim.REDUMP['tracks']]
    cue = os.path.join(ROOT, 'build', 'cd', cdsim.BASE + '.cue')
    ok = os.path.isfile(cue)
    if ok:
        files = cdsim.parse_cue(cue)
        ok = (len(files) == len(spec) and
              all(os.path.isfile(f) and os.path.getsize(f) == n * 2352 and (ty == 'AUDIO') == a
                  for (f, ty), (a, n) in zip(files, spec)))
    if not ok:
        cue = write_image('cdsim', spec)
    return spec, cue


def mount(cue, present=True):
    """mode A: the virtual drive of the port"""
    cdrom_close()
    mscdex_set_present(1 if present else 0)
    if cue is not None:
        check(cdrom_open(cue.encode('mbcs')) == 0, f'cdrom_open {cue}: {cdrom_error()}')


def model_a(spec, present=True):
    return Mscdex(Disc.linear(spec) if spec else None, present=present)


def unmount():
    cdrom_close()
    mscdex_set_present(0)


def rnd_block(rnd, drive=CD_DRIVE, subunit=0):
    b = bytearray(rnd.randrange(256) for _ in range(BLK_LEN))
    b[0xE9B9 - BLK] = drive
    b[0xE9BA - BLK] = subunit
    return bytes(b)


# ============================================================================
# tests
# ============================================================================
def test_msf_to_sectors():
    o, p = sides()
    o.m = Mscdex(None, present=False)
    rnd = random.Random(5737)
    vals = [0, 0x000200, 0x010000, 0x00FFFFFF, 0xFFFFFFFF, 0xFF000000, 0x00000095, 0x00000096,
            0x00000097, 0x00FF0000, 0x0000FF00, 0x000000FF, 0x003B3B4A, 0x12345678, 0x80000000]
    vals += [rnd.getrandbits(32) for _ in range(400)]
    vals += [rnd.getrandbits(24) for _ in range(200)]
    for v in vals:
        ro, rp = o.op('msf', v), p.op('msf', v)
        m, s, f = (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF
        if not check(ro == rp == (m * 4500 + s * 75 + f - 150) & 0xFFFFFFFF,
                     f'msf_to_sectors({v:08X}): orig {ro:08X} port {rp:08X}'):
            break


def test_inactive_module():
    """The state of the real game: o5737_cd_init never ran."""
    spec, cue = cdsim_disc()
    for present in (True, False):
        mount(cue, present)
        ops = [('nop',), ('stop',), ('play_track', 2, -1), ('play_track', 0, -1), ('play_track', 3, 100),
               ('play',), ('qchannel',), ('track_info', 2), ('status',), ('vtoc_ok',), ('stop',),
               ('reset',), ('play',)]
        r = compare(f'inactive present={present}', ops, model_a(spec, present))
        check(r[1] == 0 and r[2] == 0 and r[3] == 0 and r[4] == 0, f'inactive: stop/play_track return 0: {r}')
    unmount()


def test_cdsim_disc_game_flow():
    """init, then what the game does: track 2 in the menu (polled, replayed
    with track 0), track 3 in the story, stop - on the cdsim/redump layout."""
    spec, cue = cdsim_disc()
    t2, t3 = spec[0][1], spec[0][1] + spec[1][1]
    n2, n3 = spec[1][1], spec[2][1]
    mount(cue)
    mo = model_a(spec)
    ops = [('vtoc_ok',), ('init',), ('vtoc_ok',), ('drive_info',),
           ('play_track', 2, -1), ('play',), ('advance', 1), ('play',), ('advance', n2 - 7), ('play',),
           ('advance', 1), ('play',), ('advance', 1), ('play',), ('advance', 1), ('play',),
           ('advance', 3), ('play',), ('advance', 50), ('play',),
           ('play_track', 0, -1), ('play',), ('advance', 10), ('play',), ('qchannel',),
           ('stop',), ('play',), ('stop',), ('play',), ('play_track', 0, -1), ('advance', 5), ('play',),
           ('play_track', 3, -1), ('advance', 1), ('play',), ('advance', n3 - 6), ('play',),
           ('advance', 1), ('play',), ('advance', 100), ('play',), ('play_track', 0, -1), ('play',),
           ('advance', 77), ('play',), ('status',), ('track_info', 1), ('track_info', 2),
           ('track_info', 3), ('track_info', 0), ('track_info', 4),
           ('play_track', 3, 100), ('advance', 99), ('play',), ('advance', 1), ('play',),
           ('play_track', 2, 0), ('play',), ('play_track', 2, 0x12345), ('advance', 3), ('play',),
           ('play_track', 2, 1), ('play_track', 2, 5), ('advance', 1), ('play',), ('advance', 1), ('play',),
           ('play_track', 1, -1), ('advance', 2), ('play',), ('play_track', 1, 300),
           ('play_track', 4, -1), ('play_track', 9, 5), ('play_track', 255, -1), ('play_track', 0, -1),
           ('play_track', 2, 0x7FFFFFFF), ('play_track', 3, -0x80000000), ('play_track', 2, -2),
           ('reset',), ('play',), ('detect',), ('play_track', 2, -1), ('advance', 20), ('init',),
           ('play',), ('stop',), ('stop',), ('nop',)]
    r = compare('cdsim', ops, mo)
    check(r[0] == 0 and r[2] == 1, f'vtoc_ok: 0 before detect (drive A:), 1 after: {r[0]}, {r[2]}')
    blk = _port.state()[0]
    check(blk[0xE9C7 - BLK:0xE9CD - BLK] == bytes([1, 3]) + redbook(sum(n for _, n in spec)),
          'disk info at E9C7')
    check(blk[0xE9B6 - BLK:0xE9BA - BLK] == bytes([1, 0, ord('D'), CD_DRIVE]), 'E9B6..E9B9 after detect')
    # the play requests of the first two play_track calls, spelled out
    plays = [x for x in mo.log if x['req'][2] == 0x84]
    want = bytes([0x16, 0, 0x84]) + bytes(11) + struct.pack('<II', t2, n2) + bytes(4)
    check(plays[0]['req'] == want, f'play request track 2: {plays[0]["req"].hex()}')
    check(plays[0]['out'][3:5] == b'\x00\x03', 'play: done + busy')
    k = [i for i, x in enumerate(mo.log) if x['req'][2] == 0x84 and
         struct.unpack_from('<I', x['req'], 14)[0] == t3][0]
    check(struct.unpack_from('<II', mo.log[k]['req'], 14) == (t3, n3), 'play request track 3 (last track)')
    check(mo.log[k - 1]['xin'][0] == 0x08, 'last track: length from IOCTL 08h (volume size)')
    unmount()


def test_no_mscdex():
    """INT 2Fh unanswered: every routine still runs through."""
    mount(None, present=False)
    ops = [('vtoc_ok',), ('init',), ('vtoc_ok',), ('play_track', 2, -1), ('play',), ('stop',),
           ('play_track', 0, -1), ('play_track', 3, 5), ('play_track', 2, -1), ('track_info', 2),
           ('status',), ('qchannel',), ('reset',), ('detect',), ('stop',),
           ('ioctl', 0x4402, 6, bytes([0x0A]) + bytes(range(1, 16))), ('play',)]
    r = compare('no mscdex', ops, model_a(None, present=False))
    check(r[0] == 1 and r[2] == 1, f'vtoc_ok without MSCDEX is 1 in the original: {r[0]}, {r[2]}')
    check(r[3] == 1 and r[5] == 1, 'play_track/stop report success when nothing answers')
    rnd = random.Random(1)
    compare('no mscdex, stale block', ops, model_a(None, present=False), state=(rnd_block(rnd, 7, 9), 1, 0x1234567))
    unmount()


def test_no_disc():
    """MSCDEX and the drive are there, the tray is empty."""
    mount(None, present=True)
    ops = [('vtoc_ok',), ('init',), ('vtoc_ok',), ('play_track', 2, -1), ('play',), ('stop',),
           ('play_track', 0, -1), ('play_track', 3, 5), ('track_info', 2), ('status',), ('qchannel',),
           ('reset',), ('detect',), ('stop',), ('play',), ('play_track', 1, 0x1000)]
    r = compare('no disc', ops, model_a(None))
    check(r[0] == 0 and r[2] == 0 and r[3] == 0 and r[5] == 0, f'no disc: everything fails: {r}')
    unmount()


def test_disc_variants():
    """Data-only, audio-only, one track, many tracks: fixed walk over every
    track plus the numbers around the range."""
    rnd = random.Random(42)
    discs = {'data only': [(False, 400)],
             'audio only': [(True, 300), (True, 200)],
             'one audio': [(True, 500)],
             'tiny data': [(False, 10), (True, 100)],
             'mixed': [(False, 320), (True, 180), (True, 240), (False, 50), (True, 75)],
             'many': [(i % 5 != 0, rnd.randrange(20, 120)) for i in range(24)]}
    for name, spec in discs.items():
        mount(write_image(name.replace(' ', '_'), spec))
        n = len(spec)
        ops = [('vtoc_ok',), ('init',), ('vtoc_ok',)]
        for t in list(range(0, n + 3)) + [99, 100, 255]:
            ops += [('play_track', t, -1), ('play',), ('advance', 2), ('play',), ('track_info', t),
                    ('advance', 1000), ('play',), ('play_track', 0, -1), ('advance', 1), ('play',), ('stop',)]
        ops += [('status',), ('qchannel',), ('stop',), ('stop',), ('reset',)]
        r = compare(name, ops, model_a(spec))
        want = 0 if name in ('audio only', 'one audio', 'tiny data') else 1
        check(r[0] == 0 and r[2] == want, f'{name}: vtoc_ok {r[0]}, {r[2]} (wanted 0, {want})')
    unmount()


def test_vtoc_ok():
    """INT 2Fh AX=1505h with CX = E9B9, DX = 0; result = carry clear."""
    spec, cue = cdsim_disc()
    o, _ = sides()
    for present in (True, False):
        for disc in (True, False):
            mount(cue if disc else None, present)
            for drive in (0, CD_DRIVE, 4, 0xFF):
                b = bytearray(BLK_LEN)
                b[0xE9B9 - BLK] = drive
                r = compare(f'vtoc present={present} disc={disc} drive={drive}',
                            [('vtoc_ok',), ('vtoc_ok',)], model_a(spec if disc else None, present),
                            state=(bytes(b), 0, 0))
                want = 1 if not present else int(disc and drive == CD_DRIVE)
                check(r[0] == want and r[1] == want, f'vtoc_ok present={present} disc={disc} drive={drive}: {r}')
                check(o.ints and o.ints[0][1:] == (drive, 0), f'1505h: CX, DX = {o.ints}')
    unmount()


def test_drive_info():
    """INT 2Fh AX=1501h: subunit -> E9BA, the driver's name -> E8CC."""
    spec, cue = cdsim_disc()
    mount(cue)
    rnd = random.Random(3)
    for k in range(3):
        st = (rnd_block(rnd, CD_DRIVE, 5), k & 1, rnd.getrandbits(32)) if k else None
        r = compare(f'drive_info {k}', [('drive_info',), ('drive_info',)], model_a(spec), state=st)
        check(r[0] is True, 'drive_info returns the far pointer 16E0:E8CC')
        blk = _port.state()[0]
        check(blk[:8] == b'MSCD001 ' and blk[0xE9BA - BLK] == 0, 'E8CC name / E9BA subunit')
    unmount()


def test_read_cfg():
    """fopen(E8D8, "rt") -> (char)fp->fd or 0; on the Borland stdio of both
    sides (helpers.DosEmu / src/rtl/bc_stdio.c), game folder orig/."""
    root = os.path.join(ROOT, 'orig')
    names = [b'\0' * 8, b'SES.CFG\0', b'IST.EXE\0', b'NOPE.CFG', b'3D.CKD\0\0', b'ses.cfg\0', b'A\0CDEFGH']
    for name in names:
        e = DosEmu(root)
        e.stub_heap()
        fn('dg_reset', None)()
        fn('bc_stdio_reset', None)()
        fn('pc_set_game_dir', None, C.c_char_p)(root.encode('mbcs'))
        run_setupio(e)
        fn('bc_setupio', None)()
        o, p = Orig(e), _port or sides()[1]
        o.m = Mscdex(None, present=False)
        blk = bytearray(BLK_LEN)
        blk[0xE8D8 - BLK:0xE8E0 - BLK] = name
        o.set_state(bytes(blk), 0, 0)
        p.set_state(bytes(blk), 0, 0)
        for k in range(3):                      # three opens: three handles
            ro, rp = o.op('read_cfg'), p.op('read_cfg')
            check(ro == rp, f'read_cfg {name!r} #{k}: orig {ro:#x} port {rp:#x}')
            check(o.ints == [], 'read_cfg: no INT 2Fh')
            far = struct.unpack('<HH', e.dgroup(0xE9CD, 4))
            check((far != (0, 0)) == bool(c_test_ptr(3)), f'read_cfg {name!r}: E9CD {far} vs port')
            if far != (0, 0):                   # the FILE it points to, stream by stream
                idx = (far[0] - 0x929E) // 20
                check(far[1] == DGSEG + LOAD and c_test_ptr(3) == C.addressof(DG) + far[0],
                      f'read_cfg {name!r}: FILE* is _streams[{idx}] on both sides')
            for s in range(20):
                a = e.dgroup(0x929E + 20 * s, 20)
                b = bytes(DG[0x929E + 20 * s:0x929E + 20 * s + 20])
                check(a[:8] + a[16:] == b[:8] + b[16:], f'read_cfg {name!r}: _streams[{s}] {a.hex()} vs {b.hex()}')
            check(o.state() == p.state(), f'read_cfg {name!r}: module state')
        e.close_all()
        fn('bc_stdio_reset', None)()
    fn('dg_reset', None)()


def _pair(disc, **kw):
    return Mscdex(disc, **kw), Mscdex(disc, **kw)


def test_status_handling():
    """stop / play_track: E9B2 = sign-extended (status lo | hi) << 8, result
    = bit 15 of that clear - for every kind of status word (busy, done,
    error, error code >= 80h without the error bit), and for a status that is
    never written."""
    disc = Disc.linear([(False, 1000), (True, 500), (True, 700)])
    bytes_ = [0x00, 0x01, 0x02, 0x03, 0x08, 0x40, 0x7F, 0x80, 0x81, 0x83, 0xC0, 0xFF]
    words = [lo | hi << 8 for lo in bytes_ for hi in bytes_]
    ops = [('init',)]
    for w in words:
        ops += [('fault', [{'st': w}]), ('stop',),
                ('fault', [{'st': w}]), ('play_track', 0, -1),
                ('fault', [None, None, {'st': w, 'run': True}]), ('play_track', 2, -1),
                ('fault', [None, None, {'st': w}]), ('play_track', 3, -1)]
    ops += [('fault', [{'st': None}]), ('stop',), ('fault', [{'st': None}]), ('play_track', 0, -1),
            ('stop',), ('play_track', 2, -1), ('stop',)]
    mo, mp = _pair(disc)
    r = compare('status', ops, mo, mp)
    for k, w in enumerate(words):
        want = 0 if (w | w >> 8) & 0x80 else 1
        got = r[1 + 8 * k + 1], r[1 + 8 * k + 3], r[1 + 8 * k + 5], r[1 + 8 * k + 7]
        if not check(got == (want,) * 4, f'status {w:04X}: results {got}, wanted {want}'):
            break


def test_driver_faults():
    """The driver fails single requests inside each routine: the transfer
    buffer stays as it was (uninitialised stack in the original - including
    what the previous call of the same o5737_cd_play_track left there)."""
    disc = Disc.linear([(False, 1000), (True, 500), (True, 700), (True, 333)])
    err = {'st': 0x8102}
    cases = []
    for track in (1, 2, 3, 4, 5):
        for length in (-1, 50):
            for bad in ([err], [None, err], [None, None, err], [err, err], [err, None, None],
                        [None, {'st': None}], [{'st': 0x8103}, {'st': 0x8108}, {'st': 0x810C}],
                        [None, {'st': 0x0100, 'x': (2, b'\x11\x22\x33\x44\x55')}],
                        [{'st': 0x0100, 'x': (1, b'\xF1\xF2\xF3\xF4\xF5\xF6')}, err]):
                cases.append([('fault', bad), ('play_track', track, length), ('nofault',),
                              ('play',), ('stop',)])
    ops = [('init',)] + [x for c in cases for x in c]
    # play / qchannel / track_info / status / detect / init with a failing driver
    ops += [('play_track', 2, -1), ('advance', 10),
            ('fault', [err]), ('play',), ('fault', [{'st': None}]), ('play',), ('play',),
            ('fault', [err]), ('qchannel',), ('fault', [err]), ('track_info', 2),
            ('fault', [err]), ('status',), ('fault', [err]), ('detect',),
            ('fault', [err, err]), ('init',), ('fault', [err, None]), ('init',),
            ('fault', [None, err]), ('init',), ('fault', [err]), ('reset',), ('nofault',),
            ('play_track', 3, -1), ('play',)]
    mo, mp = _pair(disc)
    compare('faults', ops, mo, mp)


def test_play_window():
    """o5737_cd_play: 1 while start < head < start + length - 4 (unsigned 32
    bit, both comparisons word by word), for head positions the Q channel
    can report and start/length values around them."""
    disc = Disc.linear([(True, 1000)])
    rnd = random.Random(7)
    ops = []

    def case(start, length, m, s, f):
        q = bytes([0x01, 1, 1, 0, 0, 0, 0, m, s, f])
        ops.extend([('set', 1, start & 0xFFFFFFFF), ('poke', 0xE8D4, struct.pack('<I', length & 0xFFFFFFFF)),
                    ('fault', [{'st': 0x0100, 'x': (1, q)}]), ('play',)])

    def pos(m, s, f):
        return (m * 4500 + s * 75 + f - 150) & 0xFFFFFFFF
    for m, s, f in ((0, 2, 0), (0, 0, 0), (0, 1, 74), (14, 33, 44), (14, 33, 41), (14, 33, 42),
                    (255, 255, 255), (72, 10, 3), (29, 7, 38), (29, 7, 37), (58, 15, 61), (0, 2, 1)):
        x = pos(m, s, f)
        for start in (x - 1, x, x + 1, x - 0x10000, x + 0x10000, (x & 0xFFFF0000) - 1, x & 0xFFFF0000,
                      (x | 0xFFFF) + 1, 0, 0xFFFFFFFF, x - 100):
            for end in (x - 1, x, x + 1, x + 2, x + 0x10000, x - 0x10000, (x | 0xFFFF) + 1, x | 0xFFFF,
                        x & 0xFFFF0000, 0, 3, 4, 5, 0xFFFFFFFF, start, start + 1):
                case(start, end + 4 - start, m, s, f)
    for _ in range(600):
        m, s, f = (rnd.randrange(256) for _ in range(3))
        x = pos(m, s, f)
        start = rnd.choice([x - rnd.randrange(4), rnd.getrandbits(32), rnd.getrandbits(20), x - rnd.randrange(70000)])
        length = rnd.choice([rnd.randrange(12), rnd.getrandbits(32), rnd.getrandbits(18), x - start + rnd.randrange(8)])
        case(start, length, m, s, f)
    mo, mp = _pair(disc)
    blk = bytearray(BLK_LEN)
    blk[0xE9B9 - BLK] = CD_DRIVE
    r = compare('window', ops, mo, mp, state=(bytes(blk), 1, 0))
    seen = set()
    for k in range(0, len(ops), 4):
        if r[k + 3] is None:
            break
        start = ops[k][2]
        length = struct.unpack('<I', ops[k + 1][2])[0]
        q = ops[k + 2][1][0]['x'][1]
        x = pos(q[7], q[8], q[9])
        end = (start + length - 4) & 0xFFFFFFFF
        want = 1 if start < x < end else 0
        seen.add(want)
        if not check(r[k + 3] == want, f'play start {start:#x} len {length:#x} head {x:#x}: {r[k + 3]}'):
            break
    check(seen == {0, 1}, 'both results of o5737_cd_play reached')


def test_play_track_lengths():
    """Length rules (given, to the next track, to the end of the disc) with
    lengths and track numbers at the edges, E9C8 (highest track) forced, and
    discs whose addresses use the whole Red Book range."""
    rnd = random.Random(11)
    big = Disc([(1, False, 0), (2, True, 4500 * 20 - 150), (3, True, 4500 * 200 + 75 * 59 + 74 - 150),
                (7, True, 4500 * 255 + 75 * 59 + 74 - 150), (8, True, 4500 * 300)], 4500 * 400)
    odd = Disc([(5, True, 0), (6, True, 1), (9, True, 149), (10, True, 150), (254, True, 400),
                (255, True, 500)], 600)
    lens = [-1, 0, 1, 2, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x7FFFFFFF, -2, -0x8000, -0x10000, -0x80000000,
            0x00018000, 150, 149]
    for name, disc in (('big', big), ('odd', odd)):
        nums = sorted({t[0] for t in disc.tracks} | {0, 1, 2, 4, 11, 100, 253, 254, 255})
        ops = [('init',)]
        for t in nums:
            for ln in lens:
                ops += [('play_track', t, ln), ('play',), ('advance', rnd.randrange(4)), ('play',)]
        for hi in (0, 1, 2, 3, 6, 7, 8, 9, 200, 254, 255):       # E9C8 forced
            ops.append(('poke', 0xE9C8, bytes([hi])))
            for t in nums:
                ops += [('play_track', t, -1), ('play_track', 0, 77), ('play_track', 0, -1)]
        for sub in (1, 0x80, 0xFF):                               # E9BA -> request byte 1
            ops += [('poke', 0xE9BA, bytes([sub])), ('play_track', 2, -1), ('stop',), ('status',), ('play',)]
        for drv in (0, 2, 4, 0xFF):                               # E9B9: not the CD drive
            ops += [('poke', 0xE9B9, bytes([drv])), ('play_track', 2, -1), ('play_track', 5, 9), ('stop',),
                    ('play',), ('reset',), ('track_info', 2), ('status',)]
        mo, mp = _pair(disc)
        compare(f'lengths {name}', ops, mo, mp)
        mo, mp = _pair(disc)
        compare(f'lengths {name}, stale block', ops, mo, mp, state=(rnd_block(rnd), 0, rnd.getrandbits(32)))


def test_ioctl_direct():
    """o5737_cd_ioctl with every kind of fn / count / control code: which
    header bytes it writes (0-2, 13-19) and which it leaves (stale bytes of
    the previous request), on the model and on cdrom.c."""
    spec, cue = cdsim_disc()
    rnd = random.Random(9)

    def ops_for(fns):
        ops = [('init',), ('play_track', 2, -1), ('advance', 30)]
        for f in fns:
            for code in (0x00, 0x01, 0x02, 0x08, 0x0A, 0x0B, 0x0C, 0x0D, 0xFF):
                buf = bytes([code, rnd.choice([0, 1, 2, 3, 4, 9])]) + bytes(rnd.randrange(256) for _ in range(14))
                ops.append(('ioctl', f, rnd.choice([0, 1, 4, 6, 10, 0x1234, 0xFFFF]), buf))
            ops += [('play_track', 0, -1), ('stop',)]
        return ops
    fns = (0x4402, 0x4403, 0x4400, 0x0000, 0xFFFF, 0x4402, 0x0244)
    mount(cue)
    r = compare('ioctl cdrom.c', ops_for(fns), model_a(spec), state=(rnd_block(rnd, 0, 0), 0, 0))
    check(all(x is None or not isinstance(x, tuple) or x[0] == 1 for x in r), 'ioctl always returns 1')
    unmount()
    mo, mp = _pair(Disc.linear(spec))
    compare('ioctl model', ops_for(fns), mo, mp, state=(rnd_block(rnd, CD_DRIVE, 0x33), 0, 0))
    mo, mp = _pair(None, present=False)
    compare('ioctl nobody', ops_for(fns), mo, mp, state=(rnd_block(rnd, CD_DRIVE, 0x33), 0, 0))


def _random_ops(rnd, n, ntracks, faults):
    ops = []
    for _ in range(n):
        k = rnd.random()
        if faults and rnd.random() < 0.25:
            ops.append(('fault', [rnd.choice([None, {'st': rnd.getrandbits(16)}, {'st': 0x8102}, {'st': None},
                                              {'st': rnd.choice([0x0100, 0x0300]), 'run': True},
                                              {'st': 0x0100, 'x': (rnd.randrange(1, 3),
                                                                   bytes(rnd.randrange(256) for _ in range(9)))}])
                                  for _ in range(rnd.randrange(1, 4))]))
        if k < 0.30:
            ops.append(('play_track', rnd.choice([0, 0] + list(range(0, ntracks + 3)) + [255]),
                        rnd.choice([-1, -1, -1, 0, 1, 5, rnd.randrange(1, 3000), rnd.getrandbits(32) - (1 << 31)])))
        elif k < 0.50:
            ops.append(('play',))
        elif k < 0.65:
            ops.append(('advance', rnd.choice([1, 1, 2, 3, 5, 50, 500, 5000])))
        elif k < 0.75:
            ops.append(('stop',))
        elif k < 0.80:
            ops.append(('init',))
        elif k < 0.84:
            ops.append(('detect',))
        elif k < 0.87:
            ops.append(('reset',))
        elif k < 0.91:
            ops.append(('track_info', rnd.randrange(0, ntracks + 3)))
        elif k < 0.94:
            ops.append(('status',))
        elif k < 0.97:
            ops.append(('qchannel',))
        elif k < 0.98:
            ops.append(('drive_info',))
        else:
            ops.append(('nop',))
        if faults:
            ops.append(('nofault',))
    return ops


def test_random_sequences_cdrom_c():
    """Random call sequences on random discs through the real cdrom.c."""
    rnd = random.Random(2026)
    for k in range(6):
        spec = [(rnd.random() < 0.75 if i else rnd.random() < 0.3, rnd.randrange(17, 700))
                for i in range(rnd.randrange(1, 9))]
        mount(write_image(f'rnd{k}', spec))
        st = (rnd_block(rnd, rnd.choice([0, CD_DRIVE]), 0), rnd.randrange(2), rnd.getrandbits(12))
        ops = _random_ops(rnd, 250, len(spec), False)
        compare(f'random disc {k} {spec}', ops, model_a(spec), state=st)
    unmount()


def test_random_sequences_model():
    """Random call sequences with random driver faults (model on both sides);
    faults a call did not consume are dropped before the next."""
    rnd = random.Random(5737)
    for k in range(8):
        tracks, lba = [], 0
        for n in range(1, rnd.randrange(2, 12)):
            tracks.append((n, rnd.random() < 0.8, lba))
            lba += rnd.choice([rnd.randrange(1, 300), rnd.randrange(1, 400000)])
        disc = Disc(tracks, lba)
        ops = _random_ops(rnd, 300, len(tracks), True)
        mo, mp = _pair(disc)
        st = (rnd_block(rnd, rnd.choice([0, CD_DRIVE, CD_DRIVE]), rnd.choice([0, 0, 7])),
              rnd.randrange(2), rnd.getrandbits(32))
        compare(f'random model {k}', ops, mo, mp, state=st)
