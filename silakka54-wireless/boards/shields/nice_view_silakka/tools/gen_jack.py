"""Build themes/jack/right/art.c: five animation frames from the two Bayer bitmaps in src/.
Usage: python3 gen_jack.py   (needs Pillow). Make new src/*.png with img2art.py.
jack_a/b: staff pose open/blink; jack_c: classic bust (pose change);
coffin_a/b: classic bust asleep without/with Zs."""
import sys, os, json
HERE = os.path.dirname(os.path.abspath(__file__))
from PIL import Image, ImageDraw
W, H = 68, 140
# With the stock palette this board showed the art as a negative while the
# canvas-drawn strip was fine. Flipping the data fixed it on the screen; cause
# not found. Set False if a new board shows Jack as a negative again.
INVERT = True

def to_lvgl(bits, name):
    BW, BH = 140, 68
    stride = (BW + 7) // 8
    data = bytearray(stride * BH)
    for py in range(H):
        for px in range(W):
            bx, by = 139 - py, px
            if not bits[py][px]:
                data[by * stride + bx // 8] |= 0x80 >> (bx % 8)
    rows = ["        " + ", ".join(f"0x{b:02x}" for b in data[i:i + 15]) + "," for i in range(0, len(data), 15)]
    body = "\n".join(rows)
    return f"""
const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t {name}_map[] = {{
#if CONFIG_NICE_VIEW_SILAKKA_WIDGET_INVERTED
        0xff, 0xff, 0xff, 0xff, /*Color of index 0*/
        0x00, 0x00, 0x00, 0xff, /*Color of index 1*/
#else
        0x00, 0x00, 0x00, 0xff, /*Color of index 0*/
        0xff, 0xff, 0xff, 0xff, /*Color of index 1*/
#endif

{body}
}};

const lv_img_dsc_t {name} = {{
    .header.cf = LV_IMG_CF_INDEXED_1BIT,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = {BW},
    .header.h = {BH},
    .data_size = {len(data) + 8},
    .data = {name}_map,
}};
"""


def load_bits(path):
    im = Image.open(path).convert('1'); px = im.load()
    return [[0 if px[x, y] else 1 for x in range(W)] for y in range(H)]  # 1 = ink

def blobs(bits, ymax=70):
    seen = [[False]*W for _ in range(H)]; out = []
    for y in range(ymax):
        for x in range(W):
            if bits[y][x] and not seen[y][x]:
                st = [(x, y)]; seen[y][x] = True; pts = []
                while st:
                    cx, cy = st.pop(); pts.append((cx, cy))
                    for nx, ny in ((cx+1,cy),(cx-1,cy),(cx,cy+1),(cx,cy-1)):
                        if 0 <= nx < W and 0 <= ny < ymax and bits[ny][nx] and not seen[ny][nx]:
                            seen[ny][nx] = True; st.append((nx, ny))
                out.append(pts)
    return out

def eyes(bits):
    # two biggest compact blobs in the upper half = eye sockets
    cands = []
    for pts in blobs(bits):
        xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
        w, h = max(xs)-min(xs)+1, max(ys)-min(ys)+1
        fill = len(pts) / (w*h)
        if 15 <= len(pts) <= 400 and fill > 0.55 and w < 25 and h < 25:
            cands.append(pts)
    cands.sort(key=len, reverse=True)
    return cands[:2]

def close_eyes(bits, lids=2):
    out = [row[:] for row in bits]
    for pts in eyes(bits):
        bottom = {}
        for x, y in pts:
            out[y][x] = 0
            bottom[x] = max(bottom.get(x, 0), y)
        for x, y in bottom.items():
            for k in range(lids):
                if 0 <= y-k < H: out[y-k][x] = 1
    return out

def draw_zs(bits, pos):
    im = Image.new('1', (W, H), 1); px = im.load()
    for y in range(H):
        for x in range(W):
            px[x, y] = 0 if bits[y][x] else 1
    d = ImageDraw.Draw(im)
    for (x, y, s) in pos:
        # Z glyph: top bar, diagonal, bottom bar
        d.line([(x, y), (x+s, y)], fill=0); d.line([(x+s, y), (x, y+s)], fill=0); d.line([(x, y+s), (x+s, y+s)], fill=0)
        # clear a 1px halo so it reads on dark areas
    return [[0 if px[x, y] else 1 for x in range(W)] for y in range(H)]

def halo_zs(bits, pos):
    out = [row[:] for row in bits]
    for (x, y, s) in pos:
        for yy in range(y-2, y+s+3):
            for xx in range(x-2, x+s+3):
                if 0 <= xx < W and 0 <= yy < H: out[yy][xx] = 0
    return draw_zs(out, pos)

def preview(bits, path, z=4):
    im = Image.new('1', (W, H), 1); px = im.load()
    for y in range(H):
        for x in range(W):
            px[x, y] = 0 if bits[y][x] else 1
    im.resize((W*z, H*z), Image.NEAREST).save(path)

def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    staff = load_bits(os.path.join(HERE, 'src', 'jack_staff.png'))
    classic = load_bits(os.path.join(HERE, 'src', 'jack_classic.png'))
    frames = {
        'jack_a': staff,
        'jack_b': close_eyes(staff),
        'jack_c': classic,
        'coffin_a': close_eyes(classic),
        'coffin_b': halo_zs(close_eyes(classic), [(49, 26, 4), (56, 18, 6)]),
    }
    hdr = ["/*", " * Jack Skellington for the right half. Frames come from two illustrations",
           " * fitted to 68x140 and Bayer-dithered to one bit by tools/img2art.py, with",
           " * eyes closed and Zs added by tools/gen_jack.py. Stored in the 140x68 buffer",
           " * orientation like the stock balloon/mountain.", " *", " * SPDX-License-Identifier: MIT", " */", "",
           "#include <lvgl.h>", "", "#ifndef LV_ATTRIBUTE_MEM_ALIGN", "#define LV_ATTRIBUTE_MEM_ALIGN", "#endif"]
    src = hdr[:]
    for n, b in frames.items():
        if INVERT:
            b = [[1 - p for p in row] for row in b]
        src.append(to_lvgl(b, n))
        preview(b, os.path.join(outdir, f'preview_{n}.png'))
    open(os.path.join(HERE, '..', 'themes', 'jack', 'right', 'art.c'), 'w').write("\n".join(src))
    sheet = Image.new('L', (W*4*5 + 40, H*4), 128)
    for i, n in enumerate(frames):
        sheet.paste(Image.open(os.path.join(outdir, f'preview_{n}.png')).convert('L'), (i*(W*4+8)+8, 0))
    sheet.save(os.path.join(outdir, 'preview_sheet.png'))
    print('eyes staff', [len(e) for e in eyes(staff)], 'classic', [len(e) for e in eyes(classic)])

if __name__ == '__main__':
    main(os.path.join(HERE, 'preview'))
