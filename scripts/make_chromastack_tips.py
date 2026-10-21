#!/usr/bin/env python3
"""Draw the ChromaStack tooltip illustrations.

The panel's other illustrations are renders of a model. These cannot be: what they show is what a
stack of translucent filament reaches, which is a property of the colour model rather than of any
particular print. So they are computed from that model - the same Beer-Lambert absorption
chroma_palette() uses, with the same constants - and this script is kept so they can be drawn again
if those constants ever change. A picture that disagrees with the code is worse than no picture.

    python3 scripts/make_chromastack_tips.py

Writes into resources/images/texture_displacement/, matching the house format: 256x256 PNGs and
192x192 GIFs of 16 frames at 100 ms, on the same near-black ground.
"""

import math
import os
from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "resources", "images", "texture_displacement")

BG = (7, 7, 7)
DIM = (120, 124, 128)
PNG = 256
GIF = 192
FRAMES = 16

# CHROMA_TD_OPAQUE_FRACTION / OPAQUE_EFOLDS, from libslic3r/ChromaStack.{hpp,cpp}.
TD_OPAQUE_FRACTION = 0.1
OPAQUE_EFOLDS = 3.0


def s2l(c):
    c /= 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def l2s(v):
    v = max(0.0, min(1.0, v))
    return round(255 * (v * 12.92 if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055))


def transmittance(td, layer_height):
    return math.exp(-layer_height / (TD_OPAQUE_FRACTION * td / OPAQUE_EFOLDS))


def palette(bands, layer_height):
    """The colour a column of k layers shows, for every k. bands: [(rgb, td, layers), ...] bottom-up."""
    per = []
    for rgb, td, n in bands:
        per += [(rgb, td)] * n
    cur = [s2l(c) for c in per[0][0]]
    out = [tuple(l2s(v) for v in cur)]
    for rgb, td in per[1:]:
        t = transmittance(td, layer_height)
        lin = [s2l(c) for c in rgb]
        cur = [(1 - t) * lin[i] + t * cur[i] for i in range(3)]
        out.append(tuple(l2s(v) for v in cur))
    return out


def nearest(color, choices):
    return min(choices, key=lambda c: sum((c[i] - color[i]) ** 2 for i in range(3)))


