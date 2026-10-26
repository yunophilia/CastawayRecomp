# Compares call-count dumps written by the "counts" script command (counts build):
# lists the functions called in <a> but not in <b>, with their ELF symbol names.
# usage: python -I count_diff.py <elf> <a.bin> <b.bin> [<c.bin> ...]
#   functions called in a and in none of b, c, ...
import struct
import sys


def symbols(path):
    d = open(path, 'rb').read()
    shoff, = struct.unpack('>I', d[32:36])
    shnum, = struct.unpack('>H', d[48:50])
    secs = [struct.unpack('>IIIIIIIIII', d[shoff + i * 40:shoff + i * 40 + 40]) for i in range(shnum)]
    out = {}
    for s in secs:
        if s[1] != 2:   # SHT_SYMTAB
            continue
        strtab = secs[s[6]]
        for k in range(s[5] // 16):
            o = s[4] + k * 16
            st_name, value, size, info, _, _ = struct.unpack('>IIIBBH', d[o:o + 16])
            if info & 0xf == 2 and st_name:
                a = strtab[4] + st_name
                out[value] = d[a:d.index(b'\0', a)].decode('latin1')
    return out


def counts(path):
    raw = open(path, 'rb').read()
    n = len(raw) // 4
    return {0x80000000 + i * 4: c for i, c in enumerate(struct.unpack(f'<{n}I', raw)) if c}


names = symbols(sys.argv[1])
a = counts(sys.argv[2])
others = [counts(p) for p in sys.argv[3:]]
only = sorted((c, addr) for addr, c in a.items() if not any(addr in o for o in others))
for c, addr in only:
    print(f'{addr:08x} {c:8d} {names.get(addr, "?")}')
print(f'{len(only)} functions only in {sys.argv[2]}', file=sys.stderr)
