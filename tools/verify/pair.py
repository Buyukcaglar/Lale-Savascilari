#!/usr/bin/env python3
"""
pair.py - run one input script on both sides and compare (docs/VERIFY.md).

    python -u tools/verify/pair.py <script.txt> <tag> [--only port|dbx] [--port 7777] [--keep]
                                   [--install <file>=<NAME> ...] [--cd <disc.cue>]

Resets the game folders (out/verify/game_port, game_dbx: files the game
writes - GRP.DAT, saves - restored from orig/), runs the port
(out/windows/Lale64.exe -> out/verify/p_<tag>) and the original in DOSBox-X
(dbx_replay.py -> out/verify/d_<tag>), then vcompare.py and a byte diff of
the files the game wrote.  --keep leaves the game folders as the previous
run left them (a script that loads the saves of that run).  --install
copies a prepared file into both game folders after the reset (a save built
by mksave.py: `--install out/verify/scripts/play5.sav=SAKLA001.SAV`).

--cd: the 1996 CD edition on both sides.  The game folders are then
out/verify/game_port_cd and game_dbx_cd, made from orig_cd/ (the files of the
disc, tools/cddump.py extract: the 1996 IST.EXE) plus SES.CFG and GRP.DAT
from orig/ (the disc has none; its installer wrote SES.CFG); the disc image
is mounted as D: in DOSBox-X and given to the port with --edition cd.
DGROUP is not compared byte for byte there: the 1996 build's data segment
is laid out differently (tools/exediff.py --data), the port keeps the 2005
layout for both editions.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
V = os.path.join(ROOT, 'out', 'verify')
WRITTEN = ('GRP.DAT',)


def make_cd_folder(game):
    """game_*_cd: the disc's files, SES.CFG and GRP.DAT of the 2005 release"""
    if os.path.isdir(game):
        return
    src = os.path.join(ROOT, 'orig_cd')
    if not os.path.exists(os.path.join(src, 'IST.EXE')):
        sys.exit('orig_cd/IST.EXE is missing: python tools/cddump.py extract (docs/DISC.md)')
    os.makedirs(game)
    for f in os.listdir(src):
        shutil.copyfile(os.path.join(src, f), os.path.join(game, f))
    for f in ('SES.CFG', 'GRP.DAT'):
        shutil.copyfile(os.path.join(ROOT, 'orig', f), os.path.join(game, f))


def reset(game):
    for f in os.listdir(game):
        if f.lower().endswith('.sav'):
            os.remove(os.path.join(game, f))
    for f in WRITTEN:
        shutil.copyfile(os.path.join(ROOT, 'orig', f), os.path.join(game, f))


def install(game, items):
    for it in items or ():
        src, _, name = it.partition('=')
        shutil.copyfile(src, os.path.join(game, name or os.path.basename(src)))


def written(game):
    out = {}
    for f in os.listdir(game):
        if f.lower().endswith('.sav') or f.upper() in WRITTEN:
            out[f.upper()] = open(os.path.join(game, f), 'rb').read()
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('script')
    ap.add_argument('tag')
    ap.add_argument('--only')
    ap.add_argument('--keep', action='store_true',
                    help='keep the game folders (the saves of the previous run: load tests)')
    ap.add_argument('--install', action='append', metavar='FILE=NAME',
                    help='copy FILE into both game folders as NAME (after the reset)')
    ap.add_argument('--port', type=int, default=7777)
    ap.add_argument('--cd', metavar='CUE', help='the 1996 CD edition, with this image of the disc')
    ap.add_argument('--exe', default=os.path.join(ROOT, 'out', *(('windows', 'Lale64.exe') if os.name == 'nt' else
                                                       ('macos', 'Lale64') if sys.platform == 'darwin' else
                                                       ('linux', 'Lale64'))))
    a = ap.parse_args()
    sfx = '_cd' if a.cd else ''
    gp, gd = os.path.join(V, 'game_port' + sfx), os.path.join(V, 'game_dbx' + sfx)
    if a.cd:
        make_cd_folder(gp)
        make_cd_folder(gd)
    po, do = os.path.join(V, 'p_' + a.tag), os.path.join(V, 'd_' + a.tag)
    if a.only != 'dbx':
        if not a.keep:
            reset(gp)
        install(gp, a.install)
        shutil.rmtree(po, ignore_errors=True)
        os.makedirs(po)
        extra = ['--cd-image', os.path.abspath(a.cd), '--edition', 'cd'] if a.cd else []
        subprocess.run([a.exe, '--game-dir', gp, '--script', a.script, '--out', po] + extra,
                       timeout=1800, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if a.only != 'port':
        if not a.keep:
            reset(gd)
        install(gd, a.install)
        shutil.rmtree(do, ignore_errors=True)
        with open(os.path.join(V, f'd_{a.tag}.log'), 'w') as log:
            subprocess.run([sys.executable, '-u', os.path.join(HERE, 'dbx_replay.py'), a.script,
                            '--out', do, '--game-dir', gd, '--port', str(a.port)]
                           + (['--cd', os.path.abspath(a.cd)] if a.cd else []),
                           timeout=3600, stdout=log, stderr=subprocess.STDOUT)
    subprocess.run([sys.executable, '-u', os.path.join(HERE, 'vcompare.py'), po, do]
                   + (['--no-dg'] if a.cd else []), timeout=1800)
    wp, wd = written(gp), written(gd)
    for f in sorted(set(wp) | set(wd)):
        x, y = wp.get(f), wd.get(f)
        if x is None or y is None:
            print(f'{f}: only on the {"original" if x is None else "port"} side')
        elif x != y:
            d = [i for i in range(min(len(x), len(y))) if x[i] != y[i]]
            print(f'{f}: {len(d)} bytes differ (len {len(x)}/{len(y)}) at {[hex(i) for i in d[:16]]}')
        else:
            print(f'{f}: identical')


if __name__ == '__main__':
    main()
