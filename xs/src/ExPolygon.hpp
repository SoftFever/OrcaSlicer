#ifndef slic3r_ExPolygon_hpp_
#define slic3r_ExPolygon_hpp_

#include "Polygon.hpp"
#include <vector>

namespace Slic3r {

class ExPolygon;
typedef std::vector<ExPolygon> ExPolygons;

class ExPolygon
{
    public:
    Polygon contour;
    Polygons holes;
    operator Points() const;
    operator Polygons() const;
    void scale(double factor);
    void translate(double x, double y);
    void rotate(double angle, Point* center);
    double area() const;
    bool is_valid() const;
    bool contains_line(const Line* line) const;
    bool contains_point(const Point* point) const;
    Polygons simplify_p(double tolerance) const;
    ExPolygons simplify(double tolerance) const;
    void simplify(double tolerance, ExPolygons &expolygons) const;
    void medial_axis(double max_width, double min_width, Polylines* polylines) const;
    
    #ifdef SLIC3RXS
    void from_SV(SV* poly_sv);
    void from_SV_check(SV* poly_sv);
    SV* to_AV();
    SV* to_SV_ref();
    SV* to_SV_clone_ref() const;
    SV* to_SV_pureperl() const;
    #endif
};

}

#endif
