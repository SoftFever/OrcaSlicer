#ifndef slic3r_Point_hpp_
#define slic3r_Point_hpp_

#include <myinit.h>
#include <vector>
#include <math.h>
#include <boost/polygon/polygon.hpp>
#include <string>

namespace Slic3r {

class Line;
class Point;
class Pointf;
typedef Point Vector;
typedef std::vector<Point> Points;
typedef std::vector<Point*> PointPtrs;
typedef std::vector<Pointf> Pointfs;

class Point
{
    public:
    coord_t x;
    coord_t y;
    explicit Point(coord_t _x = 0, coord_t _y = 0): x(_x), y(_y) {};
    bool operator==(const Point& rhs) const;
    std::string wkt() const;
    void scale(double factor);
    void translate(double x, double y);
    void rotate(double angle, Point* center);
    bool coincides_with(const Point &point) const;
    bool coincides_with(const Point* point) const;
    int nearest_point_index(Points &points) const;
    int nearest_point_index(PointPtrs &points) const;
    Point* nearest_point(Points points) const;
    double distance_to(const Point* point) const;
    double distance_to(const Line* line) const;
    double distance_to(const Line &line) const;
    double ccw(const Point &p1, const Point &p2) const;
    double ccw(const Point* p1, const Point* p2) const;
    double ccw(const Line &line) const;
    
    #ifdef SLIC3RXS
    void from_SV(SV* point_sv);
    void from_SV_check(SV* point_sv);
    SV* to_SV_ref();
    SV* to_SV_clone_ref() const;
    SV* to_SV_pureperl() const;
    #endif
};

class Point3 : public Point
{
    public:
    coord_t z;
    explicit Point3(coord_t _x = 0, coord_t _y = 0, coord_t _z = 0): Point(_x, _y), z(_z) {};
};

class Pointf
{
    public:
    coordf_t x;
    coordf_t y;
    explicit Pointf(coordf_t _x = 0, coordf_t _y = 0): x(_x), y(_y) {};
    void scale(double factor);
    void translate(double x, double y);
    
    #ifdef SLIC3RXS
    bool from_SV(SV* point_sv);
    SV* to_SV_pureperl() const;
    #endif
};

class Pointf3 : public Pointf
{
    public:
    coordf_t z;
    explicit Pointf3(coordf_t _x = 0, coordf_t _y = 0, coordf_t _z = 0): Pointf(_x, _y), z(_z) {};
    void scale(double factor);
    void translate(double x, double y, double z);
};

}

// start Boost
namespace boost { namespace polygon {
    template <>
    struct geometry_concept<Point> { typedef point_concept type; };
   
    template <>
    struct point_traits<Point> {
        typedef coord_t coordinate_type;
    
        static inline coordinate_type get(const Point& point, orientation_2d orient) {
            return (orient == HORIZONTAL) ? point.x : point.y;
        }
    };
} }
// end Boost

#endif