def ramp_strip(draw, colors, x0, y0, w, h):
    for i, c in enumerate(colors):
        draw.rectangle([x0 + w * i // len(colors), y0, x0 + w * (i + 1) // len(colors) - 1, y0 + h], fill=c)


def test_image(w, h):
    """A smooth two-way ramp: warm to cool across, light to dark down. A stack has to reach all of it."""
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            u, v = x / (w - 1), y / (h - 1)
            px[x, y] = (int(255 * (0.95 - 0.55 * v) * (0.35 + 0.65 * (1 - u))),
                        int(255 * (0.85 - 0.6 * v) * (0.45 + 0.3 * u)),
                        int(255 * (0.8 - 0.65 * v) * (0.25 + 0.75 * u)))
    return img


def colors_png(name, bands, caption_bands):
    img = Image.new("RGB", (PNG, PNG), BG)
    draw = ImageDraw.Draw(img)
    pal = palette(bands, 0.08)
    reach = pal  # entry i is a column of i + 1 layers, so every one of them is reachable

    # What the picture becomes when every pixel takes the nearest colour the stack reaches.
    src = test_image(176, 132)
    out = Image.new("RGB", src.size)
    sp, op = src.load(), out.load()
    for y in range(src.size[1]):
        for x in range(src.size[0]):
            op[x, y] = nearest(sp[x, y], reach)
    img.paste(out, (40, 42))
    draw.rectangle([39, 41, 40 + 176, 42 + 132], outline=(48, 50, 54))

    ramp_strip(draw, reach, 40, 196, 176, 22)
    for i, (rgb, _td, _n) in enumerate(bands):
        x = 40 + i * 20
        draw.rectangle([x, 232, x + 14, 246], fill=rgb, outline=(60, 62, 66))
    draw.text((40 + len(bands) * 20 + 8, 234), caption_bands, fill=DIM)
    img.save(os.path.join(OUT, name))


def dither_png(name, on):
    img = Image.new("RGB", (PNG, PNG), BG)
    src = test_image(176, 176).convert("L").convert("RGB")
    levels = [(v, v, v) for v in (18, 70, 125, 180, 240)]
    out = Image.new("RGB", src.size)
    sp, op = src.load(), out.load()
    err = [[0.0] * src.size[0] for _ in range(src.size[1] + 1)]
    for y in range(src.size[1]):
        for x in range(src.size[0]):
            want = sp[x, y][0] + (err[y][x] if on else 0.0)
            got = min(levels, key=lambda c: abs(c[0] - want))
            op[x, y] = got
            if on:
                e = want - got[0]
                if x + 1 < src.size[0]:
                    err[y][x + 1] += e * 7 / 16
                if y + 1 <= src.size[1]:
                    if x:
                        err[y + 1][x - 1] += e * 3 / 16
                    err[y + 1][x] += e * 5 / 16
                    if x + 1 < src.size[0]:
                        err[y + 1][x + 1] += e * 1 / 16
    img.paste(out, (40, 40))
    ImageDraw.Draw(img).rectangle([39, 39, 40 + 176, 40 + 176], outline=(48, 50, 54))
    img.save(os.path.join(OUT, name))


def trim_png(name):
    img = Image.new("RGB", (PNG, PNG), BG)
    draw = ImageDraw.Draw(img)
    # A pale band twelve layers long over a dark one. Light over dark is the case trimming is for: it
    # saturates within about five layers and every layer after that shows what the one below showed.
    # (Dark over light goes on paying for far longer, which is why Trim measures the palette rather
    # than the filament's TD - see chroma_trim_bands().)
    bands = [((20, 20, 24), 4.0, 1), ((236, 236, 230), 4.0, 12)]
    pal = palette(bands, 0.08)[1:]
    y = 28
    for i, c in enumerate(pal):
        draw.rectangle([56, y, 200, y + 14], fill=c)
        same = i > 0 and max(abs(c[k] - pal[i - 1][k]) for k in range(3)) <= 2
        draw.text((24, y + 1), str(i + 1), fill=DIM)
        if same:
            draw.line([210, y + 7, 232, y + 7], fill=(150, 110, 50), width=2)
        y += 17
    img.save(os.path.join(OUT, name))


def save_gif(name, frames):
    frames = [f.convert("P", palette=Image.ADAPTIVE, colors=32) for f in frames]
    frames[0].save(os.path.join(OUT, name), save_all=True, append_images=frames[1:], duration=100, loop=0,
                   optimize=True)


def td_gif(name):
    """The same two filaments as their transmission distance rises: steps become a fade."""
    frames = []
    for f in range(FRAMES):
        td = 1.0 + 7.0 * (f / (FRAMES - 1))
        img = Image.new("RGB", (GIF, GIF), BG)
        draw = ImageDraw.Draw(img)
        pal = palette([((236, 236, 230), 1.0, 1), ((20, 20, 24), td, 14)], 0.08)[1:]  # above the base
        y = 24
        for c in pal:
            draw.rectangle([44, y, 148, y + 8], fill=c)
            y += 10
        draw.text((44, 170), "TD %.1f" % td, fill=DIM)
        frames.append(img)
    save_gif(name, frames)


def layers_gif(name):
    """A column built up one layer at a time, and the colour it shows from above as it grows."""
    # Chosen to stay visible against the ground: a band the colour of the background reads as a column
    # that has stopped growing rather than as a layer of dark filament.
    bands = [((232, 232, 226), 6.0, 5), ((240, 170, 40), 5.0, 5), ((48, 96, 190), 4.0, 5)]
    pal = palette(bands, 0.08)
    per = []
    for rgb, _td, n in bands:
        per += [rgb] * n
    frames = []
    for f in range(FRAMES):
        k = min(len(per), 1 + int(f * len(per) / (FRAMES - 1)))
        img = Image.new("RGB", (GIF, GIF), BG)
        draw = ImageDraw.Draw(img)
        # The column's full extent, so a growing stack reads as growing rather than as moving.
        draw.rectangle([35, 150 - len(per) * 9 + 8, 109, 158], outline=(40, 42, 46))
        for i in range(k):
            y = 150 - i * 9
            draw.rectangle([36, y, 108, y + 7], fill=per[i])
        # What the top of that column shows: the whole of it, seen from above.
        draw.rectangle([128, 44, 164, 80], fill=pal[k - 1])
        draw.rectangle([127, 43, 165, 81], outline=(60, 62, 66))
        draw.text([120, 28], "from above", fill=DIM)
        draw.text([36, 162], "%d layers" % k, fill=DIM)
        frames.append(img)
    save_gif(name, frames)


def main():
    two = [((236, 236, 230), 5.0, 12), ((20, 20, 24), 4.0, 12)]
    six = [((236, 236, 230), 5.0, 4), ((247, 199, 60), 5.0, 4), ((224, 110, 44), 4.5, 4),
           ((190, 48, 44), 4.0, 4), ((40, 90, 160), 4.0, 4), ((20, 20, 24), 3.0, 4)]
    colors_png("chroma_colors_few.png", two, "2 filaments")
    colors_png("chroma_colors_many.png", six, "6 filaments")
    dither_png("chroma_dither_off.png", False)
    dither_png("chroma_dither_on.png", True)
    trim_png("chroma_trim.png")
    td_gif("chroma_td.gif")
    layers_gif("chroma_layers.gif")
    print("wrote 7 illustrations into", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
