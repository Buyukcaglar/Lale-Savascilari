#!/usr/bin/env python3
"""Run tests/py/test_*.py differential tests.

usage: run.py [lale_testlib.dll] [filter]
  filter  substring of 'test_<file>.py:test_<name>'; files whose name does not
          match are not even imported (their C symbols may be missing from a
          private DLL built with tools/testlib.sh)."""
import importlib
import os
import sys
import time
import traceback

# Progress must reach the caller as it happens (a block-buffered pipe made
# long runs look hung and, with a stalled reader, actually hang).
sys.stdout.reconfigure(line_buffering=True)
sys.stderr.reconfigure(line_buffering=True)

HERE = os.path.dirname(os.path.abspath(__file__))
if len(sys.argv) > 1 and sys.argv[1].endswith(('.dll', '.so', '.dylib')):
    os.environ['LALE_TESTLIB'] = sys.argv.pop(1)
sys.path.insert(0, HERE)
import lale  # noqa: E402

flt = sys.argv[1] if len(sys.argv) > 1 else ''
total = failed = 0
for fname in sorted(os.listdir(HERE)):
    if not (fname.startswith('test_') and fname.endswith('.py')):
        continue
    if flt and flt.split(':')[0] not in fname and ':' in flt:
        continue
    if flt and ':' not in flt and flt not in fname and not any(
            flt in line for line in open(os.path.join(HERE, fname), encoding='utf-8')
            if line.startswith('def test_')):
        continue
    try:
        mod = importlib.import_module(fname[:-3])
    except Exception:                           # noqa: BLE001
        total += 1
        failed += 1
        print(f'FAIL {fname[:-3]} (import)')
        print(traceback.format_exc())
        continue
    for name in sorted(n for n in dir(mod) if n.startswith('test_') and flt in f'{fname}:{n}'):
        total += 1
        before = len(lale.failures)
        t0 = time.time()
        try:
            getattr(mod, name)()
        except Exception:                       # noqa: BLE001
            lale.failures.append(f'{name}: exception\n{traceback.format_exc()}')
        lale.unpatch()                          # calls the test redirected (lale.patch)
        bad = lale.failures[before:]
        state = 'FAIL' if bad else 'ok'
        print(f'{state:4} {fname[:-3]}.{name} ({time.time() - t0:.1f}s)')
        for b in bad[:10]:
            print('     ', b)
        failed += bool(bad)
print(f'{total - failed}/{total} differential tests passed')
sys.exit(1 if failed else 0)
