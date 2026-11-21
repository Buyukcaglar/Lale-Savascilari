// FixFPU.java - undo Borland 8087-emulator fixups so Ghidra sees real x87 code.
//
// Borland (like MS) C emits floating point ops as emulator interrupts; at run
// time the emulator either interprets them or, with a coprocessor present,
// patches them back into real instructions.  The encoding (FIxRQQ fixups):
//
//   CD 34..3B  xx...      ->  9B D8..DF xx...        FWAIT + ESC D8+n
//   CD 3C  E   modrm ...  ->  9B 26 E   modrm ...    FWAIT + ES: + ESC
//                              (E unmodified = ES; +40/+80/+C0 would be
//                               other segments - never seen in IST.EXE)
//   CD 3D                 ->  90 9B                  NOP + FWAIT
//   CD 3E ...             ->  emulator shortcut (RTL trig only) - left alone
//
// Only real instruction boundaries found by analysis are patched, so data
// and immediates that happen to contain CD 3x are never touched.
//@category Lale

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;

import java.util.*;

public class FixFPU extends GhidraScript {
    @Override
    public void run() throws Exception {
        Listing l = currentProgram.getListing();
        int total = 0, pass = 0;
        Set<Address> done = new HashSet<>();
        while (true) {
            List<Address> sites = new ArrayList<>();
            for (Instruction ins : l.getInstructions(true)) {
                if (!ins.getMnemonicString().equalsIgnoreCase("INT"))
                    continue;
                Scalar s = ins.getScalar(0);
                if (s == null)
                    continue;
                long v = s.getUnsignedValue();
                if (v >= 0x34 && v <= 0x3D && !done.contains(ins.getAddress()))
                    sites.add(ins.getAddress());
            }
            if (sites.isEmpty())
                break;
            pass++;
            println("FixFPU: pass " + pass + ": " + sites.size() + " sites");
            done.addAll(sites);
            for (Address a : sites) {
                byte[] b = getBytes(a, 3);
                int n = b[1] & 0xFF;
                byte[] nb;
                if (n <= 0x3B)
                    nb = new byte[] { (byte) 0x9B, (byte) (0xD8 + n - 0x34) };
                else if (n == 0x3C) {
                    int e = b[2] & 0xFF;
                    if ((e & 0xF8) != 0xD8) {
                        println("FixFPU: unexpected INT 3C operand " + Integer.toHexString(e) + " at " + a);
                        continue;
                    }
                    nb = new byte[] { (byte) 0x9B, (byte) 0x26 };
                } else
                    nb = new byte[] { (byte) 0x90, (byte) 0x9B };
                // clear the INT and whatever garbage was decoded after it
                Address end = a.add(7);
                l.clearCodeUnits(a, end, false);
                setBytes(a, nb);
                disassemble(a);
                total++;
            }
            if (pass > 100)
                break;
        }
        println("FixFPU: patched " + total + " emulator sequences in " + pass + " passes");
        analyzeChanges(currentProgram);
    }
}
