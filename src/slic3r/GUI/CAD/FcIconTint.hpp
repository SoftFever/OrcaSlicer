#pragma once

#include <cstddef>

namespace Slic3r { namespace GUI {

// FreeCAD's LGPL icons (resources/images/fc_<verb id>.svg) are coloured, drawn for a light ground. Orca's own icons are one
// grey that follows the theme, so a rasterised FreeCAD icon (straight, non-premultiplied RGBA) is redrawn in that one colour:
// the dark strokes of the original stay fully opaque, mid-tone fills become a light tint of the same colour and highlights
// nearly vanish, which keeps the drawing readable without its hues. Used by the ImGui bars and by BitmapCache, so both draw
// the same icon in both themes.
inline void monochrome_fc_rgba(unsigned char* px, size_t n_pixels, bool dark)
{
    const unsigned char fr = dark ? 0xd0 : 0x7c, fg = dark ? 0xd0 : 0x82, fb = dark ? 0xd0 : 0x82;   // Orca's icon grey per theme
    for (size_t i = 0; i < n_pixels; ++i, px += 4) {
        const float a = px[3] / 255.f;
        if (a <= 0.f) continue;
        const float lum = (0.299f * px[0] + 0.587f * px[1] + 0.114f * px[2]) / 255.f;
        float k;
        if      (lum < 0.30f) k = 1.0f;
        else if (lum < 0.75f) k = 1.0f - 0.68f * (lum - 0.30f) / 0.45f;   // 1.00 -> 0.32
        else                  k = 0.32f - 0.24f * (lum - 0.75f) / 0.25f;  // 0.32 -> 0.08
        px[0] = fr; px[1] = fg; px[2] = fb;
        px[3] = static_cast<unsigned char>(a * k * 255.f + 0.5f);
    }
}

}} // namespace Slic3r::GUI
