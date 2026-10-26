"""Find ARM/Thumb-less BL/BLX(imm) call sites to given targets in ARM9 and all overlays.
usage: python3 -I blscan.py <hex target>..."""
import json, struct, sys, os
root = os.path.dirname(os.path.abspath(__file__)) + '/../..'
info = json.load(open(root + '/extracted/rom_info.json'))
imgs = [('arm9', 0x02000000, open(root + '/extracted/arm9/arm9_dec.bin', 'rb').read())]
for ov in info['overlays']:
    p = root + '/extracted/overlay9/ov%03d.bin' % ov['id']
    if os.path.exists(p):
        imgs.append(('ov%03d' % ov['id'], ov['ram_addr'], open(p, 'rb').read()))
targets = {int(t, 16) for t in sys.argv[1:]}
for name, base, data in imgs:
    for off in range(0, len(data) - 3, 4):
        w = struct.unpack_from('<I', data, off)[0]
        if (w & 0x0f000000) == 0x0b000000 and (w >> 28) != 0xf or (w >> 25) == 0x7d:
            imm = w & 0xffffff
            if imm & 0x800000: imm -= 0x1000000
            tgt = base + off + 8 + imm * 4
            if (w >> 25) == 0x7d: tgt += ((w >> 24) & 1) * 2
            if tgt in targets:
                print(f'{name}:{base+off:08x} -> {tgt:08x}')
