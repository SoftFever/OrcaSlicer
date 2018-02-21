//#undef NDEBUG
#include <cassert>

#include "PresetBundle.hpp"
#include "PresetHints.hpp"
#include "Flow.hpp"

#include <boost/algorithm/string/predicate.hpp>

#include "../../libslic3r/libslic3r.h"

namespace Slic3r {

std::string PresetHints::cooling_description(const Preset &preset)
{
	std::string out;
	char buf[4096];
	if (preset.config.opt_bool("cooling", 0)) {
		int 	slowdown_below_layer_time 	= preset.config.opt_int("slowdown_below_layer_time", 0);
		int 	min_fan_speed 				= preset.config.opt_int("min_fan_speed", 0);
		int 	max_fan_speed 				= preset.config.opt_int("max_fan_speed", 0);
		int 	min_print_speed				= int(preset.config.opt_float("min_print_speed", 0) + 0.5);
		int 	fan_below_layer_time		= preset.config.opt_int("fan_below_layer_time", 0);
		sprintf(buf, "If estimated layer time is below ~%ds, fan will run at %d%% and print speed will be reduced so that no less than %ds are spent on that layer (however, speed will never be reduced below %dmm/s).",
            slowdown_below_layer_time, max_fan_speed, slowdown_below_layer_time, min_print_speed);
		out += buf;
        if (fan_below_layer_time > slowdown_below_layer_time) {
            sprintf(buf, "\nIf estimated layer time is greater, but still below ~%ds, fan will run at a proportionally decreasing speed between %d%% and %d%%.",
                fan_below_layer_time, max_fan_speed, min_fan_speed);
            out += buf;
        }
        out += "\nDuring the other layers, fan ";
    } else {
        out = "Fan ";
    }
	if (preset.config.opt_bool("fan_always_on", 0)) {
		int 	disable_fan_first_layers 	= preset.config.opt_int("disable_fan_first_layers", 0);
		int 	min_fan_speed 				= preset.config.opt_int("min_fan_speed", 0);
        sprintf(buf, "will always run at %d%% ", min_fan_speed);
        out += buf;
        if (disable_fan_first_layers > 1) {
        	sprintf(buf, "except for the first %d layers", disable_fan_first_layers);
	        out += buf;
        }
        else if (disable_fan_first_layers == 1)
        	out += "except for the first layer";
    } else
    	out += "will be turned off.";

    return out;
}

static const ConfigOptionFloatOrPercent& first_positive(const ConfigOptionFloatOrPercent *v1, const ConfigOptionFloatOrPercent &v2, const ConfigOptionFloatOrPercent &v3)
{
    return (v1 != nullptr && v1->value > 0) ? *v1 : ((v2.value > 0) ? v2 : v3);
}

std::string PresetHints::maximum_volumetric_flow_description(const PresetBundle &preset_bundle)
{
    // Find out, to which nozzle index is the current filament profile assigned.
    int idx_extruder  = 0;
	int num_extruders = (int)preset_bundle.filament_presets.size();
    for (; idx_extruder < num_extruders; ++ idx_extruder)
        if (preset_bundle.filament_presets[idx_extruder] == preset_bundle.filaments.get_selected_preset().name)
            break;
    if (idx_extruder == num_extruders)
        // The current filament preset is not active for any extruder.
        idx_extruder = -1;

    const DynamicPrintConfig &print_config    = preset_bundle.prints   .get_edited_preset().config;
    const DynamicPrintConfig &filament_config = preset_bundle.filaments.get_edited_preset().config;
    const DynamicPrintConfig &printer_config  = preset_bundle.printers .get_edited_preset().config;

    // Current printer values.
    float  nozzle_diameter                  = (float)printer_config.opt_float("nozzle_diameter", idx_extruder);

    // Print config values
    double layer_height                     = print_config.opt_float("layer_height");
    double first_layer_height               = print_config.get_abs_value("first_layer_height", layer_height);
    double support_material_speed           = print_config.opt_float("support_material_speed");
    double support_material_interface_speed = print_config.get_abs_value("support_material_interface_speed", support_material_speed);
    double bridge_speed                     = print_config.opt_float("bridge_speed");
    double bridge_flow_ratio                = print_config.opt_float("bridge_flow_ratio");
    double perimeter_speed                  = print_config.opt_float("perimeter_speed");
    double external_perimeter_speed         = print_config.get_abs_value("external_perimeter_speed", perimeter_speed);
    double gap_fill_speed                   = print_config.opt_float("gap_fill_speed");
    double infill_speed                     = print_config.opt_float("infill_speed");
    double small_perimeter_speed            = print_config.get_abs_value("small_perimeter_speed", perimeter_speed);
    double solid_infill_speed               = print_config.get_abs_value("solid_infill_speed", infill_speed);
    double top_solid_infill_speed           = print_config.get_abs_value("top_solid_infill_speed", solid_infill_speed);
    // Maximum print speed when auto-speed is enabled by setting any of the above speed values to zero.
    double max_print_speed                  = print_config.opt_float("max_print_speed");
    // Maximum volumetric speed allowed for the print profile.
    double max_volumetric_speed             = print_config.opt_float("max_volumetric_speed");

    const auto &extrusion_width                     = *print_config.option<ConfigOptionFloatOrPercent>("extrusion_width");
    const auto &external_perimeter_extrusion_width  = *print_config.option<ConfigOptionFloatOrPercent>("external_perimeter_extrusion_width");
    const auto &first_layer_extrusion_width         = *print_config.option<ConfigOptionFloatOrPercent>("first_layer_extrusion_width");
    const auto &infill_extrusion_width              = *print_config.option<ConfigOptionFloatOrPercent>("infill_extrusion_width");
    const auto &perimeter_extrusion_width           = *print_config.option<ConfigOptionFloatOrPercent>("perimeter_extrusion_width");
    const auto &solid_infill_extrusion_width        = *print_config.option<ConfigOptionFloatOrPercent>("solid_infill_extrusion_width");
    const auto &support_material_extrusion_width    = *print_config.option<ConfigOptionFloatOrPercent>("support_material_extrusion_width");
    const auto &top_infill_extrusion_width          = *print_config.option<ConfigOptionFloatOrPercent>("top_infill_extrusion_width");
    const auto &first_layer_speed                   = *print_config.option<ConfigOptionFloatOrPercent>("first_layer_speed");

    // Index of an extruder assigned to a feature. If set to 0, an active extruder will be used for a multi-material print.
    // If different from idx_extruder, it will not be taken into account for this hint.
    auto feature_extruder_active = [idx_extruder, num_extruders](int i) {
        return i <= 0 || i > num_extruders || idx_extruder == -1 || idx_extruder == i - 1;
    };
    bool perimeter_extruder_active                  = feature_extruder_active(print_config.opt_int("perimeter_extruder"));
    bool infill_extruder_active                     = feature_extruder_active(print_config.opt_int("infill_extruder"));
    bool solid_infill_extruder_active               = feature_extruder_active(print_config.opt_int("solid_infill_extruder"));
    bool support_material_extruder_active           = feature_extruder_active(print_config.opt_int("support_material_extruder"));
    bool support_material_interface_extruder_active = feature_extruder_active(print_config.opt_int("support_material_interface_extruder"));

    // Current filament values
    double filament_diameter                = filament_config.opt_float("filament_diameter", 0);
    double filament_crossection             = M_PI * 0.25 * filament_diameter * filament_diameter;
    double extrusion_multiplier             = filament_config.opt_float("extrusion_multiplier", 0);
    // The following value will be annotated by this hint, so it does not take part in the calculation.
//    double filament_max_volumetric_speed    = filament_config.opt_float("filament_max_volumetric_speed", 0);

    std::string out;
    for (size_t idx_type = (first_layer_extrusion_width.value == 0) ? 1 : 0; idx_type < 3; ++ idx_type) {
        // First test the maximum volumetric extrusion speed for non-bridging extrusions.
        bool first_layer = idx_type == 0;
        bool bridging    = idx_type == 2;
		const ConfigOptionFloatOrPercent *first_layer_extrusion_width_ptr = (first_layer && first_layer_extrusion_width.value > 0) ?
			&first_layer_extrusion_width : nullptr;
        const float                       lh  = float(first_layer ? first_layer_height : layer_height);
        const float                       bfr = bridging ? bridge_flow_ratio : 0.f;
        double                            max_flow = 0.;
        std::string                       max_flow_extrusion_type;
        auto                              limit_by_first_layer_speed = [&first_layer_speed, first_layer](double speed_normal, double speed_max) {
            if (first_layer && first_layer_speed.value > 0)
                // Apply the first layer limit.
                speed_normal = first_layer_speed.get_abs_value(speed_normal);
            return (speed_normal > 0.) ? speed_normal : speed_max;
        };
        if (perimeter_extruder_active) {
            double external_perimeter_rate = Flow::new_from_config_width(frExternalPerimeter, 
                first_positive(first_layer_extrusion_width_ptr, external_perimeter_extrusion_width, extrusion_width), 
                nozzle_diameter, lh, bfr).mm3_per_mm() *
                (bridging ? bridge_speed : 
                    limit_by_first_layer_speed(std::max(external_perimeter_speed, small_perimeter_speed), max_print_speed));
            if (max_flow < external_perimeter_rate) {
                max_flow = external_perimeter_rate;
                max_flow_extrusion_type = "external perimeters";
            }
            double perimeter_rate = Flow::new_from_config_width(frPerimeter, 
                first_positive(first_layer_extrusion_width_ptr, perimeter_extrusion_width, extrusion_width), 
                nozzle_diameter, lh, bfr).mm3_per_mm() *
                (bridging ? bridge_speed :
                    limit_by_first_layer_speed(std::max(perimeter_speed, small_perimeter_speed), max_print_speed));
            if (max_flow < perimeter_rate) {
                max_flow = perimeter_rate;
                max_flow_extrusion_type = "perimeters";
            }
        }
        if (! bridging && infill_extruder_active) {
            double infill_rate = Flow::new_from_config_width(frInfill, 
                first_positive(first_layer_extrusion_width_ptr, infill_extrusion_width, extrusion_width), 
                nozzle_diameter, lh, bfr).mm3_per_mm() * limit_by_first_layer_speed(infill_speed, max_print_speed);
            if (max_flow < infill_rate) {
                max_flow = infill_rate;
                max_flow_extrusion_type = "infill";
            }
        }
        if (solid_infill_extruder_active) {
            double solid_infill_rate = Flow::new_from_config_width(frInfill, 
                first_positive(first_layer_extrusion_width_ptr, solid_infill_extrusion_width, extrusion_width), 
                nozzle_diameter, lh, 0).mm3_per_mm() *
                (bridging ? bridge_speed : limit_by_first_layer_speed(solid_infill_speed, max_print_speed));
            if (max_flow < solid_infill_rate) {
                max_flow = solid_infill_rate;
                max_flow_extrusion_type = "solid infill";
            }
            if (! bridging) {
                double top_solid_infill_rate = Flow::new_from_config_width(frInfill, 
                    first_positive(first_layer_extrusion_width_ptr, top_infill_extrusion_width, extrusion_width), 
                    nozzle_diameter, lh, bfr).mm3_per_mm() * limit_by_first_layer_speed(top_solid_infill_speed, max_print_speed);
                if (max_flow < top_solid_infill_rate) {
                    max_flow = top_solid_infill_rate;
                    max_flow_extrusion_type = "top solid infill";
                }
            }
        }
        if (support_material_extruder_active) {
            double support_material_rate = Flow::new_from_config_width(frSupportMaterial,
                first_positive(first_layer_extrusion_width_ptr, support_material_extrusion_width, extrusion_width), 
                nozzle_diameter, lh, bfr).mm3_per_mm() *
                (bridging ? bridge_speed : limit_by_first_layer_speed(support_material_speed, max_print_speed));
            if (max_flow < support_material_rate) {
                max_flow = support_material_rate;
                max_flow_extrusion_type = "support";
            }
        }
        if (support_material_interface_extruder_active) {
            double support_material_interface_rate = Flow::new_from_config_width(frSupportMaterialInterface,
                first_positive(first_layer_extrusion_width_ptr, support_material_extrusion_width, extrusion_width),
                nozzle_diameter, lh, bfr).mm3_per_mm() *
                (bridging ? bridge_speed : limit_by_first_layer_speed(support_material_interface_speed, max_print_speed));
            if (max_flow < support_material_interface_rate) {
                max_flow = support_material_interface_rate;
                max_flow_extrusion_type = "support interface";
            }
        }
        //FIXME handle gap_fill_speed
        if (! out.empty())
            out += "\n";
        out += (first_layer ? "First layer volumetric" : (bridging ? "Bridging volumetric" : "Volumetric"));
        out += " flow rate is maximized ";
        bool limited_by_max_volumetric_speed = max_volumetric_speed > 0 && max_volumetric_speed < max_flow;
        out += (limited_by_max_volumetric_speed ? 
            "by the print profile maximum" : 
            ("when printing " + max_flow_extrusion_type))
            + " with a volumetric rate ";
        if (limited_by_max_volumetric_speed)
            max_flow = max_volumetric_speed;
        char buf[2048];
        sprintf(buf, "%3.2f mm³/s", max_flow);
        out += buf;
        sprintf(buf, " at filament speed %3.2f mm/s.", max_flow / filament_crossection);
        out += buf;
    }

 	return out;
}

std::string PresetHints::recommended_thin_wall_thickness(const PresetBundle &preset_bundle)
{
    const DynamicPrintConfig &print_config    = preset_bundle.prints   .get_edited_preset().config;
    const DynamicPrintConfig &printer_config  = preset_bundle.printers .get_edited_preset().config;

    float   layer_height                        = float(print_config.opt_float("layer_height"));
    int     num_perimeters                      = print_config.opt_int("perimeters");
    bool    thin_walls                          = print_config.opt_bool("thin_walls");
    float   nozzle_diameter                     = float(printer_config.opt_float("nozzle_diameter", 0));
    
    if (layer_height <= 0.f)
        return "Recommended object thin wall thickness: Not available due to invalid layer height.";

    Flow    external_perimeter_flow             = Flow::new_from_config_width(
        frExternalPerimeter, 
        *print_config.opt<ConfigOptionFloatOrPercent>("external_perimeter_extrusion_width"), 
        nozzle_diameter, layer_height, false);
    Flow    perimeter_flow                      = Flow::new_from_config_width(
        frPerimeter, 
        *print_config.opt<ConfigOptionFloatOrPercent>("perimeter_extrusion_width"), 
        nozzle_diameter, layer_height, false);

    std::string out;
    if (num_perimeters > 0) {
        int num_lines = std::min(num_perimeters * 2, 10);
        char buf[256];
        sprintf(buf, "Recommended object thin wall thickness for layer height %.2f and ", layer_height);
        out += buf;
        // Start with the width of two closely spaced 
        double width = external_perimeter_flow.width + external_perimeter_flow.spacing();
        for (int i = 2; i <= num_lines; thin_walls ? ++ i : i += 2) {
            if (i > 2)
                out += ", ";
            sprintf(buf, "%d lines: %.2lf mm", i, width);
            out += buf;
            width += perimeter_flow.spacing() * (thin_walls ? 1.f : 2.f);
        }
    }
    return out;
}

}; // namespace Slic3r
