"""Halloween art for the Silakka54 right-half nice!view.

Draws three 68x140 portrait one-bit pictures (pumpkin frame A, pumpkin frame B,
tombstone) with a tiny software rasteriser, then emits:
  - art.c      LVGL INDEXED_1BIT images in the 140x68 buffer orientation
  - art.json   the portrait bitmaps as hex rows for the HTML mock
  - ASCII previews on stdout
Pixel value 1 = ink, 0 = background (inverted to LVGL index bits on output).
"""
import json, math, os, sys

W, H = 68, 140


class Bitmap:
    def __init__(self):
        self.px = [[0] * W for _ in range(H)]

    def set(self, x, y, v=1):
        if 0 <= x < W and 0 <= y < H:
            self.px[y][x] = v

    def rect(self, x0, y0, x1, y1, v=1):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y, v)

    def ellipse(self, cx, cy, rx, ry, v=1):
        for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
            for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
                if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
                    self.set(x, y, v)

    def ellipse_outline(self, cx, cy, rx, ry, v=1):
        steps = int(8 * (rx + ry))
        for i in range(steps):
            a = 2 * math.pi * i / steps
            self.set(int(round(cx + rx * math.cos(a))), int(round(cy + ry * math.sin(a))), v)

    def polygon(self, pts, v=1):
        ys = [p[1] for p in pts]
        for y in range(int(min(ys)), int(max(ys)) + 1):
            xs = []
            n = len(pts)
            for i in range(n):
                (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % n]
                if y0 == y1:
                    continue
                if (y >= min(y0, y1)) and (y < max(y0, y1)):
                    xs.append(x0 + (y - y0) * (x1 - x0) / (y1 - y0))
            xs.sort()
            for i in range(0, len(xs) - 1, 2):
                for x in range(int(round(xs[i])), int(round(xs[i + 1])) + 1):
                    self.set(x, y, v)

    def line(self, x0, y0, x1, y1, v=1, w=1):
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for i in range(n + 1):
            t = i / n
            x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
            for dy in range(w):
                for dx in range(w):
                    self.set(int(round(x)) + dx - w // 2, int(round(y)) + dy - w // 2, v)

    def text3x5(self, s, x, y, scale=2, v=1):
        font = {
            "R": ["##.", "#.#", "##.", "#.#", "#.#"],
            "I": ["###", ".#.", ".#.", ".#.", "###"],
            "P": ["##.", "#.#", "##.", "#..", "#.."],
            "Z": ["###", "..#", ".#.", "#..", "###"],
        }
        for ch in s:
            g = font[ch]
            for r, row in enumerate(g):
                for c, cell in enumerate(row):
                    if cell == "#":
                        self.rect(x + c * scale, y + r * scale, x + c * scale + scale - 1, y + r * scale + scale - 1, v)
            x += 3 * scale + 2

    def ascii(self):
        return "\n".join("".join("#" if p else "." for p in row) for row in self.px)


def sky(b, bats, small=False):
    # crescent moon top-left
    if small:
        b.ellipse(10, 12, 7, 7, 1)
        b.ellipse(13, 10, 6, 6, 0)
    else:
        b.ellipse(15, 20, 9, 9, 1)
        b.ellipse(19, 17, 8, 8, 0)
    # stars
    stars = [(2, 60), (66, 70), (3, 100), (65, 120)] if small else [(40, 10), (58, 16), (6, 40), (62, 44), (48, 48), (30, 30)]
    for (x, y) in stars:
        b.set(x, y)
    for (x, y) in ([] if small else [(40, 10), (62, 44)]):
        b.set(x - 1, y); b.set(x + 1, y); b.set(x, y - 1); b.set(x, y + 1)
    for (cx, cy, up) in bats:
        bat(b, cx, cy, up)


def bat(b, cx, cy, up):
    b.ellipse(cx, cy, 2, 2.2, 1)
    b.set(cx - 1, cy - 3); b.set(cx + 1, cy - 3)
    for s in (-1, 1):
        if up:
            wing = [(cx + s * 2, cy - 1), (cx + s * 6, cy - 6), (cx + s * 9, cy - 3), (cx + s * 13, cy - 6),
                    (cx + s * 11, cy), (cx + s * 7, cy + 2)]
        else:
            wing = [(cx + s * 2, cy - 1), (cx + s * 7, cy - 2), (cx + s * 11, cy), (cx + s * 13, cy + 5),
                    (cx + s * 9, cy + 3), (cx + s * 6, cy + 6), (cx + s * 2, cy + 1)]
        b.polygon(wing, 1)


def ground(b, y=126):
    b.line(2, y, 65, y, 1)
    for x in (6, 14, 26, 44, 56, 62):
        b.line(x, y, x, y - 3, 1)
        b.line(x + 2, y, x + 3, y - 2, 1)


def ellipse_arc(b, cx, cy, rx, ry, a0, a1, v=1, w=1):
    steps = int(8 * (rx + ry))
    for i in range(steps + 1):
        a = a0 + (a1 - a0) * i / steps
        x, y = cx + rx * math.cos(a), cy + ry * math.sin(a)
        for dy in range(w):
            b.set(int(round(x)), int(round(y)) + dy, v)


def skull(b, hx, hy, rx, ry, mode, stitches=True):
    b.ellipse(hx, hy, rx, ry, 1)
    b.ellipse(hx, hy, rx - 2, ry - 2, 0)
    eye_dx = int(rx * 0.46)
    eye_rx = int(rx * 0.33)
    ey = hy - int(ry * 0.15)
    for ex in (hx - eye_dx, hx + eye_dx):
        if mode == "sleep":
            ellipse_arc(b, ex, ey + 2, eye_rx, 3, 0.15 * math.pi, 0.85 * math.pi, 1, 2)
        else:
            b.ellipse(ex, ey, eye_rx, int(ry * 0.37) if mode == "open" else 3, 1)
    # nostrils
    ny = hy + int(ry * 0.26)
    b.line(hx - 3, ny, hx - 2, ny + 3, 1); b.line(hx + 3, ny, hx + 2, ny + 3, 1)
    # grin
    my = hy + int(ry * 0.33)
    if mode == "sleep":
        ellipse_arc(b, hx, my, int(rx * 0.6), int(ry * 0.2), 0.2 * math.pi, 0.8 * math.pi, 1, 2)
    else:
        mrx, mry = int(rx * 0.83), int(ry * 0.37)
        ellipse_arc(b, hx, my, mrx, mry, 0.12 * math.pi, 0.88 * math.pi, 1, 2)
        if stitches:
            for a in [0.2, 0.32, 0.44, 0.56, 0.68, 0.8]:
                x = hx + mrx * math.cos(a * math.pi)
                y = my + mry * math.sin(a * math.pi)
                b.line(int(round(x)), int(round(y)) - 2, int(round(x)), int(round(y)) + 3, 1)


def bow_tie(b, x, y):
    b.ellipse(x, y, 2, 2, 1)
    b.polygon([(x - 1, y - 1), (x - 8, y - 4), (x - 7, y + 3)], 1)
    b.polygon([(x + 1, y - 1), (x + 8, y - 4), (x + 7, y + 3)], 1)


def jack_awake(b, mode, bat_up):
    """Standing Jack. mode: open | blink."""
    sky(b, [(56, 30, bat_up)])
    skull(b, 34, 52, 24, 27, mode)
    b.rect(32, 79, 35, 85, 1)
    b.polygon([(14, 87), (54, 87), (57, 104), (52, 139), (16, 139), (11, 104)], 1)
    for x in range(17, 52, 4):
        b.line(x, 94, x, 138, 0)
    b.polygon([(26, 87), (42, 87), (34, 100)], 0)
    bow_tie(b, 34, 92)


def jack_coffin(b, zz_up):
    """Jack asleep in his coffin, seen from above."""
    sky(b, [], small=True)
    # coffin: hexagonal outline, 2 px
    outer = [(22, 14), (46, 14), (62, 44), (52, 138), (16, 138), (6, 44)]
    inner = [(24, 17), (44, 17), (59, 45), (50, 135), (18, 135), (9, 45)]
    b.polygon(outer, 1)
    b.polygon(inner, 0)
    # pillow line
    b.line(12, 60, 56, 60, 1)
    skull(b, 34, 40, 16, 18, "sleep")
    # suit under a blanket edge, pinstripes
    b.polygon([(18, 72), (50, 72), (49, 133), (19, 133)], 1)
    for x in range(21, 48, 4):
        b.line(x, 76, x, 131, 0)
    b.polygon([(27, 72), (41, 72), (34, 83)], 0)
    bow_tie(b, 34, 77)
    # crossed hands as two white bars
    b.polygon([(22, 98), (44, 108), (43, 112), (21, 102)], 0)
    b.polygon([(46, 98), (24, 108), (25, 112), (47, 102)], 0)
    # zz
    y0 = 24 if zz_up else 28
    b.text3x5("Z", 55, y0 + 6, 2, 1)
    b.text3x5("Z", 62, y0 - 2, 1, 1)


def scene(name):
    b = Bitmap()
    if name == "jack_a":
        jack_awake(b, "open", True)
    elif name == "jack_b":
        jack_awake(b, "blink", True)
    elif name == "jack_c":
        jack_awake(b, "open", False)
    elif name == "coffin_a":
        jack_coffin(b, False)
    elif name == "coffin_b":
        jack_coffin(b, True)
    return b


def to_lvgl(bm, name):
    """Portrait (px, py) -> buffer (bx = 139 - py, by = px); bit 0 = ink (index 0)."""
    BW, BH = 140, 68
    stride = (BW + 7) // 8
    data = bytearray(stride * BH)
    for py in range(H):
        for px in range(W):
            bx, by = 139 - py, px
            bit = 0 if bm.px[py][px] else 1
            if bit:
                data[by * stride + bx // 8] |= 0x80 >> (bx % 8)
    hexs = ", ".join(f"0x{b:02x}" for b in data)
    rows = []
    for i in range(0, len(data), 15):
        rows.append("        " + ", ".join(f"0x{b:02x}" for b in data[i:i + 15]) + ",")
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
    scenes = {n: scene(n) for n in ("jack_a", "jack_b", "jack_c", "coffin_a", "coffin_b")}
    c = ["/*", " * Halloween art for the right half: Jack Skellington (open eyes, blink,", " * asleep when not linked). Generated by gen_art.py, 68x140 portrait pictures stored", " * in the 140x68 buffer orientation like the stock balloon/mountain.", " *", " * SPDX-License-Identifier: MIT", " */", "", "#include <lvgl.h>", "", "#ifndef LV_ATTRIBUTE_MEM_ALIGN", "#define LV_ATTRIBUTE_MEM_ALIGN", "#endif"]
    js = {}
    for name, bm in scenes.items():
        c.append(to_lvgl(bm, name))
        js[name] = ["".join(str(p) for p in row) for row in bm.px]
        print(f"=== {name} ===")
        print(bm.ascii())
    open(os.path.join(outdir, "art.c"), "w").write("\n".join(c))
    json.dump(js, open(os.path.join(outdir, "art.json"), "w"))


if __name__ == "__main__":
    main(sys.argv[1])
