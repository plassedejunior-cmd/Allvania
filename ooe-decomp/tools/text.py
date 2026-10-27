"""Game text (overlay 0 = English, overlay 1 = French) to and from editable .txt files.

Usage:
  python3 -I tools/text.py extract [--rom baserom/baserom.nds] [--lang en|fr] [-o text/en.txt]
  python3 -I tools/text.py check text/pt.txt [--rom baserom/baserom.nds]

The text comes from your own dump and stays in text/ (ignored by git). The port reads a
translation with `ooe --text text/pt.txt`; see docs/game/text.md for the format.
"""

import argparse
import os
import re
import struct
import sys

TEXT_RAM = 0x021DD280
TEXT_LIMIT = 0x021FFDA0 - TEXT_RAM  # the next overlay slot starts here
STRING_COUNT = 1893
TABLE_OFFSET = {0: 0x1DA40, 1: 0x20D80}  # pointer table inside overlay 0 / 1
LANG_OVERLAY = {"en": 0, "fr": 1}
END = 0xEA
NEWLINE = 0xE6
ARG_CODES = {0xE2, 0xE3, 0xE7, 0xE8, 0xE9}  # control codes followed by one argument byte

# Glyph codes 0x00-0x5E are ASCII minus 0x20; 0x60-0xA9 are the Latin-1 letters of the font.
LATIN = "¡¢£¨©®°±´·¿ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýŒœ"
CHARS = {c: chr(c + 0x20) for c in range(0x5F)}
CHARS.update({0x60 + i: ch for i, ch in enumerate(LATIN)})
CODES = {ch: c for c, ch in CHARS.items()}
CODES.update({"’": 0x07, "‘": 0x07, "“": 0x02, "”": 0x02, "–": 0x0D, "—": 0x0D})
ESCAPED = {CODES["{"], CODES["}"]}

HEADER_RE = re.compile(r"^\[(\d{4})\]$")
TOKEN_RE = re.compile(r"\{(n|[0-9A-Fa-f]{2})(?::([0-9A-Fa-f]{2}))?\}")


def load_rom(path: str) -> bytes:
    rom = open(path, "rb").read()
    if rom[0x0C:0x10] != b"YR9E":
        sys.exit(f"{path} is not Order of Ecclesia USA (YR9E)")
    return rom


def overlay(rom: bytes, n: int) -> bytes:
    ovt = struct.unpack_from("<I", rom, 0x50)[0]
    file_id = struct.unpack_from("<I", rom, ovt + n * 32 + 24)[0]
    fat = struct.unpack_from("<I", rom, 0x48)[0]
    start, end = struct.unpack_from("<II", rom, fat + file_id * 8)
    return rom[start:end]


def strings(rom: bytes, n: int) -> list[bytes]:
    """Each string's body: between the 2-byte header (always 01 00) and the 0xEA terminator."""
    data = overlay(rom, n)
    out = []
    for ptr in struct.unpack_from(f"<{STRING_COUNT}I", data, TABLE_OFFSET[n]):
        o = ptr - TEXT_RAM
        out.append(data[o + 2:data.index(END, o + 2)])
    return out


def decode(body: bytes) -> str:
    text, i = [], 0
    while i < len(body):
        b = body[i]
        if b == NEWLINE:
            text.append("\n")
        elif b in ARG_CODES and i + 1 < len(body):
            i += 1
            text.append(f"{{{b:02X}:{body[i]:02X}}}")
        elif b in CHARS and b not in ESCAPED:
            text.append(CHARS[b])
        else:
            text.append(f"{{{b:02X}}}")
        i += 1
    s = "".join(text)
    # trailing line breaks would be lost as blank lines between entries, so they are spelled out
    stripped = s.rstrip("\n")
    return stripped + "{n}" * (len(s) - len(stripped))


def encode(text: str, where: str) -> bytes:
    out = bytearray()
    pos = 0
    for m in TOKEN_RE.finditer(text):
        out += encode_plain(text[pos:m.start()], where)
        code, arg = m.group(1), m.group(2)
        if code == "n":
            out.append(NEWLINE)
        else:
            b = int(code, 16)
            if b == END:
                raise ValueError(f"{where}: {{EA}} ends the string and cannot be used")
            if (b in ARG_CODES) != (arg is not None):
                need = "needs" if b in ARG_CODES else "does not take"
                raise ValueError(f"{where}: {{{code}}} {need} an argument ({{{code}:00}})")
            out.append(b)
            if arg is not None:
                out.append(int(arg, 16))
        pos = m.end()
    out += encode_plain(text[pos:], where)
    return bytes(out)


