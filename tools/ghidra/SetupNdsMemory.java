// Lays out the DS ARM9 address space for a program imported from arm9.bin at 0x02000000:
// ITCM/DTCM autoloads, static BSS, I/O registers and every ARM9 overlay as a Ghidra overlay block.
// Expects script argument 1 to be the extracted/ directory produced by tools/extract_rom.py.
//@category NDS

import java.io.File;
import java.io.FileInputStream;
import java.io.FileReader;
import java.io.InputStream;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;

public class SetupNdsMemory extends GhidraScript {

	@Override
	protected void run() throws Exception {
		File extracted = new File(getScriptArgs()[0]);
		JsonObject info;
		try (FileReader r = new FileReader(new File(extracted, "rom_info.json"))) {
			info = JsonParser.parseReader(r).getAsJsonObject();
		}
		Memory mem = currentProgram.getMemory();

		for (JsonElement e : info.getAsJsonArray("autoloads")) {
			JsonObject a = e.getAsJsonObject();
			long addr = a.get("addr").getAsLong();
			String name = addr == 0x01FF8000L ? "ITCM" : addr == 0x027E0000L ? "DTCM" : String.format("autoload_%08X", addr);
			addFileBlock(mem, name, addr, new File(extracted, String.format("arm9/autoload_%08X.bin", addr)), false);
			long bss = a.get("bss_size").getAsLong();
			if (bss > 0) {
				mem.createUninitializedBlock(name + "_bss", toAddr(addr + a.get("size").getAsLong()), bss, false);
			}
		}

		JsonObject params = info.getAsJsonObject("module_params");
		long bssStart = params.get("bss_start").getAsLong();
		long bssEnd = params.get("bss_end").getAsLong();
		// The tail of arm9.bin holds the autoload source data, which crt0 copies out and then reuses as BSS.
		MemoryBlock main = mem.getBlock(toAddr(bssStart));
		long fileEnd = main.getEnd().getOffset() + 1;
		mem.split(main, toAddr(bssStart));
		MemoryBlock autoloadSrc = mem.getBlock(toAddr(bssStart));
		autoloadSrc.setName("autoload_src");
		autoloadSrc.setExecute(false);
		mem.createUninitializedBlock("bss", toAddr(fileEnd), bssEnd - fileEnd, false);
		mem.createUninitializedBlock("io", toAddr(0x04000000L), 0x01100000L, false).setVolatile(true);

		JsonArray overlays = info.getAsJsonArray("overlays");
		for (JsonElement e : overlays) {
			JsonObject o = e.getAsJsonObject();
			if (!o.get("cpu").getAsString().equals("overlay9")) {
				continue;
			}
			int id = o.get("id").getAsInt();
			long addr = o.get("ram_addr").getAsLong();
			String name = String.format("ov%03d", id);
			addFileBlock(mem, name, addr, new File(extracted, String.format("overlay9/ov%03d.bin", id)), true);
		}

		JsonObject hdr = info.getAsJsonObject("header");
		Address entry = toAddr(hdr.get("arm9_entry").getAsLong());
		createLabel(entry, "_start", true, SourceType.IMPORTED);
		addEntryPoint(entry);
		disassemble(entry);
	}

	private void addFileBlock(Memory mem, String name, long addr, File f, boolean overlay) throws Exception {
		try (InputStream in = new FileInputStream(f)) {
			MemoryBlock b = mem.createInitializedBlock(name, toAddr(addr), in, f.length(), monitor, overlay);
			b.setRead(true);
			b.setWrite(true);
			b.setExecute(true);
		}
	}
}
