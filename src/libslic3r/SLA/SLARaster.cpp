#ifndef SLARASTER_CPP
#define SLARASTER_CPP

#include <functional>

#include "SLARaster.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/MTUtils.hpp"
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

inline const Polygon& contour(const ExPolygon& p) { return p.contour; }
inline const ClipperLib::Path& contour(const ClipperLib::Polygon& p) { return p.Contour; }

inline const Polygons& holes(const ExPolygon& p) { return p.holes; }
inline const ClipperLib::Paths& holes(const ClipperLib::Polygon& p) { return p.Holes; }

namespace sla {

const Raster::TMirroring Raster::NoMirror = {false, false};
const Raster::TMirroring Raster::MirrorX  = {true, false};
const Raster::TMirroring Raster::MirrorY  = {false, true};
const Raster::TMirroring Raster::MirrorXY = {true, true};


using TPixelRenderer = agg::pixfmt_gray8; // agg::pixfmt_rgb24;
using TRawRenderer = agg::renderer_base<TPixelRenderer>;
using TPixel = TPixelRenderer::color_type;
using TRawBuffer = agg::rendering_buffer;
using TBuffer = std::vector<TPixelRenderer::pixel_type>;

using TRendererAA = agg::renderer_scanline_aa_solid<TRawRenderer>;

class Raster::Impl {
public:

    static const TPixel ColorWhite;
    static const TPixel ColorBlack;

    using Format = Raster::RawData;

private:
    Raster::Resolution m_resolution;
    Raster::PixelDim m_pxdim_scaled;    // used for scaled coordinate polygons
    TBuffer m_buf;
    TRawBuffer m_rbuf;
    TPixelRenderer m_pixfmt;
    TRawRenderer m_raw_renderer;
    TRendererAA m_renderer;
    
    std::function<double(double)> m_gammafn;
    Trafo m_trafo;
    
    inline void flipy(agg::path_storage& path) const {
        path.flip_y(0, double(m_resolution.height_px));
    }
    
    inline void flipx(agg::path_storage& path) const {
        path.flip_x(0, double(m_resolution.width_px));
    }

public:
    inline Impl(const Raster::Resolution & res,
                const Raster::PixelDim &   pd,
                const Trafo &trafo)
        : m_resolution(res)
        , m_pxdim_scaled(SCALING_FACTOR / pd.w_mm, SCALING_FACTOR / pd.h_mm)
        , m_buf(res.pixels())
        , m_rbuf(reinterpret_cast<TPixelRenderer::value_type *>(m_buf.data()),
                 unsigned(res.width_px),
                 unsigned(res.height_px),
                 int(res.width_px * TPixelRenderer::num_components))
        , m_pixfmt(m_rbuf)
        , m_raw_renderer(m_pixfmt)
        , m_renderer(m_raw_renderer)
        , m_trafo(trafo)
    {
        m_renderer.color(ColorWhite);
        
        if (trafo.gamma > 0) m_gammafn = agg::gamma_power(trafo.gamma);
        else m_gammafn = agg::gamma_threshold(0.5);
        
        clear();
    }

    template<class P> void draw(const P &poly) {
        agg::rasterizer_scanline_aa<> ras;
        agg::scanline_p8 scanlines;
        
        ras.gamma(m_gammafn);
        
        ras.add_path(to_path(contour(poly)));
        for(auto& h : holes(poly)) ras.add_path(to_path(h));
        
        agg::render_scanlines(ras, scanlines, m_renderer);
    }

    inline void clear() {
        m_raw_renderer.clear(ColorBlack);
    }

    inline TBuffer& buffer()  { return m_buf; }
    inline const TBuffer& buffer() const { return m_buf; }
    

    inline const Raster::Resolution resolution() { return m_resolution; }
    inline const Raster::PixelDim   pixdim()
    {
        return {SCALING_FACTOR / m_pxdim_scaled.w_mm,
                SCALING_FACTOR / m_pxdim_scaled.h_mm};
    }

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

    template<class PointVec> agg::path_storage _to_path(const PointVec& v)
    {
        agg::path_storage path;
        
        auto it = v.begin();
        path.move_to(getPx(*it), getPy(*it));
        while(++it != v.end()) path.line_to(getPx(*it), getPy(*it));
        path.line_to(getPx(v.front()), getPy(v.front()));
        
        return path;
    }
   
