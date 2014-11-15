#ifndef slic3r_BridgeDetector_hpp_
#define slic3r_BridgeDetector_hpp_

#include <myinit.h>
#include "ExPolygon.hpp"
#include "ExPolygonCollection.hpp"
#include <string>

namespace Slic3r {

class BridgeDetector {
    public:
    ExPolygon expolygon;
    ExPolygonCollection lower_slices;
    double extrusion_width;  // scaled
    double resolution;
    double angle;
    
    BridgeDetector(const ExPolygon &_expolygon, const ExPolygonCollection &_lower_slices, coord_t _extrusion_width);
    bool detect_angle();
    void coverage(Polygons* coverage) const;
    void coverage(double angle, Polygons* coverage) const;
    void unsupported_edges(Polylines* unsupported) const;
    void unsupported_edges(double angle, Polylines* unsupported) const;
    
    private:
    Polylines _edges;   // representing the supporting edges
    ExPolygons _anchors;
};

}

#endif
