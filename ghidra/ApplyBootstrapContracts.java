// Apply only bootstrap facts checked against original MIPS and POPSMAN.
// @category RePops
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.data.IntegerDataType;
import ghidra.program.model.data.VoidDataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.ParameterImpl;
import ghidra.program.model.symbol.SourceType;

public class ApplyBootstrapContracts extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String hash = "6a4aea3f731336916db97194c1a27983c18297c2dfcb1a1a328fd4ff8b09c8e0";
        if (!hash.equalsIgnoreCase(currentProgram.getExecutableSHA256()) ||
                currentProgram.getImageBase().getOffset() != 0) {
            throw new IllegalArgumentException("This annotation is specific to the known POPS 6.60 image at base zero");
        }
        Function exit = currentProgram.getFunctionManager().getFunctionAt(toAddr(0x3D40CL));
        Function start = currentProgram.getFunctionManager().getFunctionAt(toAddr(0x16000L));
        Function main = currentProgram.getFunctionManager().getFunctionAt(toAddr(0x16080L));
        if (exit == null || start == null || main == null) {
            throw new IllegalStateException("Run import and function analysis first");
        }
        exit.setNoReturn(true);
        exit.setReturnType(VoidDataType.dataType, SourceType.USER_DEFINED);
        exit.replaceParameters(Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS,
            true, SourceType.USER_DEFINED,
            new ParameterImpl("error", IntegerDataType.dataType, currentProgram));
        exit.setComment("POPSMAN 6.60 exits through LoadExec; its implementation does not return. "
            + "Evidence: ARK-3 contrib/PSP/popsman.s, 0x1F58..0x201C.");
        start.setBody(new AddressSet(toAddr(0x16000L), toAddr(0x1607FL)));
        start.setReturnType(IntegerDataType.dataType, SourceType.USER_DEFINED);
        start.setComment("Boot function ends with the no-return exit call at 0x16078 and its delay slot. "
            + "Do not absorb popsMainThread at 0x16080 into this function.");
        main.setBody(new AddressSet(toAddr(0x16080L), toAddr(0x161A7L)));
        main.setComment("popsmain entry passed to sceKernelCreateThread at 0x1604C. "
            + "This function terminates through the exit service.");
        analyzeChanges(currentProgram);
        println("RePops: applied no-return exit contract and bootstrap boundaries");
    }
}
