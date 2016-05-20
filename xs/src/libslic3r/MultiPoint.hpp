#ifndef slic3r_MultiPoint_hpp_
#define slic3r_MultiPoint_hpp_

#include "libslic3r.h"
#include <algorithm>
#include <vector>
#include "Line.hpp"
#include "Point.hpp"

namespace Slic3r {

class BoundingBox;

class MultiPoint
{
    public:
    Points points;
    
    operator Points() const;
    MultiPoint() {};
    explicit MultiPoint(const Points &_points): points(_points) {};
    void scale(double factor);
    void translate(double x, double y);
    void translate(const Point &vector);
    void rotate(double angle, const Point &center);
    void reverse();
    Point first_point() const;
    virtual Point last_point() const = 0;
    virtual Lines lines() const = 0;
    double length() const;
    bool is_valid() const;
    int find_point(const Point &point) const;
    bool has_boundary_point(const Point &point) const;
    BoundingBox bounding_box() const;
    void remove_duplicate_points();
    void append(const Point &point);
    void append(const Points &points);
    void append(const Points::const_iterator &begin, const Points::const_iterator &end);
    bool intersection(const Line& line, Point* intersection) const;
    std::string dump_perl() const;
    
    static Points _douglas_peucker(const Points &points, const double tolerance);
};

}

#endif
