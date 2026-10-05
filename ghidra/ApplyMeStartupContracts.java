// Read-only annotations for the hash-pinned ARK POPSMAN reference.
// @category RePops
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.data.VoidDataType;

public class ApplyMeStartupContracts extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String hash = "83ed5373388ba2af57f26eba421eacdee66f56bd3e72432af38365ae8bf4d855";
        if (!hash.equals(currentProgram.getExecutableSHA256()) ||
                currentProgram.getImageBase().getOffset() != 0) {
            throw new IllegalArgumentException("Expected pinned base-zero POPSMAN");
        }
        Function boot = getFunctionAt(toAddr(0x35D8));
        if (boot == null) throw new IllegalStateException("Missing startup function");
        boot.setName("repops_me_startup_observed", SourceType.ANALYSIS);
        boot.setReturnType(VoidDataType.dataType, SourceType.ANALYSIS);
        currentProgram.getListing().setComment(toAddr(0x35D8), CodeUnit.PLATE_COMMENT,
            "Observed CPU-side startup: reset/clock calls, 96-byte bootstrap copy, " +
            "control write, clear ack, release reset, wait for ack == 1. No timeout. " +
            "Service/device responses are NOT verified hardware behavior.");
        currentProgram.getListing().setComment(toAddr(0x3514), CodeUnit.PLATE_COMMENT,
            "Observed control handshake: request = (argument != 0), save old ack bit 0; " +
            "call codec service with !request; wait for exact ack only if callback slot " +
            "is nonzero. Return saved bit. Semantic service name remains provisional.");
        currentProgram.getListing().setComment(toAddr(0x31A8), CodeUnit.EOL_COMMENT,
            "ME side copies the current control word to acknowledgement. " +
            "This is not an unconditional successful-start flag.");
        println("RePops: annotated startup and control handshake, without modifying bytes");
    }
}
