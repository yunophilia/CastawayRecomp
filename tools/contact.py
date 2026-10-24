# Tiles screenshots into one contact sheet with ffmpeg.
# usage: python tools/contact.py <out.png> <columns> <image>...
import os
import subprocess
import sys
import tempfile

out, cols, images = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
rows = (len(images) + cols - 1) // cols
with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as fp:
    for p in images:
        fp.write(f"file '{os.path.abspath(p)}'\nduration 1\n")
    lst = fp.name
subprocess.check_call(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'concat', '-safe', '0', '-i', lst,
                       '-vf', f'scale=320:-1,tile={cols}x{rows}', '-frames:v', '1', out])
os.unlink(lst)
print(out)
