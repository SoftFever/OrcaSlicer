#include "Rasterizer.hpp"
#include <ExPolygon.hpp>
#include <libnest2d/backends/clipper/clipper_polygon.hpp>

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

// Experimental minz image write:
#include <miniz.h>

namespace Slic3r {

const Polygon& contour(const ExPolygon& p) { return p.contour; }
const ClipperLib::Path& contour(const ClipperLib::Polygon& p) { return p.Contour; }

const Polygons& holes(const ExPolygon& p) { return p.holes; }
const ClipperLib::Paths& holes(const ClipperLib::Polygon& p) { return p.Holes; }

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

    using Origin = Raster::Origin;

private:
    Raster::Resolution m_resolution;
//    Raster::PixelDim m_pxdim;
    Raster::PixelDim m_pxdim_scaled;    // used for scaled coordinate polygons
    TBuffer m_buf;
    TRawBuffer m_rbuf;
    TPixelRenderer m_pixfmt;
    TRawRenderer m_raw_renderer;
    TRendererAA m_renderer;
    
    std::function<double(double)> m_gammafn;
    Origin m_o;
    
    inline void flipy(agg::path_storage& path) const {
        path.flip_y(0, m_resolution.height_px);
    }

public:

    inline Impl(const Raster::Resolution& res, const Raster::PixelDim &pd,
                Origin o, double gamma = 1.0):
        m_resolution(res), 
//        m_pxdim(pd), 
        m_pxdim_scaled(SCALING_FACTOR / pd.w_mm, SCALING_FACTOR / pd.h_mm),
        m_buf(res.pixels()),
        m_rbuf(reinterpret_cast<TPixelRenderer::value_type*>(m_buf.data()),
              res.width_px, res.height_px,
              int(res.width_px*TPixelRenderer::num_components)),
        m_pixfmt(m_rbuf),
        m_raw_renderer(m_pixfmt),
        m_renderer(m_raw_renderer),
        m_o(o)
    {
        m_renderer.color(ColorWhite);
        
        if(gamma > 0) m_gammafn = agg::gamma_power(gamma);
        else m_gammafn = agg::gamma_threshold(0.5);
        
        clear();
    }

    template<class P> void draw(const P &poly) {
        agg::rasterizer_scanline_aa<> ras;
        agg::scanline_p8 scanlines;
        
        ras.gamma(m_gammafn);

        auto&& path = to_path(contour(poly));

        if(m_o == Origin::TOP_LEFT) flipy(path);

        ras.add_path(path);

        for(auto& h : holes(poly)) {
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
    inline double getPx(const Point& p) {
        return p(0) * m_pxdim_scaled.w_mm;
    }

    inline double getPy(const Point& p) {
        return p(1) * m_pxdim_scaled.h_mm;
    }

    inline agg::path_storage to_path(const Polygon& poly)
    {
        return to_path(poly.points);
    }

    inline double getPx(const ClipperLib::IntPoint& p) {
        return p.X * m_pxdim_scaled.w_mm;
    }

    inline double getPy(const ClipperLib::IntPoint& p) {
        return p.Y * m_pxdim_scaled.h_mm;
    }

    template<class PointVec> agg::path_storage to_path(const PointVec& poly)
    {
        agg::path_storage path;
        
        auto it = poly.begin();
        path.move_to(getPx(*it), getPy(*it));
        
        while(++it != poly.end())
            path.line_to(getPx(*it), getPy(*it));

        path.line_to(getPx(poly.front()), getPy(poly.front()));
        return path;
    }

};

const Raster::Impl::TPixel Raster::Impl::ColorWhite = Raster::Impl::TPixel(255);
const Raster::Impl::TPixel Raster::Impl::ColorBlack = Raster::Impl::TPixel(0);

Raster::Raster(const Resolution &r, const PixelDim &pd, Origin o, double g):
    m_impl(new Impl(r, pd, o, g)) {}

Raster::Raster() {}

Raster::~Raster() {}

Raster::Raster(Raster &&m):
    m_impl(std::move(m.m_impl)) {}

void Raster::reset(const Raster::Resolution &r, const Raster::PixelDim &pd, 
                   double g)
{
    // Free up the unnecessary memory and make sure it stays clear after
    // an exception
    auto o = m_impl? m_impl->origin() : Origin::TOP_LEFT;
    reset(r, pd, o, g);
}

void Raster::reset(const Raster::Resolution &r, const Raster::PixelDim &pd,
                   Raster::Origin o, double gamma)
{
    m_impl.reset();
    m_impl.reset(new Impl(r, pd, o, gamma));
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

void Raster::draw(const ExPolygon &expoly)
{
    m_impl->draw(expoly);
}

void Raster::draw(const ClipperLib::Polygon &poly)
{
    m_impl->draw(poly);
}

void Raster::save(std::ostream& stream, Compression comp)
{
    assert(m_impl);
    if(!stream.good()) return;

    switch(comp) {
    case Compression::PNG: {
        auto& b = m_impl->buffer();
        size_t out_len = 0;
        void * rawdata = tdefl_write_image_to_png_file_in_memory(
                    b.data(),
                    int(resolution().width_px),
                    int(resolution().height_px), 1, &out_len);

        if(rawdata == nullptr) break;

        stream.write(static_cast<const char*>(rawdata),
                     std::streamsize(out_len));

        MZ_FREE(rawdata);

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

RawBytes Raster::save(Raster::Compression comp)
{
    assert(m_impl);

    std::vector<std::uint8_t> data; size_t s = 0;

    switch(comp) {
    case Compression::PNG: {
        void *rawdata = tdefl_write_image_to_png_file_in_memory(
                    m_impl->buffer().data(),
                    int(resolution().width_px),
                    int(resolution().height_px), 1, &s);

        if(rawdata == nullptr) break;
        auto ptr = static_cast<std::uint8_t*>(rawdata);
        
        data.reserve(s); std::copy(ptr, ptr + s, std::back_inserter(data));
        
        MZ_FREE(rawdata);
        break;
    }
    case Compression::RAW: {
        auto header = std::string("P5 ") +
                std::to_string(m_impl->resolution().width_px) + " " +
                std::to_string(m_impl->resolution().height_px) + " " + "255 ";

        auto sz = m_impl->buffer().size()*sizeof(Impl::TBuffer::value_type);
        s = sz + header.size();
        
        data.reserve(s);
        
        auto buff = reinterpret_cast<std::uint8_t*>(m_impl->buffer().data());
        std::copy(header.begin(), header.end(), std::back_inserter(data));
        std::copy(buff, buff+sz, std::back_inserter(data));
        
        break;
    }
    }

    return {std::move(data)};
}

}
