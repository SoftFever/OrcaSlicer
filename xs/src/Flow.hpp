#ifndef slic3r_Flow_hpp_
#define slic3r_Flow_hpp_

#include <myinit.h>
#include "Config.hpp"
#include "ExtrusionEntity.hpp"

namespace Slic3r {

#define BRIDGE_EXTRA_SPACING 0.05
#define OVERLAP_FACTOR 1.0

enum FlowRole {
    frPerimeter,
    frInfill,
    frSolidInfill,
    frTopSolidInfill,
    frSupportMaterial,
    frSupportMaterialInterface,
};

class Flow
{
    public:
    float width;
    float spacing;
    float nozzle_diameter;
    bool bridge;
    coord_t scaled_width;
    coord_t scaled_spacing;
    
    Flow(float _w, float _s, float _nd): width(_w), spacing(_s), nozzle_diameter(_nd), bridge(false) {
        this->scaled_width   = scale_(this->width);
        this->scaled_spacing = scale_(this->spacing);
    };
    double mm3_per_mm(float h);
    static Flow new_from_config_width(FlowRole role, const ConfigOptionFloatOrPercent &width, float nozzle_diameter, float height, float bridge_flow_ratio);
    static Flow new_from_spacing(float spacing, float nozzle_diameter, float height, bool bridge);
    
    private:
    static float _width(FlowRole role, float nozzle_diameter, float height, float bridge_flow_ratio);
    static float _width_from_spacing(float spacing, float nozzle_diameter, float height, bool bridge);
    static float _spacing(float width, float nozzle_diameter, float height, float bridge_flow_ratio);
};

}

#endif
