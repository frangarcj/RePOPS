// Seed the separate entry supplied to POPSMAN's Media Engine startup service.
// Use only read-only processing of the pinned POPS database.
// @category RePops
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.UnsignedIntegerDataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.symbol.SourceType;

public class AnalyzeMeCallback extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String expected = "6a4aea3f731336916db97194c1a27983c18297c2dfcb1a1a328fd4ff8b09c8e0";
        if (!expected.equals(currentProgram.getExecutableSHA256()) ||
                currentProgram.getImageBase().getOffset() != 0) {
            throw new IllegalArgumentException("Expected hash-pinned, base-zero POPS reference");
        }
        Address entry = toAddr(0);
        disassemble(entry);
        Function function = getFunctionAt(entry);
        if (function == null) {
            function = createFunction(entry, "pops_me_audio_callback");
        }
        if (function == null) {
            throw new IllegalStateException("Unable to recover Media Engine callback at zero");
        }
        function.setName("pops_me_audio_callback", SourceType.ANALYSIS);
        function.setReturnType(UnsignedIntegerDataType.dataType, SourceType.ANALYSIS);
        currentProgram.getListing().setComment(entry, CodeUnit.PLATE_COMMENT,
            "Entry passed at POPS +0x1A038 to sceMeAudio_DE630CD2, with stack 0x09FF8000. " +
            "The provider consumes the returned V0 as two 16-bit values. " +
            "Recovered boundaries/pseudocode still require verification.");
        analyzeChanges(currentProgram);
        println("RePops: ME callback body " + function.getBody() + "; bytes " +
            function.getBody().getNumAddresses());
    }
}
