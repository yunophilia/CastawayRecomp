# Names guest addresses with the game's ELF symbols: "addr  function+offset".
# usage: python tools/addr2sym.py ADDR...   (hex, e.g. 802C3FA8)
import bisect
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elf import Elf                 # noqa: E402
from regions import REFERENCE      # noqa: E402

e = Elf(REFERENCE.elf())
syms = sorted((s.addr, s.size, s.name) for s in e.symbols if s.size)
starts = [s[0] for s in syms]
for arg in sys.argv[1:]:
    a = int(arg, 16)
    i = bisect.bisect_right(starts, a) - 1
    while i >= 0 and not (syms[i][0] <= a < syms[i][0] + syms[i][1]):
        i -= 1
        if i >= 0 and a - syms[i][0] > 0x100000:
            i = -1
    print(f'{a:08x}  ' + (f'{syms[i][2]}+{a - syms[i][0]:#x}' if i >= 0 else '?'))
