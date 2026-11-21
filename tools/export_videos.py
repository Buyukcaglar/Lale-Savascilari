#!/usr/bin/env python3
"""
export_videos.py - export every GFL/GHD video of the game as MP4 at its
original resolution (320x200, 10 fps) with its sound, for AI upscaling.

Frames come from tools/gflview.py (the reference decoder that
tests/py/test_gfl.py checks against the original player), every frame
shown, none dropped.  6-bit DAC colours are expanded to 8 bits the way the
port's host does ((v << 2) | (v >> 4)).

Video: H.264 High, yuv420p, BT.601, constant 12 Mbit/s (CBR with filler; x264 lands a little under its target,
so 12 keeps every file above 10 Mbit/s), every frame as stored - no scaling.
The sample aspect ratio is set to 5:6 so players show the 4:3 picture a
VGA monitor showed; the coded pixels stay 320x200.
Audio: the 8-bit 22050 Hz mono track, AAC (every shipped video has sound;
a video without would get no audio track).

usage: export_videos.py [SRC_DIR [OUT_DIR]]
       defaults: extracted/0GFL  export/videos
"""
import os
import subprocess
import sys
import tempfile
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gflview  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H = 320, 200


def dac8(pal):
    return bytes((c << 2) | (c >> 4) for c in pal)


def export(name, out_dir, tmp):
    v = gflview.Video(name)
    base = os.path.basename(name)
    fps = v.h['fps']
    audio = v.audio() if v.h['abytes'] else b''
    cmd = ['ffmpeg', '-v', 'error', '-y',
           '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{W}x{H}', '-r', str(fps), '-i', '-']
    if audio:
        wav = os.path.join(tmp, base + '.wav')
        with wave.open(wav, 'wb') as w:
            w.setnchannels(1)
            w.setsampwidth(1)              # 8-bit unsigned, as stored
            w.setframerate(v.h['abytes'])  # bytes per second = 22050 Hz
            w.writeframes(audio)
        cmd += ['-i', wav]
    cmd += ['-map', '0:v']
    if audio:
        cmd += ['-map', '1:a', '-c:a', 'aac', '-b:a', '192k', '-shortest']
    cmd += ['-c:v', 'libx264', '-profile:v', 'high', '-preset', 'slow',
            '-pix_fmt', 'yuv420p', '-sws_flags', 'accurate_rnd+full_chroma_int+bitexact',
            '-vf', 'setsar=5/6',
            '-colorspace', 'smpte170m', '-color_primaries', 'smpte170m',
            '-color_trc', 'smpte170m', '-color_range', 'tv',
            '-b:v', '12M', '-minrate', '12M', '-maxrate', '12M', '-bufsize', '12M',
            '-x264-params', 'nal-hrd=cbr:force-cfr=1',
            '-movflags', '+faststart',
            os.path.join(out_dir, base + '.mp4')]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    n = 0
    for _, pix, pal in v.frames():
        lut = dac8(pal)
        table = [lut[3 * c:3 * c + 3] for c in range(256)]
        p.stdin.write(b''.join(table[b] for b in pix))
        n += 1
    p.stdin.close()
    if p.wait():
        raise SystemExit(f'ffmpeg failed on {base}')
    return base, n, fps, len(audio) / v.h['abytes'] if audio else 0


def main(argv):
    src = argv[1] if len(argv) > 1 else os.path.join(ROOT, 'extracted', '0GFL')
    out = argv[2] if len(argv) > 2 else os.path.join(ROOT, 'export', 'videos')
    os.makedirs(out, exist_ok=True)
    names = sorted(os.path.splitext(os.path.join(src, f))[0]
                   for f in os.listdir(src) if f.upper().endswith('.GHD'))
    with tempfile.TemporaryDirectory() as tmp:
        for name in names:
            base, n, fps, secs = export(name, out, tmp)
            print(f'{base:10s} {n:5d} frames  {n / fps:7.1f} s video  '
                  f'{secs:7.1f} s audio', flush=True)


if __name__ == '__main__':
    main(sys.argv)
