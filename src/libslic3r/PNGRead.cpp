#include "PNGRead.hpp"

#include <memory>

#include <cstdio>
#include <png.h>

namespace Slic3r { namespace png {

struct png_deleter { void operator()(png_struct *p) {
    png_destroy_read_struct( &p, nullptr, nullptr); }
};

using png_ptr_t = std::unique_ptr<png_struct_def, png_deleter>;

bool is_png(const ReadBuf &rb)
{
    static const constexpr int PNG_SIG_BYTES = 8;

    return rb.sz >= PNG_SIG_BYTES &&
           !png_sig_cmp(static_cast<png_const_bytep>(rb.buf), 0, PNG_SIG_BYTES);
}

bool decode_png(const ReadBuf &rb, ImageGreyscale &img)
{
    if (!is_png(rb)) return false;

    png_ptr_t png{png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr)};

    if(!png) return false;

    png_infop info = png_create_info_struct(png.get());
    if(!info) return {};

    FILE *io = ::fmemopen(const_cast<void *>(rb.buf), rb.sz, "rb");
    png_init_io(png.get(), io);

    png_read_info(png.get(), info);

    img.cols = png_get_image_width(png.get(), info);
    img.rows = png_get_image_height(png.get(), info);
    size_t color_type = png_get_color_type(png.get(), info);
    size_t bit_depth  = png_get_bit_depth(png.get(), info);

    if (color_type != PNG_COLOR_TYPE_GRAY || bit_depth != 8)
        return false;

    img.buf.resize(img.rows * img.cols);

    auto readbuf = static_cast<png_bytep>(img.buf.data());
    for (size_t r = 0; r < img.rows; ++r)
        png_read_row(png.get(), readbuf + r * img.cols, nullptr);

    fclose(io);

    return true;
}

}}
