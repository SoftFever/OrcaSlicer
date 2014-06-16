#ifndef slic3r_Point_hpp_
#define slic3r_Point_hpp_

#include <myinit.h>
#include <vector>
#include <math.h>
#include <string>

namespace Slic3r {

class Line;
class MultiPoint;
class Point;
class Pointf;
typedef Point Vector;
typedef std::vector<Point> Points;
typedef std::vector<Point*> PointPtrs;
typedef std::vector<const Point*> PointConstPtrs;
typedef std::vector<Pointf> Pointfs;

class Point
{
    public:
    coord_t x;
    coord_t y;
    Point(coord_t _x = 0, coord_t _y = 0): x(_x), y(_y) {};
    Point(int _x, int _y): x(_x), y(_y) {};
    Point(long long _x, long long _y): x(_x), y(_y) {};  // for Clipper
    Point(double x, double y);
    bool operator==(const Point& rhs) const;
    std::string wkt() const;
    void scale(double factor);
    void translate(double x, double y);
    void rotate(double angle, const Point &center);
    bool coincides_with(const Point &point) const;
    bool coincides_with_epsilon(const Point &point) const;
    int nearest_point_index(const Points &points) const;
    int nearest_point_index(const PointConstPtrs &points) const;
    int nearest_point_index(const PointPtrs &points) const;
    void nearest_point(const Points &points, Point* point) const;
    double distance_to(const Point &point) const;
    double distance_to(const Line &line) const;
    double ccw(const Point &p1, const Point &p2) const;
    double ccw(const Line &line) const;
    Point projection_onto(const MultiPoint &poly) const;
    Point projection_onto(const Line &line) const;
    Point negative() const;
    
    #ifdef SLIC3RXS
    void from_SV(SV* point_sv);
    void from_SV_check(SV* point_sv);
    SV* to_SV_pureperl() const;
    #endif
};

Point operator+(const Point& point1, const Point& point2);
Point operator*(double scalar, const Point& point2);

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
    void from_SV_check(SV* point_sv);
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
#include <boost/polygon/polygon.hpp>
namespace boost { namespace polygon {
    template <>
    struct geometry_concept<coord_t> { typedef coordinate_concept type; };
    
    template <>
    struct coordinate_traits<coord_t> {
        typedef coord_t coordinate_type;
        typedef long double area_type;
        typedef long long manhattan_area_type;
        typedef unsigned long long unsigned_area_type;
        typedef long long coordinate_difference;
        typedef long double coordinate_distance;
    };

    template <>
    struct geometry_concept<Point> { typedef point_concept type; };
   
    template <>
    struct point_traits<Point> {
        typedef coord_t coordinate_type;
    
        static inline coordinate_type get(const Point& point, orientation_2d orient) {
            return (orient == HORIZONTAL) ? point.x : point.y;
        }
    };
    
    template <>
    struct point_mutable_traits<Point> {
        typedef coord_t coordinate_type;
        static inline void set(Point& point, orientation_2d orient, coord_t value) {
            if (orient == HORIZONTAL)
                point.x = value;
            else
                point.y = value;
        }
        static inline Point construct(coord_t x_value, coord_t y_value) {
            Point retval;
            retval.x = x_value;
            retval.y = y_value; 
            return retval;
        }
    };
} }
// end Boost

#endif
