#include "Flow.hpp"
#include "Print.hpp"
#include <cmath>
#include <assert.h>

namespace Slic3r {

/* This constructor builds a Flow object from an extrusion width config setting
   and other context properties. */
Flow
Flow::new_from_config_width(FlowRole role, const ConfigOptionFloatOrPercent &width, float nozzle_diameter, float height, float bridge_flow_ratio) {
    // we need layer height unless it's a bridge
    if (height <= 0 && bridge_flow_ratio == 0) CONFESS("Invalid flow height supplied to new_from_config_width()");
    
    float w;
    if (bridge_flow_ratio > 0) {
        // if bridge flow was requested, calculate bridge width
        height = w = Flow::_bridge_width(nozzle_diameter, bridge_flow_ratio);
    } else if (!width.percent && width.value == 0) {
        // if user left option to 0, calculate a sane default width
        w = Flow::_auto_width(role, nozzle_diameter, height);
    } else {
        // if user set a manual value, use it
        w = width.get_abs_value(height);
    }
    
    return Flow(w, height, nozzle_diameter, bridge_flow_ratio > 0);
}

/* This constructor builds a Flow object from a given centerline spacing. */
Flow
Flow::new_from_spacing(float spacing, float nozzle_diameter, float height, bool bridge) {
    // we need layer height unless it's a bridge
    if (height <= 0 && !bridge) CONFESS("Invalid flow height supplied to new_from_spacing()");

    float w = Flow::_width_from_spacing(spacing, nozzle_diameter, height, bridge);
    if (bridge) height = w;
    return Flow(w, height, nozzle_diameter, bridge);
}

/* This method returns the centerline spacing between two adjacent extrusions 
   having the same extrusion width (and other properties). */
float
Flow::spacing() const 
{
#ifdef HAS_PERIMETER_LINE_OVERLAP
    if (this->bridge)
        return this->width + BRIDGE_EXTRA_SPACING;
    // rectangle with semicircles at the ends
    float min_flow_spacing = this->width - this->height * (1 - PI/4.0);
    return this->width - PERIMETER_LINE_OVERLAP_FACTOR * (this->width - min_flow_spacing);
#else
    return this->bridge ? (this->width + BRIDGE_EXTRA_SPACING) : (this->width - this->height * (1 - PI/4.0));
#endif
}

/* This method returns the centerline spacing between an extrusion using this
   flow and another one using another flow.
   this->spacing(other) shall return the same value as other.spacing(*this) */
float
Flow::spacing(const Flow &other) const {
    assert(this->height == other.height);
    assert(this->bridge == other.bridge);
    return this->bridge ? 
        0.5f * this->width + 0.5f * other.width + BRIDGE_EXTRA_SPACING :
        0.5f * this->spacing() + 0.5f * other.spacing();
}

/* This method returns extrusion volume per head move unit. */
double Flow::mm3_per_mm() const 
{
    return this->bridge ?
        (this->width * this->width) * PI/4.0 :
        this->width * this->height + (this->height * this->height) / 4.0 * (PI-4.0);
}

/* This static method returns bridge width for a given nozzle diameter. */
float Flow::_bridge_width(float nozzle_diameter, float bridge_flow_ratio) {
    return (bridge_flow_ratio == 1.) ?
        // optimization to avoid sqrt()
        nozzle_diameter :
        sqrt(bridge_flow_ratio) * nozzle_diameter;
}

/* This static method returns a sane extrusion width default. */
float Flow::_auto_width(FlowRole role, float nozzle_diameter, float height) {
    // here we calculate a sane default by matching the flow speed (at the nozzle) and the feed rate
    // shape: rectangle with semicircles at the ends
    float width = ((nozzle_diameter*nozzle_diameter) * PI + (height*height) * (4.0 - PI)) / (4.0 * height);
    
    float min = nozzle_diameter * 1.05;
    float max = -1;
    if (role == frExternalPerimeter || role == frSupportMaterial || role == frSupportMaterialInterface) {
        min = max = nozzle_diameter;
    } else if (role != frInfill) {
        // do not limit width for sparse infill so that we use full native flow for it
        max = nozzle_diameter * 1.7;
    }
    if (max != -1 && width > max) width = max;
    if (width < min) width = min;
    
    return width;
}

/* This static method returns the extrusion width value corresponding to the supplied centerline spacing. */
float Flow::_width_from_spacing(float spacing, float nozzle_diameter, float height, bool bridge) 
{
    return bridge ? 
        (spacing - BRIDGE_EXTRA_SPACING) : 
#ifdef HAS_PERIMETER_LINE_OVERLAP
        (spacing + PERIMETER_LINE_OVERLAP_FACTOR * height * (1 - PI/4.0));
#else
        (spacing + height * (1 - PI/4.0));
#endif
}

Flow support_material_flow(const PrintObject *object, float layer_height)
{
    return Flow::new_from_config_width(
        frSupportMaterial,
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        (object->config.support_material_extrusion_width.value > 0) ? object->config.support_material_extrusion_width : object->config.extrusion_width,
        // if object->config.support_material_extruder == 0 (which means to not trigger tool change, but use the current extruder instead), get_at will return the 0th component.
        float(object->print()->config.nozzle_diameter.get_at(object->config.support_material_extruder-1)),
        (layer_height > 0.f) ? layer_height : float(object->config.layer_height.value),
        false);
}

Flow support_material_1st_layer_flow(const PrintObject *object, float layer_height)
{
    return Flow::new_from_config_width(
        frSupportMaterial,
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        (object->print()->config.first_layer_extrusion_width.value > 0) ? object->print()->config.first_layer_extrusion_width : object->config.support_material_extrusion_width,
        float(object->print()->config.nozzle_diameter.get_at(object->config.support_material_extruder-1)),
        (layer_height > 0.f) ? layer_height : object->config.first_layer_height.get_abs_value(object->config.layer_height.value),
        false);
}

Flow support_material_interface_flow(const PrintObject *object, float layer_height)
{
    return Flow::new_from_config_width(
        frSupportMaterialInterface,
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        (object->config.support_material_extrusion_width > 0) ? object->config.support_material_extrusion_width : object->config.extrusion_width,
        // if object->config.support_material_interface_extruder == 0 (which means to not trigger tool change, but use the current extruder instead), get_at will return the 0th component.
        float(object->print()->config.nozzle_diameter.get_at(object->config.support_material_interface_extruder-1)),
        (layer_height > 0.f) ? layer_height : float(object->config.layer_height.value),
        false);
}

}
