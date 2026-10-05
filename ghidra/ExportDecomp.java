// @category RePops
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;

public class ExportDecomp extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            throw new IllegalArgumentException(
                "usage: ExportDecomp.java <output.c> <addr-or-name> [addr-or-name...]"
            );
        }

        File out = new File(args[0]);
        File parent = out.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        try (BufferedWriter w = new BufferedWriter(new FileWriter(out))) {
            for (int i = 1; i < args.length; i++) {
                String key = args[i];
                Function fn = null;

                if (key.startsWith("0x") || key.matches("[0-9A-Fa-f]+")) {
                    long value = Long.decode(key.startsWith("0x") ? key : "0x" + key);
                    Address addr = currentProgram.getAddressFactory()
                        .getDefaultAddressSpace().getAddress(value);
                    fn = currentProgram.getFunctionManager().getFunctionAt(addr);
                } else {
                    for (Function candidate :
                            currentProgram.getFunctionManager().getFunctions(true)) {
                        if (candidate.getName().equals(key)) {
                            fn = candidate;
                            break;
                        }
                    }
                }

                if (fn == null) {
                    w.write("/* missing function: " + key + " */\n\n");
                    continue;
                }

                DecompileResults result = decomp.decompileFunction(fn, 60, monitor);
                w.write("/* " + fn.getName() + " @ " + fn.getEntryPoint() + " */\n");
                if (result.decompileCompleted() &&
                        result.getDecompiledFunction() != null) {
                    w.write(result.getDecompiledFunction().getC());
                } else {
                    w.write("/* decompilation failed: " +
                        result.getErrorMessage() + " */\n");
                }
                w.write("\n\n");
            }
        } finally {
            decomp.dispose();
        }

        println("RePops: wrote decompilation to " + out.getAbsolutePath());
    }
}
