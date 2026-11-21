// ExportListing.java - write an annotated assembly listing per memory block,
// plus the FPU-restored memory image, from the analysed Ghidra program.
//
// Output (directory = first script argument):
//   asm/<block>.asm     address, bytes, instruction, labels, xrefs, strings
//   <exe>_fpu.bin       linear image with x87 restored (same layout as
//                       build/<exe>_flat.bin) for capstone/unicorn tooling
//@category Lale

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

import java.io.*;
import java.nio.file.*;
import java.util.*;

public class ExportListing extends GhidraScript {

    private String refText(Reference r) {
        Address to = r.getToAddress();
        Data d = getDataAt(to);
        if (d != null && d.hasStringValue()) {
            String s = StringDataInstance.getStringDataInstance(d).getStringValue();
            if (s != null) {
                if (s.length() > 60)
                    s = s.substring(0, 60) + "...";
                return "\"" + s.replace("\n", "\\n").replace("\r", "\\r") + "\"";
            }
        }
        Symbol s = getSymbolAt(to);
        return s != null ? s.getName() : null;
    }

    private static final java.util.regex.Pattern SEGOFF =
        java.util.regex.Pattern.compile("0x([0-9a-f]{1,4}):([0-9a-f]{1,4})");

    /** Ghidra prints flow targets with a normalised segment (0x1000:3554);
     *  rewrite them as SSSS:OOOO relative to the block's own segment. */
    private String canon(String text, MemoryBlock b) {
        int seg = ((SegmentedAddress) b.getStart()).getSegment();
        long lo = b.getStart().getOffset(), hi = b.getEnd().getOffset();
        java.util.regex.Matcher m = SEGOFF.matcher(text);
        StringBuffer out = new StringBuffer();
        while (m.find()) {
            long lin = Long.parseLong(m.group(1), 16) * 16 + Long.parseLong(m.group(2), 16);
            String rep;
            if (lin >= lo && lin <= hi)
                rep = String.format("%04X:%04X", seg, lin - (long) seg * 16);
            else
                rep = m.group(0);
            m.appendReplacement(out, rep);
        }
        m.appendTail(out);
        return out.toString();
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Path out = Path.of(args[0]);
        String exe = args.length > 1 ? args[1] : "ist";
        Files.createDirectories(out.resolve("asm"));
        Memory mem = currentProgram.getMemory();
        Listing l = currentProgram.getListing();
        ReferenceManager rm = currentProgram.getReferenceManager();

        // FPU-restored linear image
        long maxLin = 0;
        for (MemoryBlock b : mem.getBlocks())
            maxLin = Math.max(maxLin, ((SegmentedAddress) b.getEnd()).getOffset() + 1);
        byte[] img = new byte[(int) maxLin];
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized())
                continue;
            byte[] buf = new byte[(int) b.getSize()];
            b.getBytes(b.getStart(), buf);
            System.arraycopy(buf, 0, img, (int) b.getStart().getOffset(), buf.length);
        }
        Files.write(out.resolve(exe + "_fpu.bin"), img);

        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isExecute())
                continue;
            StringBuilder sb = new StringBuilder();
            sb.append("; block ").append(b.getName()).append("  ").append(b.getStart())
              .append(" - ").append(b.getEnd()).append("\n");
            CodeUnitIterator it = l.getCodeUnits(new AddressSet(b.getStart(), b.getEnd()), true);
            while (it.hasNext()) {
                CodeUnit cu = it.next();
                Address a = cu.getAddress();
                Function f = getFunctionAt(a);
                if (f != null) {
                    sb.append("\n; ==================================================\n");
                    sb.append("; FUNCTION ").append(f.getName());
                    if (f.isThunk() && f.getThunkedFunction(false) != null)
                        sb.append("  (thunk -> ").append(f.getThunkedFunction(false).getName()).append(")");
                    sb.append("\n");
                    Set<Function> callers = f.getCallingFunctions(monitor);
                    if (!callers.isEmpty()) {
                        sb.append("; called from:");
                        int k = 0;
                        for (Function c : callers) {
                            if (k++ == 12) {
                                sb.append(" ...");
                                break;
                            }
                            sb.append(' ').append(c.getName());
                        }
                        sb.append('\n');
                    }
                } else {
                    Symbol s = getSymbolAt(a);
                    if (s != null && cu instanceof Instruction)
                        sb.append(s.getName()).append(":\n");
                }
                StringBuilder bytes = new StringBuilder();
                try {
                    for (byte x : cu.getBytes())
                        bytes.append(String.format("%02X", x));
                } catch (Exception ex) {
                    bytes.append("??");
                }
                String bs = bytes.length() > 16 ? bytes.substring(0, 16) + "+" : bytes.toString();
                sb.append(String.format("  %s  %-17s ", a, bs));
                if (cu instanceof Instruction) {
                    Instruction ins = (Instruction) cu;
                    String txt = canon(ins.toString(), b);
                    byte[] ib = ins.getBytes();
                    if (ib.length == 5 && ((ib[0] & 0xFF) == 0x9A || (ib[0] & 0xFF) == 0xEA)) {
                        int off = (ib[1] & 0xFF) | (ib[2] & 0xFF) << 8;
                        int seg = (ib[3] & 0xFF) | (ib[4] & 0xFF) << 8;
                        txt = ins.getMnemonicString() + " " + String.format("%04X:%04X", seg, off);
                    }
                    sb.append(txt);
                }
                else {
                    Data d = (Data) cu;
                    sb.append(d.getMnemonicString()).append(' ').append(d.getDefaultValueRepresentation());
                }
                StringBuilder cm = new StringBuilder();
                for (Reference r : cu.getReferencesFrom()) {
                    if (r.getReferenceType().isCall()) {
                        Function cf = getFunctionAt(r.getToAddress());
                        if (cf != null) {
                            if (cf.isThunk() && cf.getThunkedFunction(true) != null)
                                cf = cf.getThunkedFunction(true);
                            cm.append(" -> ").append(cf.getName());
                        }
                        continue;
                    }
                    if (r.getReferenceType().isFlow())
                        continue;
                    String t = refText(r);
                    if (t != null)
                        cm.append(' ').append(t);
                }
                if (cm.length() > 0)
                    sb.append("    ;").append(cm);
                sb.append('\n');
            }
            Files.writeString(out.resolve("asm").resolve(b.getName() + ".asm"), sb.toString());
        }
        println("ExportListing: done");
    }
}
