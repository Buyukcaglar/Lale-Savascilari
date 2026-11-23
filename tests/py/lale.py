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

_default_lib = (('windows', 'lale_testlib.dll') if os.name == 'nt' else
                ('macos', 'lale_testlib.dylib') if sys.platform == 'darwin' else
                ('linux', 'lale_testlib.so'))
_lib_path = os.environ.get('LALE_TESTLIB') or os.path.join(ROOT, 'out', *_default_lib)
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
# context manager patched(...) restores on exit.  Works by writing a jump
# over the function's first bytes (functions are 16-byte aligned, so even a
# one-instruction body has room):
#   x86-64   mov rax, target; jmp rax                 (12 bytes)
#   arm64    ldr x16, .+8; br x16; .quad target       (16 bytes)
import ctypes as _C
import platform as _platform

_ARM64 = _platform.machine().lower() in ('arm64', 'aarch64')
_PATCH_LEN = 16 if _ARM64 else 12
_k32 = _C.WinDLL('kernel32', use_last_error=True) if os.name == 'nt' else None
_libc = _C.CDLL(None, use_errno=True) if os.name != 'nt' else None
_patches = {}

if sys.platform == 'darwin':
    # Mach: code pages of a mapped image are made writable as a private copy
    # (VM_PROT_COPY), never writable and executable at once (Apple Silicon).
    _libc.mach_vm_protect.argtypes = (_C.c_uint32, _C.c_uint64, _C.c_uint64, _C.c_int, _C.c_int)
    _libc.mach_vm_protect.restype = _C.c_int
    _libc.sys_icache_invalidate.argtypes = (_C.c_void_p, _C.c_size_t)
    _libc.sys_icache_invalidate.restype = None
    _mach_task = _C.c_uint32.in_dll(_libc, 'mach_task_self_').value
elif _libc is not None:
    _libc.mprotect.argtypes = (_C.c_void_p, _C.c_size_t, _C.c_int)


def _jump(dest):
    if _ARM64:
        return b'\x50\x00\x00\x58' + b'\x00\x02\x1F\xD6' + dest.to_bytes(8, 'little')
    return b'\x48\xB8' + dest.to_bytes(8, 'little') + b'\xFF\xE0'


def _fn_addr(name):
    return _C.cast(getattr(lib, name), _C.c_void_p).value


def _write_code(addr, data):
    if _k32 is not None:
        old = _C.c_uint32()
        _k32.VirtualProtect(_C.c_void_p(addr), _C.c_size_t(len(data)), 0x40, _C.byref(old))
        _C.memmove(addr, data, len(data))
        _k32.VirtualProtect(_C.c_void_p(addr), _C.c_size_t(len(data)), old.value, _C.byref(old))
        _k32.FlushInstructionCache(_C.c_void_p(-1), _C.c_void_p(addr), _C.c_size_t(len(data)))
        return
    page = os.sysconf('SC_PAGESIZE')
    start = addr & ~(page - 1)
    size = ((addr + len(data) + page - 1) & ~(page - 1)) - start
    if sys.platform == 'darwin':
        kr = _libc.mach_vm_protect(_mach_task, start, size, 0, 0x01 | 0x02 | 0x10)   # R|W|COPY
        if kr:
            raise OSError(f'mach_vm_protect(rw) failed: {kr}')
        _C.memmove(addr, data, len(data))
        kr = _libc.mach_vm_protect(_mach_task, start, size, 0, 0x01 | 0x04)          # R|X
        if kr:
            raise OSError(f'mach_vm_protect(rx) failed: {kr}')
        _libc.sys_icache_invalidate(addr, len(data))
    else:
        if _libc.mprotect(start, size, 7):
            raise OSError(_C.get_errno(), 'mprotect')
        _C.memmove(addr, data, len(data))
        _libc.mprotect(start, size, 5)


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
    orig = _patches[name][1] if name in _patches else _C.string_at(addr, _PATCH_LEN)
    _write_code(addr, _jump(dest))
    _patches[name] = (addr, orig, keep)
    return name


def unpatch(name=None):
    """Restore one patched function, or all of them."""
    for n in ([name] if name else list(_patches)):
        if n in _patches:
            addr, orig, _ = _patches.pop(n)
            _write_code(addr, orig)


def guarded(name, *args):
    """Call C function `name` (up to 4 integer arguments, result ignored)
    where it may run into an invalid address, and raise OSError if it does -
    what ctypes does by itself on Windows.  Elsewhere the fault is a signal
    that would end the whole run: tests/stubs/testhook.c th_guard catches it."""
    if os.name == 'nt':
        f = getattr(lib, name)
        f.restype = None
        f.argtypes = [_C.c_ssize_t] * len(args)
        f(*args)
        return
    g = lib.th_guard
    g.restype = _C.c_int
    g.argtypes = [_C.c_void_p] + [_C.c_ssize_t] * 4
    sig = g(_fn_addr(name), *(list(args) + [0] * (4 - len(args))))
    if sig:
        raise OSError(f'exception: access violation (signal {sig}) in {name}')


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
