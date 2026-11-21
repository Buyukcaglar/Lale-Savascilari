#!/usr/bin/env python3
"""
branchcheck.py - verify every relative branch in the exported listings.

For each near CALL/JMP (E8/E9 rel16), short JMP/Jcc/LOOP/JCXZ (rel8) and
0F 8x Jcc rel16, recompute the target with real-mode CPU semantics
    target = CS*16 + ((IP + length + rel) & 0xFFFF)
and compare it with the target Ghidra printed.  Run by tools/ghidra_run.sh;
exits 1 if any branch disagrees (see tools/ghidra/ia_rel16.patch, FixNear).

usage: branchcheck.py analysis/ghidra_ist
"""
import glob,re,os,sys
n=bad=0; ex=[]
for p in glob.glob(sys.argv[1]+'/asm/*.asm'):
    for line in open(p,encoding='utf-8',errors='replace'):
        m=re.match(r'\s+([0-9a-f]{4}):([0-9a-f]{4})\s+([0-9A-F]+)\s+(J\w+|CALL|LOOP\w*)\s+(\S+)',line)
        if not m: continue
        seg,off,hx=int(m[1],16),int(m[2],16),m[3]
        b=bytes.fromhex(hx)
        if b[0] in (0xE8,0xE9) and len(b)==3: rel=int.from_bytes(b[1:3],'little',signed=True)
        elif (b[0]==0xEB or 0x70<=b[0]<=0x7F or 0xE0<=b[0]<=0xE3) and len(b)==2: rel=int.from_bytes(b[1:2],'little',signed=True)
        elif b[0]==0x0F and 0x80<=b[1]<=0x8F and len(b)==4: rel=int.from_bytes(b[2:4],'little',signed=True)
        else: continue
        exp=seg*16+((off+len(b)+rel)&0xFFFF); n+=1
        mm=re.match(r'(?:0x)?([0-9a-fA-F]{1,4}):([0-9a-fA-F]{4})',m[5])
        if not mm or exp!=int(mm[1],16)*16+int(mm[2],16):
            bad+=1; ex.append(line.strip()[:80])
print('branchcheck: relative branches checked',n,'wrong',bad,ex[:5])
sys.exit(1 if bad else 0)
