#!/usr/bin/env python3
"""Draws the illustrations the texture-displacement tooltips show.

The panel's tooltips can carry a picture under their text (see GLGizmoTextureDisplacement's
panel_tooltip()). A good many of those settings - the ones that are a shape rather than a photograph -
are better drawn from the same arithmetic the feature uses than captured from the screen, which is
what this script does: it shades a height field exactly as a relief would catch the light, so
"what Rotation turns" and "what Tile repeats" are visible rather than described.

Settings that involve the model, the mesh or colour are not here: those want a capture of the real
tool. See docs/HLSD/tooltip-illustrations.md.

Output goes to resources/images/texture_displacement/. Run it after changing one of the patterns
below; nothing in the build depends on it.

    python3 scripts/gen_texture_displacement_tips.py

Needs numpy and pillow.
"""

import os

import numpy as np
from PIL import Image

SIZE = 256  # px, square - the panel draws these at whatever width the tooltip has
GIF_SIZE = 192  # animations are the same picture, smaller: every frame of one ships in resources/
RELIEF = 30.0  # how hard the shading reacts to a slope; purely cosmetic

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "resources", "images", "texture_displacement")

# Neutral enough to sit in a tooltip in either theme: a dark ground, a mid grey for the flat surface
# and a cool near-white for the lit slopes.
SHADOW = np.array([0.13, 0.14, 0.15])
MIDTONE = np.array([0.40, 0.42, 0.43])
HIGHLIGHT = np.array([0.78, 0.84, 0.83])
LIGHT = np.array([-0.45, -0.62, 0.65])
LIGHT /= np.linalg.norm(LIGHT)


def grid(size=SIZE):
    """Unit coordinates, -0.5 .. 0.5, y down as in an image."""
    a = (np.arange(size) + 0.5) / size - 0.5
    return np.meshgrid(a, a)


def shade(height):
    """A height field in 0..1, lit as a relief: the picture the tooltip shows."""
    # The slope has to mean the same thing at either size, so the gradient is taken per unit of the
    # picture rather than per pixel - otherwise the smaller animations would come out twice as flat.
    gy, gx = np.gradient(height * RELIEF * (height.shape[0] / float(SIZE)))
    normal = np.dstack([-gx, -gy, np.ones_like(height)])
    normal /= np.linalg.norm(normal, axis=2, keepdims=True)
    lam = np.clip(normal @ LIGHT, 0.0, 1.0)

    # Two ramps rather than one: the dark half separates the slopes, the lit half picks up the
    # accent, and the flat ground lands in the middle of both.
    low = SHADOW + (MIDTONE - SHADOW) * (lam / 0.5)[..., None]
    high = MIDTONE + (HIGHLIGHT - MIDTONE) * ((lam - 0.5) / 0.5)[..., None]
    rgb = np.where((lam < 0.5)[..., None], low, high)

    # A touch of the height itself, so a tall feature still reads as tall where it is flat on top.
    #
    # Deliberately almost grey: these say what a shape does, and a relief catching the light is grey.
    # A coloured one would read as a third thing the setting does, and the amount of colour would move
    # with the slope - a pattern at 45 degrees would flash where the same pattern upright did not.
    rgb = np.clip(rgb * (0.88 + 0.18 * height[..., None]), 0.0, 1.0)
    return (rgb * 255.0 + 0.5).astype(np.uint8)


def vignette(rgb):
    """Rounds the picture off at its border, so it does not end in a hard rectangle."""
    x, y = grid(rgb.shape[0])
    edge = np.clip((0.5 - np.maximum(np.abs(x), np.abs(y))) / 0.045, 0.0, 1.0)
    return (rgb * edge[..., None]).astype(np.uint8)


