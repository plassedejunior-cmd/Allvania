"""Print 32-bit words from the ARM9 image: python3 -I tools/peek.py <addr> [count]"""
import struct
import sys

b = open("extracted/arm9/arm9_dec.bin", "rb").read()
addr = int(sys.argv[1], 16)
n = int(sys.argv[2]) if len(sys.argv) > 2 else 1
for i in range(n):
    a = addr + 4 * i
    print(f"{a:08x}: {struct.unpack_from('<I', b, a - 0x02000000)[0]:08x}")
