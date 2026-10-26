// Writes a CSV of every function (block, address, size, name, thumb) to the path in script argument 1.
//@category NDS

import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;

public class ExportFunctions extends GhidraScript {

	@Override
	protected void run() throws Exception {
		Register tmode = currentProgram.getRegister("TMode");
		try (PrintWriter out = new PrintWriter(getScriptArgs()[0])) {
			out.println("block,address,size,name,thumb");
			for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
				var entry = f.getEntryPoint();
				var block = currentProgram.getMemory().getBlock(entry);
				var tv = tmode == null ? null : currentProgram.getProgramContext().getValue(tmode, entry, false);
				out.printf("%s,0x%08X,%d,%s,%d%n", block == null ? "?" : block.getName(), entry.getOffset(),
					f.getBody().getNumAddresses(), f.getName(), tv != null && tv.intValue() == 1 ? 1 : 0);
			}
		}
	}
}
