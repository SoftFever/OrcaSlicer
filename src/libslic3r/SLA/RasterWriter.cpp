#include <string_view>

#include <libslic3r/SLA/RasterWriter.hpp>

#include "libslic3r/PrintConfig.hpp"
#include <libslic3r/Zipper.hpp>
#include <libslic3r/Time.hpp>

#include "ExPolygon.hpp"
#include <libnest2d/backends/clipper/clipper_polygon.hpp>

#include <boost/log/trivial.hpp>
#include <boost/filesystem/path.hpp>

namespace Slic3r { namespace sla {

void RasterWriter::write_ini(const std::map<std::string, std::string> &m, std::string &ini)
{
    for (auto &param : m) ini += param.first + " = " + param.second + "\n";    
}

std::string RasterWriter::create_ini_content(const std::string& projectname) const 
{
    std::string out("action = print\njobDir = ");
    out += projectname + "\n";
    write_ini(m_config, out);
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
        save(zipper, prjname);
        zipper.finalize();
    } catch(std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << e.what();
        // Rethrow the exception
        throw;
    }
}

void RasterWriter::save(Zipper &zipper, const std::string &prjname)
{
    try {
        std::string project =
            prjname.empty() ?
                boost::filesystem::path(zipper.get_filename()).stem().string() :
                prjname;

        zipper.add_entry("config.ini");

        zipper << create_ini_content(project);
        
        zipper.add_entry("prusaslicer.ini");
        std::string prusaslicer_ini;
        write_ini(m_slicer_config, prusaslicer_ini);
        zipper << prusaslicer_ini;

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

void append_full_config(const DynamicPrintConfig &cfg, std::map<std::string, std::string> &keys)
{
    using namespace std::literals::string_view_literals;
    
    // Sorted list of config keys, which shall not be stored into the ini.
    static constexpr auto banned_keys = { 
		"compatible_printers"sv,
        "compatible_prints"sv,
        "print_host"sv,
        "printhost_apikey"sv,
        "printhost_cafile"sv
    };
    
    assert(std::is_sorted(banned_keys.begin(), banned_keys.end()));
    auto is_banned = [](const std::string &key) {
        return std::binary_search(banned_keys.begin(), banned_keys.end(), key);
    };
    
    for (const std::string &key : cfg.keys())
        if (! is_banned(key) && ! cfg.option(key)->is_nil())
            keys[key] = cfg.opt_serialize(key);
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
    append_full_config(cfg, m_slicer_config);
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
