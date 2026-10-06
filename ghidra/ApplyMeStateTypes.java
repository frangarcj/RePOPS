// Typed ME state in a fresh analysis copy. Synthetic backing stays uninitialized;
// cached/uncached addresses are a byte-mapped alias, not independent allocations.
// @category RePops
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.UnsignedIntegerDataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryAccessException;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;
import com.google.gson.GsonBuilder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.*;

public class ApplyMeStateTypes extends GhidraScript {
    private DataType type(String name) {
        DataType type = currentProgram.getDataTypeManager().getDataType("/RePops/Recovered/" + name);
        if (type == null) throw new IllegalStateException("ImportStateLayouts must run first: " + name);
        return type;
    }

    private void bind(long address, String name, String type) throws Exception {
        createData(toAddr(address), type(type));
        currentProgram.getSymbolTable().createLabel(toAddr(address), name, SourceType.USER_DEFINED).setPrimary();
    }

    private Map<String,Object> blockInfo(MemoryBlock block) {
        Map<String,Object> row = new LinkedHashMap<>();
        row.put("name", block.getName()); row.put("start", block.getStart().toString());
        row.put("size", block.getSize()); row.put("initialized", block.isInitialized());
        row.put("mapped", block.isMapped()); row.put("execute", block.isExecute());
        row.put("volatile_analysis", block.isVolatile());
        boolean readableValue;
        try { currentProgram.getMemory().getByte(block.getStart()); readableValue = true; }
        catch (MemoryAccessException expected) { readableValue = false; }
        row.put("initial_byte_has_value", readableValue);
        return row;
    }

    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1 ||
            !"6a4aea3f731336916db97194c1a27983c18297c2dfcb1a1a328fd4ff8b09c8e0"
                .equals(currentProgram.getExecutableSHA256()) ||
            currentProgram.getImageBase().getOffset() != 0 ||
            !"Allegrex:LE:32:default".equals(currentProgram.getLanguageID().getIdAsString()))
            throw new IllegalArgumentException("Expected pinned base-zero Allegrex image and new output directory");
        Path out = Path.of(args[0]);
        if (Files.exists(out)) throw new IllegalArgumentException("Refusing existing export directory");
        Memory memory = currentProgram.getMemory();
        for (long address : new long[]{0x09F40000, 0x49F40000, 0x09FF0000})
            if (memory.getBlock(toAddr(address)) != null)
                throw new IllegalStateException("Use a fresh import; ME analysis region already exists");
        /* Known prefix plus the range selected by the 16-bit ADPCM address
         * multiplied by eight. This is analysis backing, not a recovered malloc. */
        final long sharedBytes = 0x802C0;
        MemoryBlock cached = memory.createUninitializedBlock("repops_me_shared_cached", toAddr(0x09F40000), sharedBytes, false);
        MemoryBlock uncached = memory.createByteMappedBlock("repops_me_shared_uncached", toAddr(0x49F40000), toAddr(0x09F40000), sharedBytes, false);
        MemoryBlock mixer = memory.createUninitializedBlock("repops_me_mixer", toAddr(0x09FF0000), type("MeMixerPrefix").getLength(), false);
        for (MemoryBlock b : new MemoryBlock[]{cached, uncached, mixer}) {
            b.setRead(true); b.setWrite(true); b.setExecute(false);
            b.setComment("RePops analysis-only uninitialized state. No initial bytes are supplied by the ELF.");
        }
        /* Producer mailboxes have asynchronous readers/writers. Byte mapping
         * alone does not prevent the decompiler moving loads past alias stores. */
        cached.setVolatile(true);
        uncached.setVolatile(true);
        bind(0x09F40000, "me_shared_cached", "MeSharedPrefix");
        bind(0x49F40000, "me_shared_uncached", "MeSharedPrefix");
        bind(0x09FF0000, "me_mixer", "MeMixerPrefix");

        disassemble(toAddr(0));
        Function f = getFunctionAt(toAddr(0));
        if (f == null) f = currentProgram.getFunctionManager().createFunction("pops_me_audio_callback", toAddr(0),
                new AddressSet(toAddr(0), toAddr(0x19DF)), SourceType.USER_DEFINED);
        if (f == null) throw new IllegalStateException("Cannot create the bounded ME callback view");
        f.setBody(new AddressSet(toAddr(0), toAddr(0x19DF)));
        f.setName("pops_me_audio_callback", SourceType.USER_DEFINED);
        f.setReturnType(UnsignedIntegerDataType.dataType, SourceType.USER_DEFINED);
        f.setComment("RePops bounded ME callback view. GP is set to shared uncached state, FP is its cached alias, and RA becomes the private mixer base after saving caller RA. Types describe synthetic uninitialized analysis memory, not initial hardware values or a complete C reconstruction.");

        Files.createDirectories(out);
        DecompInterface decompiler = new DecompInterface();
        Map<String,Object> report = new LinkedHashMap<>();
        report.put("input_sha256", currentProgram.getExecutableSHA256());
        report.put("shared_prefix_bytes", type("MeSharedPrefix").getLength());
        report.put("mixer_prefix_bytes", type("MeMixerPrefix").getLength());
        report.put("synthetic_blocks", Arrays.asList(blockInfo(cached), blockInfo(uncached), blockInfo(mixer)));
        report.put("alias_source", "09f40000"); report.put("alias_destination", "49f40000");
        report.put("function_body_bytes", f.getBody().getNumAddresses());
        report.put("native_runtime_modified", false);
        report.put("shared_volatile_is_analysis_annotation", true);
        try {
            if (!decompiler.openProgram(RePopsDecompilerView.withoutGpSpacebase(currentProgram, out)))
                throw new IllegalStateException("Decompiler open failed: " + decompiler.getLastMessage());
            DecompileResults result = decompiler.decompileFunction(f, 120, monitor);
            report.put("completed", result.decompileCompleted()); report.put("error", result.getErrorMessage());
            if (result.decompileCompleted() && result.getDecompiledFunction() != null) {
                String code = result.getDecompiledFunction().getC();
                report.put("has_warnings", code.contains("WARNING"));
                report.put("shared_state_named", code.contains("me_shared_"));
                report.put("mixer_state_named", code.contains("me_mixer"));
                report.put("voice_layout_named", code.contains(".voices") || code.contains("->voices") || code.contains("MixerVoice"));
                Files.writeString(out.resolve("pops_me_audio_callback.c"),
                    "/* Typed decompiler view only. Preserve warnings; not a native implementation.\n" +
                    " * ME cached and uncached shared symbols alias one uninitialized backing.\n" +
                    " * Shared blocks are marked volatile for protocol ordering in this analysis. */\n\n" + code);
            }
        } finally {
            decompiler.dispose();
        }
        Files.writeString(out.resolve("me_state.json"), new GsonBuilder().setPrettyPrinting().create().toJson(report));
        println("RePops: ME state type view exported to " + out);
    }
}
