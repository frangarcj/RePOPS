// Import recovered types into a NEW analysis project and export a reusable archive.
// No memory is retyped: core scratchpad and base-zero module offsets overlap.
// @category RePops
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.CodeUnit;
import com.google.gson.*;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.*;
import java.util.regex.*;

public class ImportStateLayouts extends GhidraScript {
    private final CategoryPath category = new CategoryPath("/RePops/Recovered");
    private final Map<String, DataType> resolved = new LinkedHashMap<>();
    private JsonObject definitions;

    private int number(String value) { return Integer.decode(value); }

    private DataType wireType(String name) {
        if (resolved.containsKey(name)) return resolved.get(name);
        switch (name) {
            case "u8": case "bytes": return ByteDataType.dataType;
            case "i8": return SignedByteDataType.dataType;
            case "u16": return UnsignedShortDataType.dataType;
            case "i16": return ShortDataType.dataType;
            case "u32": return UnsignedIntegerDataType.dataType;
            case "i32": case "i32_fixed_point": return IntegerDataType.dataType;
            case "guest_address32": case "guest_code_address32":
                DataType alias = new TypedefDataType(category, name, UnsignedIntegerDataType.dataType);
                resolved.put(name, alias);
                return alias;
            default: break;
        }
        Matcher array = Pattern.compile("(.+)\\[(\\d+)\\]").matcher(name);
        if (array.matches()) {
            DataType element = wireType(array.group(1));
            return new ArrayDataType(element, Integer.parseInt(array.group(2)), element.getLength());
        }
        if (!definitions.has(name)) throw new IllegalArgumentException("Unknown wire type: " + name);
        return buildType(name);
    }

    private void field(StructureDataType target, int offset, String name, DataType type, String comment) {
        if (offset < 0 || offset + type.getLength() > target.getLength())
            throw new IllegalArgumentException("Field outside " + target.getName() + ": " + name);
        target.replaceAtOffset(offset, type, type.getLength(), name, comment);
    }

    private DataType buildType(String name) {
        if (resolved.containsKey(name)) return resolved.get(name);
        JsonObject spec = definitions.getAsJsonObject(name);
        StructureDataType structure = new StructureDataType(category, name, number(spec.get("size").getAsString()));
        structure.setDescription("Recovered layout; " + spec.get("confidence").getAsString() +
            ". Descriptive names, not recovered source identifiers. See state_layout.json.");
        resolved.put(name, structure);
        JsonArray fields = spec.has("fields") ? spec.getAsJsonArray("fields") : spec.getAsJsonArray("analysis_fields");
        int end = 0;
        for (JsonElement row : fields) {
            JsonArray f = row.getAsJsonArray();
            int offset = number(f.get(0).getAsString());
            if (offset != end) throw new IllegalArgumentException("Unexpected gap/overlap in " + name);
            DataType type = wireType(f.get(2).getAsString());
            field(structure, offset, f.get(1).getAsString(), type, "Offset " + f.get(0).getAsString());
            end = offset + type.getLength();
        }
        if (end != structure.getLength()) throw new IllegalArgumentException("Size mismatch: " + name);
        if (name.equals("InstructionRecord")) {
            StructureDataType tag = new StructureDataType(category, "RecordAnalysisTag", 4);
            field(tag, 0, "category", wireType("u16"), "Analysis phase");
            field(tag, 2, "boundary_cost", wireType("u16"), "Cost pass");
            UnionDataType tagOrEntry = new UnionDataType(category, "RecordTagOrEntry");
            tagOrEntry.add(tag, 4, "analysis", "Before emission");
            tagOrEntry.add(wireType("guest_code_address32"), 4, "emitted_entry", "Entry publication phase");
            structure.clearAtOffset(4); structure.clearAtOffset(6);
            field(structure, 4, "tag_or_entry", tagOrEntry, "Phase-dependent overlay; not simultaneous values");
            StructureDataType sources = new StructureDataType(category, "RecordSources", 4);
            String[] names = {"left", "right", "coprocessor_field", "operation_cost"};
            for (int i = 0; i < 4; i++) field(sources, i, names[i], wireType("u8"), "Analysis phase");
            UnionDataType sourcesOrPatch = new UnionDataType(category, "RecordSourcesOrPatch");
            sourcesOrPatch.add(sources, 4, "analysis", "Before emission");
            sourcesOrPatch.add(wireType("guest_address32"), 4, "patch_site", "Linking phase");
            for (int i = 12; i < 16; i++) structure.clearAtOffset(i);
            field(structure, 12, "sources_or_patch", sourcesOrPatch, "Phase-dependent overlay");
        }
        return structure;
    }

