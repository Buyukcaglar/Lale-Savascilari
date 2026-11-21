// ApplyNames.java - apply curated names/comments from analysis/names.tsv.
//
// TSV columns:  address (SSSS:OOOO)  name  [comment]
// Lines starting with '#' are ignored.  A function is created at the address if
// analysis did not find one.  The comment becomes the function's plate comment.
//@category Lale

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;

import java.nio.file.*;
import java.util.*;

public class ApplyNames extends GhidraScript {
    @Override
    public void run() throws Exception {
        Path p = Path.of(getScriptArgs()[0]);
        if (!Files.exists(p)) {
            println("ApplyNames: no " + p);
            return;
        }
        SegmentedAddressSpace sp = (SegmentedAddressSpace) currentProgram.getAddressFactory()
                .getDefaultAddressSpace();
        int n = 0, bad = 0;
        for (String line : Files.readAllLines(p)) {
            if (line.isBlank() || line.startsWith("#") || line.startsWith("address\t"))
                continue;
            String[] c = line.split("\t", -1);
            String[] so = c[0].split(":");
            Address a = sp.getAddress(Integer.parseInt(so[0], 16), Integer.parseInt(so[1], 16));
            Function f = getFunctionAt(a);
            if (f == null) {
                disassemble(a);
                f = createFunction(a, c[1]);
            }
            if (f == null) {
                println("ApplyNames: cannot create function at " + c[0]);
                bad++;
                continue;
            }
            f.setName(c[1], SourceType.USER_DEFINED);
            if (c.length > 2 && !c[2].isBlank())
                f.setComment(c[2]);
            n++;
        }
        println("ApplyNames: " + n + " names applied, " + bad + " failed");
    }
}
