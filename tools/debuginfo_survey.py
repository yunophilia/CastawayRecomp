# Surveys leftover debug info: source paths and GX symbols in the Wii ELF, and any
# function-name strings in the (stripped) PS2 ELF.
# usage: python -I debuginfo_survey.py <wii elf> <ps2 elf>
import collections
import re
import sys

w = open(sys.argv[1], 'rb').read()
p = open(sys.argv[2], 'rb').read()

srcs = sorted(set(m.group().decode() for m in re.finditer(rb'[A-Za-z]:[\\/][\x20-\x7e]{3,200}?\.(?:cpp|c|h)\b', w)))
print("wii source paths:", len(srcs))
print("  e.g.", srcs[:3])
def top(s):
    s = s.replace('\\', '/')
    m = re.search(r'/src/([^/]+)/', s, re.I)
    return m.group(1) if m else s.split('/')[1] if '/' in s else s
print("  top dirs:", collections.Counter(top(s) for s in srcs).most_common(15))
gx = sorted(set(m.group().decode() for m in re.finditer(rb'GX[A-Z][A-Za-z0-9]+', w)))
print("wii GX* names:", len(gx), gx[:10])

ps2s = sorted(set(m.group().decode() for m in re.finditer(rb'[A-Za-z_][A-Za-z0-9_]{4,}__F[A-Za-z0-9_]*', p)))
print("ps2 mangled C++ names:", len(ps2s), ps2s[:8])
print("ps2 .cpp/.c strings:", len(set(re.findall(rb'[\x20-\x7e]{3,120}\.c(?:pp)?\b', p))))
