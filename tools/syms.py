# Dumps the function symbols of the Wii ELF as "address size name", optionally filtered by a regex.
# usage: python -I syms.py <elf> [regex]
import re
import struct
import sys

d = open(sys.argv[1], 'rb').read()
pat = re.compile(sys.argv[2], re.I) if len(sys.argv) > 2 else None
shoff, = struct.unpack('>I', d[32:36])
shnum, = struct.unpack('>H', d[48:50])
secs = [struct.unpack('>IIIIIIIIII', d[shoff + i * 40:shoff + i * 40 + 40]) for i in range(shnum)]
for s in secs:
    if s[1] != 2:   # SHT_SYMTAB
        continue
    strtab = secs[s[6]]
    for k in range(s[5] // 16):
        o = s[4] + k * 16
        st_name, value, size, info, _, _ = struct.unpack('>IIIBBH', d[o:o + 16])
        if info & 0xf not in (1, 2) or not st_name:   # objects and functions
            continue
        a = strtab[4] + st_name
        name = d[a:d.index(b'\0', a)].decode('latin1')
        if not pat or pat.search(name):
            print(f"{value:08x} {size:6x} {'F' if info & 0xf == 2 else 'O'} {name}")
