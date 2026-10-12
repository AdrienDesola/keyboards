"""Jack Skellington for the Silakka54 right-half nice!view, drawn at 8x with
anti-aliasing, downsampled to 68x140 and thresholded to one bit.

Outputs art.c (LVGL INDEXED_1BIT, 140x68 buffer orientation), art.json for the
HTML mock, and preview PNGs at 4x for eyeballing.
"""
import json, math, os, sys
from PIL import Image, ImageDraw

W, H, S = 68, 140, 8  # target size, supersampling
INK, BG = 0, 255


class Canvas:
    def __init__(self):
        self.im = Image.new("L", (W * S, H * S), BG)
        self.d = ImageDraw.Draw(self.im)

    # all coordinates below are in target pixels (floats ok)
    def P(self, pts):
        return [(x * S, y * S) for x, y in pts]

    def poly(self, pts, fill=None, outline=None, width=1.5):
        self.d.polygon(self.P(pts), fill=fill, outline=outline, width=int(width * S))

    def ellipse(self, cx, cy, rx, ry, fill=None, outline=None, width=1.5):
        self.d.ellipse([(cx - rx) * S, (cy - ry) * S, (cx + rx) * S, (cy + ry) * S], fill=fill, outline=outline,
                       width=int(width * S))

    def arc(self, cx, cy, rx, ry, a0, a1, width=1.5, fill=INK):
        self.d.arc([(cx - rx) * S, (cy - ry) * S, (cx + rx) * S, (cy + ry) * S], a0, a1, fill=fill, width=int(width * S))

    def line(self, pts, width=1.5, fill=INK):
        self.d.line(self.P(pts), fill=fill, width=int(width * S), joint="curve")

    def skull_pts(self, cx, cy, rx, ry, n=64):
        pts = []
        for i in range(n):
            a = 2 * math.pi * i / n
            s = math.sin(a)
            narrow = 1 - 0.32 * max(0.0, s) ** 1.3  # chin tapers below the eyes
            pts.append((cx + rx * narrow * math.cos(a), cy + ry * s))
        return pts

    def rotated_ellipse_pts(self, cx, cy, rx, ry, deg, n=48):
        t = math.radians(deg)
        pts = []
        for i in range(n):
            a = 2 * math.pi * i / n
            x, y = rx * math.cos(a), ry * math.sin(a)
            pts.append((cx + x * math.cos(t) - y * math.sin(t), cy + x * math.sin(t) + y * math.cos(t)))
        return pts

    def to_bits(self):
        small = self.im.resize((W, H), Image.LANCZOS)
        return [[1 if small.getpixel((x, y)) < 128 else 0 for x in range(W)] for y in range(H)]

    def preview(self, path):
        small = self.im.resize((W, H), Image.LANCZOS).point(lambda v: 0 if v < 128 else 255, "1")
        small.convert("L").resize((W * 4, H * 4), Image.NEAREST).save(path)


# ---------------------------------------------------------------- elements

def bat(c, cx, cy, up, size=1.0):
    s = size
    body = c.rotated_ellipse_pts(cx, cy, 2.2 * s, 2.8 * s, 0)
    c.poly(body, fill=INK)
    c.poly([(cx - 1.5 * s, cy - 2.2 * s), (cx - 2.4 * s, cy - 4.5 * s), (cx - 0.3 * s, cy - 2.8 * s)], fill=INK)
    c.poly([(cx + 1.5 * s, cy - 2.2 * s), (cx + 2.4 * s, cy - 4.5 * s), (cx + 0.3 * s, cy - 2.8 * s)], fill=INK)
    for sg in (-1, 1):
        if up:
            wing = [(cx + sg * 1.5 * s, cy - 1.5 * s), (cx + sg * 6 * s, cy - 7 * s), (cx + sg * 9 * s, cy - 4 * s),
                    (cx + sg * 14 * s, cy - 7 * s), (cx + sg * 12 * s, cy - 1 * s), (cx + sg * 8.5 * s, cy + 0.5 * s),
                    (cx + sg * 5 * s, cy + 2 * s), (cx + sg * 1.5 * s, cy + 1.5 * s)]
        else:
            wing = [(cx + sg * 1.5 * s, cy - 1.5 * s), (cx + sg * 7 * s, cy - 2.5 * s), (cx + sg * 12 * s, cy - 0.5 * s),
                    (cx + sg * 14 * s, cy + 6 * s), (cx + sg * 10 * s, cy + 3.5 * s), (cx + sg * 7 * s, cy + 6.5 * s),
                    (cx + sg * 4 * s, cy + 3.5 * s), (cx + sg * 1.5 * s, cy + 1.5 * s)]
        c.poly(wing, fill=INK)


