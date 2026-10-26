"""Dump the per-enemy / per-object asset lists ({s32 fileId, s32 kind} terminated by fileId<0)
and the per-area streamed-graphics lists. usage: python3 -I asset_lists.py [enemy|object|area]"""
import struct, sys, os, collections
root = os.path.dirname(os.path.abspath(__file__)) + '/../..'
b = open(root + '/extracted/arm9/arm9_dec.bin', 'rb').read()
u32 = lambda a: struct.unpack_from('<I', b, a - 0x02000000)[0]
def path(i):
    if not 0 <= i < 2537: return '/?/%d' % i
    e = b[0xd8cec - 0 + i * 0x20 + 0x00000000:][:0x20]
    return e[6:].split(b'\0')[0].decode()
def dump_list(p):
    out = []
    while True:
        i, k = struct.unpack_from('<iI', b, p - 0x02000000)
        if i < 0: break
        out.append((i, k)); p += 8
    return out
def run(table, n, label):
    kinds = collections.Counter()
    for j in range(n):
        p = u32(table + 4 * j)
        if not 0x02000000 <= p < 0x02100000: break
        lst = dump_list(p)
        for i, k in lst: kinds[(k, path(i).split('/')[1])] += 1
        if '-v' in sys.argv: print(label, j, hex(p), [(path(i), k) for i, k in lst])
    print(label, 'entries', j, 'kind/dir counts', dict(kinds))
what = sys.argv[1] if len(sys.argv) > 1 else 'enemy'
if what == 'enemy': run(0x020f2814, 200, 'enemy')
if what == 'object': run(0x020f343c, 200, 'object')
if what == 'area':
    t = 0x020ecbb8
    for a in range(20):
        sub = u32(t + 4 * a)
        if not 0x02000000 <= sub < 0x02100000: continue
        cnt = 0
        for r in range(255):
            p = u32(sub + 4 * r)
            if not 0x02000000 <= p < 0x02100000: break
            lst = dump_list(p); cnt += 1
            if '-v' in sys.argv and r < 3: print('area', a, 'list', r, [(path(i), k) for i, k in lst])
        print('area', a, hex(sub), 'lists', cnt)
