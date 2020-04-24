#ifndef ARCHIVETRAITS_HPP
#define ARCHIVETRAITS_HPP

#include <string>

#include "libslic3r/Zipper.hpp"
#include "libslic3r/SLAPrint.hpp"

namespace Slic3r {

class SL1Archive: public SLAPrinter {
    SLAPrinterConfig m_cfg;
    
protected:
    uqptr<sla::RasterBase> create_raster() const override;
    sla::EncodedRaster encode_raster(const sla::RasterBase &rst) const override;
    
public:
    
    SL1Archive() = default;
    explicit SL1Archive(const SLAPrinterConfig &cfg): m_cfg(cfg) {}
    explicit SL1Archive(SLAPrinterConfig &&cfg): m_cfg(std::move(cfg)) {}
    
    void export_print(Zipper &zipper, const SLAPrint &print, const std::string &projectname = "");
    void export_print(const std::string &fname, const SLAPrint &print, const std::string &projectname = "")
    {
        Zipper zipper(fname);
        export_print(zipper, print, projectname);
    }
    
    void apply(const SLAPrinterConfig &cfg) override
    {
        auto diff = m_cfg.diff(cfg);
        if (!diff.empty()) {
            m_cfg.apply_only(cfg, diff);
            m_layers = {};
        }
    }
};
    

} // namespace Slic3r::sla

#endif // ARCHIVETRAITS_HPP
