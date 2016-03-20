#ifndef slic3r_Polyline_hpp_
#define slic3r_Polyline_hpp_

#include "libslic3r.h"
#include "Line.hpp"
#include "MultiPoint.hpp"
#include <string>
#include <vector>

namespace Slic3r {

class Polyline;
class ThickPolyline;
typedef std::vector<Polyline> Polylines;
typedef std::vector<ThickPolyline> ThickPolylines;

class Polyline : public MultiPoint {
    public:
    operator Polylines() const;
    operator Line() const;
    Point last_point() const;
    Point leftmost_point() const;
    virtual Lines lines() const;
    void clip_end(double distance);
    void clip_start(double distance);
    void extend_end(double distance);
    void extend_start(double distance);
    Points equally_spaced_points(double distance) const;
    void simplify(double tolerance);
    template <class T> void simplify_by_visibility(const T &area);
    void split_at(const Point &point, Polyline* p1, Polyline* p2) const;
    bool is_straight() const;
    std::string wkt() const;
};

class ThickPolyline : public Polyline {
    public:
    std::vector<coordf_t> width;
    std::pair<bool,bool> endpoints;
    ThickPolyline() : endpoints(std::make_pair(false, false)) {};
    ThickLines thicklines() const;
    void reverse();
};

}

#endif
