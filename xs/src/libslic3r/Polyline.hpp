#ifndef slic3r_Polyline_hpp_
#define slic3r_Polyline_hpp_

#include "Line.hpp"
#include "MultiPoint.hpp"
#include <string>

namespace Slic3r {

class Polyline;
typedef std::vector<Polyline> Polylines;

class Polyline : public MultiPoint {
    public:
    operator Polylines() const;
    operator Line() const;
    Point last_point() const;
    Point leftmost_point() const;
    Lines lines() const;
    void clip_end(double distance);
    void clip_start(double distance);
    void extend_end(double distance);
    void extend_start(double distance);
    void equally_spaced_points(double distance, Points* points) const;
    void simplify(double tolerance);
    void split_at(const Point &point, Polyline* p1, Polyline* p2) const;
    bool is_straight() const;
    std::string wkt() const;
    
    #ifdef SLIC3RXS
    void from_SV_check(SV* poly_sv);
    #endif
};

}

#endif
