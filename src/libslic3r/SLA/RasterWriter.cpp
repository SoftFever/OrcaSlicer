#include <libslic3r/SLA/RasterWriter.hpp>
#include <libslic3r/Zipper.hpp>
#include <libslic3r/Time.hpp>

#include "ExPolygon.hpp"
#include <libnest2d/backends/clipper/clipper_polygon.hpp>

#include <boost/log/trivial.hpp>
#include <boost/filesystem/path.hpp>

namespace Slic3r { namespace sla {

std::string RasterWriter::createIniContent(const std::string& projectname) const 
{
    std::string out("action = print\njobDir = ");
    out += projectname + "\n";
    
    for (auto &param : m_config)
        out += param.first + " = " + param.second + "\n";    
    
    return out;
}

RasterWriter::RasterWriter(const Raster::Resolution &res,
                           const Raster::PixelDim &  pixdim,
                           const Raster::Trafo &     trafo,
                           double                    gamma)
    : m_res(res), m_pxdim(pixdim), m_trafo(trafo), m_gamma(gamma)
{}

void RasterWriter::save(const std::string &fpath, const std::string &prjname)
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

namespace {

std::string get_cfg_value(const DynamicPrintConfig &cfg, const std::string &key)
{
    std::string ret;
    
    if (cfg.has(key)) {
        auto opt = cfg.option(key);
        if (opt) ret = opt->serialize();
    }
    
    return ret;    
}

} // namespace

void RasterWriter::set_config(const DynamicPrintConfig &cfg)
{
    m_config["layerHeight"]    = get_cfg_value(cfg, "layer_height");
    m_config["expTime"]        = get_cfg_value(cfg, "exposure_time");
    m_config["expTimeFirst"]   = get_cfg_value(cfg, "initial_exposure_time");
    m_config["materialName"]   = get_cfg_value(cfg, "sla_material_settings_id");
    m_config["printerModel"]   = get_cfg_value(cfg, "printer_model");
    m_config["printerVariant"] = get_cfg_value(cfg, "printer_variant");
    m_config["printerProfile"] = get_cfg_value(cfg, "printer_settings_id");
    m_config["printProfile"]   = get_cfg_value(cfg, "sla_print_settings_id");

    m_config["fileCreationTimestamp"] = Utils::utc_timestamp();
    m_config["prusaSlicerVersion"]    = SLIC3R_BUILD_ID;
}

void RasterWriter::set_statistics(const PrintStatistics &stats)
{
    m_config["usedMaterial"] = std::to_string(stats.used_material);
    m_config["numFade"]      = std::to_string(stats.num_fade);
    m_config["numSlow"]      = std::to_string(stats.num_slow);
    m_config["numFast"]      = std::to_string(stats.num_fast);
    m_config["printTime"]    = std::to_string(stats.estimated_print_time_s);
}

} // namespace sla
} // namespace Slic3r
