# Lists the disc's files with their partition offsets, or names the file at given offsets.
# usage: python tools/fst.py [OFFSET...]   (hex partition offsets as the DI log prints them)
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from regions import REFERENCE   # noqa: E402

d = open(os.path.join(REFERENCE.data_dir(), 'sys', 'fst.bin'), 'rb').read()
n = struct.unpack_from('>I', d, 8)[0]
names = 12 * n
files = []
stack = [('', n)]
for i in range(1, n):
    while stack and i >= stack[-1][1]:
        stack.pop()
    t, off, size = struct.unpack_from('>III', d, 12 * i)
    p = names + (t & 0xFFFFFF)
    name = d[p:d.index(b'\0', p)].decode('latin1')
    path = '/'.join(x for x, _ in stack[1:]) + ('/' if len(stack) > 1 else '') + name
    if t >> 24:
        stack.append((name, size))
    else:
        files.append((off << 2, size, path))   # Wii: offsets are stored >> 2
files.sort()
if len(sys.argv) == 1:
    for off, size, path in files:
        print(f'{off:10x} {size:10x} {path}')
for arg in sys.argv[1:]:
    a = int(arg, 16)
    hit = [f for f in files if f[0] <= a < f[0] + f[1]]
    print(f'{a:x}: ' + (f'{hit[0][2]} +{a - hit[0][0]:#x} of {hit[0][1]:#x}' if hit else 'no file'))
