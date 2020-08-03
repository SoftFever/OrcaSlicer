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

// A wrapper around ReadBuf to be read repeatedly like a stream. libpng needs
// this form for its buffer read callback.
struct ReadBufReader {
    const ReadBuf &rdbuf; size_t pos;
    ReadBufReader(const ReadBuf &rd): rdbuf{rd}, pos{0} {}
};

// Buffer read callback for libpng. It provides an allocated output buffer and
// the amount of data it desires to read from the input.
void png_read_callback(png_struct *png_ptr,
                       png_bytep   outBytes,
                       png_size_t  byteCountToRead)
{
    // Retrieve our input buffer through the png_ptr
    auto reader = static_cast<ReadBufReader *>(png_get_io_ptr(png_ptr));

    if (!reader || byteCountToRead > reader->rdbuf.sz - reader->pos) return;

    auto   buf = static_cast<const png_byte *>(reader->rdbuf.buf);
    size_t pos = reader->pos;

    std::copy(buf + pos, buf + (pos + byteCountToRead), outBytes);
    reader->pos += byteCountToRead;
}

bool decode_png(const ReadBuf &rb, ImageGreyscale &img)
{
    if (!is_png(rb)) return false;

    PNGDescr dsc;
    dsc.png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr,
                                     nullptr);

    if(!dsc.png) return false;

    dsc.info = png_create_info_struct(dsc.png);
    if(!dsc.info) return {};

    ReadBufReader reader {rb};
    png_set_read_fn(dsc.png, static_cast<void *>(&reader), png_read_callback);

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

    return true;
}

}} // namespace Slic3r::png
