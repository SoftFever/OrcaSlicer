#ifndef slic3r_Polyline_hpp_
#define slic3r_Polyline_hpp_

#include "Line.hpp"
#include "MultiPoint.hpp"

namespace Slic3r {

class Polyline;
typedef std::vector<Polyline> Polylines;

class Polyline : public MultiPoint {
    public:
    operator Polylines() const;
    Point last_point() const;
    Point leftmost_point() const;
    Lines lines() const;
    void clip_end(double distance);
    void clip_start(double distance);
    void extend_end(double distance);
    void extend_start(double distance);
    Points equally_spaced_points(double distance) const;
    void simplify(double tolerance);
    
    #ifdef SLIC3RXS
    void from_SV_check(SV* poly_sv);
    SV* to_SV_ref();
    SV* to_SV_clone_ref() const;
    #endif
};

}

#endif
