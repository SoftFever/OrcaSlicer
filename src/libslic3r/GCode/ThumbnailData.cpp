#include <PrintConfig.hpp>
#include "ThumbnailData.hpp"

namespace Slic3r {

void ThumbnailData::set(unsigned int w, unsigned int h)
{
    if ((w == 0) || (h == 0))
        return;

    if ((width != w) || (height != h))
    {
        width = w;
        height = h;
        // defaults to white texture
        pixels.clear();
        pixels = std::vector<unsigned char>(width * height * 4, 255);
    }
}

void ThumbnailData::reset()
{
    width = 0;
    height = 0;
    pixels.clear();
}

bool ThumbnailData::is_valid() const
{
    return (width != 0) && (height != 0) && ((unsigned int)pixels.size() == 4 * width * height);
}

float ThumbnailsParams::get_thumbnail_zoom_modifier(const DynamicPrintConfig &config) {
    if (config.has("thumbnails_zoom_modifier"))
        return (float)config.option<ConfigOptionPercent>("thumbnails_zoom_modifier")->getFloat()/100;
    else
        return 1.f;
}
} // namespace Slic3r
