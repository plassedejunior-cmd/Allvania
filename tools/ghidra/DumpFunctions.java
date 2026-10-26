// Decompiles every function in the given memory blocks and records per-function features used to spot SDK code.
// Args: <output dir> <block>[,<block>...]
// Writes <out>/<block>.c (decompiled C) and <out>/features.csv
// (address, name, size, I/O registers touched, special instructions, callees, caller count).
//@category NDS

import java.io.File;
import java.io.PrintWriter;
import java.util.Set;
import java.util.TreeSet;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;

public class DumpFunctions extends GhidraScript {

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		File outDir = new File(args[0]);
		outDir.mkdirs();
		Set<String> blocks = Set.of(args[1].split(","));

		DecompInterface decomp = new DecompInterface();
		decomp.openProgram(currentProgram);

		try (PrintWriter features = new PrintWriter(new File(outDir, "features.csv"))) {
			features.println("block,address,size,name,io,insns,callees,callers");
			for (String blockName : blocks) {
				MemoryBlock block = currentProgram.getMemory().getBlock(blockName);
				try (PrintWriter c = new PrintWriter(new File(outDir, blockName + ".c"))) {
					for (Function f : currentProgram.getFunctionManager().getFunctions(block.getStart(), true)) {
						if (!block.contains(f.getEntryPoint())) {
							break;
						}
						monitor.checkCancelled();
						features.println(describe(blockName, f));
						DecompileResults res = decomp.decompileFunction(f, 60, monitor);
						c.printf("// %s %s%n", f.getEntryPoint(), f.getName());
						c.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "// decompile failed\n");
					}
				}
			}
		}
		decomp.dispose();
	}

	private String describe(String blockName, Function f) {
		Set<String> io = new TreeSet<>();
		Set<String> insns = new TreeSet<>();
		Set<String> callees = new TreeSet<>();
		for (Instruction insn : currentProgram.getListing().getInstructions(f.getBody(), true)) {
			String mnem = insn.getMnemonicString().toLowerCase();
			if (mnem.startsWith("mrs") || mnem.startsWith("msr") || mnem.startsWith("mcr") || mnem.startsWith("mrc")
					|| mnem.startsWith("swi") || mnem.startsWith("svc") || mnem.startsWith("clz")) {
				insns.add(mnem.replaceAll("\\..*", ""));
			}
			for (Reference r : insn.getReferencesFrom()) {
				addIo(io, r.getToAddress());
			}
			// literal-pool loads put the register address in a data word, not a direct reference
			for (int i = 0; i < insn.getNumOperands(); i++) {
				for (Object o : insn.getOpObjects(i)) {
					if (o instanceof Address a) {
						addIo(io, a);
						try {
							long v = currentProgram.getMemory().getInt(a) & 0xFFFFFFFFL;
							addIo(io, toAddr(v));
						}
						catch (Exception e) {
							// not readable
						}
					}
					else if (o instanceof Scalar s && (s.getUnsignedValue() & 0xFF000000L) == 0x04000000L) {
						io.add(String.format("%08X", s.getUnsignedValue()));
					}
				}
			}
		}
		for (Function callee : f.getCalledFunctions(monitor)) {
			callees.add(callee.getEntryPoint().toString());
		}
		int callers = f.getCallingFunctions(monitor).size();
		return String.format("%s,%s,%d,%s,%s,%s,%s,%d", blockName, f.getEntryPoint(), f.getBody().getNumAddresses(),
			f.getName(), String.join(" ", io), String.join(" ", insns), String.join(" ", callees), callers);
	}

	private void addIo(Set<String> io, Address a) {
		if (a != null && (a.getOffset() & 0xFF000000L) == 0x04000000L) {
			io.add(String.format("%08X", a.getOffset()));
		}
	}
}
