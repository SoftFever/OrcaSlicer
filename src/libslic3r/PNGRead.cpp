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

#if PNG_LIBPNG_VER_MINOR <= 2
    // Earlier libpng versions had png_sig_cmp(png_bytep, ...) which is not
    // a const pointer. It is not possible to cast away the const qualifier from
    // the input buffer so... yes... life is challenging...
    png_byte buf[PNG_SIG_BYTES];
    auto inbuf = static_cast<const std::uint8_t *>(rb.buf);
    std::copy(inbuf, inbuf + PNG_SIG_BYTES, buf);
#else
    auto buf = static_cast<png_const_bytep>(rb.buf);
#endif

    return rb.sz >= PNG_SIG_BYTES && !png_sig_cmp(buf, 0, PNG_SIG_BYTES);
}

// Buffer read callback for libpng. It provides an allocated output buffer and
// the amount of data it desires to read from the input.
void png_read_callback(png_struct *png_ptr,
                       png_bytep   outBytes,
                       png_size_t  byteCountToRead)
{
    // Retrieve our input buffer through the png_ptr
    auto reader = static_cast<IStream *>(png_get_io_ptr(png_ptr));

    if (!reader || !reader->is_ok()) return;

    reader->read(static_cast<std::uint8_t *>(outBytes), byteCountToRead);
}

bool decode_png(IStream &in_buf, ImageGreyscale &out_img)
{
    static const constexpr int PNG_SIG_BYTES = 8;

    std::vector<png_byte> sig(PNG_SIG_BYTES, 0);
    in_buf.read(sig.data(), PNG_SIG_BYTES);
    if (!png_check_sig(sig.data(), PNG_SIG_BYTES))
        return false;

    PNGDescr dsc;
    dsc.png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr,
                                     nullptr);

    if(!dsc.png) return false;

    dsc.info = png_create_info_struct(dsc.png);
    if(!dsc.info) return false;

    png_set_read_fn(dsc.png, static_cast<void *>(&in_buf), png_read_callback);

    // Tell that we have already read the first bytes to check the signature
    png_set_sig_bytes(dsc.png, PNG_SIG_BYTES);

    png_read_info(dsc.png, dsc.info);

    out_img.cols = png_get_image_width(dsc.png, dsc.info);
    out_img.rows = png_get_image_height(dsc.png, dsc.info);
    size_t color_type = png_get_color_type(dsc.png, dsc.info);
    size_t bit_depth  = png_get_bit_depth(dsc.png, dsc.info);

    if (color_type != PNG_COLOR_TYPE_GRAY || bit_depth != 8)
        return false;

    out_img.buf.resize(out_img.rows * out_img.cols);

    auto readbuf = static_cast<png_bytep>(out_img.buf.data());
    for (size_t r = 0; r < out_img.rows; ++r)
        png_read_row(dsc.png, readbuf + r * out_img.cols, nullptr);

    return true;
}

}} // namespace Slic3r::png
