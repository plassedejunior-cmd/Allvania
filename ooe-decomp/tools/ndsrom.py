"""Parsing helpers for Nintendo DS ROM images (header, overlays, filesystem, BLZ)."""

import struct
from dataclasses import dataclass, field

MODULE_PARAMS_MAGIC = (0xDEC00621, 0x2106C0DE)


@dataclass
class Header:
    title: str
    game_code: str
    maker_code: str
    rom_version: int
    arm9_offset: int
    arm9_entry: int
    arm9_ram: int
    arm9_size: int
    arm7_offset: int
    arm7_entry: int
    arm7_ram: int
    arm7_size: int
    fnt_offset: int
    fnt_size: int
    fat_offset: int
    fat_size: int
    ovt9_offset: int
    ovt9_size: int
    ovt7_offset: int
    ovt7_size: int
    banner_offset: int
    rom_size: int

    @classmethod
    def parse(cls, rom: bytes) -> "Header":
        (a9o, a9e, a9r, a9s, a7o, a7e, a7r, a7s,
         fnto, fnts, fato, fats, o9o, o9s, o7o, o7s) = struct.unpack_from("<16I", rom, 0x20)
        return cls(
            title=rom[0:12].rstrip(b"\0").decode("ascii"),
            game_code=rom[0x0C:0x10].decode("ascii"),
            maker_code=rom[0x10:0x12].decode("ascii"),
            rom_version=rom[0x1E],
            arm9_offset=a9o, arm9_entry=a9e, arm9_ram=a9r, arm9_size=a9s,
            arm7_offset=a7o, arm7_entry=a7e, arm7_ram=a7r, arm7_size=a7s,
            fnt_offset=fnto, fnt_size=fnts, fat_offset=fato, fat_size=fats,
            ovt9_offset=o9o, ovt9_size=o9s, ovt7_offset=o7o, ovt7_size=o7s,
            banner_offset=struct.unpack_from("<I", rom, 0x68)[0],
            rom_size=struct.unpack_from("<I", rom, 0x80)[0],
        )


@dataclass
class ModuleParams:
    offset: int  # offset of the struct inside the ARM9 binary
    autoload_list_start: int
    autoload_list_end: int
    autoload_start: int
    bss_start: int
    bss_end: int
    compressed_static_end: int
    sdk_version: int

    @classmethod
    def find(cls, arm9: bytes, footer: bytes) -> "ModuleParams":
        magic, off = struct.unpack_from("<II", footer, 0)
        if magic != MODULE_PARAMS_MAGIC[0]:
            raise ValueError("ARM9 footer magic not found")
        vals = struct.unpack_from("<9I", arm9, off)
        if vals[7:9] != MODULE_PARAMS_MAGIC:
            raise ValueError("module params magic mismatch")
        return cls(off, *vals[:7])


@dataclass
class Overlay:
    id: int
    ram_addr: int
    ram_size: int
    bss_size: int
    sinit_start: int
    sinit_end: int
    file_id: int
    compressed_size: int
    compressed: bool

    @classmethod
    def parse_table(cls, table: bytes) -> list["Overlay"]:
        out = []
        for i in range(0, len(table), 32):
            ovid, ram, size, bss, ss, se, fid, flags = struct.unpack_from("<8I", table, i)
            out.append(cls(ovid, ram, size, bss, ss, se, fid,
                           flags & 0xFFFFFF, bool((flags >> 24) & 1)))
        return out


@dataclass
class FsEntry:
    path: str
    file_id: int


@dataclass
class Directory:
    path: str
    entries: list = field(default_factory=list)


def parse_fat(rom: bytes, hdr: Header) -> list[tuple[int, int]]:
    return [struct.unpack_from("<II", rom, hdr.fat_offset + i)
            for i in range(0, hdr.fat_size, 8)]


def parse_fnt(fnt: bytes) -> list[FsEntry]:
    """Walk the file name table, returning every named file with its FAT id."""
    files: list[FsEntry] = []

    def walk(dir_id: int, prefix: str) -> None:
        sub_off, first_id = struct.unpack_from("<IH", fnt, (dir_id & 0xFFF) * 8)
        pos, fid = sub_off, first_id
        while True:
            tl = fnt[pos]
            pos += 1
            if tl == 0:
                return
            name = fnt[pos:pos + (tl & 0x7F)].decode("ascii")
            pos += tl & 0x7F
            if tl & 0x80:
                child = struct.unpack_from("<H", fnt, pos)[0]
                pos += 2
                walk(child, f"{prefix}{name}/")
            else:
                files.append(FsEntry(prefix + name, fid))
                fid += 1

    walk(0xF000, "")
    return files


def blz_decompress(data: bytes) -> bytes:
    """Decompress a backwards-LZ (BLZ) block as used for ARM9/overlay code.

    The footer's last 8 bytes hold the encoded length, header length and the
    number of bytes the data grows by; decoding runs from the end backwards.
    """
    word0, inc_len = struct.unpack_from("<II", data, len(data) - 8)
    enc_len, hdr_len = word0 & 0xFFFFFF, word0 >> 24
    if inc_len == 0:
        return bytes(data)
    out = bytearray(data) + bytearray(inc_len)
    end = len(data) - enc_len
    src = len(data) - hdr_len
    dst = len(out)
    while dst > end:
        src -= 1
        flags = data[src]
        for _ in range(8):
            if dst <= end:
                break
            if flags & 0x80:
                v = (data[src - 1] << 8) | data[src - 2]
                src -= 2
                count, disp = (v >> 12) + 3, (v & 0xFFF) + 3
                for _ in range(count):
                    dst -= 1
                    out[dst] = out[dst + disp]
            else:
                src -= 1
                dst -= 1
                out[dst] = data[src]
            flags = (flags << 1) & 0xFF
    return bytes(out)
