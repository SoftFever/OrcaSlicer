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
#include <png/writer.hpp>

namespace Slic3r {

class Raster::Impl {
public:
    using TPixelRenderer = agg::pixfmt_gray8; // agg::pixfmt_rgb24;
    using TRawRenderer = agg::renderer_base<TPixelRenderer>;
    using TPixel = TPixelRenderer::color_type;
    using TRawBuffer = agg::rendering_buffer;

    using TBuffer = std::vector<TPixelRenderer::value_type>;

    using TRendererAA = agg::renderer_scanline_aa_solid<TRawRenderer>;

    static const TPixel ColorWhite;
    static const TPixel ColorBlack;

    using Origin = Raster::Origin;

	enum
	{
		num_components = 1,
	};

private:
    Raster::Resolution m_resolution;
    Raster::PixelDim m_pxdim;
    TBuffer m_buf;
    TRawBuffer m_rbuf;
    TPixelRenderer m_pixfmt;
    TRawRenderer m_raw_renderer;
    TRendererAA m_renderer;
    Origin m_o;

    inline void flipy(agg::path_storage& path) const {
        path.flip_y(0, m_resolution.height_px);
    }

public:

    inline Impl(const Raster::Resolution& res, const Raster::PixelDim &pd,
                Origin o):
        m_resolution(res), m_pxdim(pd),
        m_buf(res.pixels()),
        m_rbuf(reinterpret_cast<TPixelRenderer::value_type*>(m_buf.data()),
              res.width_px, res.height_px,
              int(res.width_px*num_components)),
        m_pixfmt(m_rbuf),
        m_raw_renderer(m_pixfmt),
        m_renderer(m_raw_renderer),
        m_o(o)
    {
        m_renderer.color(ColorWhite);

        // If we would like to play around with gamma
        // ras.gamma(agg::gamma_power(1.0));

        clear();
    }

    void draw(const ExPolygon &poly) {
        agg::rasterizer_scanline_aa<> ras;
        agg::scanline_p8 scanlines;

        auto&& path = to_path(poly.contour);

        if(m_o == Origin::TOP_LEFT) flipy(path);

        ras.add_path(path);

        for(auto h : poly.holes) {
            auto&& holepath = to_path(h);
            if(m_o == Origin::TOP_LEFT) flipy(holepath);
            ras.add_path(holepath);
        }

        agg::render_scanlines(ras, scanlines, m_renderer);
    }

    inline void clear() {
        m_raw_renderer.clear(ColorBlack);
    }

    inline TBuffer& buffer()  { return m_buf; }

    inline const Raster::Resolution resolution() { return m_resolution; }

    inline Origin origin() const /*noexcept*/ { return m_o; }

private:
    double getPx(const Point& p) {
        return p(0) * SCALING_FACTOR/m_pxdim.w_mm;
    }

    double getPy(const Point& p) {
        return p(1) * SCALING_FACTOR/m_pxdim.h_mm;
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

Raster::Raster(const Resolution &r, const PixelDim &pd, Origin o):
    m_impl(new Impl(r, pd, o)) {}

Raster::Raster() {}

Raster::~Raster() {}

Raster::Raster(Raster &&m):
    m_impl(std::move(m.m_impl)) {}

void Raster::reset(const Raster::Resolution &r, const Raster::PixelDim &pd)
{
    // Free up the unnecessary memory and make sure it stays clear after
    // an exception
    auto o = m_impl? m_impl->origin() : Origin::TOP_LEFT;
    reset(r, pd, o);
}

void Raster::reset(const Raster::Resolution &r, const Raster::PixelDim &pd,
                   Raster::Origin o)
{
    m_impl.reset();
    m_impl.reset(new Impl(r, pd, o));
}

void Raster::reset()
{
    m_impl.reset();
}

Raster::Resolution Raster::resolution() const
{
    if(m_impl) return m_impl->resolution();

    return Resolution(0, 0);
}

void Raster::clear()
{
    assert(m_impl);
    m_impl->clear();
}

void Raster::draw(const ExPolygon &poly)
{
    assert(m_impl);
    m_impl->draw(poly);
}

void Raster::save(std::ostream& stream, Compression comp)
{
    assert(m_impl);
    switch(comp) {
    case Compression::PNG: {

        png::writer<std::ostream> wr(stream);

        wr.set_bit_depth(8);
        wr.set_color_type(png::color_type_gray);
        wr.set_width(resolution().width_px);
        wr.set_height(resolution().height_px);
        wr.set_compression_type(png::compression_type_default);

        wr.write_info();

        auto& b = m_impl->buffer();
        auto ptr = reinterpret_cast<png::byte*>( b.data() );
        unsigned stride =
                sizeof(Impl::TBuffer::value_type) *  resolution().width_px;

        for(unsigned r = 0; r < resolution().height_px; r++, ptr+=stride) {
            wr.write_row(ptr);
        }

        wr.write_end_info();

        break;
    }
    case Compression::RAW: {
        stream << "P5 "
               << m_impl->resolution().width_px << " "
               << m_impl->resolution().height_px << " "
               << "255 ";

        auto sz = m_impl->buffer().size()*sizeof(Impl::TBuffer::value_type);
        stream.write(reinterpret_cast<const char*>(m_impl->buffer().data()),
                     std::streamsize(sz));
    }
    }
}

}
