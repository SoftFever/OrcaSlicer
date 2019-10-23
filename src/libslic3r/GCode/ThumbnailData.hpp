#ifndef slic3r_ThumbnailData_hpp_
#define slic3r_ThumbnailData_hpp_

#if ENABLE_THUMBNAIL_GENERATOR

#include <vector>

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

} // namespace Slic3r

#endif // ENABLE_THUMBNAIL_GENERATOR

#endif // slic3r_ThumbnailData_hpp_