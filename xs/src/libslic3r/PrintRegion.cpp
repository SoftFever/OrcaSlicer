#include "Print.hpp"

namespace Slic3r {

Flow
PrintRegion::flow(FlowRole role, double layer_height, bool bridge, bool first_layer, double width, const PrintObject &object) const
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

coordf_t PrintRegion::nozzle_dmr_avg(const PrintConfig &print_config) const
{
    return (print_config.nozzle_diameter.get_at(this->config.perimeter_extruder.value    - 1) + 
            print_config.nozzle_diameter.get_at(this->config.infill_extruder.value       - 1) + 
            print_config.nozzle_diameter.get_at(this->config.solid_infill_extruder.value - 1)) / 3.;
}

coordf_t PrintRegion::bridging_height_avg(const PrintConfig &print_config) const
{
    return this->nozzle_dmr_avg(print_config) * sqrt(this->config.bridge_flow_ratio.value);
}

}
