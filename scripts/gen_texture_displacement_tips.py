#!/usr/bin/env python3
"""Draws the illustrations the texture-displacement tooltips show.

The panel's tooltips can carry a picture under their text (see GLGizmoTextureDisplacement's
panel_tooltip()). A few of those settings - the ones that are a shape rather than a photograph -
are better drawn from the same arithmetic the feature uses than captured from the screen, which is
what this script does: it shades a height field exactly as a relief would catch the light, so
"what Rotation turns" and "what Tile repeats" are visible rather than described.

Output goes to resources/images/texture_displacement/. Run it after changing one of the patterns
below; nothing in the build depends on it.

    python3 scripts/gen_texture_displacement_tips.py

Needs numpy and pillow.
"""

import os

import numpy as np
from PIL import Image

SIZE = 256  # px, square - the panel draws these at whatever width the tooltip has
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
    """A height field in 0..1, lit as a relief and tinted: the picture the tooltip shows."""
    gy, gx = np.gradient(height * RELIEF)
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
    print("wrote", os.path.normpath(path))


def save_gif(name, frames, delay_ms):
    path = os.path.join(OUT_DIR, name)
    images = [Image.fromarray(vignette(f)).convert("P", palette=Image.ADAPTIVE, colors=32) for f in frames]
    images[0].save(path, save_all=True, append_images=images[1:], duration=delay_ms, loop=0, optimize=True)
    print("wrote", os.path.normpath(path))


# --- the patterns ------------------------------------------------------------------------------


def ridges(angle_deg, period=0.16):
    """Parallel ridges at an angle: the simplest pattern whose direction is unmistakable."""
    x, y = grid()
    a = np.radians(angle_deg)
    t = (x * np.cos(a) + y * np.sin(a)) / period
    return 0.2 + 0.6 * (0.5 + 0.5 * np.sin(2.0 * np.pi * t))


def motif(repeats):
    """A ring inside a square frame, laid out `repeats` times across the picture."""
    x, y = grid()
    u = (x * repeats) % 1.0 - 0.5
    v = (y * repeats) % 1.0 - 0.5
    r = np.hypot(u, v)

    ring = np.clip(1.0 - np.abs(r - 0.26) / 0.085, 0.0, 1.0)
    frame_d = np.maximum(np.abs(u), np.abs(v))
    frame = np.clip(1.0 - np.abs(frame_d - 0.42) / 0.055, 0.0, 1.0)
    return np.clip(np.maximum(ring, frame) ** 0.7, 0.0, 1.0)


def decal(motif_scale=0.46):
    """One copy of the motif in the middle of a flat field - what Tile off leaves."""
    x, y = grid()
    u, v = x / motif_scale, y / motif_scale
    r = np.hypot(u, v)
    ring = np.clip(1.0 - np.abs(r - 0.26) / 0.085, 0.0, 1.0)
    frame_d = np.maximum(np.abs(u), np.abs(v))
    frame = np.clip(1.0 - np.abs(frame_d - 0.42) / 0.055, 0.0, 1.0)
    inside = (np.maximum(np.abs(u), np.abs(v)) <= 0.5).astype(float)
    return np.clip(np.maximum(ring, frame) ** 0.7, 0.0, 1.0) * inside


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    # Tile: one copy against the pattern filling the painted area.
    save_png("tile_off.png", shade(decal()))
    save_png("tile_on.png", shade(motif(repeats=3)))

    # Rotation: the same ridges turning a quarter of a turn and back, so the loop has no jump.
    # A half turn brings parallel ridges back onto themselves, so a single sweep loops with no jump
    # and the file stays a fraction of the size a there-and-back one would be.
    steps = 16
    save_gif("rotation.gif", [shade(ridges(180.0 * i / steps)) for i in range(steps)], delay_ms=100)


if __name__ == "__main__":
    main()
