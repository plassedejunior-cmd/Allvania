// Decompiles the functions at the given addresses (creating them if needed) and writes the C to a file.
// Args: <output file> <hex address>[,<hex address>...]
//@category NDS

import java.io.PrintWriter;

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DecompileAt extends GhidraScript {

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		DecompInterface decomp = new DecompInterface();
		decomp.openProgram(currentProgram);
		try (PrintWriter out = new PrintWriter(args[0])) {
			for (String a : args[1].split(",")) {
				Address addr = parseAddr(a);
				Function f = getFunctionContaining(addr);
				if (f == null) {
					new DisassembleCommand(addr, null, true).applyTo(currentProgram, monitor);
					f = createFunction(addr, null);
				}
				if (f == null) {
					out.printf("// %s: no function%n%n", addr);
					continue;
				}
				DecompileResults res = decomp.decompileFunction(f, 120, monitor);
				out.printf("// %s %s%n", f.getEntryPoint(), f.getName());
				out.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "// decompile failed\n");
			}
		}
		decomp.dispose();
	}

	// Accepts "02000c78" (main address space) or "ov020:02212dd4" (overlay block)
	private Address parseAddr(String s) {
		s = s.trim();
		int colon = s.indexOf(':');
		if (colon < 0) {
			return toAddr(Long.parseLong(s, 16));
		}
		var space = currentProgram.getAddressFactory().getAddressSpace(s.substring(0, colon));
		return space.getAddress(Long.parseLong(s.substring(colon + 1), 16));
	}
}
