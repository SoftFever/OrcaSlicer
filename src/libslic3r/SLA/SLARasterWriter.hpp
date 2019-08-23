#ifndef SLARASTERWRITER_HPP
#define SLARASTERWRITER_HPP

// For png export of the sliced model
#include <fstream>
#include <sstream>
#include <vector>
#include <array>

#include "libslic3r/PrintConfig.hpp"

#include "SLARaster.hpp"

namespace Slic3r { namespace sla {

// Implementation for PNG raster output
// Be aware that if a large number of layers are allocated, it can very well
// exhaust the available memory especially on 32 bit platform.
// This class is designed to be used in parallel mode. Layers have an ID and
// each layer can be written and compressed independently (in parallel).
// At the end when all layers where written, the save method can be used to 
// write out the result into a zipped archive.
class SLARasterWriter
{
public:
    enum RasterOrientation {
        roLandscape,
        roPortrait
    };
    
    // Used for addressing parameters of set_statistics()
    enum ePrintStatistics
    {
        psUsedMaterial = 0,
        psNumFade,
        psNumSlow,
        psNumFast,
    
        psCnt
    };
    
private:
    
    // A struct to bind the raster image data and its compressed bytes together.
    struct Layer {
        Raster raster;
        RawBytes rawbytes;

        Layer() = default;
        Layer(const Layer&) = delete; // The image is big, do not copy by accident
        Layer& operator=(const Layer&) = delete;
        
        // /////////////////////////////////////////////////////////////////////
        // FIXME: the following is needed for MSVC2013 compatibility
        // /////////////////////////////////////////////////////////////////////

        // Layer(Layer&& m) = default;
        // Layer& operator=(Layer&&) = default;
        Layer(Layer &&m):
            raster(std::move(m.raster)), rawbytes(std::move(m.rawbytes)) {}
        Layer& operator=(Layer &&m) {
            raster = std::move(m.raster); rawbytes = std::move(m.rawbytes);
            return *this;
        }
    };

    // We will save the compressed PNG data into RawBytes type buffers in 
    // parallel. Later we can write every layer to the disk sequentially.
    std::vector<Layer> m_layers_rst;
    Raster::Resolution m_res;
    Raster::PixelDim m_pxdim;
    double m_exp_time_s = .0, m_exp_time_first_s = .0;
    double m_layer_height = .0;
    RasterOrientation m_o = roPortrait;
    std::array<bool, 2> m_mirror;
    
    double m_gamma;

    double m_used_material = 0.0;
    int    m_cnt_fade_layers = 0;
    int    m_cnt_slow_layers = 0;
    int    m_cnt_fast_layers = 0;

    std::string createIniContent(const std::string& projectname) const;
    
    static void flpXY(ClipperLib::Polygon& poly);
    static void flpXY(ExPolygon& poly);

public:

    SLARasterWriter(const SLAPrinterConfig& cfg, 
                    const SLAMaterialConfig& mcfg, 
                    double layer_height);

    SLARasterWriter(const SLARasterWriter& ) = delete;
    SLARasterWriter& operator=(const SLARasterWriter&) = delete;

    // /////////////////////////////////////////////////////////////////////////
    // FIXME: the following is needed for MSVC2013 compatibility
    // /////////////////////////////////////////////////////////////////////////

    // SLARasterWriter(SLARasterWriter&& m) = default;
    // SLARasterWriter& operator=(SLARasterWriter&&) = default;
    SLARasterWriter(SLARasterWriter&& m):
        m_layers_rst(std::move(m.m_layers_rst)),
        m_res(m.m_res),
        m_pxdim(m.m_pxdim),
        m_exp_time_s(m.m_exp_time_s),
        m_exp_time_first_s(m.m_exp_time_first_s),
        m_layer_height(m.m_layer_height),
        m_o(m.m_o),
        m_mirror(std::move(m.m_mirror)),
        m_gamma(m.m_gamma),
        m_used_material(m.m_used_material),
        m_cnt_fade_layers(m.m_cnt_fade_layers),
        m_cnt_slow_layers(m.m_cnt_slow_layers),
        m_cnt_fast_layers(m.m_cnt_fast_layers)
    {}

    // /////////////////////////////////////////////////////////////////////////

    inline void layers(unsigned cnt) { if(cnt > 0) m_layers_rst.resize(cnt); }
    inline unsigned layers() const { return unsigned(m_layers_rst.size()); }
    
    template<class Poly> void draw_polygon(const Poly& p, unsigned lyr) {
        assert(lyr < m_layers_rst.size());
        if(m_o == roPortrait) {
            Poly poly(p); flpXY(poly);
            m_layers_rst[lyr].raster.draw(poly);
        }
        else m_layers_rst[lyr].raster.draw(p);
    }

    inline void begin_layer(unsigned lyr) {
        if(m_layers_rst.size() <= lyr) m_layers_rst.resize(lyr+1);
        m_layers_rst[lyr].raster.reset(m_res, m_pxdim, m_mirror, m_gamma);
    }

    inline void begin_layer() {
        m_layers_rst.emplace_back();
        m_layers_rst.front().raster.reset(m_res, m_pxdim, m_mirror, m_gamma);
    }

    inline void finish_layer(unsigned lyr_id) {
        assert(lyr_id < m_layers_rst.size());
        m_layers_rst[lyr_id].rawbytes =
                m_layers_rst[lyr_id].raster.save(Raster::Format::PNG);
        m_layers_rst[lyr_id].raster.reset();
    }

    inline void finish_layer() {
        if(!m_layers_rst.empty()) {
            m_layers_rst.back().rawbytes =
                    m_layers_rst.back().raster.save(Raster::Format::PNG);
            m_layers_rst.back().raster.reset();
        }
    }

    void save(const std::string& fpath, const std::string& prjname = "");

    void set_statistics(const std::vector<double> statistics);
};

} // namespace sla
} // namespace Slic3r

#endif // SLARASTERWRITER_HPP
