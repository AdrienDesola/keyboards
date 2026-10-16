#!/usr/bin/env python3
"""Turn a video excerpt into a one-bit flipbook for the right nice!view.

    vid2clip.py <video> <name> --start 30 --duration 8 --fps 12 \
        [--crop x,y,w,h] [--mode thresh|bayer] [--contrast 1.5] [--gamma 1.0] \
        [--smooth 0.3] [--invert] [--out DIR]

Writes <out>/<name>.json (frames + stats for the browser mock) and
<out>/clip_<name>.c (packed frames for the firmware, drop it in widgets/). Frames are stored in the
display's own orientation (140 wide, 68 tall) as run-length bytes: bit 7 is the
pixel value, bits 0-6 the run length (1-127). Every frame after the first is
XORed with the previous one before packing, so still backgrounds cost nothing.

Needs ffmpeg on PATH and Pillow.
"""

import argparse
import base64
import json
import os
import subprocess
import sys
import tempfile

from PIL import Image

W, H = 68, 140  # portrait, as the user sees it
BAYER4 = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def extract(video, start, duration, fps, crop, tmp):
    vf = []
    if crop:
        x, y, w, h = crop
        vf.append(f"crop={w}:{h}:{x}:{y}")
    vf.append(f"fps={fps}")
    # fit to the portrait aspect by scaling then centre-cropping
    vf.append(f"scale='if(gt(a,{W}/{H}),-2,{W*8})':'if(gt(a,{W}/{H}),{H*8},-2)'")
    vf.append(f"crop={W*8}:{H*8}")
    cmd = ["ffmpeg", "-v", "error", "-y", "-ss", str(start), "-t", str(duration), "-i", video,
           "-vf", ",".join(vf), "-pix_fmt", "gray", os.path.join(tmp, "f%04d.png")]
    subprocess.run(cmd, check=True)
    return sorted(os.path.join(tmp, f) for f in os.listdir(tmp) if f.endswith(".png"))


def to_gray(path, contrast, gamma):
    im = Image.open(path).convert("L").resize((W, H), Image.LANCZOS)
    px = im.load()
    out = [[0.0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            v = px[x, y] / 255.0
            v = (v - 0.5) * contrast + 0.5
            v = min(1.0, max(0.0, v)) ** gamma
            out[y][x] = v
    return out


def to_bits(gray, mode, thresh):
    bits = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            v = gray[y][x]
            if mode == "bayer":
                t = (BAYER4[y % 4][x % 4] + 0.5) / 16.0
            else:
                t = thresh
            bits[y][x] = 1 if v < t else 0  # 1 = ink
    return bits


def rotate(bits):
    """Portrait (x,y) -> display image (bx = 139 - y, by = x), 140 wide, 68 tall."""
    img = [[0] * 140 for _ in range(68)]
    for y in range(H):
        for x in range(W):
            img[x][139 - y] = bits[y][x]
    return img


def pack(img):
    flat = [p for row in img for p in row]
    out = bytearray()
    i = 0
    while i < len(flat):
        v = flat[i]
        n = 1
        while i + n < len(flat) and flat[i + n] == v and n < 127:
            n += 1
        out.append((v << 7) | n)
        i += n
    return bytes(out)


def raw_bytes(img):
    out = bytearray()
    for row in img:
        for b in range(0, 140, 8):
            byte = 0
            for k in range(8):
                if b + k < 140 and row[b + k]:
                    byte |= 0x80 >> k
            out.append(byte)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video")
    ap.add_argument("name")
    ap.add_argument("--start", type=float, default=0)
    ap.add_argument("--duration", type=float, default=8)
    ap.add_argument("--fps", type=int, default=12)
    ap.add_argument("--crop", help="x,y,w,h in source pixels before fitting")
    ap.add_argument("--mode", choices=["thresh", "bayer"], default="thresh")
    ap.add_argument("--thresh", type=float, default=0.5)
    ap.add_argument("--contrast", type=float, default=1.5)
    ap.add_argument("--gamma", type=float, default=1.0)
    ap.add_argument("--smooth", type=float, default=0.0,
                    help="blend with the previous frame's grey (0 = off, 0.5 = half) to calm flicker")
    ap.add_argument("--invert", action="store_true")
    ap.add_argument("--out", default=".")
    a = ap.parse_args()
    crop = tuple(int(v) for v in a.crop.split(",")) if a.crop else None

    with tempfile.TemporaryDirectory() as tmp:
        files = extract(a.video, a.start, a.duration, a.fps, crop, tmp)
        frames = []
        prev_gray = None
        for f in files:
            g = to_gray(f, a.contrast, a.gamma)
            if prev_gray and a.smooth > 0:
                g = [[g[y][x] * (1 - a.smooth) + prev_gray[y][x] * a.smooth for x in range(W)]
                     for y in range(H)]
            prev_gray = g
            bits = to_bits(g, a.mode, a.thresh)
            if a.invert:
                bits = [[1 - p for p in row] for row in bits]
            frames.append(bits)

    imgs = [rotate(b) for b in frames]
    packed = []
    prev = None
    for img in imgs:
        delta = img if prev is None else [[p ^ q for p, q in zip(r1, r2)] for r1, r2 in zip(img, prev)]
        packed.append(pack(delta))
        prev = img
    raw_total = len(imgs) * 1190
    packed_total = sum(len(p) for p in packed)

    os.makedirs(a.out, exist_ok=True)
    meta = {
        "name": a.name, "fps": a.fps, "w": W, "h": H, "frames": len(imgs),
        "raw_bytes": raw_total, "packed_bytes": packed_total,
        "source": os.path.basename(a.video), "start": a.start, "duration": a.duration,
        "mode": a.mode, "contrast": a.contrast, "smooth": a.smooth,
        # portrait bitmaps, row-major, 1 = ink, for the mock
        "bits": [base64.b64encode(bytes(
            sum((row[x + k] << (7 - k)) for k in range(8) if x + k < W) for row in fr for x in range(0, W, 8)
        )).decode() for fr in frames],
    }
    with open(os.path.join(a.out, a.name + ".json"), "w") as fh:
        json.dump(meta, fh)

    with open(os.path.join(a.out, "clip_" + a.name + ".c"), "w") as fh:
        fh.write(f"/* Generated by vid2clip.py from {meta['source']} at {a.start}s, {a.fps} fps.\n"
                 f" * {len(imgs)} frames, {packed_total} bytes packed ({raw_total} raw). */\n")
        fh.write("#include \"clip.h\"\n\n")
        fh.write(f"static const uint8_t {a.name}_data[] = {{\n")
        for p in packed:
            fh.write("    " + ", ".join(f"0x{b:02x}" for b in p) + ",\n")
        fh.write("};\n\n")
        fh.write(f"static const uint32_t {a.name}_offsets[] = {{\n    ")
        off = 0
        offs = []
        for p in packed:
            offs.append(off)
            off += len(p)
        offs.append(off)
        fh.write(", ".join(str(o) for o in offs) + "\n};\n\n")
        fh.write(f"const struct clip {a.name} = {{\n"
                 f"    .frames = {len(imgs)},\n    .fps = {a.fps},\n"
                 f"    .data = {a.name}_data,\n    .offsets = {a.name}_offsets,\n}};\n")

    print(f"{a.name}: {len(imgs)} frames @ {a.fps} fps, packed {packed_total} B "
          f"({packed_total / len(imgs):.0f} B/frame, raw {raw_total} B)")


if __name__ == "__main__":
    main()
