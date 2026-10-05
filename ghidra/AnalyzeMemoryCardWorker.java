// Recover the thread entry passed by +0x1A950. Use read-only processing.
// @category RePops
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class AnalyzeMemoryCardWorker extends GhidraScript {
    @Override
    protected void run() throws Exception {
        if (!"6a4aea3f731336916db97194c1a27983c18297c2dfcb1a1a328fd4ff8b09c8e0"
                .equals(currentProgram.getExecutableSHA256()) ||
                currentProgram.getImageBase().getOffset() != 0) {
            throw new IllegalArgumentException("Expected pinned base-zero POPS");
        }
        long[] entries = {0x1AA90, 0x27EA8};
        String[] names = {"pops_memory_card_worker", "pops_mark_memory_cards_ready"};
        for (int i = 0; i < entries.length; ++i) {
            Address address = toAddr(entries[i]);
            disassemble(address);
            Function function = getFunctionAt(address);
            if (function == null) function = createFunction(address, names[i]);
            if (function == null) throw new IllegalStateException("Cannot recover " + address);
            function.setName(names[i], SourceType.ANALYSIS);
        }
        analyzeChanges(currentProgram);
        println("RePops: recovered memory-card worker entries for read-only export");
    }
}
