// ExportDecomp.java - Ghidra post-analysis script: dump decompiled C for every
// function, grouped by memory block (segment), plus a function inventory.
//
// Output (directory given as first script argument):
//   <block>.c          decompiler output, functions in address order
//   functions.tsv      address, block, name, size, calling convention, callers, callees
//
// This is *draft* material only. The real source under src/ is hand-written
// from it, the disassembly, and verification against the original.
//@category Lale

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;

import java.io.*;
import java.nio.file.*;
import java.util.*;

public class ExportDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        Path out = Path.of(getScriptArgs()[0]);
        Files.createDirectories(out);
        DecompInterface di = new DecompInterface();
        DecompileOptions opt = new DecompileOptions();
        di.setOptions(opt);
        di.toggleCCode(true);
        di.toggleSyntaxTree(false);
        di.openProgram(currentProgram);

        Map<String, StringBuilder> files = new TreeMap<>();
        StringBuilder tsv = new StringBuilder("address\tblock\tname\tsize\tconvention\tncallers\tncallees\n");
        int ok = 0, fail = 0;
        for (Function f : currentProgram.getListing().getFunctions(true)) {
            if (monitor.isCancelled())
                break;
            if (f.isThunk())
                continue;
            MemoryBlock b = currentProgram.getMemory().getBlock(f.getEntryPoint());
            String bn = b == null ? "unknown" : b.getName();
            tsv.append(f.getEntryPoint()).append('\t').append(bn).append('\t').append(f.getName())
               .append('\t').append(f.getBody().getNumAddresses()).append('\t')
               .append(f.getCallingConventionName()).append('\t')
               .append(f.getCallingFunctions(monitor).size()).append('\t')
               .append(f.getCalledFunctions(monitor).size()).append('\n');
            StringBuilder sb = files.computeIfAbsent(bn, k -> new StringBuilder(
                "/* Ghidra draft decompilation of block " + k + " - NOT final source */\n\n"));
            sb.append("/* ==== ").append(f.getEntryPoint()).append("  ").append(f.getName())
              .append(" ==== */\n");
            DecompileResults r = di.decompileFunction(f, 180, monitor);
            if (r != null && r.decompileCompleted()) {
                sb.append(r.getDecompiledFunction().getC()).append('\n');
                ok++;
            } else {
                sb.append("/* decompilation failed: ")
                  .append(r == null ? "null" : r.getErrorMessage()).append(" */\n\n");
                fail++;
            }
        }
        for (Map.Entry<String, StringBuilder> e : files.entrySet())
            Files.writeString(out.resolve(e.getKey() + ".c"), e.getValue().toString());
        Files.writeString(out.resolve("functions.tsv"), tsv.toString());
        println("ExportDecomp: " + ok + " ok, " + fail + " failed");
    }
}