def moon(c, cx, cy, r):
    c.ellipse(cx, cy, r, r, fill=BG, outline=INK, width=1.2)
    # a few craters
    for (dx, dy, rr) in [(-0.35, -0.25, 0.16), (0.3, 0.1, 0.11), (-0.1, 0.4, 0.08), (0.15, -0.45, 0.07)]:
        c.ellipse(cx + dx * r, cy + dy * r, rr * r, rr * r * 0.8, outline=INK, width=1.0)


def skull(c, cx, cy, rx, ry, eyes="open", tilt=0):
    pts = c.skull_pts(cx, cy, rx, ry)
    c.poly(pts, fill=BG, outline=INK, width=1.6)
    # cheek hollows: short curves at the lower sides
    c.arc(cx - rx * 0.55, cy + ry * 0.35, rx * 0.22, ry * 0.3, 300, 60, width=1.0)
    c.arc(cx + rx * 0.55, cy + ry * 0.35, rx * 0.22, ry * 0.3, 120, 240, width=1.0)
    # eye sockets: big, slightly tilted outward
    ex, ey = rx * 0.42, cy - ry * 0.12
    erx, ery = rx * 0.3, ry * 0.36
    if eyes == "open":
        c.poly(c.rotated_ellipse_pts(cx - ex, ey, erx, ery, -14), fill=INK)
        c.poly(c.rotated_ellipse_pts(cx + ex, ey, erx, ery, 14), fill=INK)
    elif eyes == "blink":
        c.poly(c.rotated_ellipse_pts(cx - ex, ey, erx, ery * 0.22, -14), fill=INK)
        c.poly(c.rotated_ellipse_pts(cx + ex, ey, erx, ery * 0.22, 14), fill=INK)
    else:  # closed, sleeping
        c.arc(cx - ex, ey - ery * 0.1, erx, ery * 0.5, 20, 160, width=1.6)
        c.arc(cx + ex, ey - ery * 0.1, erx, ery * 0.5, 20, 160, width=1.6)
    # nostrils: two small teardrops
    ny = cy + ry * 0.3
    c.poly([(cx - 1.2, ny - 2.5), (cx - 2.6, ny + 1.5), (cx - 0.6, ny + 1.2)], fill=INK)
    c.poly([(cx + 1.2, ny - 2.5), (cx + 2.6, ny + 1.5), (cx + 0.6, ny + 1.2)], fill=INK)
    # the grin: wide thin arc with stitches
    my = cy + ry * 0.42
    mrx, mry = rx * 0.88, ry * 0.3
    if eyes == "sleep":
        c.arc(cx, my, mrx * 0.75, mry * 0.5, 15, 165, width=1.3)
    else:
        c.arc(cx, my, mrx, mry, 12, 168, width=1.3)
        for k in range(8):
            a = math.radians(26 + k * 18)
            x, y = cx + mrx * math.cos(a), my + mry * math.sin(a)
            c.line([(x, y - 2.2), (x, y + 2.4)], width=1.1)
        # grin corners curl up a touch
        c.arc(cx - mrx, my - 1.5, 2.2, 2.2, 150, 300, width=1.0)
        c.arc(cx + mrx, my - 1.5, 2.2, 2.2, 240, 30, width=1.0)


def bow_tie(c, cx, cy, up, size=1.0):
    bat(c, cx, cy, up, size)
    # tiny white eyes on the bat
    c.ellipse(cx - 1.0 * size, cy - 0.6 * size, 0.55 * size, 0.55 * size, fill=BG)
    c.ellipse(cx + 1.0 * size, cy - 0.6 * size, 0.55 * size, 0.55 * size, fill=BG)


def suit(c, top, bottom, left, right, waist_in=2.5, stripe_gap=3.6):
    body = [(left, top), (right, top), (right + 1.5, top + 16), (right - waist_in, bottom), (left + waist_in, bottom),
            (left - 1.5, top + 16)]
    c.poly(body, fill=INK)
    x = left + 3
    while x < right - 2:
        c.line([(x, top + 5), (x, bottom)], width=0.9, fill=BG)
        x += stripe_gap
    # lapels: two white wedges meeting at the chest
    mid = (left + right) / 2
    c.poly([(mid - 6, top), (mid, top + 11), (mid, top + 1)], fill=BG)
    c.poly([(mid + 6, top), (mid, top + 11), (mid, top + 1)], fill=BG)
    c.line([(mid - 6, top), (mid, top + 11), (mid + 6, top)], width=1.0)


def hand(c, cx, cy, size=1.0, fingers_up=True):
    # spindly bone hand: small palm, four long thin fingers
    c.ellipse(cx, cy, 2.6 * size, 2.0 * size, fill=BG, outline=INK, width=1.1)
    for k, (dx, L) in enumerate([(-2.4, 6.5), (-0.8, 8), (0.8, 8), (2.4, 6.5)]):
        d = -1 if fingers_up else 1
        c.line([(cx + dx * size, cy + d * 1.5 * size), (cx + dx * 1.9 * size, cy + d * L * size)], width=1.15)


