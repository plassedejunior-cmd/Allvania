"""List NitroSDK ARM9 functions in link order (library, then source file, then definition order).

The SDK libraries are linked into the ARM9 right after crt0 in the order the SDK build lists them,
so this list is the template the binary's function order is aligned against.

Usage: python3 -I tools/sdk/expected_order.py <NitroSDK checkout> [lib,lib,...] > build/sdk_expected.csv
The optional library list overrides the NitroSDK order, e.g. "fnd,g2d,g3d,gfd,snd" for NitroSystem.
"""

import os
import re
import sys

LIB_ORDER = ["fx", "gx", "os", "mi", "pxi", "init", "stubs", "ext", "pad", "exi", "fs", "cp", "spi", "rtc",
             "wm", "wbt", "wfs", "prc", "card", "mb", "ctrdg", "math", "wvr", "snd", "utest", "std", "env"]

SRCS_RE = re.compile(r"lib(\w+?)_srcs\s*=\s*files\((.*?)\)", re.S)
# A top-level definition: return type and name at column 0, parameter list, then '{' on a following line.
FUNC_RE = re.compile(r"^(?!static\s+inline|inline)([A-Za-z_][\w\s\*]*?)\b([A-Za-z_]\w*)\s*\(([^;{}]*?)\)\s*\n\s*\{",
                     re.M)
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof"}


def arm9_sources(lib_dir: str) -> list[str]:
    meson = os.path.join(lib_dir, "src", "meson.build")
    if not os.path.exists(meson):
        return []
    text = open(meson).read()
    for name, body in SRCS_RE.findall(text):
        if "arm7" in name:
            continue
        return [os.path.join(lib_dir, "src", f) for f in re.findall(r"'([^']+)'", body)]
    return []


def functions(path: str) -> list[str]:
    if not path.endswith(".c") or not os.path.exists(path):
        return []
    text = open(path, errors="replace").read()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return [m.group(2) for m in FUNC_RE.finditer(text) if m.group(2) not in KEYWORDS]


def main() -> None:
    root = os.path.join(sys.argv[1], "libraries")
    libs = sys.argv[2].split(",") if len(sys.argv) > 2 else LIB_ORDER
    print("lib,file,function")
    for lib in libs:
        for src in arm9_sources(os.path.join(root, lib)):
            for fn in functions(src):
                print(f"{lib},{os.path.basename(src)},{fn}")


if __name__ == "__main__":
    main()
