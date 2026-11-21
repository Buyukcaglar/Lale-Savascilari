"""q3dview_common.py - shared helpers of the 3DQUICK tests (test_q3dview*.py).

ovl2700_image(): the emulator image for the 3DQUICK tests.  It used to be
a copy of analysis/ghidra_ist/ist_fpu.bin with ovl2700 rebuilt from IST.EXE
(the old ist_fpu.bin mis-converted 69 8087-emulator sequences there); the
regenerated ist_fpu.bin is byte-identical to that rebuild, so it is used
as it is.
"""
import os

from lale import ROOT


def ovl2700_image():
    """analysis/ghidra_ist/ist_fpu.bin (see the module doc)."""
    return os.path.join(ROOT, 'analysis', 'ghidra_ist', 'ist_fpu.bin')
