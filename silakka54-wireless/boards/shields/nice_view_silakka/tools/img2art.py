"""Fit any image into 68x140 one-bit, several conversion modes, preview sheet."""
import sys
from PIL import Image, ImageOps, ImageEnhance, ImageFilter
W, H = 68, 140
BAYER4 = [[0,8,2,10],[12,4,14,6],[3,11,1,9],[15,7,13,5]]

def load(path, crop=None, bg=(255,255,255)):
    im = Image.open(path).convert('RGBA')
    base = Image.new('RGBA', im.size, bg + (255,))
    base.alpha_composite(im)
    im = base.convert('L')
    if crop: im = im.crop(crop)
    return im

def fit(im, scale=8):
    # pre-scale to 8x target with lanczos, then down to target: smoother edges
    big = ImageOps.fit(im, (W*scale, H*scale), Image.LANCZOS)
    return big

def to_1bit(big, mode, contrast=1.0, thresh=128, gamma=1.0):
    im = big.resize((W, H), Image.LANCZOS)
    if contrast != 1.0: im = ImageEnhance.Contrast(im).enhance(contrast)
    if gamma != 1.0: im = im.point(lambda v: int(255 * ((v/255) ** gamma)))
    if mode == 'thresh':
        return im.point(lambda v: 255 if v > thresh else 0).convert('1')
    if mode == 'floyd':
        return im.convert('1')  # PIL default Floyd-Steinberg
    if mode == 'bayer':
        px = im.load(); out = Image.new('1', (W, H)); op = out.load()
        for y in range(H):
            for x in range(W):
                t = (BAYER4[y%4][x%4] + 0.5) / 16 * 255
                op[x, y] = 1 if px[x, y] > t else 0
        return out
    if mode == 'edges':
        # ink outline from edges + dark fill: comic look
        e = im.filter(ImageFilter.FIND_EDGES)
        e = e.point(lambda v: 0 if v > 40 else 255)
        d = im.point(lambda v: 255 if v > thresh else 0)
        return Image.eval(Image.blend(e.convert('L'), d.convert('L'), 0.5), lambda v: 255 if v > 200 else 0).convert('1')
    raise ValueError(mode)

def sheet(variants, out, zoom=3):
    n = len(variants)
    s = Image.new('L', (n * (W*zoom + 8) + 8, H*zoom + 16), 128)
    for i, (label, im) in enumerate(variants):
        s.paste(im.convert('L').resize((W*zoom, H*zoom), Image.NEAREST), (8 + i*(W*zoom+8), 8))
    s.save(out)

if __name__ == '__main__':
    path, out = sys.argv[1], sys.argv[2]
    crop = tuple(int(v) for v in sys.argv[3].split(',')) if len(sys.argv) > 3 and sys.argv[3] else None
    big = fit(load(path, crop))
    vs = []
    for mode, kw in [('thresh', {}), ('thresh', {'thresh': 150}), ('floyd', {}), ('floyd', {'contrast': 1.6}), ('bayer', {}), ('bayer', {'contrast': 1.6}), ('edges', {})]:
        vs.append((mode, to_1bit(big, mode, **kw)))
    sheet(vs, out)
    print('ok', len(vs))
