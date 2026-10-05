// @category RePops
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;

public class ExportFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            throw new IllegalArgumentException("usage: ExportFunctions.java <output.csv>");
        }

        File out = new File(args[0]);
        File parent = out.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }

        int count = 0;
        try (BufferedWriter w = new BufferedWriter(new FileWriter(out))) {
            w.write("entry,name,size,is_thunk,calling_convention\n");
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext()) {
                Function f = it.next();
                String name = f.getName().replace("\"", "\"\"");
                String cc = f.getCallingConventionName();
                w.write(String.format(
                    "0x%08X,\"%s\",%d,%s,\"%s\"\n",
                    f.getEntryPoint().getOffset(),
                    name,
                    f.getBody().getNumAddresses(),
                    f.isThunk() ? "1" : "0",
                    cc == null ? "" : cc.replace("\"", "\"\"")
                ));
                count++;
            }
        }

        println("RePops: exported " + count + " functions to " + out.getAbsolutePath());
    }
}