def save_png(name, rgb):
    path = os.path.join(OUT_DIR, name)
    Image.fromarray(vignette(rgb)).save(path, optimize=True)
    print("wrote", os.path.normpath(path), os.path.getsize(path) // 1024, "KB")


def save_gif(name, frames, delay_ms):
    path = os.path.join(OUT_DIR, name)
    images = [Image.fromarray(vignette(f)).convert("P", palette=Image.ADAPTIVE, colors=32) for f in frames]
    images[0].save(path, save_all=True, append_images=images[1:], duration=delay_ms, loop=0, optimize=True)
    print("wrote", os.path.normpath(path), os.path.getsize(path) // 1024, "KB")


def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def blur(a, radius):
    """Separable box blur, run twice - near enough a gaussian for a picture of a blur."""
    for _ in range(2):
        for axis in (0, 1):
            padded = np.pad(a, [(radius, radius) if i == axis else (0, 0) for i in range(2)], mode="edge")
            cumulative = np.cumsum(padded, axis=axis)
            cumulative = np.concatenate([np.zeros_like(np.take(cumulative, [0], axis=axis)), cumulative], axis=axis)
            n = a.shape[axis]
            hi = np.take(cumulative, np.arange(n) + 2 * radius + 1, axis=axis)
            lo = np.take(cumulative, np.arange(n), axis=axis)
            a = (hi - lo) / (2.0 * radius + 1.0)
    return a


# --- the patterns ------------------------------------------------------------------------------
#
# One motif runs through nearly all of them - a ring inside a square frame. Using the same shape for
# every setting is what lets the pictures be compared: whatever changes between two of them is the
# setting, because nothing else is different.


def _motif_cell(u, v):
    """The motif within one cell, u and v in -0.5 .. 0.5."""
    r = np.hypot(u, v)
    ring = np.clip(1.0 - np.abs(r - 0.26) / 0.085, 0.0, 1.0)
    frame_d = np.maximum(np.abs(u), np.abs(v))
    frame = np.clip(1.0 - np.abs(frame_d - 0.42) / 0.055, 0.0, 1.0)
    return np.clip(np.maximum(ring, frame) ** 0.7, 0.0, 1.0)


def motif(repeats, size=SIZE):
    """The motif laid out `repeats` times across the picture."""
    x, y = grid(size)
    return _motif_cell((x * repeats) % 1.0 - 0.5, (y * repeats) % 1.0 - 0.5)


def decal(motif_scale=0.46):
    """One copy of the motif in the middle of a flat field - what Tile off leaves."""
    x, y = grid()
    u, v = x / motif_scale, y / motif_scale
    inside = (np.maximum(np.abs(u), np.abs(v)) <= 0.5).astype(float)
    return _motif_cell(u, v) * inside


def ridges(angle_deg, period=0.16, size=SIZE):
    """Parallel ridges at an angle: the simplest pattern whose direction is unmistakable."""
    x, y = grid(size)
    a = np.radians(angle_deg)
    t = (x * np.cos(a) + y * np.sin(a)) / period
    return 0.2 + 0.6 * (0.5 + 0.5 * np.sin(2.0 * np.pi * t))


def bosses(repeats, size=SIZE):
    """Rounded domes standing on a flat field - a shape with an unmistakable right way up."""
    x, y = grid(size)
    u = (x * repeats) % 1.0 - 0.5
    v = (y * repeats) % 1.0 - 0.5
    return smoothstep(0.36, 0.08, np.hypot(u, v))


def chevron_tiles(mirrored, repeats=3):
    """Cells of diagonal ridges, laid out repeated or mirrored.

    Repeated, every cell starts the pattern over and the seam between two of them is a step; mirrored,
    each cell is the reflection of its neighbour and the ridges meet across the seam as chevrons. That
    seam is the whole of what the Repeat/Mirror pair does, and a motif symmetric in itself would hide
    it entirely.
    """

    def axis(t):
        if not mirrored:
            return (t * repeats) % 1.0
        s = (t * repeats) % 2.0
        return np.where(s < 1.0, s, 2.0 - s)

    x, y = grid()
    t = (axis(x + 0.5) + axis(y + 0.5)) * 1.5
    return 0.22 + 0.56 * (0.5 + 0.5 * np.cos(2.0 * np.pi * t))


def painted_area(size=SIZE, radius=0.44):
    """A round painted area in the middle of bare surface, with a soft edge so it does not alias."""
    x, y = grid(size)
    return smoothstep(radius, radius - 1.5 / size, np.hypot(x, y))


def painted_patch(fade, size=SIZE):
    """The motif inside a painted area, either stopping at its edge or fading into it."""
    x, y = grid(size)
    r = np.hypot(x, y) / 0.44
    amount = smoothstep(1.0, 0.55, r) if fade else 1.0
    return motif(3, size) * painted_area(size) * amount


def profile_wave(size=SIZE, periods=2.5):
    """A one-dimensional height profile with clear highs, lows and a flat stretch between them."""
    t = (np.arange(size) + 0.5) / size * periods * 2.0 * np.pi
    h = 0.5 + 0.34 * np.cos(t) + 0.16 * np.cos(2.0 * t + 0.9)
    return np.clip((h - h.min()) / (h.max() - h.min()), 0.0, 1.0)


def profile_view(height_1d, midlevel, amplitude=0.26, size=SIZE):
    """A side view of the surface: material below, air above, and the bare surface as a dashed line.

    Shading cannot say how high something is - it reads slopes, and a height field moved up or down as
    a whole has exactly the slopes it had before. Midlevel is entirely about where the texture sits
    relative to the surface it is on, so it is drawn in section, where that is the one thing visible.
    """
    base = 0.52 * size
    surface = base - (height_1d - midlevel) * amplitude * size
    rows = np.arange(size)[:, None]

    covered = np.clip(rows - surface[None, :] + 0.5, 0.0, 1.0)  # 1 where there is material
    air = np.array([0.11, 0.12, 0.13])
    material = np.array([0.44, 0.46, 0.47])
    rgb = air * (1.0 - covered)[..., None] + material * covered[..., None]

    # The lit top face, and the bare surface the texture is measured against.
    edge = np.clip(1.0 - np.abs(rows - surface[None, :]) / 1.6, 0.0, 1.0)
    rgb = rgb * (1.0 - edge[..., None]) + np.array([0.80, 0.86, 0.85]) * edge[..., None]
    dash = ((np.arange(size) // 7) % 2 == 0)[None, :]
    line = (np.abs(rows - base) < 0.9) & dash
    rgb[line] = np.array([0.58, 0.62, 0.62])
    return (np.clip(rgb, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)


def dome(size=SIZE):
    """One broad swell filling the picture - the layer underneath, for the blend pictures."""
    x, y = grid(size)
    return smoothstep(0.52, 0.02, np.hypot(x, y))


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    # Tile: one copy against the pattern filling the painted area.
    save_png("tile_off.png", shade(decal()))
    save_png("tile_on.png", shade(motif(3)))

    # Tile size: how wide one copy is.
    save_png("tile_size_large.png", shade(motif(2)))
    save_png("tile_size_small.png", shade(motif(5)))

    # Repeat against Mirror, where the seam between two copies is the whole point.
    save_png("tile_repeat.png", shade(chevron_tiles(mirrored=False)))
    save_png("tile_mirror.png", shade(chevron_tiles(mirrored=True)))

    # Invert: the negative of the image. Domes rather than the ring motif, which is close enough to
    # its own negative that inverting it would look like nothing had happened.
    save_png("invert_off.png", shade(bosses(3)))
    save_png("invert_on.png", shade(1.0 - bosses(3)))

    # Smoothing: a stepped image against the same one blurred before it is used. Domes again, where
    # the steps show up as contour rings - what a photograph with too few grey levels does.
    stepped = np.floor(bosses(3) * 5.0) / 4.0
    save_png("smoothing_off.png", shade(np.clip(stepped, 0.0, 1.0)))
    save_png("smoothing_on.png", shade(np.clip(blur(stepped, max(2, SIZE // 40)), 0.0, 1.0)))

    # Edge fade: relief that stops at the edge of the painted area against relief that sinks into it.
    save_png("edge_fade_off.png", shade(painted_patch(fade=False)))
    save_png("edge_fade_on.png", shade(painted_patch(fade=True)))

    # Blend: piling relief on top of the layer below against using it as a mask over it. Add keeps
    # both shapes, Multiply keeps the motif only where the layer under it is high.
    base = dome()
    save_png("blend_add.png", shade(np.clip(0.55 * base + 0.42 * motif(3), 0.0, 1.0)))
    save_png("blend_multiply.png", shade(base * motif(3)))

    # Rotation: a half turn brings parallel ridges back onto themselves, so a single sweep loops with
    # no jump and the file stays a fraction of the size a there-and-back one would be.
    steps = 16
    save_gif("rotation.gif", [shade(ridges(180.0 * i / steps, size=GIF_SIZE)) for i in range(steps)], delay_ms=100)

    # Depth and Midlevel are both about where the relief sits relative to the surface it is on, so
    # both are drawn as a painted area in the middle of bare surface, which is that reference. Without
    # it neither would show: shading reads slopes, and a height field that moves up or down as a whole
    # has exactly the same slopes it had before.
    #
    # A cosine over the frame count starts and ends at the same value with no step in between, so a
    # single pass through it loops.
    area = painted_area(GIF_SIZE)
    pattern = motif(3, GIF_SIZE)
    sweep = [0.5 - 0.5 * np.cos(2.0 * np.pi * i / steps) for i in range(steps)]

    # Depth: the relief growing out of the surface and settling back flush with it.
    frames = [shade(np.clip(0.5 + 0.45 * pattern * (0.08 + 0.92 * t) * area, 0.0, 1.0)) for t in sweep]
    save_gif("depth.gif", frames, delay_ms=100)

    # Midlevel: the grey that stays put, sweeping from "the whole texture pushes out" through
    # "mid-grey stays where the surface is" to "the whole of it cuts in". Drawn in section - see
    # profile_view() for why this one is not a relief like the rest.
    wave = profile_wave(GIF_SIZE)
    frames = [profile_view(wave, t, size=GIF_SIZE) for t in sweep]
    save_gif("midlevel.gif", frames, delay_ms=100)


if __name__ == "__main__":
    main()
