//#undef NDEBUGc
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
        sprintf(buf, "will always run at %d% ", min_fan_speed);
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

std::string PresetHints::maximum_volumetric_flow_description(const PresetBundle &preset_bundle)
{
    // Find out, to which nozzle index is the current filament profile assigned.
    unsigned int idx_nozzle = 0;
    for (; idx_nozzle < (unsigned int)preset_bundle.filaments.size(); ++ idx_nozzle)
        if (preset_bundle.filament_presets[idx_nozzle] == preset_bundle.filaments.get_selected_preset().name)
            break;
    if (idx_nozzle == (unsigned int)preset_bundle.filaments.size())
        // The current filament preset is not active for any extruder.
        idx_nozzle = (unsigned int)-1;

    const DynamicPrintConfig &print_config    = preset_bundle.prints   .get_edited_preset().config;
    const DynamicPrintConfig &filament_config = preset_bundle.filaments.get_edited_preset().config;
    const DynamicPrintConfig &printer_config  = preset_bundle.printers .get_edited_preset().config;

    // Current printer values.
    double nozzle_diameter                  = printer_config.opt_float("nozzle_diameter", idx_nozzle);

    // Print config values
    double layer_height                     = print_config.get_abs_value("layer_height", nozzle_diameter);
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

    const auto &extrusion_width                     = print_config.get_abs_value("extrusion_width");
    const auto &support_material_extrusion_width    = *print_config.option<ConfigOptionFloatOrPercent>("support_material_extrusion_width");
    const auto &external_perimeter_extrusion_width  = *print_config.option<ConfigOptionFloatOrPercent>("external_perimeter_extrusion_width");
    const auto &infill_extrusion_width              = *print_config.option<ConfigOptionFloatOrPercent>("infill_extrusion_width");
    const auto &solid_infill_extrusion_width        = *print_config.option<ConfigOptionFloatOrPercent>("solid_infill_extrusion_width");
    const auto &perimeter_extrusion_width           = *print_config.option<ConfigOptionFloatOrPercent>("perimeter_extrusion_width");
    const auto &top_infill_extrusion_width          = *print_config.option<ConfigOptionFloatOrPercent>("top_infill_extrusion_width");
    const auto &first_layer_extrusion_width         = *print_config.option<ConfigOptionFloatOrPercent>("first_layer_extrusion_width");

    int perimeter_extruder                  = print_config.opt_int("perimeter_extruder");
    int infill_extruder                     = print_config.opt_int("infill_extruder");
    int solid_infill_extruder               = print_config.opt_int("solid_infill_extruder");
    int support_material_extruder           = print_config.opt_int("support_material_extruder");
    int support_material_interface_extruder = print_config.opt_int("support_material_interface_extruder");

    // Current filament values
    double filament_diameter                = filament_config.opt_float("filament_diameter", 0);
    double extrusion_multiplier             = filament_config.opt_float("extrusion_multiplier", 0);
    double filament_max_volumetric_speed    = filament_config.opt_float("filament_max_volumetric_speed", 0);


    auto external_perimeter_flow = Flow::new_from_config_width(frExternalPerimeter, external_perimeter_extrusion_width, (float)nozzle_diameter, (float)layer_height, 0);
    auto perimeter_flow = Flow::new_from_config_width(frPerimeter, perimeter_extrusion_width, (float)nozzle_diameter, (float)layer_height, 0);
    auto infill_flow = Flow::new_from_config_width(frInfill, infill_extrusion_width, (float)nozzle_diameter, (float)layer_height, 0);
    auto solid_infill_flow = Flow::new_from_config_width(frInfill, solid_infill_extrusion_width, (float)nozzle_diameter, (float)layer_height, 0);
    auto top_solid_infill_flow = Flow::new_from_config_width(frInfill, top_infill_extrusion_width, (float)nozzle_diameter, (float)layer_height, 0);
//    auto support_material_flow = Flow::new_from_config_width(frSupportMaterial, , 
//        (float)nozzle_diameter, (float)layer_height, 0);
    auto support_material_interface_flow = Flow::new_from_config_width(frSupportMaterialInterface, *print_config.option<ConfigOptionFloatOrPercent>("support_material_extrusion_width"), 
        (float)nozzle_diameter, (float)layer_height, 0);

    std::string out;
    out="Hu";
	return out;
}

