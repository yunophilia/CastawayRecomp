# Lists the functions that reference given symbols, using the Wii ELF's .rela.text.
# usage: python -I callers.py <elf> <symbol regex>
import bisect
import re
import struct
import sys

d = open(sys.argv[1], 'rb').read()
pat = re.compile(sys.argv[2])
shoff, = struct.unpack('>I', d[32:36])
shnum, = struct.unpack('>H', d[48:50])
secs = [struct.unpack('>IIIIIIIIII', d[shoff + i * 40:shoff + i * 40 + 40]) for i in range(shnum)]
shstr = secs[struct.unpack('>H', d[50:52])[0]]

def secname(s):
    a = shstr[4] + s[0]
    return d[a:d.index(b'\0', a)].decode()

symtab = next(s for s in secs if s[1] == 2)
strtab = secs[symtab[6]]
syms = []
for k in range(symtab[5] // 16):
    o = symtab[4] + k * 16
    st_name, value, size, info, _, _ = struct.unpack('>IIIBBH', d[o:o + 16])
    a = strtab[4] + st_name
    syms.append((value, size, info & 0xf, d[a:d.index(b'\0', a)].decode('latin1')))
funcs = sorted((v, sz, n) for v, sz, t, n in syms if t == 2 and sz)
starts = [f[0] for f in funcs]

def func_at(addr):
    i = bisect.bisect_right(starts, addr) - 1
    if i >= 0 and addr < funcs[i][0] + funcs[i][1]:
        return funcs[i][2]
    return f"?{addr:08x}"

hits = {}
for s in secs:
    if s[1] != 4 or secname(s) != '.rela.text':
        continue
    for k in range(s[5] // 12):
        o = s[4] + k * 12
        r_offset, r_info, _ = struct.unpack('>IIi', d[o:o + 12])
        target = syms[r_info >> 8][3]
        if pat.search(target):
            hits.setdefault(target, set()).add(func_at(r_offset))
for t in sorted(hits):
    print(t)
    for c in sorted(hits[t]):
        print("   <-", c)
