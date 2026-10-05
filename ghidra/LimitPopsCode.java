// @category RePops
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;

public class LimitPopsCode extends GhidraScript {
    private static final long CODE_END = 0x0003D4A4L;

    private void seedFunction(long offset, String name) throws Exception {
        Address address = toAddr(offset);

        if (currentProgram.getListing().getInstructionAt(address) == null) {
            DisassembleCommand dis = new DisassembleCommand(address, null, true);
            if (!dis.applyTo(currentProgram, monitor)) {
                println("RePops: failed to disassemble " + name + " at " + address +
                    ": " + dis.getStatusMsg());
                return;
            }
        }

        Function fn = currentProgram.getFunctionManager().getFunctionAt(address);
        if (fn == null) {
            CreateFunctionCmd create = new CreateFunctionCmd(address);
            if (!create.applyTo(currentProgram, monitor)) {
                println("RePops: failed to create " + name + " at " + address);
                return;
            }
            fn = currentProgram.getFunctionManager().getFunctionAt(address);
        }

        if (fn != null) {
            fn.setName(name, SourceType.USER_DEFINED);
            println("RePops: seeded " + name + " at " + address);
        }
    }

    @Override
    protected void run() throws Exception {
        Memory memory = currentProgram.getMemory();
        Address cut = toAddr(CODE_END);
        MemoryBlock block = memory.getBlock(cut);

        if (block != null && block.getStart().compareTo(cut) < 0 &&
                block.getEnd().compareTo(cut) >= 0) {
            memory.split(block, cut);
            block = memory.getBlock(cut);
        }

        if (block != null) {
            block.setExecute(false);
            println("RePops: marked tail non-executable from " + cut +
                " through " + block.getEnd());
        }

        // These three internal entry points are independently documented by
        // the long-lived PEOPS POPS hooks. Two are reached through indirect
        // dispatch and are otherwise easy for generic function discovery to
        // miss, so seed them before auto-analysis.
        seedFunction(0x00016000L, "module_start");
        seedFunction(0x00016080L, "popsMainThread");
        seedFunction(0x00024998L, "patchedSyscallBridge");
        seedFunction(0x00007F00L, "spuWriteRegister");
        seedFunction(0x000083E8L, "cdrTransferSector");
        seedFunction(0x0000D1B0L, "cdrWriteRegister");
    }
}
