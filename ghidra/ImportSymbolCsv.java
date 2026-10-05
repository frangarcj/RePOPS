// @category RePops
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

import java.io.BufferedReader;
import java.io.FileReader;

public class ImportSymbolCsv extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) {
            throw new IllegalArgumentException("usage: ImportSymbolCsv.java <symbols.csv>");
        }

        int renamed = 0;
        int created = 0;
        try (BufferedReader reader = new BufferedReader(new FileReader(args[0]))) {
            String line = reader.readLine(); // header
            while ((line = reader.readLine()) != null) {
                String[] fields = line.split(",", 2);
                if (fields.length != 2) {
                    continue;
                }

                long raw = Long.decode(fields[0].trim());
                String name = fields[1].trim();
                Address address = toAddr(raw);

                Function fn = currentProgram.getFunctionManager().getFunctionAt(address);
                if (fn == null) {
                    if (currentProgram.getListing().getInstructionAt(address) == null) {
                        DisassembleCommand dis = new DisassembleCommand(address, null, true);
                        dis.applyTo(currentProgram, monitor);
                    }
                    CreateFunctionCmd create = new CreateFunctionCmd(address);
                    if (create.applyTo(currentProgram, monitor)) {
                        fn = currentProgram.getFunctionManager().getFunctionAt(address);
                        created++;
                    }
                }

                if (fn != null) {
                    fn.setName(name, SourceType.IMPORTED);
                    renamed++;
                } else {
                    currentProgram.getSymbolTable().createLabel(
                        address, name, SourceType.IMPORTED
                    );
                }
            }
        }

        println("RePops: imported " + renamed + " function names (" +
            created + " newly created)");
    }
}
