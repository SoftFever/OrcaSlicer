#include "PrintConfig.hpp"
#include "CommonDefs.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "PrintConfigConstants.hpp"
#include "BeltTransform.hpp"
#include "ClipperUtils.hpp"
#include "Config.hpp"
#include "Geometry.hpp"
#include "FilamentMixer.hpp"
#include "MaterialType.hpp"
#include "I18N.hpp"
#include "enum_bitmask.hpp"
#include "format.hpp"

#include "GCode/Thumbnails.hpp"
#include <numeric>
#include <cstddef>
#include <algorithm>
#include <cassert>
#include <map>
#include <boost/algorithm/string/classification.hpp>
#include <iterator>
#include <cstdlib>
#include "libslic3r.h"
#include <boost/algorithm/string/predicate.hpp>
#include <cmath>
#include <boost/algorithm/string/constants.hpp>
#include <limits>
#include <memory>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/tuple/to_seq.hpp>
#include <cstdint>
#include <set>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>
#include <boost/thread.hpp>
#include <float.h>
#include <string>
#include <vector>
#include <utility>
#include <sstream>
#include <unordered_map>


namespace Slic3r {

//! macro used to mark string used at localization,
//! return same string
#define L(s) (s)

// Note: the L() macro is already used for LABEL and TOOLTIP
#define new_def(OPT_KEY, TYPE, LABEL, TOOLTIP)         def = this->add(OPT_KEY, TYPE);         def->label = L(LABEL);         def->tooltip = L(TOOLTIP);

PrintStatisticsConfigDef::PrintStatisticsConfigDef()
{
    ConfigOptionDef* def;

    def = this->add("extruded_volume", coFloats);
    def->label = L("Volume per extruder");
    def->tooltip = L("Total filament volume extruded per extruder during the entire print.");

    def = this->add("total_toolchanges", coInt);
    def->label = L("Total tool changes");
    def->tooltip = L("Number of tool changes during the print.");

    def = this->add("extruded_volume_total", coFloat);
    def->label = L("Total volume");
    def->tooltip = L("Total volume of filament used during the entire print.");

    def = this->add("extruded_weight", coFloats);
    def->label = L("Weight per extruder");
    def->tooltip = L("Weight per extruder extruded during the entire print. Calculated from filament_density value in Filament Settings.");

    def = this->add("extruded_weight_total", coFloat);
    def->label = L("Total weight");
    def->tooltip = L("Total weight of the print. Calculated from filament_density value in Filament Settings.");

    def = this->add("total_layer_count", coInt);
    def->label = L("Total layer count");
    def->tooltip = L("Number of layers in the entire print.");

    def = this->add("normal_print_time", coString);
    def->label = L("Print time (normal mode)");
    def->tooltip = L("Estimated print time when printed in normal mode (i.e. not in silent mode). Same as print_time.");

    //def = this->add("num_printing_extruders", coInt);
    //def->label = L("Number of printing extruders");
    //def->tooltip = L("Number of extruders used during the print.");

    def = this->add("print_time", coString);
    def->label = L("Print time (normal mode)");
    def->tooltip = L("Estimated print time when printed in normal mode (i.e. not in silent mode). Same as normal_print_time.");

    //def = this->add("printing_filament_types", coString);
    //def->label = L("Used filament types");
    //def->tooltip = L("Comma-separated list of all filament types used during the print.");

    def = this->add("silent_print_time", coString);
    def->label = L("Print time (silent mode)");
    def->tooltip = L("Estimated print time when printed in silent mode.");

    def = this->add("total_cost", coFloat);
    def->label = L("Total cost");
    def->tooltip = L("Total cost of all material used in the print. Calculated from filament_cost value in Filament Settings.");

    def = this->add("total_weight", coFloat);
    def->label = L("Total weight");
    def->tooltip = L("Total weight of the print. Calculated from filament_density value in Filament Settings.");

    def = this->add("total_wipe_tower_cost", coFloat);
    def->label = L("Total wipe tower cost");
    def->tooltip = L("Total cost of the material wasted on the wipe tower. Calculated from filament_cost value in Filament Settings.");

    def = this->add("total_wipe_tower_filament", coFloat);
    def->label = L("Wipe tower volume");
    def->tooltip = L("Total filament volume extruded on the wipe tower.");

    def = this->add("used_filament", coFloat);
    def->label = L("Used filament");
    def->tooltip = L("Total length of filament used in the print.");

    def = this->add("print_time_total_sec", coString);
    def->label = L("Print time (total seconds)");
    def->tooltip = L("Total estimated print time in seconds. Replaced with actual value during post-processing.");

    def = this->add("print_time_day", coString);
    def->label = L("Print time (days component)");
    def->tooltip = L("Estimated print time day component (normal mode). Replaced with actual value during post-processing.");

    def = this->add("print_time_hour", coString);
    def->label = L("Print time (hours component)");
    def->tooltip = L("Estimated print time hour component (normal mode). Replaced with actual value during post-processing.");

    def = this->add("print_time_minute", coString);
    def->label = L("Print time (minutes component)");
    def->tooltip = L("Estimated print time minute component (normal mode). Replaced with actual value during post-processing.");

    def = this->add("print_time_sec", coString);
    def->label = L("Print time (seconds component)");
    def->tooltip = L("Estimated print time second component (normal mode). Replaced with actual value during post-processing.");

    def = this->add("used_filament_length", coString);
    def->label = L("Filament length (meters)");
    def->tooltip = L("Total filament length used in meters. Replaced with actual value during post-processing.");
}

ObjectsInfoConfigDef::ObjectsInfoConfigDef()
{
    ConfigOptionDef* def;

    def = this->add("num_objects", coInt);
    def->label = L("Number of objects");
    def->tooltip = L("Total number of objects in the print.");

    def = this->add("num_instances", coInt);
    def->label = L("Number of instances");
    def->tooltip = L("Total number of object instances in the print, summed over all objects.");

    def = this->add("scale", coStrings);
    def->label = L("Scale per object");
    def->tooltip = L("Contains a string with the information about what scaling was applied to the individual objects. "
                     "Indexing of the objects is zero-based (first object has index 0).\n"
                     "Example: 'x:100% y:50% z:100%'.");

    def = this->add("input_filename_base", coString);
    def->label = L("Input filename without extension");
    def->tooltip = L("Source filename of the first object, without extension.");

    new_def("input_filename", coString, L("Full input filename"), L("Source filename of the first object."));
    new_def("plate_name", coString, L("Plate name"), L("Name of the plate sliced."));
}

DimensionsConfigDef::DimensionsConfigDef()
{
    ConfigOptionDef* def;

    const std::string point_tooltip   = L("The vector has two elements: X and Y coordinate of the point. Values in mm.");
    const std::string bb_size_tooltip = L("The vector has two elements: X and Y dimension of the bounding box. Values in mm.");

    def = this->add("first_layer_print_convex_hull", coPoints);
    def->label = L("First layer convex hull");
    def->tooltip = L("Vector of points of the first layer convex hull. Each element has the following format:"
                     "'[x, y]' (x and y are floating-point numbers in mm).");

    def = this->add("first_layer_print_min", coFloats);
    def->label = L("Bottom-left corner of the first layer bounding box");
    def->tooltip = point_tooltip;

    def = this->add("first_layer_print_max", coFloats);
    def->label = L("Top-right corner of the first layer bounding box");
    def->tooltip = point_tooltip;

    def = this->add("first_layer_print_size", coFloats);
    def->label = L("Size of the first layer bounding box");
    def->tooltip = bb_size_tooltip;

    def = this->add("print_bed_min", coFloats);
    def->label = L("Bottom-left corner of print bed bounding box");
    def->tooltip = point_tooltip;

    def = this->add("print_bed_max", coFloats);
    def->label = L("Top-right corner of print bed bounding box");
    def->tooltip = point_tooltip;

    def = this->add("print_bed_size", coFloats);
    def->label = L("Size of the print bed bounding box");
    def->tooltip = bb_size_tooltip;

    new_def("first_layer_center_no_wipe_tower", coFloats, L("First layer center without wipe tower"), point_tooltip);
    new_def("first_layer_height", coFloat, L("First layer height"), L("Height of the first layer."));
}

TemperaturesConfigDef::TemperaturesConfigDef()
{
    ConfigOptionDef* def;

    new_def("bed_temperature", coInts, L("Bed temperature"), L("Vector of bed temperatures for each extruder/filament."))
    new_def("bed_temperature_initial_layer", coInts, L("First layer bed temperature"), L("Vector of first layer bed temperatures for each extruder/filament. Provides the same value as first_layer_bed_temperature."))
    new_def("bed_temperature_initial_layer_single", coInt, L("First layer bed temperature (initial extruder)"), L("First layer bed temperature for the initial extruder. Same as bed_temperature_initial_layer[initial_extruder]"))
    new_def("chamber_temperature", coInts, L("Chamber temperature"), L("Vector of chamber temperatures for each extruder/filament."))
    new_def("chamber_minimal_temperature", coInts, L("Chamber minimal temperature"), L("Vector of minimal chamber temperatures for each extruder/filament."))
    new_def("overall_chamber_temperature", coInt, L("Overall chamber temperature"), L("Overall chamber temperature. This value is the maximum chamber temperature of any extruder/filament used."))
    new_def("first_layer_bed_temperature", coInts, L("First layer bed temperature"), L("Vector of first layer bed temperatures for each extruder/filament. Provides the same value as bed_temperature_initial_layer."))
    new_def("first_layer_temperature", coInts, L("First layer temperature"), L("Vector of first layer temperatures for each extruder/filament."))
}

TimestampsConfigDef::TimestampsConfigDef()
{
    ConfigOptionDef* def;

    def = this->add("timestamp", coString);
    def->label = L("Timestamp");
    def->tooltip = L("String containing current time in yyyyMMdd-hhmmss format.");

    def = this->add("year", coInt);
    def->label = L("Year");

    def = this->add("month", coInt);
    def->label = L("Month");

    def = this->add("day", coInt);
    def->label = L("Day");

    def = this->add("hour", coInt);
    def->label = L("Hour");

    def = this->add("minute", coInt);
    def->label = L("Minute");

    def = this->add("second", coInt);
    def->label = L("Second");
}

OtherPresetsConfigDef::OtherPresetsConfigDef()
{
    ConfigOptionDef* def;

    def = this->add("print_preset", coString);
    def->label = L("Print preset name");
    def->tooltip = L("Name of the print preset used for slicing.");

    def = this->add("filament_preset", coString);
    def->label = L("Filament preset name");
    def->tooltip = L("Names of the filament presets used for slicing. The variable is a vector "
                     "containing one name for each extruder.");

    def = this->add("printer_preset", coString);
    def->label = L("Printer preset name");
    def->tooltip = L("Name of the printer preset used for slicing.");

    def = this->add("physical_printer_preset", coString);
    def->label = L("Physical printer name");
    def->tooltip = L("Name of the physical printer used for slicing.");
}


static std::map<t_custom_gcode_key, t_config_option_keys> s_CustomGcodeSpecificPlaceholders{
    // Machine G-code
    {"file_start_gcode",           {}},
    {"machine_start_gcode",         {}},
    {"machine_end_gcode",           {"layer_num", "layer_z", "max_layer_z", "filament_extruder_id"}},
    {"before_layer_change_gcode",   {"layer_num", "layer_z", "max_layer_z"}},
    {"layer_change_gcode",          {"layer_num", "layer_z", "max_layer_z"}},
    {"timelapse_gcode",             {"layer_num", "layer_z", "max_layer_z"}},
    {"change_filament_gcode",       {"layer_num", "layer_z", "max_layer_z", "next_extruder", "previous_extruder", "fan_speed",
                               "first_flush_volume", "flush_length_1", "flush_length_2", "flush_length_3", "flush_length_4",
                               "new_filament_e_feedrate", "new_filament_temp", "new_retract_length",
                               "new_retract_length_toolchange", "old_filament_e_feedrate", "old_filament_temp", "old_retract_length",
                               "old_retract_length_toolchange", "relative_e_axis", "second_flush_volume", "toolchange_count", "toolchange_z",
                               "travel_point_1_x", "travel_point_1_y", "travel_point_2_x", "travel_point_2_y", "travel_point_3_x",
                               "travel_point_3_y", "x_after_toolchange", "y_after_toolchange", "z_after_toolchange"}},
    {"change_extrusion_role_gcode", {"layer_num", "layer_z", "extrusion_role", "last_extrusion_role"}},
    {"filament_change_extrusion_role_gcode", {"layer_num", "layer_z", "extrusion_role", "last_extrusion_role"}},
    {"process_change_extrusion_role_gcode", {"layer_num", "layer_z", "extrusion_role", "last_extrusion_role"}},
    {"printing_by_object_gcode",    {}},
    {"machine_pause_gcode",         {}},
    {"template_custom_gcode",       {}},
    // Filament G-code
    {"filament_start_gcode",        {"filament_extruder_id"}},
    {"filament_end_gcode",          {"layer_num", "layer_z", "max_layer_z", "filament_extruder_id"}},
};

const std::map<t_custom_gcode_key, t_config_option_keys>& custom_gcode_specific_placeholders()
{
    return s_CustomGcodeSpecificPlaceholders;
}

CustomGcodeSpecificConfigDef::CustomGcodeSpecificConfigDef()
{
    ConfigOptionDef* def;

// Common Defs
    def = this->add("layer_num", coInt);
    def->label = L("Layer number");
    def->tooltip = L("Index of the current layer. Zero-based (i.e. first layer is number 0), except in extrusion role change G-code, where it is one-based.");

    def = this->add("layer_z", coFloat);
    def->label = L("Layer Z");
    def->tooltip = L("Height of the current layer above the print bed, measured to the top of the layer.");

    def = this->add("max_layer_z", coFloat);
    def->label = L("Maximal layer Z");
    def->tooltip = L("Height of the last layer above the print bed.");

    def = this->add("filament_extruder_id", coInt);
    def->label = L("Filament extruder ID");
    def->tooltip = L("The current extruder ID. The same as current_extruder.");

// change_filament_gcode
    new_def("previous_extruder", coInt, L("Previous extruder"), L("Index of the extruder that is being unloaded. The index is zero based (first extruder has index 0)."));
    new_def("next_extruder", coInt, L("Next extruder"), L("Index of the extruder that is being loaded. The index is zero based (first extruder has index 0)."));
    new_def("relative_e_axis", coBool, L("Relative e-axis"), L("Indicates if relative positioning is being used."));
    new_def("toolchange_count", coInt, L("Toolchange count"), L("The number of toolchanges throughout the print."));
    new_def("fan_speed", coNone, "", ""); //Option is no longer used and is zeroed by placeholder parser for compatability
    new_def("old_retract_length", coFloat, L("Old retract length"), L("The retraction length of the previous filament."));
    new_def("new_retract_length", coFloat, L("New retract length"), L("The retraction length of the new filament."));
    new_def("old_retract_length_toolchange", coFloat, L("Old retract length toolchange"), L("The toolchange retraction length of the previous filament."));
    new_def("new_retract_length_toolchange", coFloat, L("New retract length toolchange"), L("The toolchange retraction length of the new filament."));
    new_def("old_filament_temp", coInt, L("Old filament temp"), L("The old filament temp."));
    new_def("new_filament_temp", coInt, L("New filament temp"), L("The new filament temp."));
    new_def("x_after_toolchange", coFloat, L("X after toolchange"), L("The X pos after toolchange."));
    new_def("y_after_toolchange", coFloat, L("Y after toolchange"), L("The Y pos after toolchange."));
    new_def("z_after_toolchange", coFloat, L("Z after toolchange"), L("The Z pos after toolchange."));
    new_def("first_flush_volume", coFloat, L("First flush volume"), L("The first flush volume."));
    new_def("second_flush_volume", coFloat, L("Second flush volume"), L("The second flush volume."));
    new_def("old_filament_e_feedrate", coInt, L("Old filament e feedrate"), L("The old filament extruder feedrate."));
    new_def("new_filament_e_feedrate", coInt, L("New filament e feedrate"), L("The new filament extruder feedrate."));
    new_def("travel_point_1_x", coFloat, L("Travel point 1 X"), L("The travel point 1 X."));
    new_def("travel_point_1_y", coFloat, L("Travel point 1 Y"), L("The travel point 1 Y."));
    new_def("travel_point_2_x", coFloat, L("Travel point 2 X"), L("The travel point 2 X."));
    new_def("travel_point_2_y", coFloat, L("Travel point 2 Y"), L("The travel point 2 Y."));
    new_def("travel_point_3_x", coFloat, L("Travel point 3 X"), L("The travel point 3 X."));
    new_def("travel_point_3_y", coFloat, L("Travel point 3 Y"), L("The travel point 3 Y."));
    new_def("flush_length_1", coFloat, L("Flush Length 1"), L("The first flush length."));
    new_def("flush_length_2", coFloat, L("Flush Length 2"), L("The second flush length."));
    new_def("flush_length_3", coFloat, L("Flush Length 3"), L("The third flush length."));
    new_def("flush_length_4", coFloat, L("Flush Length 4"), L("The fourth flush length."));

// change_extrusion_role_gcode
    // The whole tooltip is one literal so that the GUI can look it up in the catalog.
    def = this->add("extrusion_role", coString);
    def->label = L("Extrusion role");
    // TRN The quoted values are G-code identifiers; keep them in English.
    def->tooltip = L("The new extrusion role/type that is going to be used\nPossible values:\n[\"Perimeter\", \"ExternalPerimeter\", "
                     "\"OverhangPerimeter\", \"InternalInfill\", \"SolidInfill\", \"TopSolidInfill\", \"BottomSurface\", \"BridgeInfill\", \"GapFill\", \"Ironing\", "
                     "\"Skirt\", \"Brim\", \"SupportMaterial\", \"SupportMaterialInterface\", \"SupportTransition\", \"WipeTower\", \"Mixed\"]");

    def = this->add("last_extrusion_role", coString);
    def->label = L("Last extrusion role");
    // TRN The quoted values are G-code identifiers; keep them in English.
    def->tooltip = L("The previously used extrusion role/type\nPossible values:\n[\"Perimeter\", \"ExternalPerimeter\", "
                     "\"OverhangPerimeter\", \"InternalInfill\", \"SolidInfill\", \"TopSolidInfill\", \"BottomSurface\", \"BridgeInfill\", \"GapFill\", \"Ironing\", "
                     "\"Skirt\", \"Brim\", \"SupportMaterial\", \"SupportMaterialInterface\", \"SupportTransition\", \"WipeTower\", \"Mixed\"]");
}

const CustomGcodeSpecificConfigDef custom_gcode_specific_config_def;

#undef new_def

uint64_t ModelConfig::s_last_timestamp = 1;

static Points to_points(const std::vector<Vec2d> &dpts)
{
    Points pts; pts.reserve(dpts.size());
    for (auto &v : dpts)
        pts.emplace_back( coord_t(scale_(v.x())), coord_t(scale_(v.y())) );
    return pts;
}

Polygon get_shared_poly(const std::vector<Pointfs>& extruder_polys)
{
    Polygon result;
    for (int index = 0; index < extruder_polys.size(); index++)
    {
        const Pointfs& extruder_area = extruder_polys[index];
        if (index == 0)
            result.points = to_points(extruder_area);
        else {
            Polygon extruer_poly;
            extruer_poly.points = to_points(extruder_area);
            Polygons result_polygon = intersection(extruer_poly, result);
            result = result_polygon[0];
        }
    }
    return result;
}
Points get_bed_shape(const DynamicPrintConfig &config, bool use_share)
{
    const ConfigOptionPoints *bed_shape_opt = config.opt<ConfigOptionPoints>("printable_area");
    if (!bed_shape_opt) {

        // Here, it is certain that the bed shape is missing, so an infinite one
        // has to be used, but still, the center of bed can be queried
        if (auto center_opt = config.opt<ConfigOptionPoint>("center"))
            return { scaled(center_opt->value) };

        return {};
    }

    Polygon bed_poly;
    if (use_share) {
        const ConfigOptionPointsGroups *extruder_area_opt = config.opt<ConfigOptionPointsGroups>("extruder_printable_area");
        if (extruder_area_opt && (extruder_area_opt->size() > 0)) {
            const std::vector<Pointfs>& extruder_areas = extruder_area_opt->values;
            bed_poly = get_shared_poly(extruder_areas);
        }
        else
            bed_poly.points = to_points(make_counter_clockwise(bed_shape_opt->values));
    }
    else
        bed_poly.points = to_points(make_counter_clockwise(bed_shape_opt->values));

    return bed_poly.points;
}

Points get_bed_shape(const PrintConfig &cfg, bool use_share)
{
    Polygon bed_poly;
    if (use_share) {
        const std::vector<Pointfs>& extruder_areas = cfg.extruder_printable_area.values;
        if (extruder_areas.size() > 0) {
            bed_poly = get_shared_poly(extruder_areas);
        }
        else
            bed_poly.points = to_points(make_counter_clockwise(cfg.printable_area.values));
    }
    else
        bed_poly.points = to_points(make_counter_clockwise(cfg.printable_area.values));

    return bed_poly.points;
}

Points get_bed_shape(const SLAPrinterConfig &cfg) { return to_points(make_counter_clockwise(cfg.printable_area.values)); }

Polygons get_bed_excluded_area(const PrintConfig& cfg)
{
    const Pointfs exclude_area_points = cfg.bed_exclude_area.values;

    // Belt printer: project exclusion zone points from the belt surface to machine-frame XY.
    // On the belt surface Z=0, so the in-plane axis foreshortens by cos(tilt).  The tilt
    // axis decides which bed axis foreshortens: tilt about X (belt along Y) scales Y,
    // tilt about Y (belt along X) scales X.  Derived from belt_slice_rotation.
    const bool is_belt = cfg.belt_printer.value;
    const auto tilt    = BeltTransformPipeline::physical_tilt(
        cfg.belt_slice_rotation.value, cfg.belt_slice_rotation_angle.value);
    const double cos_x = is_belt ? std::cos(Geometry::deg2rad(tilt.tilt_x_deg)) : 1.0; // foreshortens Y
    const double cos_y = is_belt ? std::cos(Geometry::deg2rad(tilt.tilt_y_deg)) : 1.0; // foreshortens X

    Polygon exclude_poly;
    for (int i = 0; i < exclude_area_points.size(); i++) {
        auto pt = exclude_area_points[i];
        double x = is_belt ? pt.x() * cos_y : pt.x();
        double y = is_belt ? pt.y() * cos_x : pt.y();
        exclude_poly.points.emplace_back(scale_(x), scale_(y));
    }

    exclude_poly.make_counter_clockwise();

    return {exclude_poly};
}

Polygon get_bed_shape_with_excluded_area(const PrintConfig& cfg, bool use_share)
{
    Polygon bed_poly;
    bed_poly.points = get_bed_shape(cfg, use_share);


    Polygons exclude_polys = get_bed_excluded_area(cfg);
    auto tmp = diff({ bed_poly }, exclude_polys);
    if (!tmp.empty()) bed_poly = tmp[0];
    return bed_poly;
}
bool has_skirt(const DynamicPrintConfig& cfg)
{
    auto opt_skirt_height = cfg.option("skirt_height");
    auto opt_skirt_loops = cfg.option("skirt_loops");
    auto opt_draft_shield = cfg.option("draft_shield");
    return (opt_skirt_height && opt_skirt_height->getInt() > 0 && opt_skirt_loops && opt_skirt_loops->getInt() > 0)
        || (opt_draft_shield && opt_draft_shield->getInt() != dsDisabled);
}
float get_real_skirt_dist(const DynamicPrintConfig& cfg) {
    if (!has_skirt(cfg)) return 0.f;

    float dist = cfg.opt_float("skirt_distance");

    int loops = cfg.opt_int("skirt_loops");
    auto opt_draft_shield = cfg.option("draft_shield");
    if (opt_draft_shield && opt_draft_shield->getInt() != dsDisabled && loops == 0) {
        loops = 1;
    }

    float width = cfg.opt_float("initial_layer_line_width");
    if (width <= 0.f) {
        width = cfg.opt_float("line_width");
    }
    if (width <= 0.f) {
        auto* nd = cfg.opt<ConfigOptionFloats>("nozzle_diameter");
        if (nd && !nd->values.empty()) {
            width = *std::max_element(nd->values.begin(), nd->values.end());
        } else {
            width = 0.4f;
        }
    }

    return dist + loops * width;
}

static bool is_XL_printer(const std::string& printer_notes)
{
    return boost::algorithm::contains(printer_notes, "PRINTER_VENDOR_PRUSA3D")
        && boost::algorithm::contains(printer_notes, "PRINTER_MODEL_XL");
}

bool is_XL_printer(const DynamicPrintConfig &cfg)
{
    auto *printer_notes = cfg.opt<ConfigOptionString>("printer_notes");
    return printer_notes && is_XL_printer(printer_notes->value);
}

bool is_XL_printer(const PrintConfig &cfg)
{
    return is_XL_printer(cfg.printer_notes.value);
}

} // namespace Slic3r