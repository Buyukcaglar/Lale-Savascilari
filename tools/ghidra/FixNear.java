// FixNear.java - re-disassemble code whose real-mode segment context is wrong.
//
// The patched x86 language (tools/ghidra/ia_rel16.patch) resolves near
// branches inside the code segment held in the context field `rmseg`.
// SetupIST stores rmseg per segment, but Ghidra's disassembler starts a flow
// with the *flowing* context of where it came from (a far call from another
// segment, or the default 0), so instructions can end up decoded with the
// wrong rmseg - and near CALL/JMP targets that cross a 64 KB linear boundary
// or wrap the segment offset land in the wrong place.
//
// For every code block (seg*/ovl*/stub*/asm*) with any instruction whose
// rmseg differs from the block's segment: restore the stored context, clear
// the block's code and disassemble again from all previous instruction
// starts with an explicit initial context, restricted to the block.
//@category Lale

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;

import java.math.BigInteger;

public class FixNear extends GhidraScript {
    @Override
    public void run() throws Exception {
        Listing l = currentProgram.getListing();
        ProgramContext ctx = currentProgram.getProgramContext();
        Register rmseg = ctx.getRegister("rmseg");
        Register base = ctx.getBaseContextRegister();
        int blocks = 0, fixed = 0;
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            String n = b.getName();
            if (!n.matches("(seg|ovl|stub|asm)[0-9A-Fa-f]{4}") || !b.isInitialized())
                continue;
            int seg = Integer.parseInt(n.substring(n.length() - 4), 16);
            AddressSet starts = new AddressSet();
            int wrong = 0;
            for (Instruction i : l.getInstructions(new AddressSet(b.getStart(), b.getEnd()), true)) {
                starts.add(i.getAddress());
                BigInteger v = i.getValue(rmseg, false);
                if (v == null || v.intValue() != seg)
                    wrong++;
            }
            if (wrong == 0)
                continue;
            blocks++;
            fixed += wrong;
            l.clearCodeUnits(b.getStart(), b.getEnd(), false);
            ctx.setValue(rmseg, b.getStart(), b.getEnd(), BigInteger.valueOf(seg));
            DisassembleCommand cmd = new DisassembleCommand(starts,
                new AddressSet(b.getStart(), b.getEnd()), true);
            cmd.setInitialContext(new RegisterValue(base).assign(rmseg, BigInteger.valueOf(seg)));
            cmd.applyTo(currentProgram, monitor);
            println(String.format("FixNear: %s: %d instructions had the wrong segment context", n, wrong));
        }
        println("FixNear: re-disassembled " + blocks + " blocks (" + fixed + " instructions)");
        analyzeChanges(currentProgram);
    }
}