#if 0
static create_flow(FlowRole role, ConfigOptionFloatOrPercent &width, double layer_height, bool bridge, bool first_layer, double width) const
{
    ConfigOptionFloatOrPercent config_width;
    if (width != -1) {
        // use the supplied custom width, if any
        config_width.value = width;
        config_width.percent = false;
    } else {
        // otherwise, get extrusion width from configuration
        // (might be an absolute value, or a percent value, or zero for auto)
        if (first_layer && this->_print->config.first_layer_extrusion_width.value > 0) {
            config_width = this->_print->config.first_layer_extrusion_width;
        } else if (role == frExternalPerimeter) {
            config_width = this->config.external_perimeter_extrusion_width;
        } else if (role == frPerimeter) {
            config_width = this->config.perimeter_extrusion_width;
        } else if (role == frInfill) {
            config_width = this->config.infill_extrusion_width;
        } else if (role == frSolidInfill) {
            config_width = this->config.solid_infill_extrusion_width;
        } else if (role == frTopSolidInfill) {
            config_width = this->config.top_infill_extrusion_width;
        } else {
            CONFESS("Unknown role");
        }
    }
    if (config_width.value == 0) {
        config_width = object.config.extrusion_width;
    }
    
    // get the configured nozzle_diameter for the extruder associated
    // to the flow role requested
    size_t extruder = 0;    // 1-based
    if (role == frPerimeter || role == frExternalPerimeter) {
        extruder = this->config.perimeter_extruder;
    } else if (role == frInfill) {
        extruder = this->config.infill_extruder;
    } else if (role == frSolidInfill || role == frTopSolidInfill) {
        extruder = this->config.solid_infill_extruder;
    } else {
        CONFESS("Unknown role $role");
    }
    double nozzle_diameter = this->_print->config.nozzle_diameter.get_at(extruder-1);
    
    return Flow::new_from_config_width(role, config_width, nozzle_diameter, layer_height, bridge ? (float)this->config.bridge_flow_ratio : 0.0);
}

        if (first_layer && this->_print->config.first_layer_extrusion_width.value > 0) {
            config_width = this->_print->config.first_layer_extrusion_width;
    auto flow = Flow::new_from_config_width(frExternalPerimeter, const ConfigOptionFloatOrPercent &width, float nozzle_diameter, float height, float bridge_flow_ratio);



Flow Print::skirt_flow() const
{
    ConfigOptionFloatOrPercent width = this->config.first_layer_extrusion_width;
    if (width.value == 0) width = this->regions.front()->config.perimeter_extrusion_width;
    
    /* We currently use a random object's support material extruder.
       While this works for most cases, we should probably consider all of the support material
       extruders and take the one with, say, the smallest index;
       The same logic should be applied to the code that selects the extruder during G-code
       generation as well. */
    return Flow::new_from_config_width(
        frPerimeter,
        width, 
        this->config.nozzle_diameter.get_at(this->objects.front()->config.support_material_extruder-1),
        this->skirt_first_layer_height(),
        0
    );
}

Flow Print::brim_flow() const
{
    ConfigOptionFloatOrPercent width = this->config.first_layer_extrusion_width;
    if (width.value == 0) width = this->regions.front()->config.perimeter_extrusion_width;
    
    /* We currently use a random region's perimeter extruder.
       While this works for most cases, we should probably consider all of the perimeter
       extruders and take the one with, say, the smallest index.
       The same logic should be applied to the code that selects the extruder during G-code
       generation as well. */
    return Flow::new_from_config_width(
        frPerimeter,
        width, 
        this->config.nozzle_diameter.get_at(this->regions.front()->config.perimeter_extruder-1),
        this->skirt_first_layer_height(),
        0
    );
}

Flow support_material_flow(const PrintObject *object, float layer_height)
{
    return Flow::new_from_config_width(
        frSupportMaterial,
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        (support_material_extrusion_width.value > 0) ? support_material_extrusion_width : extrusion_width,
        // if object->config.support_material_extruder == 0 (which means to not trigger tool change, but use the current extruder instead), get_at will return the 0th component.
        float(nozzle_diameter.get_at(support_material_extruder-1)),
        (layer_height > 0.f) ? layer_height : float(layer_height.value),
        false);
}

Flow support_material_1st_layer_flow(const PrintObject *object, float layer_height)
{
    return Flow::new_from_config_width(
        frSupportMaterial,
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        (first_layer_extrusion_width.value > 0) ? first_layer_extrusion_width : support_material_extrusion_width,
        float(nozzle_diameter.get_at(object->config.support_material_extruder-1)),
        (layer_height > 0.f) ? layer_height : float(first_layer_height)),
        false);
}

Flow support_material_interface_flow(const PrintObject *object, float layer_height)
{
    return Flow::new_from_config_width(
        frSupportMaterialInterface,
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        (support_material_extrusion_width > 0) ? support_material_extrusion_width : extrusion_width,
        // if object->config.support_material_interface_extruder == 0 (which means to not trigger tool change, but use the current extruder instead), get_at will return the 0th component.
        float(nozzle_diameter.get_at(object->config.support_material_interface_extruder-1)),
        layer_height,
        false);
}
#endif

}; // namespace Slic3r
