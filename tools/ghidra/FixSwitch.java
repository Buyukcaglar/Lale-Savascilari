// FixSwitch.java - resolve Borland C++ switch jump tables that Ghidra's
// real-mode analysis leaves unresolved, disassemble the case targets and give
// the decompiler jump-table overrides.
//
// Borland C++ 3.x emits two dispatch shapes, both with the table in CS:
//
//  A) dense index table
//        (SUB BX,lo)                   optional bias
//        CMP BX,N  ;  JA default | JBE ok ; JMP default
//        SHL BX,1
//        JMP word ptr CS:[BX + T]      N+1 targets at CS:T
//
//  B) value search (sparse cases)
//        MOV CX,N  ;  MOV BX,T
//     L: MOV AX,CS:[BX] / CMP AX,... ; JZ found ; ADD BX,2 ; LOOP L ; JMP default
//     found:
//        JMP word ptr CS:[BX + 2N]     N case values at CS:T, N targets at CS:T+2N
//
// Repeats until no new table is found (resolved targets may hold switches
// themselves).  Arg 0 (optional): a TSV report path.
//@category Lale

import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.pcode.JumpTable;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;

import java.io.PrintWriter;
import java.math.BigInteger;
import java.util.*;
import java.util.regex.*;

public class FixSwitch extends GhidraScript {
    static final Pattern JMP_CS_BX = Pattern.compile(
        "word ptr CS:\\[BX \\+ (0x[0-9a-fA-F]+)\\]", Pattern.CASE_INSENSITIVE);

    Listing l;
    SegmentedAddressSpace space;
    Register cs;
    StringBuilder report = new StringBuilder("jump\tform\tcases\ttable\tnew_targets\n");

    int csOf(Address a) {
        BigInteger v = currentProgram.getProgramContext().getValue(cs, a, false);
        if (v != null) return v.intValue();
        return ((SegmentedAddress) a).getSegment();
    }

    Address at(int seg, int off) {
        return space.getAddress(seg & 0xFFFF, off & 0xFFFF);
    }

    Long immOf(Instruction i, int op) {
        Scalar s = i.getScalar(op);
        return s == null ? null : s.getUnsignedValue();
    }

    boolean is(Instruction i, String mnem, String op0, String op1) {
        if (!i.getMnemonicString().equalsIgnoreCase(mnem)) return false;
        if (op0 != null && !i.getDefaultOperandRepresentation(0).equalsIgnoreCase(op0)) return false;
        if (op1 != null && (i.getNumOperands() < 2
                || !i.getDefaultOperandRepresentation(1).toUpperCase().startsWith(op1))) return false;
        return true;
    }

    @Override
    public void run() throws Exception {
        l = currentProgram.getListing();
        space = (SegmentedAddressSpace) currentProgram.getAddressFactory().getDefaultAddressSpace();
        cs = currentProgram.getRegister("CS");
        Set<Address> handled = new HashSet<>();
        int tables = 0, targets = 0, unresolved = 0, pass = 0;
        List<String> unres = new ArrayList<>();
        while (true) {
            List<Instruction> todo = new ArrayList<>();
            for (Instruction ins : l.getInstructions(true)) {
                if (!ins.getMnemonicString().equalsIgnoreCase("JMP") || handled.contains(ins.getAddress()))
                    continue;
                if (JMP_CS_BX.matcher(ins.getDefaultOperandRepresentation(0)).find())
                    todo.add(ins);
            }
            if (todo.isEmpty() || ++pass > 50) break;
            for (Instruction ins : todo) {
                handled.add(ins.getAddress());
                Matcher m = JMP_CS_BX.matcher(ins.getDefaultOperandRepresentation(0));
                m.find();
                int disp = Integer.decode(m.group(1));
                int seg = csOf(ins.getAddress());
                String form = null;
                int n = -1, tbl = -1, first = -1;
                // walk back over the dispatch prologue (flow-insensitive, short)
                Instruction p = ins;
                Long cmpN = null, movCX = null, movBX = null;
                boolean shl = false;
                for (int k = 0; k < 12 && p != null; k++) {
                    p = l.getInstructionBefore(p.getAddress());
                    if (p == null) break;
                    if (is(p, "SHL", "BX", "0X1")) shl = true;
                    else if (cmpN == null && is(p, "CMP", "BX", "0X")) cmpN = immOf(p, 1);
                    else if (movCX == null && is(p, "MOV", "CX", "0X")) movCX = immOf(p, 1);
                    else if (movBX == null && is(p, "MOV", "BX", "0X")) movBX = immOf(p, 1);
                }
                if (shl && cmpN != null) {
                    form = "index"; n = (int) (cmpN + 1); tbl = disp; first = disp;
                } else if (movCX != null && movBX != null && disp == 2 * movCX) {
                    form = "search"; n = (int) (long) movCX; tbl = (int) (long) movBX;
                    first = tbl + 2 * n;
                }
                if (form == null || n <= 0 || n > 512) {
                    unresolved++;
                    unres.add(ins.getAddress().toString());
                    report.append(ins.getAddress()).append("\t?\t\t\t\n");
                    continue;
                }
                // read and type the table(s)
                ArrayList<Address> dests = new ArrayList<>();
                int tblStart = form.equals("search") ? tbl : first;
                Address ts = at(seg, tblStart), te = at(seg, first + 2 * n - 1);
                try {
                    l.clearCodeUnits(ts, te, false);
                } catch (Exception e) { /* nothing there */ }
                for (int i = 0; i < (form.equals("search") ? 2 * n : n); i++) {
                    Address wa = at(seg, tblStart + 2 * i);
                    try { createWord(wa); } catch (Exception e) { }
                }
                int fresh = 0;
                for (int i = 0; i < n; i++) {
                    int off = getShort(at(seg, first + 2 * i)) & 0xFFFF;
                    Address d = at(seg, off);
                    if (currentProgram.getMemory().getBlock(d) == null) continue;
                    dests.add(d);
                    currentProgram.getReferenceManager().addMemoryReference(
                        ins.getAddress(), d, RefType.COMPUTED_JUMP, SourceType.ANALYSIS, 0);
                    if (l.getInstructionAt(d) == null) {
                        disassemble(d);
                        fresh++;
                    }
                }
                createLabel(ts, "switchtab_" + ins.getAddress().toString().replace(':', '_'), true);
                Function f = getFunctionContaining(ins.getAddress());
                if (f != null) {
                    CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor);
                    try {
                        new JumpTable(ins.getAddress(), dests, true, 0).writeOverride(f);
                    } catch (Exception e) {
                        println("FixSwitch: override failed at " + ins.getAddress() + ": " + e.getMessage());
                    }
                }
                tables++;
                targets += fresh;
                report.append(ins.getAddress()).append('\t').append(form).append('\t').append(n)
                      .append('\t').append(ts).append('\t').append(fresh).append('\n');
            }
            analyzeChanges(currentProgram);
        }
        println("FixSwitch: " + tables + " tables, " + targets + " newly disassembled targets, "
                + unresolved + " unresolved " + unres + " in " + pass + " passes");
        String[] args = getScriptArgs();
        if (args.length > 0)
            try (PrintWriter w = new PrintWriter(args[0], "UTF-8")) { w.print(report); }
    }
}
