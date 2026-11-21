#!/usr/bin/env python3
"""frposdis.py - disassembler for the FRPOS event scripts (PCSn, FRPOS.CKD).

    python tools/frposdis.py extracted/FRPOS/PCS1 [...]     listing
    python tools/frposdis.py --check extracted/FRPOS/PCS*   decode all, report errors

Format (docs/FORMATS.md "FRPOS event scripts"): an AMOS bank (20-byte
"AmBk" header), then at file offset 14h a big-endian u16 event count and
the events, each [BE16 len][body].  Body: BE16 command budget (the
interpreter's counter BB93, shared by all sections), BE16 offsets (from
the body start) of the sections for triggers 2, 3, 4; the trigger 1
section starts at body + 8.  A section is [12 + trigger][flags byte] and
commands.  Strings are [len][len bytes], every byte stored 10 lower.

The decoder follows fr_skip_commands (IST.EXE 3FD1:4ECB) for operand sizes
and fr_run_event (3FD1:0000) for their meaning.  Every section is decoded
linearly to the next section's start; an unknown opcode or a command that
overruns its section is an error.
"""
import struct
import sys

# opcode: (mnemonic, operand layout)
#   b = byte, w = BE word, s = [len] string, S2 = [l1][l2] str1 str2 (op 5),
#   P = op 30's [len][skip] str
OPS = {
    1: ('if', 'bbb'), 2: ('text', 's'), 3: ('icon', 'b'), 4: ('end', ''),
    5: ('ask', 'S2'), 6: ('skip', 'b'), 7: ('setflag', 'b'), 8: ('battle', 'bbbbbbb'),
    9: ('exchange', 'bbbwww'), 10: ('text2', 'ss'), 11: ('teleport', 'bwb'),
    12: ('scalestat', 'bbb'), 13: ('nop13', 'b'), 14: ('nop14', 'b'), 15: ('nop15', 'b'),
    16: ('cancel', ''), 17: ('say', 'bs'), 18: ('acc_flag1', ''), 19: ('film', 'b'),
    20: ('random', 'b'), 21: ('menu', 'ssssss'), 22: ('findchar', 'bbb'),
    23: ('findminmax', 'bb'), 24: ('pickchar', ''), 25: ('music', 'b'), 26: ('pay', 'b'),
    27: ('story', 's' * 40), 28: ('aphorism', ''), 29: ('takeitem', 'bw'),
    30: ('password', 'P'), 31: ('wait', 'b'), 32: ('redraw_end', ''), 33: ('leave', ''),
}


def decode_text(b):
    """+10 per byte; printable ASCII kept, the rest as \\xNN"""
    out = []
    for c in b:
        c = (c + 10) & 0xFF
        out.append(chr(c) if 0x20 <= c < 0x7F else '\\x%02x' % c)
    return ''.join(out)


class Err(Exception):
    pass


def be16(d, p):
    if p + 2 > len(d):
        raise Err('word past the end at %04X' % p)
    return struct.unpack('>H', d[p:p + 2])[0]


def byte(d, p, end):
    if p >= end:
        raise Err('operand past the section end at %04X' % p)
    return d[p]


def command(d, p, end):
    """-> (text, next p, opcode)"""
    op = byte(d, p, end)
    if op not in OPS:
        raise Err('unknown opcode %d at %04X' % (op, p))
    name, lay = OPS[op]
    q = p + 1
    args = []
    if lay == 'S2':
        l1, l2 = byte(d, q, end), byte(d, q + 1, end)
        q += 2
        for ln in (l1, l2):
            if q + ln > end:
                raise Err('string past the section end at %04X' % q)
            args.append('"%s"' % decode_text(d[q:q + ln]))
            q += ln
    elif lay == 'P':
        ln, skip = byte(d, q, end), byte(d, q + 1, end)
        q += 2
        if q + ln > end:
            raise Err('string past the section end at %04X' % q)
        args = ['skip=%d' % skip, '"%s"' % decode_text(d[q:q + ln])]
        q += ln
    else:
        for k in lay:
            if k == 'b':
                args.append(str(byte(d, q, end)))
                q += 1
            elif k == 'w':
                byte(d, q + 1, end)
                args.append('0x%04X' % be16(d, q))
                q += 2
            else:
                ln = byte(d, q, end)
                q += 1
                if q + ln > end:
                    raise Err('string past the section end at %04X' % q)
                args.append('"%s"' % decode_text(d[q:q + ln]))
                q += ln
    return '%-11s %s' % (name, ', '.join(args)), q, op


def disassemble(d, out=None, ops_seen=None):
    """decode one PCS file; returns the number of events.  Raises Err."""
    if d[:4] != b'AmBk':
        raise Err('not an AMOS bank')
    n = be16(d, 0x14)
    p = 0x16
    w = out.append if out is not None else (lambda s: None)
    w('; %d events' % n)
    for e in range(n):
        ln = be16(d, p)
        body = p + 2
        end = body + ln
        if end > len(d):
            raise Err('event %d past the end of the file' % e)
        budget = be16(d, body)
        offs = [8] + [be16(d, body + 2 + 2 * k) for k in range(3)]
        w('')
        w('event %d  (file %04X, len %d, command budget %d)' % (e, p, ln, budget))
        starts = [body + o for o in offs]
        if d[starts[3]:end] != bytes((4, 0)):
            raise Err('event %d: trailer %s' % (e, d[starts[3]:end].hex()))
        for t in range(3):
            s = starts[t]
            se = starts[t + 1]
            if not body + 8 <= s <= se <= end:
                raise Err('event %d: bad section %d bounds %04X..%04X' % (e, t + 1, s, se))
            if s == se:
                w('  trigger %d: (empty)' % (t + 1))
                continue
            h = byte(d, s, se)
            fl = byte(d, s + 1, se)
            if h != 13 + t:
                raise Err('event %d: section %d header %d' % (e, t + 1, h))
            w('  trigger %d: (flags %d)' % (t + 1, fl))
            q = s + 2
            k = 0
            while q < se:
                text, nq, op = command(d, q, se)
                if ops_seen is not None:
                    ops_seen.add(op)
                w('    %04X %3d  %s' % (q, k, text))
                q = nq
                k += 1
        p = end
    if p != len(d):
        w('; %d bytes after the last event' % (len(d) - p))
    return n


def main(argv):
    check = '--check' in argv
    files = [a for a in argv if a != '--check']
    bad = 0
    seen = set()
    for f in files:
        d = open(f, 'rb').read()
        out = []
        try:
            n = disassemble(d, out, seen)
            if check:
                print('%-28s ok, %d events' % (f, n))
            else:
                print('; %s' % f)
                print('\n'.join(out))
        except Err as ex:
            bad += 1
            print('%-28s ERROR %s' % (f, ex))
    if check:
        print('opcodes used: %s' % ' '.join(str(o) for o in sorted(seen)))
        print('opcodes never used: %s' % ' '.join(str(o) for o in sorted(set(OPS) - seen)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
