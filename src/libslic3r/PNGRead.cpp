#include "PNGRead.hpp"

#include <memory>

#include <cstdio>
#include <png.h>

namespace Slic3r { namespace png {

struct PNGDescr {
    png_struct *png = nullptr; png_info *info = nullptr;

    PNGDescr() = default;
    PNGDescr(const PNGDescr&) = delete;
    PNGDescr(PNGDescr&&) = delete;
    PNGDescr& operator=(const PNGDescr&) = delete;
    PNGDescr& operator=(PNGDescr&&) = delete;

    ~PNGDescr()
    {
        if (png && info) png_destroy_info_struct(png, &info);
        if (png) png_destroy_read_struct( &png, nullptr, nullptr);
    }
};

bool is_png(const ReadBuf &rb)
{
    static const constexpr int PNG_SIG_BYTES = 8;

    return rb.sz >= PNG_SIG_BYTES &&
           !png_sig_cmp(static_cast<png_const_bytep>(rb.buf), 0, PNG_SIG_BYTES);
}

bool decode_png(const ReadBuf &rb, ImageGreyscale &img)
{
    if (!is_png(rb)) return false;

    PNGDescr dsc;
    dsc.png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);

    if(!dsc.png) return false;

    dsc.info = png_create_info_struct(dsc.png);
    if(!dsc.info) return {};

    FILE *io = ::fmemopen(const_cast<void *>(rb.buf), rb.sz, "rb");
    png_init_io(dsc.png, io);

    png_read_info(dsc.png, dsc.info);

    img.cols = png_get_image_width(dsc.png, dsc.info);
    img.rows = png_get_image_height(dsc.png, dsc.info);
    size_t color_type = png_get_color_type(dsc.png, dsc.info);
    size_t bit_depth  = png_get_bit_depth(dsc.png, dsc.info);

    if (color_type != PNG_COLOR_TYPE_GRAY || bit_depth != 8)
        return false;

    img.buf.resize(img.rows * img.cols);

    auto readbuf = static_cast<png_bytep>(img.buf.data());
    for (size_t r = 0; r < img.rows; ++r)
        png_read_row(dsc.png, readbuf + r * img.cols, nullptr);

    fclose(io);

    return true;
}

}}
