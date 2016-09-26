#ifndef slic3r_BridgeDetector_hpp_
#define slic3r_BridgeDetector_hpp_

#include "libslic3r.h"
#include "ExPolygon.hpp"
#include "ExPolygonCollection.hpp"
#include <string>

namespace Slic3r {

class BridgeDetector {
public:
    // The non-grown hole.
    ExPolygon expolygon;
    // Lower slices, all regions.
    ExPolygonCollection lower_slices;
    // Scaled extrusion width of the infill.
    double extrusion_width;
    // Angle resolution for the brute force search of the best bridging angle.
    double resolution;
    // The final optimal angle.
    double angle;
    
    BridgeDetector(const ExPolygon &_expolygon, const ExPolygonCollection &_lower_slices, coord_t _extrusion_width);
    bool detect_angle();
    void coverage(double angle, Polygons* coverage) const;
    Polygons coverage(double angle = -1) const;
    void unsupported_edges(double angle, Polylines* unsupported) const;
    Polylines unsupported_edges(double angle = -1) const;
    
private:
    // Open lines representing the supporting edges.
    Polylines _edges;
    // Closed polygons representing the supporting areas.
    ExPolygons _anchors;
};

}

#endif
