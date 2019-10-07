#ifndef slic3r_BridgeDetector_hpp_
#define slic3r_BridgeDetector_hpp_

#include "libslic3r.h"
#include "ExPolygon.hpp"
#include <string>

namespace Slic3r {

// The bridge detector optimizes a direction of bridges over a region or a set of regions.
// A bridge direction is considered optimal, if the length of the lines strang over the region is maximal.
// This is optimal if the bridge is supported in a single direction only, but
// it may not likely be optimal, if the bridge region is supported from all sides. Then an optimal 
// solution would find a direction with shortest bridges.
// The bridge orientation is measured CCW from the X axis.
class BridgeDetector {
public:
    // The non-grown holes.
    const ExPolygons            &expolygons;
    // In case the caller gaves us the input polygons by a value, make a copy.
    ExPolygons                   expolygons_owned;
    // Lower slices, all regions.
    const ExPolygons   			&lower_slices;
    // Scaled extrusion width of the infill.
    coord_t                      spacing;
    // Angle resolution for the brute force search of the best bridging angle.
    double                       resolution;
    // The final optimal angle.
    double                       angle;
    
    BridgeDetector(ExPolygon _expolygon, const ExPolygons &_lower_slices, coord_t _extrusion_width);
    BridgeDetector(const ExPolygons &_expolygons, const ExPolygons &_lower_slices, coord_t _extrusion_width);
    // If bridge_direction_override != 0, then the angle is used instead of auto-detect.
    bool detect_angle(double bridge_direction_override = 0.);
    Polygons coverage(double angle = -1) const;
    void unsupported_edges(double angle, Polylines* unsupported) const;
    Polylines unsupported_edges(double angle = -1) const;
    
private:
    // Suppress warning "assignment operator could not be generated"
    BridgeDetector& operator=(const BridgeDetector &);

    void initialize();

    struct BridgeDirection {
        BridgeDirection(double a = -1.) : angle(a), coverage(0.), max_length(0.) {}
        // the best direction is the one causing most lines to be bridged (thus most coverage)
        bool operator<(const BridgeDirection &other) const {
            // Initial sort by coverage only - comparator must obey strict weak ordering
            return this->coverage > other.coverage;
        };
        double angle;
        double coverage;
        double max_length;
    };

    // Get possible briging direction candidates.
    std::vector<double> bridge_direction_candidates() const;

    // Open lines representing the supporting edges.
    Polylines _edges;
    // Closed polygons representing the supporting areas.
    ExPolygons _anchor_regions;
};

}

#endif
