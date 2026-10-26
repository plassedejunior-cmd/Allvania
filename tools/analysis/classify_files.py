"""Classify every ROM data file by directory/prefix, size, magic and possible LZ header.
usage: python3 -I classify_files.py [-v]"""
import os, struct, collections, sys
root = os.path.dirname(os.path.abspath(__file__)) + '/../../extracted/files'
groups = collections.defaultdict(list)
for d, _, fs in os.walk(root):
    for f in sorted(fs):
        p = os.path.join(d, f); rel = os.path.relpath(p, root)
        b = open(p, 'rb').read()
        top = rel.split('/')[0]
        pre = f.split('_')[0] + '_' if '_' in f else f[:3]
        magic = b[:4]
        if magic[:1] in (b'\x10', b'\x11') and len(b) >= 4:
            dec = struct.unpack('<I', b[:4])[0] >> 8
            kind = 'LZ%02x(dec=%d)' % (b[0], dec) if dec >= len(b) and dec < 8 * len(b) + 64 else 'raw'
        else:
            kind = 'raw'
        tag = magic.hex() if not all(32 <= c < 127 for c in magic) else magic.decode()
        groups[(top, pre)].append((rel, len(b), tag, kind, b))
for (top, pre), lst in sorted(groups.items()):
    sizes = collections.Counter(s for _, s, _, _, _ in lst)
    kinds = collections.Counter(k.split('(')[0] for _, _, _, k, _ in lst)
    mags = collections.Counter(t for _, _, t, _, _ in lst)
    print(f'{top}/{pre}*: n={len(lst)} sizes(top)={sizes.most_common(4)} min={min(sizes)} max={max(sizes)}')
    print(f'    kinds={dict(kinds)} magic(top)={mags.most_common(4)}')
    if '-v' in sys.argv:
        for rel, s, t, k, _ in lst[:5]: print('     ', rel, s, t, k)
