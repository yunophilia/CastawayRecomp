# Disassembles Wii ELF functions by symbol name, naming branch targets and relocated
# operands from the symbol table and .rela.text.
# usage: python -I dis.py <elf> <ppc.py dir> <exact symbol>...
import bisect
import struct
import sys

d = open(sys.argv[1], 'rb').read()
sys.path.insert(0, sys.argv[2])
import ppc  # noqa: E402

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
by_name = {n: (v, sz) for v, sz, t, n in syms if t == 2 and sz}
by_addr = {}
for v, sz, t, n in syms:
    if n and t in (1, 2):
        by_addr.setdefault(v, n)
relocs = {}
for s in secs:
    if s[1] == 4 and secname(s) == '.rela.text':
        for k in range(s[5] // 12):
            o = s[4] + k * 12
            r_offset, r_info, addend = struct.unpack('>IIi', d[o:o + 12])
            relocs[r_offset] = (syms[r_info >> 8][3], addend, r_info & 0xff)
text = next(s for s in secs if secname(s) == '.text')

for name in sys.argv[3:]:
    start, size = by_name[name]
    print(f"== {name} @ {start:08x} ({size:#x} bytes)")
    for a in range(start, start + size, 4):
        w, = struct.unpack('>I', d[text[4] + a - text[3]:text[4] + a - text[3] + 4])
        line = ppc.disasm(a, w)
        note = ''
        if (w >> 26) == 18:   # b/bl
            t = ppc.sext(w & 0x3fffffc, 26) + (0 if w & 2 else a)
            note = by_addr.get(t & 0xffffffff, '')
        for off in (a, a + 2):
            if off in relocs:
                n, add, typ = relocs[off]
                note = f"{n}{add:+#x}" if add else n
        print(f"  {a:08x}  {line:40} {note}")
