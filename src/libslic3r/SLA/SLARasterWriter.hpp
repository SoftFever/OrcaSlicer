#ifndef SLARASTERWRITER_HPP
#define SLARASTERWRITER_HPP

// For png export of the sliced model
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <map>
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
    enum Orientation {
        roLandscape,
        roPortrait
    };
    
    // Used for addressing parameters of set_statistics()
    struct PrintStatistics
    {    
        double used_material = 0.;
        double estimated_print_time_s = 0.;
        size_t num_fade = 0;
        size_t num_slow = 0;
        size_t num_fast = 0;
    };
    
private:
    
    // A struct to bind the raster image data and its compressed bytes together.
    struct Layer {
        Raster raster;
        RawBytes rawbytes;

        Layer() = default;
        
        // The image is big, do not copy by accident
        Layer(const Layer&) = delete; 
        Layer& operator=(const Layer&) = delete;

        Layer(Layer &&m) = default;
        Layer &operator=(Layer &&) = default;
    };

    // We will save the compressed PNG data into RawBytes type buffers in 
    // parallel. Later we can write every layer to the disk sequentially.
    std::vector<Layer> m_layers_rst;
    Raster::Resolution m_res;
    Raster::PixelDim m_pxdim;
    std::array<bool, 2> m_mirror;
    double m_gamma;
    
    std::map<std::string, std::string> m_config;
    
    std::string createIniContent(const std::string& projectname) const;
    
    static void flpXY(ClipperLib::Polygon& poly);
    static void flpXY(ExPolygon& poly);

public:
    SLARasterWriter(const Raster::Resolution  &res,
                    const Raster::PixelDim    &pixdim,
                    const std::array<bool, 2> &mirror,
                    double gamma = 1.);

    SLARasterWriter(const SLARasterWriter& ) = delete;
    SLARasterWriter& operator=(const SLARasterWriter&) = delete;
    SLARasterWriter(SLARasterWriter&& m) = default;
    SLARasterWriter& operator=(SLARasterWriter&&) = default;

    inline void layers(unsigned cnt) { if(cnt > 0) m_layers_rst.resize(cnt); }
    inline unsigned layers() const { return unsigned(m_layers_rst.size()); }
    
    template<class Poly> void draw_polygon(const Poly& p, unsigned lyr,
                      Orientation o = roPortrait)
    {
        assert(lyr < m_layers_rst.size());
        
        switch (o) {
        case roPortrait: {
            Poly poly(p);
            flpXY(poly);
            m_layers_rst[lyr].raster.draw(poly);
            break;
        }
        case roLandscape:
            m_layers_rst[lyr].raster.draw(p);
            break;
        }
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

    void save(const std::string &fpath, const std::string &prjname = "");

    void set_statistics(const PrintStatistics &statistics);
    
    void set_config(const DynamicPrintConfig &cfg);
};

} // namespace sla
} // namespace Slic3r

#endif // SLARASTERWRITER_HPP
