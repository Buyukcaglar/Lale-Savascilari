"""
lale.py - shared helpers for the differential tests.

    from lale import lib, emu, check
    lib            ctypes.CDLL of lale_testlib (path from argv / LALE_TESTLIB)
    Emu            tools/emu86.Emu (the original, in Unicorn)
    check(cond, msg)  records a failure without stopping the test

Each test module defines test_* functions; run.py calls them and reports.
"""
import ctypes
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

from emu86 import Emu, EmuError, DGROUP, LOAD  # noqa: E402,F401

_lib_path = os.environ.get('LALE_TESTLIB') or os.path.join(ROOT, 'out', 'windows', 'lale_testlib.dll')
lib = ctypes.CDLL(_lib_path)

NAMES = {}
for line in open(os.path.join(ROOT, 'analysis', 'names.tsv'), encoding='utf-8'):
    f = line.rstrip('\n').split('\t')
    if len(f) >= 2 and ':' in f[0]:
        NAMES[f[1]] = f[0]

failures = []


def check(cond, msg):
    if not cond:
        failures.append(msg)
    return cond


def fn(name, restype, *argtypes):
    """Typed C function from the test library.  'dg_reset' also resets the
    far heap (see _dg_reset)."""
    if name == 'dg_reset':
        return _dg_reset
    f = getattr(lib, name)
    f.restype = restype
    f.argtypes = argtypes
    return f


def _dg_reset():
    """dg_reset() + bc_farheap_reset(): DGROUP back to the image, the far heap
    to its state at main().  The heap's state is partly in DGROUP (_brklvl,
    _heaptop ...), so a DGROUP reset alone would leave it inconsistent; the
    game's start-up does both (src/platform/sdl3_main.c)."""
    lib.dg_reset()
    lib.bc_farheap_reset()


def var(ctype, name):
    return ctype.in_dll(lib, name)


# ---- DGROUP of the port (src/engine/dgroup.h) ------------------------------
DG = (ctypes.c_uint8 * 0x10000).in_dll(lib, 'dg')
fn('dg_reset', None)()

# o559A_fatal records the call and returns (src/engine/fatal.h); tests that
# exercise the real shutdown path (test_fatal) switch to mode 2 and back.
ctypes.c_int16.in_dll(lib, 'o559A_test_mode').value = 1


def dg_read(off, n):
    return bytes(DG[off:off + n])


def dg_write(off, data):
    for i, b in enumerate(data):
        DG[(off + i) & 0xFFFF] = b


def dg_from_emu(e):
    """Copy the emulated original's whole DGROUP into the port's dg[]."""
    ctypes.memmove(ctypes.addressof(DG), e.dgroup(0, 0x10000), 0x10000)


def dg_diff(e, ranges=None, ignore=()):
    """[(offset, orig, port)] for bytes that differ (optionally only in ranges)."""
    a, b = e.dgroup(0, 0x10000), bytes(DG)
    rs = ranges or [(0, 0x10000)]
    out = []
    for lo, hi in rs:
        for i in range(lo, hi):
            if a[i] != b[i] and i not in ignore:
                out.append((i, a[i], b[i]))
    return out


# ---- call interception in the shared DLL (tests/stubs/testhook.c) ---------------
# patch('s3d_load_district', pyfunc, None, c_int16, c_uint8) makes every call
# of the C function go to pyfunc (a ctypes callback); patch(name, 'th_tick')
# to another C function of the DLL.  Returns a handle for unpatch(); the
# context manager patched(...) restores on exit.  Works by writing
# "mov rax, target; jmp rax" over the function's first 12 bytes (functions
# are 16-byte aligned, so even a 1-byte body has room).
import ctypes as _C

_k32 = _C.WinDLL('kernel32', use_last_error=True) if os.name == 'nt' else None
_patches = {}


def _fn_addr(name):
    return _C.cast(getattr(lib, name), _C.c_void_p).value


def _write_code(addr, data):
    old = _C.c_uint32()
    if _k32 is not None:
        _k32.VirtualProtect(_C.c_void_p(addr), _C.c_size_t(len(data)), 0x40, _C.byref(old))
    _C.memmove(addr, data, len(data))
    if _k32 is not None:
        _k32.VirtualProtect(_C.c_void_p(addr), _C.c_size_t(len(data)), old.value, _C.byref(old))
        _k32.FlushInstructionCache(_C.c_void_p(-1), _C.c_void_p(addr), _C.c_size_t(len(data)))


def patch(name, target, restype=None, *argtypes):
    """Redirect C function `name` to target (python callable or the name of
    another C function in the DLL).  Patching an already patched function
    replaces the target."""
    addr = _fn_addr(name)
    if isinstance(target, str):
        keep, dest = None, _fn_addr(target)
    else:
        keep = _C.CFUNCTYPE(restype, *argtypes)(target)
        dest = _C.cast(keep, _C.c_void_p).value
    orig = _patches[name][1] if name in _patches else _C.string_at(addr, 12)
    _write_code(addr, b'\x48\xB8' + dest.to_bytes(8, 'little') + b'\xFF\xE0')
    _patches[name] = (addr, orig, keep)
    return name


def unpatch(name=None):
    """Restore one patched function, or all of them."""
    for n in ([name] if name else list(_patches)):
        if n in _patches:
            addr, orig, _ = _patches.pop(n)
            _write_code(addr, orig)


class patched:
    """with patched({'name': (pyfunc, restype, *argtypes) | 'c_name', ...}):"""
    def __init__(self, table):
        self.table = table

    def __enter__(self):
        for n, t in self.table.items():
            if isinstance(t, (str,)) or callable(t):
                patch(n, t)
            else:
                patch(n, t[0], *t[1:])
        return self

    def __exit__(self, *exc):
        for n in self.table:
            unpatch(n)
        return False
