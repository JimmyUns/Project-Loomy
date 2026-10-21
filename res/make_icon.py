# Builds res/loomy.ico from loomy_logo.png. Small sizes as BMP (safest for tray), big ones as PNG (smaller).
import struct, io
from PIL import Image
src = Image.open("res/loomy_logo.png").convert("RGBA")
entries = []
for size in (16, 20, 24, 32, 48, 256):
    im = src.resize((size, size), Image.LANCZOS)
    if size >= 48:
        im = im.quantize(32, method=Image.FASTOCTREE).convert("RGBA")  # fewer colors = smaller, still 32-bit RGBA
        b = io.BytesIO(); im.save(b, "PNG", optimize=True); data = b.getvalue()
    else:
        px = im.tobytes("raw", "BGRA")
        rows = [px[y*size*4:(y+1)*size*4] for y in range(size)][::-1]
        mask_row = ((size + 31) // 32) * 4
        data = struct.pack("<IiiHHIIiiII", 40, size, size*2, 1, 32, 0, 0, 0, 0, 0, 0) + b"".join(rows) + b"\0" * mask_row * size
    entries.append((size, data))
out = struct.pack("<HHH", 0, 1, len(entries))
off = 6 + 16 * len(entries)
for size, data in entries:
    out += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), off); off += len(data)
for _, data in entries: out += data
open("res/loomy.ico", "wb").write(out)
print("ico bytes:", len(out), [(s, len(d)) for s, d in entries])