    template<class PointVec> agg::path_storage _to_path_flpxy(const PointVec& v)
    {
        agg::path_storage path;
        
        auto it = v.begin();
        path.move_to(getPy(*it), getPx(*it));
        while(++it != v.end()) path.line_to(getPy(*it), getPx(*it));
        path.line_to(getPy(v.front()), getPx(v.front()));
        
        return path;
    }
    
    template<class PointVec> agg::path_storage to_path(const PointVec &v)
    {
        auto path = m_trafo.flipXY ? _to_path_flpxy(v) : _to_path(v);
        
        path.translate_all_paths(m_trafo.origin_x * m_pxdim_scaled.w_mm,
                                 m_trafo.origin_y * m_pxdim_scaled.h_mm);
        
        if(m_trafo.mirror_x) flipx(path);
        if(m_trafo.mirror_y) flipy(path);
        
        return path;
    }

};

const TPixel Raster::Impl::ColorWhite = TPixel(255);
const TPixel Raster::Impl::ColorBlack = TPixel(0);

Raster::Raster() { reset(); }

Raster::Raster(const Raster::Resolution &r,
               const Raster::PixelDim &  pd,
               const Raster::Trafo &     tr)
{
    reset(r, pd, tr);
}

Raster::~Raster() = default;

Raster::Raster(Raster &&m) = default;
Raster &Raster::operator=(Raster &&) = default;

void Raster::reset(const Raster::Resolution &r, const Raster::PixelDim &pd,
                   const Trafo &trafo)
{
    m_impl.reset();
    m_impl.reset(new Impl(r, pd, trafo));
}

void Raster::reset()
{
    m_impl.reset();
}

Raster::Resolution Raster::resolution() const
{
    if (m_impl) return m_impl->resolution();
    
    return Resolution{0, 0};
}

Raster::PixelDim Raster::pixel_dimensions() const
{
    if (m_impl) return m_impl->pixdim();
    
    return PixelDim{0., 0.};
}

void Raster::clear()
{
    assert(m_impl);
    m_impl->clear();
}

void Raster::draw(const ExPolygon &expoly)
{
    assert(m_impl);
    m_impl->draw(expoly);
}

void Raster::draw(const ClipperLib::Polygon &poly)
{
    assert(m_impl);
    m_impl->draw(poly);
}

uint8_t Raster::read_pixel(size_t x, size_t y) const
{
    assert (m_impl);
    TPixel::value_type px;
    m_impl->buffer()[y * resolution().width_px + x].get(px);
    return px;
}

PNGImage & PNGImage::serialize(const Raster &raster)
{
    size_t s = 0;
    m_buffer.clear();
    
    void *rawdata = tdefl_write_image_to_png_file_in_memory(
        get_internals(raster).buffer().data(),
        int(raster.resolution().width_px),
        int(raster.resolution().height_px), 1, &s);
    
    // On error, data() will return an empty vector. No other info can be
    // retrieved from miniz anyway...
    if (rawdata == nullptr) return *this;
    
    auto ptr = static_cast<std::uint8_t*>(rawdata);
    
    m_buffer.reserve(s);
    std::copy(ptr, ptr + s, std::back_inserter(m_buffer));
    
    MZ_FREE(rawdata);
    return *this;
}

std::ostream &operator<<(std::ostream &stream, const Raster::RawData &bytes)
{
    stream.write(reinterpret_cast<const char *>(bytes.data()),
                 std::streamsize(bytes.size()));
    
    return stream;
}

Raster::RawData::~RawData() = default;

PPMImage & PPMImage::serialize(const Raster &raster)
{
    auto header = std::string("P5 ") +
            std::to_string(raster.resolution().width_px) + " " +
            std::to_string(raster.resolution().height_px) + " " + "255 ";
    
    const auto &impl = get_internals(raster);
    auto sz = impl.buffer().size() * sizeof(TBuffer::value_type);
    size_t s = sz + header.size();
    
    m_buffer.clear();
    m_buffer.reserve(s);

    auto buff = reinterpret_cast<const std::uint8_t*>(impl.buffer().data());
    std::copy(header.begin(), header.end(), std::back_inserter(m_buffer));
    std::copy(buff, buff+sz, std::back_inserter(m_buffer));
    
    return *this;
}

const Raster::Impl &Raster::RawData::get_internals(const Raster &raster)
{
    return *raster.m_impl;
}

} // namespace sla
} // namespace Slic3r

#endif // SLARASTER_CPP
