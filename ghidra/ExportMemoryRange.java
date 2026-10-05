// Export a bounded, relocated, initialized range for reproducible instruction tests.
// @category RePops
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.reloc.Relocation;
import com.google.gson.GsonBuilder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class ExportMemoryRange extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 3) {
            throw new IllegalArgumentException("ExportMemoryRange.java <new-dir> <start> <size>");
        }
        Path output = Path.of(args[0]);
        long start = Long.decode(args[1]);
        int size = Integer.decode(args[2]);
        if (start < 0 || size <= 0 || size > 1024 * 1024) {
            throw new IllegalArgumentException("Invalid bounded range");
        }
        if (Files.exists(output)) {
            throw new IllegalArgumentException("Refusing existing output " + output);
        }
        Address address = toAddr(start);
        byte[] bytes = new byte[size];
        int read = currentProgram.getMemory().getBytes(address, bytes);
        if (read != size) {
            throw new IllegalStateException("Incomplete initialized-memory read");
        }
        Map<String, Object> report = new LinkedHashMap<>();
        report.put("source_sha256", currentProgram.getExecutableSHA256());
        report.put("language", currentProgram.getLanguageID().getIdAsString());
        report.put("image_base", currentProgram.getImageBase().toString());
        report.put("start", start);
        report.put("size", size);
        report.put("sha256", HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes)));
        report.put("filename", "range.bin");
        report.put("kind", "initialized_memory_after_ghidra_relocation_not_original_file_bytes");
        List<Object> relocations = new ArrayList<>();
        Iterator<Relocation> iterator = currentProgram.getRelocationTable().getRelocations();
        while (iterator.hasNext()) {
            Relocation r = iterator.next();
            long where = r.getAddress().getOffset();
            if (where >= start && where < start + size) {
                Map<String, Object> item = new LinkedHashMap<>();
                item.put("address", String.format("0x%08X", where));
                item.put("status", r.getStatus().toString());
                relocations.add(item);
            }
        }
        report.put("relocations", relocations);
        Files.createDirectories(output);
        Files.write(output.resolve("range.bin"), bytes);
        Files.writeString(output.resolve("manifest.json"),
            new GsonBuilder().setPrettyPrinting().create().toJson(report));
        println("RePops: exported " + size + " relocated bytes to " + output);
    }
}
