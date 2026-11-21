// PruneFunctions.java - delete functions that are artifacts of earlier wrong
// flows:
//   * any function inside the data group (DGROUP holds no code), and
//   * auto-named (FUN_*) functions nobody references whose entry is reached
//     by fall-through from the previous instruction - i.e. the "entry" is in
//     the middle of another routine's body.
// Unreferenced functions that follow a RET/JMP are KEPT: in this program many
// real routines are reached only through far-pointer tables and callbacks
// (DSMI driver API, UI callbacks, RTL exit tables) that Ghidra cannot see.
//@category Lale

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.*;

import java.util.*;

public class PruneFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        List<Function> kill = new ArrayList<>();
        ReferenceManager rm = currentProgram.getReferenceManager();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            MemoryBlock b = getMemoryBlock(f.getEntryPoint());
            boolean inData = b != null && b.getName().equals("dgroup");
            boolean auto = f.getSymbol().getSource() == SourceType.DEFAULT;
            boolean unref = rm.getReferenceCountTo(f.getEntryPoint()) == 0;
            Instruction prev = getInstructionBefore(f.getEntryPoint());
            boolean midBody = prev != null && prev.getFallThrough() != null
                && prev.getFallThrough().equals(f.getEntryPoint());
            if (inData || (auto && unref && midBody && !f.isThunk()))
                kill.add(f);
        }
        for (Function f : kill) {
            println("PruneFunctions: removing " + f.getName() + " @ " + f.getEntryPoint());
            removeFunction(f);
        }
        println("PruneFunctions: removed " + kill.size() + " functions");
    }
}
