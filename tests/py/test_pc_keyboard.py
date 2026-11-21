"""pc_bioskey (src/platform/pc.c) against the original's Borland bioskey()
(0000:1F3F) running over a BIOS INT 16h model fed from the same key words."""
import ctypes
from unicorn import x86_const as X
from lale import lib, Emu, NAMES, check, fn

c_bioskey = fn('pc_bioskey', ctypes.c_int16, ctypes.c_int16)
c_push = fn('pc_key_push', None, ctypes.c_uint16)
c_reset = fn('pc_reset', None)


class Bios:
    """INT 16h AH=00h/01h/02h/10h/11h/12h over a key list (IBM BIOS rules)."""
    def __init__(self, keys, shift):
        self.keys, self.shift = list(keys), shift

    def word(self, k, enhanced):
        if not enhanced and (k & 0xFF) == 0xE0 and k >> 8:
            k &= 0xFF00
        return k

    def install(self, e):
        def ret(uc, ax, zf=None):
            uc.reg_write(X.UC_X86_REG_AX, ax & 0xFFFF)
            if zf is not None:
                f = uc.reg_read(X.UC_X86_REG_EFLAGS)
                f = (f | 0x40) if zf else (f & ~0x40)
                uc.reg_write(X.UC_X86_REG_EFLAGS, f)
            return True

        def h(ah):
            def f(emu, uc):
                enh = ah >= 0x10
                if ah & 0xEF == 0:
                    return ret(uc, self.word(self.keys.pop(0), enh))
                if ah & 0xEF == 1:
                    if not self.keys:
                        return ret(uc, uc.reg_read(X.UC_X86_REG_AX), True)
                    return ret(uc, self.word(self.keys[0], enh), False)
                if ah == 0x12:                       # AH = extended shift flags
                    return ret(uc, self.shift)
                return ret(uc, (uc.reg_read(X.UC_X86_REG_AX) & 0xFF00) | self.shift)
            return f
        e.int_hooks = {(0x16, ah): h(ah) for ah in (0, 1, 2, 0x10, 0x11, 0x12)}


def run(keys, cmds, shift=0x05):
    c_reset()
    lib.pc_set_virtual_time(1, 1000)
    for k in keys:
        c_push(k)
    lib.pc_set_shift_flags(shift)
    e = Emu()
    Bios(keys, shift).install(e)
    for i, cmd in enumerate(cmds):
        a = e.call(NAMES['bioskey'], cmd)[0] & 0xFFFF
        b = c_bioskey(cmd) & 0xFFFF
        if not check(a == b, f'keys {[hex(k) for k in keys]} step {i} bioskey({cmd:#x}): '
                             f'orig {a:04X} C {b:04X}'):
            return


def test_bioskey():
    keys = [0x1C0D, 0x011B, 0x4800, 0x48E0, 0x00E0, 0x0000, 0x1E61, 0x8500]
    seq = [1, 1, 0, 2, 0x12, 1, 0, 0x11, 0x10, 1, 0, 1, 0, 1, 0x11, 0, 1, 0, 0, 1, 1]
    run(keys, seq)
    run(keys[:3], [1, 0, 1, 0, 1, 0, 1, 2])
