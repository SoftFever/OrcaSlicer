#ifndef slic3r_InternalBridgeDetector_hpp_
#define slic3r_InternalBridgeDetector_hpp_

#include "libslic3r.h"
#include "ExPolygon.hpp"
#include <string>

namespace Slic3r {

// BBS: InternalBridgeDetector is used to detect bridge angle for internal bridge.
// this step may enlarge internal bridge area for a little(only occupy sparse infill area) for better anchoring
class InternalBridgeDetector {
public:
    // input: all fill area in LayerRegion without overlap with perimeter.
    ExPolygons                   fill_no_overlap;
    // input: internal bridge infill area.
    ExPolygons                    internal_bridge_infill;
    // input: scaled extrusion width of the infill.
    coord_t                      spacing;
    // output: the final optimal angle.
    double angle = -1.;

    InternalBridgeDetector(ExPolygon _internal_bridge, const ExPolygons &_fill_no_overlap, coord_t _spacing);
    bool detect_angle();

private:
    void initialize();
    std::vector<double> bridge_direction_candidates() const;

    struct InternalBridgeDirection {
        InternalBridgeDirection(double a = -1.) : angle(a), coverage(0.), max_length(0.) {}
        // the best direction is the one causing most lines to be bridged and the span is short
        bool operator<(const InternalBridgeDirection &other) const {
            double delta = this->coverage - other.coverage;
            if (delta > 0.001)
                return true;
            else if (delta < -0.001)
                return false;
            else
                // coverage is almost same, then compare span
                return this->max_length < other.max_length;
        };
        double angle;
        double coverage;
        double max_length;
    };

    double resolution = PI/36.0;
    ExPolygons m_anchor_regions;
};

}

#endif
