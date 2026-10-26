// Applies function names from a symbols CSV (address,name,lib,file,confidence,evidence) to the program.
// Args: <symbols.csv>. Creates a function where none exists yet and tags it with its SDK library/file.
//@category NDS

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class ApplySymbols extends GhidraScript {

	@Override
	protected void run() throws Exception {
		List<String> lines = Files.readAllLines(Path.of(getScriptArgs()[0]));
		int renamed = 0, created = 0;
		for (String line : lines.subList(1, lines.size())) {
			String[] col = line.split(",", -1);
			if (col.length < 5 || col[1].isEmpty()) {
				continue;
			}
			Address addr = parseAddr(col[0]);
			Function f = getFunctionAt(addr);
			if (f == null) {
				new DisassembleCommand(addr, null, true).applyTo(currentProgram, monitor);
				f = createFunction(addr, col[1]);
				if (f == null) {
					printerr("could not create function at " + addr + " for " + col[1]);
					continue;
				}
				created++;
			}
			f.setName(col[1], SourceType.USER_DEFINED);
			f.setComment(String.format("%s/%s (%s)", col[2], col[3], col[4]));
			renamed++;
		}
		println(String.format("applied %d names (%d new functions)", renamed, created));
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
