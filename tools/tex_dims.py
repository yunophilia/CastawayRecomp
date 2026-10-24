# Reads width/height of every TXFL texture in the Wii and PS2 textures.arc and compares them by name.
# Layout found by inspection: name, then u32 0x00010420, ..., u16 w at name_end+0x14, h at +0x16, palette size at +0x18.
import re, struct, sys, collections
def scan(path, e):
    d = open(path, 'rb').read(); out = {}
    tag = b'TXFL' if e == '>' else b'LFXT'
    for m in re.finditer(re.escape(tag) + rb'\xff\xff\xff\xff', d):
        p = m.start() + 8
        n, = struct.unpack(e+'I', d[p:p+4]); name = d[p+4:p+4+n-1].decode('latin1'); q = p+4+n
        w, h, c = struct.unpack(e+'HHH', d[q+0x14:q+0x1a])
        out[name] = (w, h, c, struct.unpack(e+'I', d[q:q+4])[0], struct.unpack(e+'I', d[q+0xc:q+0x10])[0])
    return out
wii = scan(sys.argv[1], '>'); ps2 = scan(sys.argv[2], '<')
common = wii.keys() & ps2.keys()
print(f"textures: wii {len(wii)}, ps2 {len(ps2)}, same name {len(common)}")
cmp = collections.Counter(); pix_w = pix_p = 0; diffs = []
for k in common:
    (ww, wh, wc, *_), (pw, ph, pc, *_) = wii[k], ps2[k]
    pix_w += ww*wh; pix_p += pw*ph
    a, b = ww*wh, pw*ph
    cmp['wii larger' if a > b else 'ps2 larger' if b > a else 'same size'] += 1
    if a != b: diffs.append((a/b if b else 0, k, (ww, wh), (pw, ph)))
print(dict(cmp)); print(f"total texels: wii {pix_w:,}  ps2 {pix_p:,}  ratio {pix_w/pix_p:.3f}")
print("palette/format field (3rd u16) wii:", collections.Counter(v[2] for v in wii.values()).most_common(6))
print("palette/format field (3rd u16) ps2:", collections.Counter(v[2] for v in ps2.values()).most_common(6))
print("size distribution wii:", collections.Counter(f"{v[0]}x{v[1]}" for v in wii.values()).most_common(8))
print("size distribution ps2:", collections.Counter(f"{v[0]}x{v[1]}" for v in ps2.values()).most_common(8))
diffs.sort()
print("differing examples:"); [print("  ", f"{r:.2f}", k, "wii", a, "ps2", b) for r, k, a, b in diffs[:6] + diffs[-6:]]
