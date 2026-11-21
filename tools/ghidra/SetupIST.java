// SetupIST.java - Ghidra pre-analysis script for the flat IST.EXE image.
//
// Input: build/<exe>_segments.json from tools/segmodel.py
//  * splits the single raw block at every segment boundary and names it
//  * sets CS (= segment) and DS (= DGROUP) context over all code
//  * creates functions at every known far entry point
//
// Run via tools/ghidra_run.sh (analyzeHeadless -preScript SetupIST.java <json>)
//@category Lale

import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.SourceType;

import java.math.BigInteger;
import java.nio.file.*;
import java.util.*;

public class SetupIST extends GhidraScript {

    private SegmentedAddressSpace sp;

    private Address at(int seg, int off) {
        return sp.getAddress(seg, off);
    }

    private void splitAt(Memory mem, long linear, int seg, String name) throws Exception {
        Address a = at(seg, (int) (linear - (long) seg * 16));
        MemoryBlock b = mem.getBlock(a);
        if (b == null)
            return;
        if (b.getStart().getOffset() != a.getOffset())
            mem.split(b, a);
        b = mem.getBlock(a);
        b.setName(name);
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        JsonObject j = JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonObject();
        sp = (SegmentedAddressSpace) currentProgram.getAddressFactory().getDefaultAddressSpace();
        Memory mem = currentProgram.getMemory();
        ProgramContext ctx = currentProgram.getProgramContext();
        Register cs = ctx.getRegister("CS");
        Register ds = ctx.getRegister("DS");
        // rmseg: context field added by tools/ghidra/ia_rel16.patch - the code
        // segment the SLEIGH near-branch rules wrap within (CPU semantics)
        Register rmseg = ctx.getRegister("rmseg");
        if (rmseg == null)
            throw new IllegalStateException("x86 language lacks 'rmseg': run tools/ghidra_patch.sh");
        int dgroup = Integer.parseInt(j.get("dgroup").getAsString(), 16);

        List<JsonObject> segs = new ArrayList<>();
        for (JsonElement e : j.getAsJsonArray("segments"))
            segs.add(e.getAsJsonObject());

        // split + name blocks: segments, gaps after them, DGROUP
        for (JsonObject s : segs) {
            int seg = s.get("seg").getAsInt();
            int start = s.get("start").getAsInt();
            int size = s.get("size").getAsInt();
            splitAt(mem, (long) seg * 16 + start, seg, s.get("name").getAsString());
            long end = (long) seg * 16 + size;
            splitAt(mem, end, (int) (end >> 4), String.format("gap_%05X", end));
        }
        splitAt(mem, (long) dgroup * 16, dgroup, "dgroup");

        // gaps between segments (padding) are not code: make them uninitialised
        // so analysis cannot flow from one overlay into the next
        for (MemoryBlock b : mem.getBlocks()) {
            if (b.getName().startsWith("gap_") && b.isInitialized()
                    && b.getStart().getOffset() >= 0x27000)
                mem.convertToUninitialized(b);
        }
        for (MemoryBlock b : mem.getBlocks()) {
            String n = b.getName();
            b.setWrite(true);
            b.setExecute(n.startsWith("seg") || n.startsWith("ovl") || n.startsWith("stub"));
        }

        // CS / DS context over every code segment
        for (JsonObject s : segs) {
            int seg = s.get("seg").getAsInt();
            int start = s.get("start").getAsInt();
            int size = s.get("size").getAsInt();
            if (size <= start)
                continue;
            Address a0 = at(seg, start), a1 = at(seg, size - 1);
            ctx.setValue(cs, a0, a1, BigInteger.valueOf(seg));
            ctx.setValue(ds, a0, a1, BigInteger.valueOf(dgroup));
            ctx.setValue(rmseg, a0, a1, BigInteger.valueOf(seg));
        }

        // entry points
        int n = 0;
        for (Map.Entry<String, JsonElement> e : j.getAsJsonObject("entries").entrySet()) {
            String[] p = e.getKey().split(":");
            Address a = at(Integer.parseInt(p[0], 16), Integer.parseInt(p[1], 16));
            String name = e.getValue().getAsString();
            currentProgram.getSymbolTable().addExternalEntryPoint(a);
            disassemble(a);
            Function f = getFunctionAt(a);
            if (f == null)
                f = createFunction(a, name);
            if (f != null) {
                f.setName(name, SourceType.IMPORTED);
                try {
                    f.setCallingConvention("__cdecl16far");
                } catch (Exception ex) {
                    // leave default when the cspec lacks it
                }
                n++;
            }
        }
        // overlay stub thunks (JMP FAR) -> thunk functions of their targets
        int t = 0;
        for (Map.Entry<String, JsonElement> e : j.getAsJsonObject("thunks").entrySet()) {
            String[] ps = e.getKey().split(":"), pt = e.getValue().getAsString().split(":");
            Address sa = at(Integer.parseInt(ps[0], 16), Integer.parseInt(ps[1], 16));
            Address ta = at(Integer.parseInt(pt[0], 16), Integer.parseInt(pt[1], 16));
            Function tf = getFunctionAt(ta);
            if (tf == null)
                continue;
            disassemble(sa);
            Function sf = getFunctionAt(sa);
            if (sf == null)
                sf = createFunction(sa, "thunk_" + tf.getName());
            if (sf != null) {
                sf.setThunkedFunction(tf);
                t++;
            }
        }
        println("SetupIST: " + t + " overlay thunks");
        println("SetupIST: " + segs.size() + " segments, " + n + " entry functions, DGROUP="
                + Integer.toHexString(dgroup));
    }
}
