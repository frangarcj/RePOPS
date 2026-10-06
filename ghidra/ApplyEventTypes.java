// Apply checked event/timer prototypes in a fresh analysis copy and export C.
// Runtime state addresses are not retyped: GP and module addresses can overlap.
// @category RePops
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.ParameterImpl;
import ghidra.program.model.listing.Program;
import ghidra.program.model.symbol.SourceType;
import com.google.gson.GsonBuilder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.*;

public class ApplyEventTypes extends GhidraScript {
    private boolean implicitCore;

    private Program contextView(Path out) throws Exception {
        return implicitCore ? RePopsDecompilerView.withoutGpSpacebase(currentProgram, out) : currentProgram;
    }
    private DataType pointerTo(String name) {
        DataType type = currentProgram.getDataTypeManager().getDataType("/RePops/Recovered/" + name);
        if (type == null) throw new IllegalStateException("Run ImportStateLayouts first: " + name);
        return new PointerDataType(type, 4, currentProgram.getDataTypeManager());
    }

    private Function contract(long begin, long end, String name, DataType result,
                              String[] paramNames, DataType[] paramTypes) throws Exception {
        disassemble(toAddr(begin));
        Function f = getFunctionAt(toAddr(begin));
        if (f == null) f = currentProgram.getFunctionManager().createFunction(name, toAddr(begin),
                new AddressSet(toAddr(begin), toAddr(end)), SourceType.USER_DEFINED);
        if (f == null) throw new IllegalStateException("Cannot create " + name);
        f.setBody(new AddressSet(toAddr(begin), toAddr(end)));
        f.setName(name, SourceType.USER_DEFINED);
        f.setReturnType(result, SourceType.USER_DEFINED);
        Parameter[] parameters = new Parameter[paramNames.length];
        for (int i = 0; i < parameters.length; ++i)
            parameters[i] = new ParameterImpl(paramNames[i], paramTypes[i], currentProgram);
        f.replaceParameters(Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS,
                true, SourceType.USER_DEFINED, parameters);
        if (implicitCore) {
            List<Parameter> explicit = new ArrayList<>();
            for (int i = 0; i < paramNames.length; ++i)
                explicit.add(new ParameterImpl(paramNames[i], paramTypes[i],
                        currentProgram.getRegister("a" + i), currentProgram));
            explicit.add(new ParameterImpl("core", pointerTo("CoreStatePartial"),
                    currentProgram.getRegister("gp"), currentProgram));
            f.replaceParameters(explicit, Function.FunctionUpdateType.CUSTOM_STORAGE,
                    true, SourceType.USER_DEFINED);
        }
        f.setComment("RePops: signature and bounded body checked against the pinned Allegrex listing. " +
            "Pointer arguments model 32-bit guest addresses, not host C pointers. " +
            (implicitCore ? "The core parameter is an analysis representation of the implicit GP register, not a new ABI argument." :
            "GP-relative globals remain unresolved between runtime scratchpad and module-offset storage."));
        return f;
    }

    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1 || args.length > 2 ||
            !"6a4aea3f731336916db97194c1a27983c18297c2dfcb1a1a328fd4ff8b09c8e0"
                .equals(currentProgram.getExecutableSHA256()) ||
            currentProgram.getImageBase().getOffset() != 0 ||
            !"Allegrex:LE:32:default".equals(currentProgram.getLanguageID().getIdAsString()))
            throw new IllegalArgumentException("Expected pinned base-zero Allegrex program and new output directory");
        if (args.length == 2 && !args[1].equals("implicit-core"))
            throw new IllegalArgumentException("Unknown mode: " + args[1]);
        implicitCore = args.length == 2;
        Path out = Path.of(args[0]);
        if (Files.exists(out)) throw new IllegalArgumentException("Refusing existing export directory");
        DataType event = pointerTo("PopsEvent");
        DataType timer = pointerTo("PopsTimer");
        DataType u32 = UnsignedIntegerDataType.dataType;
        List<Function> targets = new ArrayList<>();
        targets.add(contract(0x945C, 0x94C3, "pops_schedule_guest_event", VoidDataType.dataType,
                new String[]{"event", "delay_cycles"}, new DataType[]{event, u32}));
        targets.add(contract(0x9668, 0x96AB, "pops_remove_guest_event", VoidDataType.dataType,
                new String[]{"event"}, new DataType[]{event}));
        targets.add(contract(0x9B6C, 0x9BDF, "pops_synchronize_timer_counter", u32,
                new String[]{"timer"}, new DataType[]{timer}));
        targets.add(contract(0x9A54, 0x9ACF, "pops_schedule_timer_counter", VoidDataType.dataType,
                new String[]{"timer"}, new DataType[]{timer}));

        Files.createDirectories(out);
        DecompInterface decompiler = new DecompInterface();
        if (!decompiler.openProgram(contextView(out)))
            throw new IllegalStateException("Decompiler open failed: " + decompiler.getLastMessage());
        List<Map<String,Object>> rows = new ArrayList<>();
        try {
            for (Function f : targets) {
                DecompileResults result = decompiler.decompileFunction(f, 60, monitor);
                Map<String,Object> row = new LinkedHashMap<>();
                row.put("entry", f.getEntryPoint().toString());
                row.put("name", f.getName()); row.put("body_bytes", f.getBody().getNumAddresses());
                row.put("signature", f.getPrototypeString(false, false));
                row.put("completed", result.decompileCompleted()); row.put("error", result.getErrorMessage());
                List<String> storage = new ArrayList<>();
                for (Parameter p : f.getParameters()) storage.add(p.getName() + ":" + p.getVariableStorage());
                row.put("parameter_storage", storage);
                if (result.decompileCompleted() && result.getDecompiledFunction() != null) {
                    String code = result.getDecompiledFunction().getC();
                    row.put("has_warnings", code.contains("WARNING"));
                    row.put("named_field_access", code.contains("->"));
                    row.put("typed_core_access", code.contains("core->"));
                    Files.writeString(out.resolve(f.getName() + ".c"),
                        "/* Typed decompiler output, NOT native implementation or equivalence proof.\n" +
                        " * GP globals may still be represented as base-zero module-offset labels.\n" +
                        " * Keep warnings; the saved type contract only names pointer fields. */\n\n" + code);
                }
                rows.add(row);
            }
        } finally {
            decompiler.dispose();
        }
        Map<String,Object> report = new LinkedHashMap<>();
        report.put("input_sha256", currentProgram.getExecutableSHA256());
        report.put("functions", rows); report.put("original_bytes_modified", false);
        report.put("global_state_memory_typed", false);
        report.put("implicit_gp_parameter", implicitCore);
        report.put("gp_spacebase_disabled_in_local_decompiler_view_only", implicitCore);
        report.put("scope", "Four checked pointer/prototype contracts, not global GP recovery or new emulator code.");
        Files.writeString(out.resolve("contracts.json"), new GsonBuilder().setPrettyPrinting().create().toJson(report));
        println("RePops: exported " + rows.size() + " typed event/timer contracts to " + out);
    }
}
