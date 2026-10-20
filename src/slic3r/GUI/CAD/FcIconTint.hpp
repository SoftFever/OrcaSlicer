#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>

namespace Slic3r { namespace GUI {

// FreeCAD's LGPL icons (resources/images/fc_<verb id>.svg) draw on a light ground: their near-neutral dark strokes (and, on a
// light UI, near-white ones) are re-tinted when the SVG is loaded, so an outline never disappears into the toolbar. Shared by
// the ImGui ribbon and the wx toolbox in the top row (through BitmapCache), so both draw the same icon.
inline bool fc_hex_rgb(const std::string& s, size_t i, int& r, int& g, int& b)
{
    if (i + 7 > s.size() || s[i] != '#') return false;
    for (size_t k = 1; k <= 6; ++k) if (!std::isxdigit(static_cast<unsigned char>(s[i + k]))) return false;
    if (i + 7 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 7]))) return false;   // longer token
    r = std::stoi(s.substr(i + 1, 2), nullptr, 16); g = std::stoi(s.substr(i + 3, 2), nullptr, 16); b = std::stoi(s.substr(i + 5, 2), nullptr, 16);
    return true;
}

inline void retint_fc_neutrals(std::string& s, bool dark)
{
    for (size_t i = 0; i < s.size(); ++i) {
        int r, g, b;
        if (s[i] != '#' || !fc_hex_rgb(s, i, r, g, b)) continue;
        const int mx = std::max({r, g, b}), mn = std::min({r, g, b});
        const bool  neutral = (mx - mn) <= 40;                      // grey-ish, not a hue
        const float lum     = (0.299f * r + 0.587f * g + 0.114f * b) / 255.f;
        const char* to      = nullptr;
        if (dark && neutral && lum < 0.30f)       to = "#d3d7cf";   // Tango aluminium 2
        else if (!dark && neutral && lum > 0.93f) to = "#555753";   // Tango aluminium 5
        if (to) s.replace(i, 7, to);
    }
}

}} // namespace Slic3r::GUI
