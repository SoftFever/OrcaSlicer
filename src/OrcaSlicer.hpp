#ifndef SLIC3R_HPP
#define SLIC3R_HPP

#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"

namespace Slic3r {

namespace IO {
	enum ExportFormat : int {
        AMF,
        OBJ,
        STL,
        // SVG,
        TMF,
        Gcode
    };
}

class CLI {
public:
    int run(int argc, char **argv);

private:
    DynamicPrintAndCLIConfig    m_config;
    DynamicPrintConfig			m_print_config;
    DynamicPrintConfig          m_extra_config;
    std::vector<std::string>    m_input_files;
    std::vector<std::string>    m_actions;
    std::vector<std::string>    m_transforms;
    std::vector<Model>          m_models;

    bool setup(int argc, char **argv);

    /// Prints usage of the CLI.
    void print_help(bool include_print_options = false, PrinterTechnology printer_technology = ptAny) const;

    /// Exports loaded models to a file of the specified format, according to the options affecting output filename.
    bool export_models(IO::ExportFormat format);
    //BBS: add export_project function
    bool export_project(Model *model, std::string& path, PlateDataPtrs &partplate_data, std::vector<Preset*>& project_presets,
        std::vector<ThumbnailData*>& thumbnails, std::vector<ThumbnailData*>& top_thumbnails, std::vector<ThumbnailData*>& pick_thumbnails,
        std::vector<ThumbnailData*>& calibration_thumbnails,
        std::vector<PlateBBoxData*>& plate_bboxes, const DynamicPrintConfig* config);

    bool has_print_action() const { return m_config.opt_bool("export_gcode") || m_config.opt_bool("export_sla"); }

    std::string output_filepath(const Model &model, IO::ExportFormat format) const;
};

}

#endif