def zz(c, x, y, size):
    s = size
    c.line([(x, y), (x + 3 * s, y), (x, y + 4 * s), (x + 3 * s, y + 4 * s)], width=1.1)


# ---------------------------------------------------------------- scenes

def jack_awake(c, eyes, bat_up):
    moon(c, 50, 22, 17)
    bat(c, 11, 12, bat_up, 0.75)
    # skull
    skull(c, 33, 44, 21, 25, eyes)
    # neck
    c.poly([(31, 68), (36, 68), (36.5, 78), (30.5, 78)], fill=BG, outline=INK, width=1.1)
    # suit, then a wide white collar with the bat bow tie on it
    suit(c, 80, 140, 13, 54)
    c.poly([(17, 79), (50, 79), (33.5, 99)], fill=BG)
    c.line([(17, 79), (33.5, 99), (50, 79)], width=1.1)
    bow_tie(c, 33.5, 85.5, bat_up, 0.62)
    # left arm down along the body, right arm raised with the open hand by the head
    c.line([(13, 86), (6, 108), (7, 124)], width=2.2)
    hand(c, 7, 129, 0.9, fingers_up=False)
    c.line([(54, 86), (62, 74), (63, 64)], width=2.2)
    hand(c, 63, 59, 0.95, fingers_up=True)


def jack_coffin(c, zz_up):
    # coffin seen from above: hexagon with a lining line
    outer = [(21, 8), (46, 8), (63, 44), (54, 138), (13, 138), (4, 44)]
    inner = [(23.5, 11), (43.5, 11), (59.5, 45), (51, 135), (16, 135), (7.5, 45)]
    c.poly(outer, fill=BG, outline=INK, width=2.0)
    c.poly(inner, fill=None, outline=INK, width=0.9)
    # pillow
    c.ellipse(33.5, 38, 22, 11, fill=BG, outline=INK, width=1.0)
    skull(c, 33.5, 40, 15.5, 18.5, "sleep")
    # neck, suit, collar with the bow tie, hands crossed on the chest
    c.poly([(32, 58), (35, 58), (35.5, 64), (31.5, 64)], fill=BG, outline=INK, width=1.0)
    suit(c, 65, 132, 17, 50, waist_in=1.5, stripe_gap=3.3)
    c.poly([(21, 65), (46, 65), (33.5, 80)], fill=BG)
    c.line([(21, 65), (33.5, 80), (46, 65)], width=1.0)
    bow_tie(c, 33.5, 70, False, 0.5)
    hand(c, 28.5, 100, 1.1, fingers_up=False)
    hand(c, 38.5, 97, 1.1, fingers_up=False)
    # the Zs
    y0 = 14 if zz_up else 19
    zz(c, 50, y0 + 6, 1.3)
    zz(c, 57, y0, 0.9)
    zz(c, 62, y0 - 5, 0.6)


def scene(name):
    c = Canvas()
    {
        "jack_a": lambda: jack_awake(c, "open", True),
        "jack_b": lambda: jack_awake(c, "blink", True),
        "jack_c": lambda: jack_awake(c, "open", False),
        "coffin_a": lambda: jack_coffin(c, False),
        "coffin_b": lambda: jack_coffin(c, True),
    }[name]()
    return c


# ---------------------------------------------------------------- output

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


def main(outdir):
    names = ["jack_a", "jack_b", "jack_c", "coffin_a", "coffin_b"]
    c_src = ["/*", " * Jack Skellington for the right half: awake (open eyes, blink, bat flap)",
             " * while linked, asleep in his coffin (two Z positions) while not. Drawn at", " * 8x by tools/gen_art.py, "
             "downsampled to 68x140 and stored in the 140x68 buffer", " * orientation like the stock balloon/mountain.",
             " *", " * SPDX-License-Identifier: MIT", " */", "", "#include <lvgl.h>", "", "#ifndef LV_ATTRIBUTE_MEM_ALIGN",
             "#define LV_ATTRIBUTE_MEM_ALIGN", "#endif"]
    js = {}
    for n in names:
        c = scene(n)
        bits = c.to_bits()
        c_src.append(to_lvgl(bits, n))
        js[n] = ["".join(str(p) for p in row) for row in bits]
        c.preview(os.path.join(outdir, f"preview_{n}.png"))
    open(os.path.join(outdir, "art.c"), "w").write("\n".join(c_src))
    json.dump(js, open(os.path.join(outdir, "art.json"), "w"))
    # contact sheet of all five at 4x
    sheet = Image.new("L", (W * 4 * 5 + 40, H * 4), 128)
    for i, n in enumerate(names):
        sheet.paste(Image.open(os.path.join(outdir, f"preview_{n}.png")), (i * (W * 4 + 10), 0))
    sheet.save(os.path.join(outdir, "preview_sheet.png"))


if __name__ == "__main__":
    main(sys.argv[1])
