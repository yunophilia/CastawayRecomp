# Scans an EA .arc for records shaped [u32][fourcc][ffffffff][u32 namelen][name\0] and
# writes name -> (fourcc, record size) as JSON. Record size = distance to the next record.
import json, re, struct, sys
path, big, out = sys.argv[1], sys.argv[2] == 'be', sys.argv[3]
d = open(path, 'rb').read()
e = '>' if big else '<'
recs = []
for m in re.finditer(rb'\xff\xff\xff\xff', d):
    p = m.start()
    if p < 8: continue
    tag = d[p-4:p]
    if not re.fullmatch(rb'[A-Z0-9_]{4}', tag): continue
    n, = struct.unpack(e+'I', d[p+4:p+8])
    if not 1 < n < 256: continue
    name = d[p+8:p+8+n]
    if name[-1:] != b'\0' or not re.fullmatch(rb'[\x20-\x7e]+', name[:-1]): continue
    fourcc = tag.decode() if big else tag[::-1].decode()
    recs.append((p-8, fourcc, name[:-1].decode()))
res = {}
for i, (p, fcc, name) in enumerate(recs):
    end = recs[i+1][0] if i+1 < len(recs) else len(d)
    res.setdefault(fcc, {})[name] = end - p
json.dump(res, open(out, 'w'))
for fcc, v in sorted(res.items(), key=lambda kv: -sum(kv[1].values())):
    print(f"{fcc} {len(v):6} records {sum(v.values()):12} bytes")
