"""List enclosing functions of call sites: python3 -I callers.py <cfile> <name>..."""
import re, sys
src = open(sys.argv[1]).read().split('\n')
hdr = re.compile(r'^// (?:ov0\d\d:)?([0-9a-f]{8}) (\S+)')
for name in sys.argv[2:]:
    cur = None; out = []
    for ln in src:
        m = hdr.match(ln)
        if m: cur = (m.group(1), m.group(2)); continue
        if name + '(' in ln and cur and cur[1] != name and cur not in out:
            out.append(cur)
    print(name, '<-', ' '.join(f'{a}:{n}' for a, n in out))
