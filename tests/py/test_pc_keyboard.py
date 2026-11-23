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


def test_dos_break():
    """Ctrl-C / Ctrl-S / Ctrl-Break at kbhit() as MS-DOS handles them (pc.c,
    from the published MS-DOS source: STATCHK): kbhit() - INT 21h AH=0Bh -
    reads a ^C at the head of the buffer and issues INT 23h; reads a ^S and
    the character after it (waiting for it) and reports a character; getch()
    - AH=07h - never checks.  Ctrl-Break: the BIOS empties the buffer and
    leaves the word 0000h (bioskey(1) = FFFFh in Borland's RTL), the next
    kbhit() issues INT 23h, once.
    C side only: this is DOS/BIOS behaviour, not game code."""
    HANDLER = ctypes.CFUNCTYPE(None)
    calls = []
    h = HANDLER(lambda: calls.append(1))
    set23 = fn('pc_set_int23', None, ctypes.c_void_p)
    kbhit = fn('pc_kbhit', ctypes.c_int16)
    getch = fn('pc_getch', ctypes.c_int16)
    bioskey = fn('pc_bioskey', ctypes.c_int16, ctypes.c_int16)
    brk = fn('pc_ctrl_break', None)

    def drain():
        while kbhit():
            getch()

    set23(None)
    drain()
    try:
        # no handler: the ^C is read, nothing else happens
        c_push(0x2E03)
        check(kbhit() == 0 and not calls, 'break: ^C without a handler')
        set23(ctypes.cast(h, ctypes.c_void_p))
        # getch() returns a ^C like any character
        c_push(0x2E03)
        check(getch() == 3 and not calls, 'break: getch() must not check for ^C')
        # kbhit() reads it and issues INT 23h; the key behind it stays
        c_push(0x2E03)
        c_push(0x1E61)
        check(kbhit() == 1 and len(calls) == 1, f'break: kbhit() on ^C, calls {len(calls)}')
        check(getch() == 0x61 and kbhit() == 0 and len(calls) == 1, 'break: key after ^C')
        # only the head of the buffer is looked at
        c_push(0x1E61)
        c_push(0x2E03)
        check(kbhit() == 1 and len(calls) == 1 and getch() == 0x61, 'break: ^C second in line')
        check(kbhit() == 0 and len(calls) == 2, 'break: ^C reached')
        # ^S: read together with the next character; "available" is reported
        c_push(0x1F13)
        c_push(0x1E61)
        c_push(0x3062)
        check(kbhit() == 1 and len(calls) == 2, 'break: ^S + key')
        check(getch() == 0x62 and kbhit() == 0, 'break: the key after ^S was taken')
        # ^S then an extended key: the 00h is taken, the scan code remains
        c_push(0x1F13)
        c_push(0x3B00)
        check(kbhit() == 1 and getch() == 0x3B and kbhit() == 0, 'break: ^S + F1')
        # ^S then ^C breaks
        c_push(0x1F13)
        c_push(0x2E03)
        check(kbhit() == 1 and len(calls) == 3, 'break: ^S + ^C')
        check(kbhit() == 0, 'break: nothing left after ^S + ^C')
        # Ctrl-Break
        c_push(0x1E61)
        brk()
        check(bioskey(1) == -1 and len(calls) == 3, 'break: bioskey(1) after Ctrl-Break')
        check(kbhit() == 1 and len(calls) == 4, 'break: kbhit() after Ctrl-Break')
        check(kbhit() == 1 and len(calls) == 4, 'break: only once')
        check(getch() == 0 and getch() == 0 and kbhit() == 0, 'break: the 0000h word')
    finally:
        set23(None)
        drain()