def encode_plain(text: str, where: str) -> bytes:
    out = bytearray()
    for ch in text:
        if ch == "\n":
            out.append(NEWLINE)
        elif ch in "{}":
            raise ValueError(f"{where}: invalid code near {ch!r} (a literal brace is {{5B}} or {{5D}})")
        elif ch in CODES:
            out.append(CODES[ch])
        else:
            raise ValueError(f"{where}: the font has no {ch!r}")
    return bytes(out)


def parse(path: str) -> dict[int, str]:
    """Reads a translation file: `[NNNN]` header lines, each followed by the string's lines."""
    entries: dict[int, list[str]] = {}
    current = None
    for lineno, line in enumerate(open(path, encoding="utf-8-sig").read().split("\n"), 1):
        line = line.rstrip("\r")
        m = HEADER_RE.match(line)
        if m:
            current = int(m.group(1))
            if current >= STRING_COUNT:
                raise ValueError(f"{path}:{lineno}: there are only {STRING_COUNT} strings (0000-{STRING_COUNT - 1:04})")
            if current in entries:
                raise ValueError(f"{path}:{lineno}: [{current:04}] appears twice")
            entries[current] = []
        elif current is not None:
            entries[current].append(line)
        elif line.strip() and not line.startswith("#"):
            raise ValueError(f"{path}:{lineno}: text before the first [NNNN] header")
    out = {}
    for n, lines in entries.items():
        while lines and lines[-1] == "":
            lines.pop()
        out[n] = "\n".join(lines)
    return out


def cmd_extract(args) -> None:
    rom = load_rom(args.rom)
    out = args.output or f"text/{args.lang}.txt"
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"# Order of Ecclesia text ({args.lang}), extracted from your dump. Format: docs/game/text.md\n\n")
        for i, body in enumerate(strings(rom, LANG_OVERLAY[args.lang])):
            if body:
                f.write(f"[{i:04}]\n{decode(body)}\n\n")
    print(f"wrote {out}")


def cmd_check(args) -> None:
    rom = load_rom(args.rom)
    original = strings(rom, 0)
    try:
        entries = parse(args.file)
        encoded = {n: encode(t, f"[{n:04}]") for n, t in entries.items()}
    except ValueError as e:
        sys.exit(str(e))
    codes = re.compile(rb"[\xe2-\xff]")
    for n, body in sorted(encoded.items()):
        a = [c for c in codes.findall(original[n]) if c != b"\xe6"]
        b = [c for c in codes.findall(body) if c != b"\xe6"]
        if a != b:
            print(f"warning: [{n:04}] control codes differ from the English ({len(a)} vs {len(b)})")
    final = [encoded.get(n, body) for n, body in enumerate(original)]
    unique = {b"\x01\x00" + b + bytes([END]) for b in final}
    size = (sum(map(len, unique)) + 3) // 4 * 4 + 4 + STRING_COUNT * 4 + 12
    print(f"{len(entries)} of {STRING_COUNT} strings translated; "
          f"overlay {size} of {TEXT_LIMIT} bytes ({size * 100 // TEXT_LIMIT}%)")
    if size > TEXT_LIMIT:
        sys.exit(f"too big by {size - TEXT_LIMIT} bytes: shorten some strings")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    ex = sub.add_parser("extract", help="write the game's text to a .txt file")
    ex.add_argument("--rom", default="baserom/baserom.nds")
    ex.add_argument("--lang", choices=LANG_OVERLAY, default="en")
    ex.add_argument("-o", "--output")
    ch = sub.add_parser("check", help="validate a translation and report its size")
    ch.add_argument("file")
    ch.add_argument("--rom", default="baserom/baserom.nds")
    args = ap.parse_args()
    {"extract": cmd_extract, "check": cmd_check}[args.cmd](args)


if __name__ == "__main__":
    main()
