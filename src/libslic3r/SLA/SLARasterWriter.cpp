#include "SLARasterWriter.hpp"
#include "libslic3r/Zipper.hpp"
#include "ExPolygon.hpp"
#include <libnest2d/backends/clipper/clipper_polygon.hpp>

#include <boost/log/trivial.hpp>
#include <boost/filesystem/path.hpp>

namespace Slic3r { namespace sla {

std::string SLARasterWriter::createIniContent(const std::string& projectname) const 
{
    auto expt_str = std::to_string(m_exp_time_s);
    auto expt_first_str = std::to_string(m_exp_time_first_s);
    auto layerh_str = std::to_string(m_layer_height);

    const std::string cnt_fade_layers = std::to_string(m_cnt_fade_layers);
    const std::string cnt_slow_layers = std::to_string(m_cnt_slow_layers);
    const std::string cnt_fast_layers = std::to_string(m_cnt_fast_layers);
    const std::string used_material   = std::to_string(m_used_material);

    return std::string(
    "action = print\n"
    "jobDir = ") + projectname + "\n" +
    "expTime = " + expt_str + "\n"
    "expTimeFirst = " + expt_first_str + "\n"
    "numFade = " + cnt_fade_layers + "\n"
    "layerHeight = " + layerh_str + "\n"
    "usedMaterial = " + used_material + "\n"
    "numSlow = " + cnt_slow_layers + "\n"
                                     "numFast = " + cnt_fast_layers + "\n";
}

void SLARasterWriter::flpXY(ClipperLib::Polygon &poly)
{
    for(auto& p : poly.Contour) std::swap(p.X, p.Y);
    std::reverse(poly.Contour.begin(), poly.Contour.end());
    
    for(auto& h : poly.Holes) {
        for(auto& p : h) std::swap(p.X, p.Y);
        std::reverse(h.begin(), h.end());
    }
}

void SLARasterWriter::flpXY(ExPolygon &poly)
{
    for(auto& p : poly.contour.points) p = Point(p.y(), p.x());
    std::reverse(poly.contour.points.begin(), poly.contour.points.end());
    
    for(auto& h : poly.holes) {
        for(auto& p : h.points) p = Point(p.y(), p.x());
        std::reverse(h.points.begin(), h.points.end());
    }
}

SLARasterWriter::SLARasterWriter(const SLAPrinterConfig &cfg, 
                                 const SLAMaterialConfig &mcfg, 
                                 double layer_height)
{
    double w = cfg.display_width.getFloat();
    double h = cfg.display_height.getFloat();
    auto pw = unsigned(cfg.display_pixels_x.getInt());
    auto ph = unsigned(cfg.display_pixels_y.getInt());
    
    m_mirror[X] = cfg.display_mirror_x.getBool();
    
    // PNG raster will implicitly do an Y mirror
    m_mirror[Y] = ! cfg.display_mirror_y.getBool();
        
    auto ro = cfg.display_orientation.getInt();
    
    if(ro == roPortrait) {
        std::swap(w, h);
        std::swap(pw, ph);
        m_o = roPortrait;
        
        // XY flipping implicitly does an X mirror
        m_mirror[X] = ! m_mirror[X];
    } else m_o = roLandscape;
    
    m_res = Raster::Resolution(pw, ph);
    m_pxdim = Raster::PixelDim(w/pw, h/ph);
    m_exp_time_s = mcfg.exposure_time.getFloat();
    m_exp_time_first_s = mcfg.initial_exposure_time.getFloat();
    m_layer_height = layer_height;
    
    m_gamma = cfg.gamma_correction.getFloat();
}

void SLARasterWriter::save(const std::string &fpath, const std::string &prjname)
{
    try {
        Zipper zipper(fpath); // zipper with no compression
        
        std::string project = prjname.empty()?
                    boost::filesystem::path(fpath).stem().string() : prjname;
        
        zipper.add_entry("config.ini");
        
        zipper << createIniContent(project);
        
        for(unsigned i = 0; i < m_layers_rst.size(); i++)
        {
            if(m_layers_rst[i].rawbytes.size() > 0) {
                char lyrnum[6];
                std::sprintf(lyrnum, "%.5d", i);
                auto zfilename = project + lyrnum + ".png";
                
                // Add binary entry to the zipper
                zipper.add_entry(zfilename,
                                 m_layers_rst[i].rawbytes.data(),
                                 m_layers_rst[i].rawbytes.size());
            }
        }
        
        zipper.finalize();
    } catch(std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << e.what();
        // Rethrow the exception
        throw;
    }
}

void SLARasterWriter::set_statistics(const std::vector<double> statistics)
{
    if (statistics.size() != psCnt)
        return;
    
    m_used_material   = statistics[psUsedMaterial];
    m_cnt_fade_layers = int(statistics[psNumFade]);
    m_cnt_slow_layers = int(statistics[psNumSlow]);
    m_cnt_fast_layers = int(statistics[psNumFast]);
}

} // namespace sla
} // namespace Slic3r
