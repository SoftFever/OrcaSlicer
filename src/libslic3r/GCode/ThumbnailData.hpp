#ifndef slic3r_ThumbnailData_hpp_
#define slic3r_ThumbnailData_hpp_

#if ENABLE_THUMBNAIL_GENERATOR

#include <vector>
#include "libslic3r/Point.hpp"

namespace Slic3r {

struct ThumbnailData
{
    unsigned int width;
    unsigned int height;
    std::vector<unsigned char> pixels;

    ThumbnailData() { reset(); }
    void set(unsigned int w, unsigned int h);
    void reset();

    bool is_valid() const;
};

typedef std::vector<ThumbnailData> ThumbnailsList;
typedef std::function<void(ThumbnailsList & thumbnails, const Vec2ds & sizes, bool printable_only, bool parts_only, bool show_bed, bool transparent_background)> ThumbnailsGeneratorCallback;

} // namespace Slic3r

#endif // ENABLE_THUMBNAIL_GENERATOR

#endif // slic3r_ThumbnailData_hpp_