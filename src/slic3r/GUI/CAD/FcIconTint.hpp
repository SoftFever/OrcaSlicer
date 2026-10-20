#pragma once

#include <cstddef>

namespace Slic3r { namespace GUI {

// Icons that come in colour (FreeCAD's LGPL set, drawn for a light ground, and Orca's own green-and-white import glyphs) are
// decolorized to grey so a toolbar reads as one family: the hue goes, the shading stays. A rasterised icon (straight,
// non-premultiplied RGBA) keeps its alpha and its tonal structure: on a dark ground the original's near-black outlines become
// the light, the fills stay darker and the highlights stay bright but never brighter than an outline; on a light ground the
// dark outlines stay dark and the whites stay white. Used by the ImGui bars and by BitmapCache for fc_* icons.
inline void decolorize_rgba(unsigned char* px, size_t n_pixels, bool dark)
{
    for (size_t i = 0; i < n_pixels; ++i, px += 4) {
        if (px[3] == 0) continue;
        const float lum = (0.299f * px[0] + 0.587f * px[1] + 0.114f * px[2]) / 255.f;
        float g;
        if (dark) {
            if      (lum < 0.18f) g = 0.88f;                                         // outline
            else if (lum < 0.34f) g = 0.88f + (0.46f - 0.88f) * (lum - 0.18f) / 0.16f;
            else if (lum < 0.75f) g = 0.46f + (0.64f - 0.46f) * (lum - 0.34f) / 0.41f;   // fill
            else                  g = 0.64f + (0.78f - 0.64f) * (lum - 0.75f) / 0.25f;   // highlight
        } else {
            g = 0.22f + 0.70f * lum;
        }
        const unsigned char v = static_cast<unsigned char>(g * 255.f + 0.5f);
        px[0] = px[1] = px[2] = v;
    }
}

}} // namespace Slic3r::GUI
