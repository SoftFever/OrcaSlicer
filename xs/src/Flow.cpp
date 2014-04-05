#include "Flow.hpp"
#include <cmath>

namespace Slic3r {

Flow
Flow::new_from_config_width(FlowRole role, const ConfigOptionFloatOrPercent &width, float nozzle_diameter, float height, float bridge_flow_ratio) {
    // we need layer height unless it's a bridge
    if (height <= 0 && bridge_flow_ratio == 0) CONFESS("Invalid flow height supplied to new_from_config_width()");
    
    float w;
    if (!width.percent && width.value == 0) {
        w = Flow::_width(role, nozzle_diameter, height, bridge_flow_ratio);
    } else {
        w = width.get_abs_value(height);
    }
    
    Flow flow(w, Flow::_spacing(w, nozzle_diameter, height, bridge_flow_ratio), nozzle_diameter);
    if (bridge_flow_ratio > 0) flow.bridge = true;
    return flow;
}

Flow
Flow::new_from_spacing(float spacing, float nozzle_diameter, float height, bool bridge) {
    // we need layer height unless it's a bridge
    if (height <= 0 && !bridge) CONFESS("Invalid flow height supplied to new_from_spacing()");

    float w = Flow::_width_from_spacing(spacing, nozzle_diameter, height, bridge);
    Flow flow(w, spacing, nozzle_diameter);
    flow.bridge = bridge;
    return flow;
}

double
Flow::mm3_per_mm(float h) {
    if (this->bridge) {
        return (this->width * this->width) * PI/4.0;
    } else if (this->width >= (this->nozzle_diameter + h)) {
        // rectangle with semicircles at the ends
        return this->width * h + (h*h) / 4.0 * (PI-4.0);
    } else {
        // rectangle with shrunk semicircles at the ends
        return this->nozzle_diameter * h * (1 - PI/4.0) + h * this->width * PI/4.0;
    }
}

float
Flow::_width(FlowRole role, float nozzle_diameter, float height, float bridge_flow_ratio) {
    if (bridge_flow_ratio > 0) {
        return sqrt(bridge_flow_ratio * (nozzle_diameter*nozzle_diameter));
    }
    
    // here we calculate a sane default by matching the flow speed (at the nozzle) and the feed rate
    float volume = (nozzle_diameter*nozzle_diameter) * PI/4.0;
    float shape_threshold = nozzle_diameter * height + (height*height) * PI/4.0;
    float width;
    if (volume >= shape_threshold) {
        // rectangle with semicircles at the ends
        width = ((nozzle_diameter*nozzle_diameter) * PI + (height*height) * (4.0 - PI)) / (4.0 * height);
    } else {
        // rectangle with squished semicircles at the ends
        width = nozzle_diameter * (nozzle_diameter/height - 4.0/PI + 1);
    }
    
    float min = nozzle_diameter * 1.05;
    float max = -1;
    if (role == frPerimeter || role == frSupportMaterial) {
        min = max = nozzle_diameter;
    } else if (role != frInfill) {
        // do not limit width for sparse infill so that we use full native flow for it
        max = nozzle_diameter * 1.7;
    }
    if (max != -1 && width > max) width = max;
    if (width < min) width = min;
    
    return width;
}


float
Flow::_width_from_spacing(float spacing, float nozzle_diameter, float height, bool bridge) {
    if (bridge) {
        return spacing - BRIDGE_EXTRA_SPACING;
    }
    
    float w_threshold = height + nozzle_diameter;
    float s_threshold = w_threshold - OVERLAP_FACTOR * (w_threshold - (w_threshold - height * (1 - PI/4.0)));
    
    if (spacing >= s_threshold) {
        // rectangle with semicircles at the ends
        return spacing + OVERLAP_FACTOR * height * (1 - PI/4.0);
    } else {
        // rectangle with shrunk semicircles at the ends
        return (spacing + nozzle_diameter * OVERLAP_FACTOR * (PI/4.0 - 1)) / (1 + OVERLAP_FACTOR * (PI/4.0 - 1));
    }
}

float
Flow::_spacing(float width, float nozzle_diameter, float height, float bridge_flow_ratio) {
    if (bridge_flow_ratio > 0) {
        return width + BRIDGE_EXTRA_SPACING;
    }
    
    float min_flow_spacing;
    if (width >= (nozzle_diameter + height)) {
        // rectangle with semicircles at the ends
        min_flow_spacing = width - height * (1 - PI/4.0);
    } else {
        // rectangle with shrunk semicircles at the ends
        min_flow_spacing = nozzle_diameter * (1 - PI/4.0) + width * PI/4.0;
    }
    return width - OVERLAP_FACTOR * (width - min_flow_spacing);
}

}
