#include "Rasterizer.hpp"

#include <cstdint>

// For rasterizing
#include <agg/agg_basics.h>
#include <agg/agg_rendering_buffer.h>
#include <agg/agg_pixfmt_gray.h>
#include <agg/agg_pixfmt_rgb.h>
#include <agg/agg_renderer_base.h>
#include <agg/agg_renderer_primitives.h>
#include <agg/agg_renderer_scanline.h>

#include <agg/agg_scanline_p.h>
#include <agg/agg_rasterizer_scanline_aa.h>
#include <agg/agg_path_storage.h>

// For compression
#include <png.h>

namespace Slic3r {

class Raster::Impl {
public:
    using TBuffer = std::vector<std::array<std::uint8_t,3>>;
    using TPixelRenderer = agg::pixfmt_rgb24;
    using TRawRenderer = agg::renderer_base<TPixelRenderer>;
    using TRenderer = agg::renderer_primitives<TRawRenderer>;
    using TPixel = TPixelRenderer::color_type;
    using TRawBuffer = agg::rendering_buffer;

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
public:
    inline Impl(const Raster::Resolution& res, const Raster::PixelDim &pd):
        resolution_(res), pxdim_(pd),
        buf_(res.pixels()),
        rbuf_(reinterpret_cast<std::uint8_t*>(buf_.data()),
              res.width_px, res.height_px, res.width_px*sizeof(TBuffer::value_type)),
        pixfmt_(rbuf_),
        raw_renderer_(pixfmt_)
    {
        clear();
    }

    void draw(const Polygon &p) {
        TRendererAA ren_aa(raw_renderer_);
        agg::rasterizer_scanline_aa<> ras;
        agg::scanline_p8 scanlines;

        agg::path_storage paths;

//        ras.gamma(agg::gamma_power(1.0));

        auto it = p.points.begin();
        auto itnext = std::next(it);

        {
            double xf = std::round(it->x * SCALING_FACTOR/pxdim_.w_mm);
            double yf = std::round(it->y * SCALING_FACTOR/pxdim_.h_mm);
            double nxf = std::round(itnext->x * SCALING_FACTOR/pxdim_.w_mm);
            double nyf = std::round(itnext->y * SCALING_FACTOR/pxdim_.h_mm);

            paths.move_to(xf, yf);
            paths.line_to(nxf, nyf);

            ++it, ++itnext;
        }
        while(itnext != p.points.end() ) {
//            double xf = std::round(it->x * SCALING_FACTOR/pxdim_.w_mm);
//            double yf = std::round(it->y * SCALING_FACTOR/pxdim_.h_mm);
//            auto x = renderer_.coord(xf);
//            auto y = renderer_.coord(yf);

            double nxf = std::round(itnext->x * SCALING_FACTOR/pxdim_.w_mm);
            double nyf = std::round(itnext->y * SCALING_FACTOR/pxdim_.h_mm);
//            auto nx = renderer_.coord(nxf);
//            auto ny = renderer_.coord(nyf);

//            renderer_.move_to(x, y);
//            renderer_.line_to(nx, ny);

//            paths.move_to(xf, yf);
            paths.line_to(nxf, nyf);

            /*++it,*/ ++itnext;
        }

        paths.close_polygon();
        ras.add_path(paths);

        ren_aa.color(ColorWhite);
        agg::render_scanlines(ras, scanlines, ren_aa);
    }

    inline void clear() {
        raw_renderer_.clear(ColorBlack);
    }

    inline const TBuffer& buffer() const { return buf_; }

    inline const Raster::Resolution resolution() { return resolution_; }
};

const Raster::Impl::TPixel Raster::Impl::ColorWhite = Raster::Impl::TPixel(255, 255, 255);
const Raster::Impl::TPixel Raster::Impl::ColorBlack = Raster::Impl::TPixel(0, 0, 0);

Raster::Raster(const Resolution &r, const PixelDim &pd):
    impl_(new Impl(r, pd)) {}

Raster::~Raster() {}

Raster::Raster(const Raster &cpy) {
    *impl_ = *(cpy.impl_);
}

Raster::Raster(Raster &&m):
    impl_(std::move(m.impl_)) {}

void Raster::clear()
{
    impl_->clear();
}

void Raster::draw(const Polygon &poly)
{
    impl_->draw(poly);
}

void Raster::save(std::ostream& stream, Compression comp)
{
    switch(comp) {
    case Compression::RAW:
    case Compression::PNG:
        stream << "P6 "
               << impl_->resolution().width_px << " "
               << impl_->resolution().height_px << " "
               << "255 ";

        stream.write(reinterpret_cast<const char*>(impl_->buffer().data()),
                     impl_->buffer().size()*sizeof(Impl::TBuffer::value_type));
        break;
    }
}

}
