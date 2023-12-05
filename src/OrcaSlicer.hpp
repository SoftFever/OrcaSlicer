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

#define JSON_ASSEMPLE_PLATES                   "plates"
#define JSON_ASSEMPLE_PLATE_PARAMS             "plate_params"
#define JSON_ASSEMPLE_PLATE_NAME               "plate_name"
#define JSON_ASSEMPLE_PLATE_NEED_ARRANGE       "need_arrange"
#define JSON_ASSEMPLE_OBJECTS                  "objects"
#define JSON_ASSEMPLE_OBJECT_PATH              "path"
#define JSON_ASSEMPLE_OBJECT_COUNT             "count"
#define JSON_ASSEMPLE_OBJECT_FILAMENTS         "filaments"
#define JSON_ASSEMPLE_OBJECT_POS_X             "pos_x"
#define JSON_ASSEMPLE_OBJECT_POS_Y             "pos_y"
#define JSON_ASSEMPLE_OBJECT_POS_Z             "pos_z"
#define JSON_ASSEMPLE_OBJECT_ASSEMBLE_INDEX    "assemble_index"
#define JSON_ASSEMPLE_OBJECT_PRINT_PARAMS      "print_params"


typedef struct _assemble_object_info {
    std::string         path;
    int                 count;

    std::vector<int>    filaments;
    std::vector<int>    assemble_index;
    std::vector<float>  pos_x;
    std::vector<float>  pos_y;
    std::vector<float>  pos_z;
    std::map<std::string, std::string> print_params;
}assemble_object_info_t;

typedef struct _assemble_plate_info {
    std::string         plate_name;
    bool                need_arrange {false};
    int                 filaments_count {0};

    std::map<std::string, std::string> plate_params;
    std::vector<assemble_object_info_t> assemble_obj_list;
    std::vector<ModelObject *> loaded_obj_list;
}assemble_plate_info_t;


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
        std::vector<PlateBBoxData*>& plate_bboxes, const DynamicPrintConfig* config, bool minimum_save, int plate_to_export = -1);

    bool has_print_action() const { return m_config.opt_bool("export_gcode") || m_config.opt_bool("export_sla"); }

    std::string output_filepath(const Model &model, IO::ExportFormat format) const;
    std::string output_filepath(const ModelObject &object, unsigned int index, IO::ExportFormat format) const;
};

}

#endif
