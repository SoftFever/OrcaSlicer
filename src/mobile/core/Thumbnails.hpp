#pragma once

#include "libslic3r/GCode/ThumbnailData.hpp"

// Software renderer for the thumbnails embedded in a mobile-exported G-code.
// The desktop uses the GL canvas; on iOS the Metal viewport is not reachable
// from the slicer thread, so this is a small CPU rasterizer that projects
// every visible ModelInstance triangle top-down onto the plate. It matches
// the desktop's default look closely enough for Moonraker / Mainsail /
// OctoPrint / Fluidd previews.

namespace Slic3r {
class Model;
} // namespace Slic3r

namespace Slic3r { namespace Mobile {

struct BedInfo;

// Render every printable object instance in `model` top-down onto a
// `width` x `height` RGBA image. `bed` supplies the printable-area bounds
// so the framing matches what the app draws on screen. Empty pixels are
// transparent so Klipper's GCodeMetadata scraper detects them cleanly.
Slic3r::ThumbnailData render_plate_thumbnail(const Slic3r::Model& model,
                                             const BedInfo&       bed,
                                             unsigned int         width,
                                             unsigned int         height);

}} // namespace Slic3r::Mobile