    private DataType coreView(JsonObject schema) {
        StructureDataType core = new StructureDataType(category, "CoreStatePartial", 0x4000);
        core.setDescription("Partial 16-KiB scratchpad view. Unknown gaps retained. Never apply to module code at 0x10000.");
        for (JsonElement row : schema.getAsJsonArray("core_fields")) {
            JsonArray f = row.getAsJsonArray();
            field(core, number(f.get(0).getAsString()), f.get(1).getAsString(),
                  wireType(f.get(2).getAsString()), "Recovered role; see state_layout.json");
        }
        field(core, 0x64C, "timers", wireType("PopsTimer[3]"), "Original stride 0x20");
        field(core, 0x1000, "io_handlers", wireType("IoHandlerPair[512]"), "Guest code targets, not host pointers");
        field(core, 0x35D0, "rate_event", wireType("PopsEvent"), "Quota at +0x35E0 is separate");
        field(core, 0x35E0, "rate_quota", wireType("u32"), "Recovered counter role");
        field(core, 0x35E4, "frame_phase", wireType("FramePhaseState"), "Callback at +0x35F0");
        field(core, 0x35F8, "display_event", wireType("PopsEvent"), "Callback at +0x3604");
        return core;
    }

    private Map<String,Object> describe(DataType type) {
        Map<String,Object> report = new LinkedHashMap<>();
        report.put("path", type.getPathName()); report.put("size", type.getLength());
        List<Map<String,Object>> fields = new ArrayList<>();
        if (type instanceof Structure) {
            for (DataTypeComponent f : ((Structure)type).getDefinedComponents()) {
                Map<String,Object> row = new LinkedHashMap<>();
                row.put("offset", f.getOffset()); row.put("name", f.getFieldName());
                row.put("size", f.getLength()); row.put("type", f.getDataType().getPathName());
                fields.add(row);
            }
        }
        report.put("fields", fields); return report;
    }

    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2) throw new IllegalArgumentException("usage: ImportStateLayouts.java <schema.json> <new-output-directory>");
        JsonObject schema = JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonObject();
        if (!schema.get("input_sha256").getAsString().equals(currentProgram.getExecutableSHA256()) ||
                currentProgram.getImageBase().getOffset() != 0 ||
                !currentProgram.getLanguageID().getIdAsString().equals("Allegrex:LE:32:default"))
            throw new IllegalArgumentException("Expected pinned base-zero Allegrex POPS");
        Path out = Path.of(args[1]);
        if (Files.exists(out)) throw new IllegalArgumentException("Refusing existing output directory");
        definitions = schema.getAsJsonObject("types");
        DataTypeManager programTypes = currentProgram.getDataTypeManager();
        List<DataType> roots = new ArrayList<>();
        for (String name : definitions.keySet()) roots.add(programTypes.addDataType(buildType(name), DataTypeConflictHandler.REPLACE_HANDLER));
        roots.add(programTypes.addDataType(coreView(schema), DataTypeConflictHandler.REPLACE_HANDLER));
        currentProgram.getListing().setComment(toAddr(0x1C254), CodeUnit.PLATE_COMMENT,
            "RePops: initializer uses incoming GP, not an absolute 0x10000. Caller +0x1BF40 sets GP=0x09FF8000. Clears 0xB04 bytes; distinct alternate context. Do not propagate a universal core GP here.");
        currentProgram.getListing().setComment(toAddr(0), CodeUnit.PLATE_COMMENT,
            "RePops ME callback: GP=0x49F40000, FP=0x09F40000 alias, RA=0x09FF0000 mixer base. Caller RA saved on stack. See /RePops/Recovered/MixerVoice.");
        currentProgram.getListing().setComment(toAddr(0x945C), CodeUnit.PLATE_COMMENT,
            "RePops event insertion: A0 refers to a 16-byte PopsEvent; next/prev/deadline/callback at +0/+4/+8/+C. Guest addresses stay 32-bit. Core sentinel is a separate overlapping layout.");
        Files.createDirectories(out);
        FileDataTypeManager archive = FileDataTypeManager.createFileArchive(out.resolve("repops_types.gdt").toFile());
        int tx = archive.startTransaction("Recovered state layouts");
        boolean success = false;
        try {
            for (DataType type : roots) archive.addDataType(type, DataTypeConflictHandler.REPLACE_HANDLER);
            success = true;
        } finally {
            archive.endTransaction(tx, success);
            try { if (success) archive.save(); } finally { archive.close(); }
        }
        List<Map<String,Object>> layouts = new ArrayList<>();
        for (DataType type : roots) layouts.add(describe(type));
        Map<String,Object> report = new LinkedHashMap<>();
        report.put("input_sha256", currentProgram.getExecutableSHA256());
        report.put("language", currentProgram.getLanguageID().getIdAsString());
        report.put("types", layouts); report.put("memory_data_applied", false);
        report.put("reason", "Base-zero module offsets overlap core scratchpad; register contexts are not globally interchangeable.");
        Files.writeString(out.resolve("types.json"), new GsonBuilder().setPrettyPrinting().create().toJson(report));
        println("RePops: imported " + roots.size() + " root layouts; exported repops_types.gdt; original bytes unchanged.");
    }
}
