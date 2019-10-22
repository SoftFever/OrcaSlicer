#include "libslic3r/libslic3r.h"
#include "ThumbnailData.hpp"

#if ENABLE_THUMBNAIL_GENERATOR

namespace Slic3r {

void ThumbnailData::set(unsigned int w, unsigned int h)
{
    if (!pixels.empty())
        reset();

    if ((w == 0) || (h == 0))
        return;

    width = w;
    height = h;
    // defaults to white texture
    pixels = std::vector<unsigned char>(width * height * 4, 255);
}

void ThumbnailData::reset()
{
    width = 0;
    height = 0;
    pixels.clear();
}

} // namespace Slic3r

#endif // ENABLE_THUMBNAIL_GENERATOR