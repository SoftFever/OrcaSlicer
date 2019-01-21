#include "Print.hpp"

namespace Slic3r {

Flow PrintRegion::flow(FlowRole role, double layer_height, bool bridge, bool first_layer, double width, const PrintObject &object) const
{
    ConfigOptionFloatOrPercent config_width;
    if (width != -1) {
        // use the supplied custom width, if any
        config_width.value = width;
        config_width.percent = false;
    } else {
        // otherwise, get extrusion width from configuration
        // (might be an absolute value, or a percent value, or zero for auto)
        if (first_layer && m_print->config().first_layer_extrusion_width.value > 0) {
            config_width = m_print->config().first_layer_extrusion_width;
        } else if (role == frExternalPerimeter) {
            config_width = m_config.external_perimeter_extrusion_width;
        } else if (role == frPerimeter) {
            config_width = m_config.perimeter_extrusion_width;
        } else if (role == frInfill) {
            config_width = m_config.infill_extrusion_width;
        } else if (role == frSolidInfill) {
            config_width = m_config.solid_infill_extrusion_width;
        } else if (role == frTopSolidInfill) {
            config_width = m_config.top_infill_extrusion_width;
        } else {
            throw std::invalid_argument("Unknown role");
        }
    }
    if (config_width.value == 0) {
        config_width = object.config().extrusion_width;
    }
    
    // get the configured nozzle_diameter for the extruder associated
    // to the flow role requested
    size_t extruder = 0;    // 1-based
    if (role == frPerimeter || role == frExternalPerimeter) {
        extruder = m_config.perimeter_extruder;
    } else if (role == frInfill) {
        extruder = m_config.infill_extruder;
    } else if (role == frSolidInfill || role == frTopSolidInfill) {
        extruder = m_config.solid_infill_extruder;
    } else {
        throw std::invalid_argument("Unknown role");
    }
    double nozzle_diameter = m_print->config().nozzle_diameter.get_at(extruder-1);
    
    return Flow::new_from_config_width(role, config_width, nozzle_diameter, layer_height, bridge ? (float)m_config.bridge_flow_ratio : 0.0);
}

coordf_t PrintRegion::nozzle_dmr_avg(const PrintConfig &print_config) const
{
    return (print_config.nozzle_diameter.get_at(m_config.perimeter_extruder.value    - 1) + 
            print_config.nozzle_diameter.get_at(m_config.infill_extruder.value       - 1) + 
            print_config.nozzle_diameter.get_at(m_config.solid_infill_extruder.value - 1)) / 3.;
}

coordf_t PrintRegion::bridging_height_avg(const PrintConfig &print_config) const
{
    return this->nozzle_dmr_avg(print_config) * sqrt(m_config.bridge_flow_ratio.value);
}

void PrintRegion::collect_object_printing_extruders(const PrintConfig &print_config, const PrintRegionConfig &region_config, std::vector<unsigned int> &object_extruders)
{
    // These checks reflect the same logic used in the GUI for enabling/disabling extruder selection fields.
    if (region_config.perimeters.value > 0 || print_config.brim_width.value > 0)
        object_extruders.emplace_back(region_config.perimeter_extruder - 1);
    if (region_config.fill_density.value > 0)
        object_extruders.emplace_back(region_config.infill_extruder - 1);
    if (region_config.top_solid_layers.value > 0 || region_config.bottom_solid_layers.value > 0)
        object_extruders.emplace_back(region_config.solid_infill_extruder - 1);
}

void PrintRegion::collect_object_printing_extruders(std::vector<unsigned int> &object_extruders) const
{
    collect_object_printing_extruders(print()->config(), this->config(), object_extruders);
}

}
