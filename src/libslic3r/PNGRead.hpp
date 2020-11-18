#ifndef PNGREAD_HPP
#define PNGREAD_HPP

#include <vector>
#include <string>
#include <istream>

namespace Slic3r { namespace png {

// Interface for an input stream of encoded png image data.
struct IStream {
    virtual ~IStream() = default;
    virtual size_t read(std::uint8_t *outp, size_t amount) = 0;
    virtual bool is_ok() const = 0;
};

// The output format of decode_png: a 2D pixel matrix stored continuously row
// after row (row major layout).
template<class PxT> struct Image {
    std::vector<PxT> buf;
    size_t rows, cols;
    PxT get(size_t row, size_t col) const { return buf[row * cols + col]; }
};

using ImageGreyscale = Image<uint8_t>;

// Only decodes true 8 bit grayscale png images. Returns false for other formats
// TODO (if needed): implement transformation of rgb images into grayscale...
bool decode_png(IStream &stream, ImageGreyscale &out_img);

// TODO (if needed)
// struct RGB { uint8_t r, g, b; };
// using ImageRGB = Image<RGB>;
// bool decode_png(IStream &stream, ImageRGB &img);


// Encoded png data buffer: a simple read-only buffer and its size.
struct ReadBuf { const void *buf = nullptr; const size_t sz = 0; };

bool is_png(const ReadBuf &pngbuf);

template<class Img> bool decode_png(const ReadBuf &in_buf, Img &out_img)
{
    struct ReadBufStream: public IStream {
        const ReadBuf &rbuf_ref; size_t pos = 0;

        explicit ReadBufStream(const ReadBuf &buf): rbuf_ref{buf} {}

        size_t read(std::uint8_t *outp, size_t amount) override
        {
            if (amount > rbuf_ref.sz - pos) return 0;

            auto buf = static_cast<const std::uint8_t *>(rbuf_ref.buf);
            std::copy(buf + pos, buf + (pos + amount), outp);
            pos += amount;

            return amount;
        }

        bool is_ok() const override { return pos < rbuf_ref.sz; }
    } stream{in_buf};

    return decode_png(stream, out_img);
}

// TODO: std::istream of FILE* could be similarly adapted in case its needed...

}}     // namespace Slic3r::png

#endif // PNGREAD_HPP
