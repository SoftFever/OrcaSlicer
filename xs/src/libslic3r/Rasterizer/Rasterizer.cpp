#include "Rasterizer.hpp"
#include <ExPolygon.hpp>

#include <cstdint>

// For rasterizing
#include <agg/agg_basics.h>
#include <agg/agg_rendering_buffer.h>
#include <agg/agg_pixfmt_gray.h>
#include <agg/agg_pixfmt_rgb.h>
#include <agg/agg_renderer_base.h>
#include <agg/agg_renderer_scanline.h>

#include <agg/agg_scanline_p.h>
#include <agg/agg_rasterizer_scanline_aa.h>
#include <agg/agg_path_storage.h>

// For png compression
#if defined(WIN32) || defined(__APPLE__ )
inline char *strerror_r(int errnum, char *buf, size_t buflen) {
    strerror_s(buf, buflen, errnum);
    return buf;
}
#endif
#include <png/writer.hpp>

namespace Slic3r {

class Raster::Impl {
public:
    using TPixelRenderer = agg::pixfmt_gray8; // agg::pixfmt_rgb24;
    using TRawRenderer = agg::renderer_base<TPixelRenderer>;
    using TPixel = TPixelRenderer::color_type;
    using TRawBuffer = agg::rendering_buffer;

    using TBuffer = std::vector<TPixelRenderer::pixel_type>;

    using TRendererAA = agg::renderer_scanline_aa_solid<TRawRenderer>;

    static const TPixel ColorWhite;
    static const TPixel ColorBlack;

private:
    Raster::Resolution resolution_;
    Raster::PixelDim pxdim_;
    TBuffer buf_;
    TRawBuffer rbuf_;
    TPixelRenderer pixfmt_;
    TRawRenderer raw_renderer_;
    TRendererAA renderer_;
public:
    inline Impl(const Raster::Resolution& res, const Raster::PixelDim &pd):
        resolution_(res), pxdim_(pd),
        buf_(res.pixels()),
        rbuf_(reinterpret_cast<TPixelRenderer::value_type*>(buf_.data()),
              res.width_px, res.height_px,
              res.width_px*TPixelRenderer::num_components),
        pixfmt_(rbuf_),
        raw_renderer_(pixfmt_),
        renderer_(raw_renderer_)
    {
        renderer_.color(ColorWhite);

        // If we would like to play around with gamma
        // ras.gamma(agg::gamma_power(1.0));

        clear();
    }

    void draw(const ExPolygon &poly) {
        agg::rasterizer_scanline_aa<> ras;
        agg::scanline_p8 scanlines;

        auto&& path = to_path(poly.contour);
        ras.add_path(path);

        for(auto h : poly.holes) {
            auto&& holepath = to_path(h);
            ras.add_path(holepath);
        }

        agg::render_scanlines(ras, scanlines, renderer_);
    }

    inline void clear() {
        raw_renderer_.clear(ColorBlack);
    }

    inline TBuffer& buffer()  { return buf_; }

    inline const Raster::Resolution resolution() { return resolution_; }

private:
    double getPx(const Point& p) {
        return p.x * SCALING_FACTOR/pxdim_.w_mm;
    }

    double getPy(const Point& p) {
        return p.y * SCALING_FACTOR/pxdim_.h_mm;
    }

    agg::path_storage to_path(const Polygon& poly) {
        agg::path_storage path;
        auto it = poly.points.begin();
        path.move_to(getPx(*it), getPy(*it));
        while(++it != poly.points.end())
            path.line_to(getPx(*it), getPy(*it));

        path.line_to(getPx(poly.points.front()), getPy(poly.points.front()));
        return path;
    }

};

const Raster::Impl::TPixel Raster::Impl::ColorWhite = Raster::Impl::TPixel(255);
const Raster::Impl::TPixel Raster::Impl::ColorBlack = Raster::Impl::TPixel(0);

Raster::Raster(const Resolution &r, const PixelDim &pd):
    impl_(new Impl(r, pd)) {}

Raster::Raster() {}

Raster::~Raster() {}

Raster::Raster(const Raster &cpy) {
    *impl_ = *(cpy.impl_);
}

Raster::Raster(Raster &&m):
    impl_(std::move(m.impl_)) {}

void Raster::reset(const Raster::Resolution &r, const Raster::PixelDim &pd)
{
    impl_.reset(new Impl(r, pd));
}

void Raster::reset()
{
    impl_.reset();
}

Raster::Resolution Raster::resolution() const
{
    if(impl_) return impl_->resolution();

    return Resolution(0, 0);
}

void Raster::clear()
{
    assert(impl_);
    impl_->clear();
}

void Raster::draw(const ExPolygon &poly)
{
    assert(impl_);
    impl_->draw(poly);
}

void Raster::save(std::ostream& stream, Compression comp)
{
    assert(impl_);
    switch(comp) {
    case Compression::PNG: {

        png::writer<std::ostream> wr(stream);

        wr.set_bit_depth(8);
        wr.set_color_type(png::color_type_gray);
        wr.set_width(resolution().width_px);
        wr.set_height(resolution().height_px);
        wr.set_compression_type(png::compression_type_default);

        wr.write_info();

        auto& b = impl_->buffer();
        auto ptr = reinterpret_cast<png::byte*>( b.data() );
        unsigned stride =
                sizeof(Impl::TBuffer::value_type) *  resolution().width_px;

        for(unsigned r = 0; r < resolution().height_px; r++, ptr+=stride) {
            wr.write_row(ptr);
        }

        break;
    }
    case Compression::RAW: {
        stream << "P5 "
               << impl_->resolution().width_px << " "
               << impl_->resolution().height_px << " "
               << "255 ";
        stream.write(reinterpret_cast<const char*>(impl_->buffer().data()),
                     impl_->buffer().size()*sizeof(Impl::TBuffer::value_type));
    }
    }
}

}
