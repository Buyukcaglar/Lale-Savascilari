"""GRTEXT.L (ovl5784) against the original: the case helpers over all 256
bytes, text width and drawing on the real fonts (loaded as main loads
them), and the input field with scripted keys (uiworld.py)."""
import ctypes
import random

from lale import NAMES, Emu, check, fn
from uiworld import run, pc_key_push, world  # noqa: F401

C = ctypes
u8, i16, vp, cp = C.c_uint8, C.c_int16, C.c_void_p, C.c_char_p

c_is_upper = fn('grt_is_upper', i16, u8)
c_char_to_ascii = fn('grt_char_to_ascii', u8, u8)
c_inplace = {n: fn(n, None, vp) for n in ('grt_toupper_inplace', 'grt_tolower_inplace',
                                          'grt_strip_accent_inplace')}
c_decode = fn('grt_decode_turkish_lower', vp, vp)
c_width = fn('grt_text_width', i16, vp, u8)
c_draw = fn('grt_draw_text', None, u8, i16, i16, vp, u8, u8)
c_draw_alt = fn('grt_draw_text_alt', None, u8, i16, i16, vp, u8)
c_input = fn('grt_input_line', u8, vp, i16, u8, u8, i16, i16, i16, u8, i16, i16, u8)


def test_case_helpers():
    e = Emu()
    seg = e.alloc(0x200)
    for c in range(256):
        ax = e.call(NAMES['grt_is_upper'], c)[0]
        check(ax == c_is_upper(c), f'is_upper {c:02X}: {ax} vs {c_is_upper(c)}')
        ax = e.call(NAMES['grt_char_to_ascii'], c)[0] & 0xFF
        check(ax == c_char_to_ascii(c), f'char_to_ascii {c:02X}: {ax:02X} vs {c_char_to_ascii(c):02X}')
        for name, f in c_inplace.items():
            e.wr(seg, 0, bytes([c, 0x5A]), runtime=True)
            e.call(NAMES[name], 0, seg)
            b = C.create_string_buffer(bytes([c, 0x5A]), 2)
            f(C.addressof(b))
            o = e.rd(seg, 0, 2, runtime=True)
            check(o == b.raw, f'{name} {c:02X}: {o.hex()} vs {b.raw.hex()}')
    data = bytes(range(1, 256))
    e.wr(seg, 0, data + b'\0', runtime=True)
    ax, dx = e.call(NAMES['grt_decode_turkish_lower'], 0, seg)
    b = C.create_string_buffer(data, 256)
    r = c_decode(C.addressof(b))
    check((ax, dx) == (0, seg) and r == C.addressof(b), 'decode returns s')
    o = e.rd(seg, 0, 256, runtime=True)
    check(o == b.raw, f'decode: {o.hex()} vs {b.raw.hex()}')


STRINGS = [bytes(range(1, 128)), bytes(range(128, 256)), b'Merhaba D\x81nya! \x98stanbul',
           b'\x80\x81\x87\x8d\x94\x98\x99\x9a\x9e\x9f\xa6\xa7*', b'', b'iki\nsatir\xb6uc']


def test_text_width():
    def op(s):
        out = []
        for t in STRINGS:
            for font in (1, 2, 0, 3):
                h, a = s.string(t)
                if s.side == 'o':
                    out.append(s.c('grt_text_width', *a, font)[0])
                else:
                    out.append(c_width(a, font) & 0xFFFF)
        return out
    run('grt_text_width', op)


def draw_ops(s, dst):
    r = random.Random(11)
    lines = [bytes(range(1, 64)), bytes(range(64, 128)), bytes(range(128, 192)),
             bytes(range(192, 256)), b'Ka\x87 alt\x8dn? \x98Y\x98 \x9e\x9f \xa6\xa7\nyeni']
    for i, t in enumerate(lines):
        for big in (5, 0x54):
            h, a = s.string(t)
            x, y, col = r.randrange(0, 20), 8 + 22 * i + (big == 0x54) * 11, r.randrange(256)
            if s.side == 'o':
                s.c('grt_draw_text', dst, x, y, *a, col, big)
            else:
                c_draw(dst, x, y, a, col, big)
    for i, t in enumerate(lines + [b'uzun bir satir ' * 8]):
        h, a = s.string(t)
        x, y, col = r.randrange(0, 40), 130 + 9 * i, r.randrange(256)
        if s.side == 'o':
            s.c('grt_draw_text_alt', dst, x, y, *a, col)
        else:
            c_draw_alt(dst, x, y, a, col)


def test_draw_text():
    run('grt_draw_text screen', lambda s: draw_ops(s, 0))
    run('grt_draw_text EMS surface', lambda s: draw_ops(s, 1), slots=(1,))


def input_op(text, font, maxw, size=40):
    def op(s):
        h, a = s.string(text, size)
        if s.side == 'o':
            r = s.c('grt_input_line', *a, font, 0x0F, 0, maxw, 30, 40, 0, 10, 20, 1)[0] & 0xFF
        else:
            r = c_input(a, font, 0x0F, 0, maxw, 30, 40, 0, 10, 20, 1)
        return r, s.string_read(h, size)
    return op


def test_input_line():
    typed = [0x1E61, 0x2E43, 0x1749, 0x1F53, 0x0E08, 0x4B00, 0x1655, 0x1D47, 0x184F, 0x3920]
    for font in (1, 2):
        run(f'grt_input_line enter font {font}', input_op(b'Ad', font, 200),
            keys=typed + [0x1C0D])
        run(f'grt_input_line esc font {font}', input_op(b'Eski isim', font, 200),
            keys=typed + [0x011B])
        run(f'grt_input_line maxw font {font}', input_op(b'X', font, 30),
            keys=[0x1E61] * 12 + [0x0E08] * 14 + [0x1E61, 0x1C0D])
