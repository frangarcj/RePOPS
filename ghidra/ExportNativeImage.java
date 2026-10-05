// Export analysis memory for the native-C harness, not executable host code.
// @category RePops
import ghidra.app.script.GhidraScript;
import ghidra.program.model.mem.MemoryBlock;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.HexFormat;
import java.util.LinkedHashMap;
import java.util.Map;
import com.google.gson.GsonBuilder;

public class ExportNativeImage extends GhidraScript {
    public void run() throws Exception {
        String hash = "6a4aea3f731336916db97194c1a27983c18297c2dfcb1a1a328fd4ff8b09c8e0";
        String[] args = getScriptArgs();
        if (args.length != 1 || !hash.equals(currentProgram.getExecutableSHA256()) ||
                currentProgram.getImageBase().getOffset() != 0) {
            throw new IllegalArgumentException("Expected pinned base-zero POPS and new output directory");
        }
        Path out = Path.of(args[0]);
        if (Files.exists(out)) throw new IllegalArgumentException("Refusing existing output");
        int length = 0x4ae730;
        byte[] image = new byte[length];
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (block.isOverlay() || !block.isInitialized()) continue;
            long start = block.getStart().getOffset();
            long size = block.getSize();
            if (start < 0 || start + size > length) {
                throw new IllegalStateException("Unexpected program memory block: " + block.getName());
            }
            int read = currentProgram.getMemory().getBytes(block.getStart(), image, (int)start, (int)size);
            if (read != size) throw new IllegalStateException("Incomplete memory export");
        }
        Map<String,Object> manifest = new LinkedHashMap<>();
        manifest.put("source_sha256", hash);
        manifest.put("image_sha256", HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(image)));
        manifest.put("image_base", 0);
        manifest.put("image_bytes", length);
        manifest.put("language", currentProgram.getLanguageID().getIdAsString());
        manifest.put("kind", "relocated_analysis_memory_for_native_C_data_access");
        Files.createDirectories(out);
        Files.write(out.resolve("pops_image.bin"), image);
        Files.writeString(out.resolve("manifest.json"), new GsonBuilder().setPrettyPrinting().create().toJson(manifest));
        println("RePops: native-C data image exported, " + length + " bytes");
    }
}
