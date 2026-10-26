"""Extract code and data from baserom.nds into extracted/.

Usage: python3 -I tools/extract_rom.py [baserom/baserom.nds] [extracted]
"""

import hashlib
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ndsrom import Header, ModuleParams, Overlay, blz_decompress, parse_fat, parse_fnt  # noqa: E402

EXPECTED_SHA1 = "f9ebf006a489b8c9391e8d1fd05ec6b9cef6fd7b"  # YR9E, Order of Ecclesia (USA)


def write(path: str, data: bytes) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def main() -> None:
    rom_path = sys.argv[1] if len(sys.argv) > 1 else "baserom/baserom.nds"
    out = sys.argv[2] if len(sys.argv) > 2 else "extracted"
    rom = open(rom_path, "rb").read()

    sha1 = hashlib.sha1(rom).hexdigest()
    if sha1 != EXPECTED_SHA1:
        sys.exit(f"baserom sha1 {sha1} does not match expected {EXPECTED_SHA1}")

    hdr = Header.parse(rom)
    write(f"{out}/header.bin", rom[:0x4000])
    write(f"{out}/banner.bin", rom[hdr.banner_offset:hdr.banner_offset + 0x840])

    # ARM9: raw image, footer, and decompressed static module
    arm9 = rom[hdr.arm9_offset:hdr.arm9_offset + hdr.arm9_size]
    footer = rom[hdr.arm9_offset + hdr.arm9_size:hdr.arm9_offset + hdr.arm9_size + 12]
    params = ModuleParams.find(arm9, footer)
    write(f"{out}/arm9/arm9.bin", arm9)
    write(f"{out}/arm9/footer.bin", footer)
    if params.compressed_static_end:
        comp_len = params.compressed_static_end - hdr.arm9_ram
        arm9_dec = bytearray(blz_decompress(arm9[:comp_len]) + arm9[comp_len:])
        # a decompressed image must report itself as uncompressed
        struct.pack_into("<I", arm9_dec, params.offset + 0x14, 0)
    else:
        arm9_dec = bytearray(arm9)
    write(f"{out}/arm9/arm9_dec.bin", bytes(arm9_dec))

    # Autoload segments (ITCM/DTCM etc.) are copied out of the ARM9 image at boot
    autoloads = []
    data_pos = params.autoload_start - hdr.arm9_ram
    for i in range(params.autoload_list_start - hdr.arm9_ram,
                   params.autoload_list_end - hdr.arm9_ram, 12):
        addr, size, bss = struct.unpack_from("<III", arm9_dec, i)
        write(f"{out}/arm9/autoload_{addr:08X}.bin", bytes(arm9_dec[data_pos:data_pos + size]))
        autoloads.append({"addr": addr, "size": size, "bss_size": bss})
        data_pos += size

    arm7 = rom[hdr.arm7_offset:hdr.arm7_offset + hdr.arm7_size]
    write(f"{out}/arm7/arm7.bin", arm7)

    fat = parse_fat(rom, hdr)

    overlays = []
    for name, off, size in (("overlay9", hdr.ovt9_offset, hdr.ovt9_size),
                            ("overlay7", hdr.ovt7_offset, hdr.ovt7_size)):
        if not size:
            continue
        table = rom[off:off + size]
        write(f"{out}/{name}/table.bin", table)
        for ov in Overlay.parse_table(table):
            start, end = fat[ov.file_id]
            raw = rom[start:end]
            dec = blz_decompress(raw) if ov.compressed else raw
            write(f"{out}/{name}/ov{ov.id:03d}.bin", dec)
            overlays.append({**ov.__dict__, "cpu": name, "decompressed_size": len(dec)})

    fnt = rom[hdr.fnt_offset:hdr.fnt_offset + hdr.fnt_size]
    files = parse_fnt(fnt)
    for f in files:
        start, end = fat[f.file_id]
        write(f"{out}/files/{f.path}", rom[start:end])

    info = {
        "header": hdr.__dict__,
        "module_params": params.__dict__,
        "autoloads": autoloads,
        "overlays": overlays,
        "files": [{"path": f.path, "file_id": f.file_id, "size": fat[f.file_id][1] - fat[f.file_id][0]}
                  for f in files],
    }
    with open(f"{out}/rom_info.json", "w") as fp:
        json.dump(info, fp, indent=2)

    print(f"{hdr.title} [{hdr.game_code}] rev {hdr.rom_version}")
    print(f"ARM9 @ {hdr.arm9_ram:08X} entry {hdr.arm9_entry:08X}: {len(arm9):#x} -> {len(arm9_dec):#x} bytes")
    print(f"ARM7 @ {hdr.arm7_ram:08X} entry {hdr.arm7_entry:08X}: {len(arm7):#x} bytes")
    print(f"SDK version {params.sdk_version:08X}, BSS {params.bss_start:08X}-{params.bss_end:08X}")
    for a in autoloads:
        print(f"  autoload {a['addr']:08X} size {a['size']:#x} bss {a['bss_size']:#x}")
    print(f"{len(overlays)} overlays, {len(files)} files")


if __name__ == "__main__":
    main()
