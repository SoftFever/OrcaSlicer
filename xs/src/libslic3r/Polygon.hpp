#ifndef slic3r_Polygon_hpp_
#define slic3r_Polygon_hpp_

#include <myinit.h>
#include <vector>
#include <string>
#include "Line.hpp"
#include "MultiPoint.hpp"
#include "Polyline.hpp"

namespace Slic3r {

class Polygon;
typedef std::vector<Polygon> Polygons;

class Polygon : public MultiPoint {
    public:
    operator Polygons() const;
    operator Polyline() const;
    Point& operator[](Points::size_type idx);
    const Point& operator[](Points::size_type idx) const;
    
    Polygon() {};
    explicit Polygon(const Points &points): MultiPoint(points) {};
    Point last_point() const;
    Lines lines() const;
    Polyline split_at_vertex(const Point &point) const;
    Polyline split_at_index(int index) const;
    Polyline split_at_first_point() const;
    Points equally_spaced_points(double distance) const;
    double area() const;
    bool is_counter_clockwise() const;
    bool is_clockwise() const;
    bool make_counter_clockwise();
    bool make_clockwise();
    bool is_valid() const;
    bool contains(const Point &point) const;
    Polygons simplify(double tolerance) const;
    void simplify(double tolerance, Polygons &polygons) const;
    void triangulate_convex(Polygons* polygons) const;
    Point centroid() const;
    std::string wkt() const;
    Points concave_points(double angle = PI) const;
    Points convex_points(double angle = PI) const;
    
    #ifdef SLIC3RXS
    void from_SV_check(SV* poly_sv);
    #endif
};

}

// start Boost
#include <boost/polygon/polygon.hpp>
namespace boost { namespace polygon {
    template <>
    struct geometry_concept<Polygon>{ typedef polygon_concept type; };

    template <>
    struct polygon_traits<Polygon> {
        typedef coord_t coordinate_type;
        typedef Points::const_iterator iterator_type;
        typedef Point point_type;

        // Get the begin iterator
        static inline iterator_type begin_points(const Polygon& t) {
            return t.points.begin();
        }

        // Get the end iterator
        static inline iterator_type end_points(const Polygon& t) {
            return t.points.end();
        }

        // Get the number of sides of the polygon
        static inline std::size_t size(const Polygon& t) {
            return t.points.size();
        }

        // Get the winding direction of the polygon
        static inline winding_direction winding(const Polygon& t) {
            return unknown_winding;
        }
    };

    template <>
    struct polygon_mutable_traits<Polygon> {
        // expects stl style iterators
        template <typename iT>
        static inline Polygon& set_points(Polygon& polygon, iT input_begin, iT input_end) {
            polygon.points.clear();
            while (input_begin != input_end) {
                polygon.points.push_back(Point());
                boost::polygon::assign(polygon.points.back(), *input_begin);
                ++input_begin;
            }
            // skip last point since Boost will set last point = first point
            polygon.points.pop_back();
            return polygon;
        }
    };
    
    template <>
    struct geometry_concept<Polygons> { typedef polygon_set_concept type; };

    //next we map to the concept through traits
    template <>
    struct polygon_set_traits<Polygons> {
        typedef coord_t coordinate_type;
        typedef Polygons::const_iterator iterator_type;
        typedef Polygons operator_arg_type;

        static inline iterator_type begin(const Polygons& polygon_set) {
            return polygon_set.begin();
        }

        static inline iterator_type end(const Polygons& polygon_set) {
            return polygon_set.end();
        }

        //don't worry about these, just return false from them
        static inline bool clean(const Polygons& polygon_set) { return false; }
        static inline bool sorted(const Polygons& polygon_set) { return false; }
    };

    template <>
    struct polygon_set_mutable_traits<Polygons> {
        template <typename input_iterator_type>
        static inline void set(Polygons& polygons, input_iterator_type input_begin, input_iterator_type input_end) {
          polygons.assign(input_begin, input_end);
        }
    };
} }
// end Boost

#endif
