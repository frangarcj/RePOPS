// Export one raw C file per function, plus machine-readable provenance.
// @category RePops
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.reloc.Relocation;
import com.google.gson.GsonBuilder;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class ExportAllDecomp extends GhidraScript {
    private String hex(long value) {
        return String.format("0x%08X", value);
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) {
            throw new IllegalArgumentException("ExportAllDecomp.java <new-output-directory>");
        }
        Path output = Path.of(args[0]);
        if (Files.exists(output)) {
            try (var stream = Files.list(output)) {
                if (stream.findAny().isPresent()) {
                    throw new IllegalArgumentException("Refusing to overwrite nonempty " + output);
                }
            }
        }
        Files.createDirectories(output);

        Map<String, Object> report = new LinkedHashMap<>();
        report.put("program", currentProgram.getName());
        report.put("input_sha256", currentProgram.getExecutableSHA256());
        report.put("language", currentProgram.getLanguageID().getIdAsString());
        report.put("image_base", currentProgram.getImageBase().toString());
        report.put("status", "raw_decompiler_output_not_verified_source");
        List<Object> blocks = new ArrayList<>();
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            Map<String, Object> row = new LinkedHashMap<>();
            row.put("name", b.getName());
            row.put("start", b.getStart().toString());
            row.put("end", b.getEnd().toString());
            row.put("execute", b.isExecute());
            row.put("initialized", b.isInitialized());
            blocks.add(row);
        }
        report.put("memory_blocks", blocks);
        Map<String, Integer> relocationStatuses = new LinkedHashMap<>();
        Iterator<Relocation> relocs = currentProgram.getRelocationTable().getRelocations();
        while (relocs.hasNext()) {
            Relocation r = relocs.next();
            String status = r.getStatus().toString();
            relocationStatuses.merge(status, 1, Integer::sum);
        }
        report.put("relocations_by_status", relocationStatuses);

        List<Object> functions = new ArrayList<>();
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException(decompiler.getLastMessage());
        }
        int total = 0, completed = 0;
        try {
            FunctionIterator iterator = currentProgram.getFunctionManager().getFunctions(true);
            while (iterator.hasNext()) {
                monitor.checkCancelled();
                Function f = iterator.next();
                long address = f.getEntryPoint().getOffset();
                String safeName = f.getName().replaceAll("[^A-Za-z0-9_]", "_");
                String filename = String.format("%08X_%s.c", address, safeName);
                long startTime = System.nanoTime();
                DecompileResults result = decompiler.decompileFunction(f, 30, monitor);
                boolean ok = result.decompileCompleted() && result.getDecompiledFunction() != null;
                String code = ok ? result.getDecompiledFunction().getC() : "";
                String error = result.getErrorMessage();
                Map<String, Object> row = new LinkedHashMap<>();
                row.put("address", hex(address));
                row.put("name", f.getName());
                row.put("filename", filename);
                row.put("body_bytes", f.getBody().getNumAddresses());
                row.put("is_thunk", f.isThunk());
                row.put("decompile_completed", ok);
                row.put("has_warnings", code.contains("WARNING:"));
                row.put("has_unrecovered_jumptable", code.contains("UNRECOVERED_JUMPTABLE"));
                row.put("error", error == null ? "" : error);
                row.put("elapsed_ms", (System.nanoTime() - startTime) / 1000000);
                row.put("validation", "not_executed_not_equivalence_proven");
                List<Function> callees = new ArrayList<>(f.getCalledFunctions(monitor));
                callees.sort(Comparator.comparing(Function::getEntryPoint));
                List<Object> calls = new ArrayList<>();
                for (Function callee : callees) {
                    Map<String, Object> call = new LinkedHashMap<>();
                    call.put("address", callee.getEntryPoint().toString());
                    call.put("name", callee.getName());
                    calls.add(call);
                }
                row.put("known_callees", calls);
                String text = "/* RAW GHIDRA OUTPUT. Not a verified or portable implementation.\n"
                    + " * Function: " + f.getName() + " @ " + hex(address) + "\n"
                    + " * Original SHA-256: " + currentProgram.getExecutableSHA256() + "\n"
                    + " * Keep warnings and consult index.json before reconstructing.\n */\n\n";
                if (ok) {
                    text += code;
                    completed++;
                } else {
                    text += "/* DECOMPILATION FAILED. See index.json for the error. */\n";
                }
                Files.writeString(output.resolve(filename), text, StandardCharsets.UTF_8);
                functions.add(row);
                total++;
                if (total % 100 == 0) {
                    println("RePops: exported " + total + " functions, " + completed + " decompiled");
                }
            }
        } finally {
            decompiler.dispose();
        }
        report.put("function_count", total);
        report.put("decompile_completed", completed);
        report.put("functions", functions);
        Files.writeString(output.resolve("index.json"),
            new GsonBuilder().setPrettyPrinting().create().toJson(report), StandardCharsets.UTF_8);
        Files.writeString(output.resolve("README.md"),
            "# Raw POPS decompilation\n\nOne file per Ghidra function, not a buildable source tree.\n"
            + "Names and boundaries may be provisional. A successful decompile does not prove equivalence.\n"
            + "See index.json for input hash, language, relocations, calls, errors and warnings.\n"
            + "This folder is generated and excluded from Git.\n", StandardCharsets.UTF_8);
        println("RePops: " + completed + "/" + total + " decompiled into " + output);
    }
}
