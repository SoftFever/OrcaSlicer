#ifndef slic3r_Polygon_hpp_
#define slic3r_Polygon_hpp_

#include "libslic3r.h"
#include <vector>
#include <string>
#include "Line.hpp"
#include "MultiPoint.hpp"
#include "Polyline.hpp"

namespace Slic3r {

class Polygon;
typedef std::vector<Polygon> Polygons;

class Polygon : public MultiPoint
{
public:
    operator Polygons() const { Polygons pp; pp.push_back(*this); return pp; }
    operator Polyline() const { return this->split_at_first_point(); }
    Point& operator[](Points::size_type idx) { return this->points[idx]; }
    const Point& operator[](Points::size_type idx) const { return this->points[idx]; }

    Polygon() = default;
    virtual ~Polygon() = default;
    explicit Polygon(const Points &points) : MultiPoint(points) {}
	Polygon(std::initializer_list<Point> points) : MultiPoint(points) {}
    Polygon(const Polygon &other) : MultiPoint(other.points) {}
    Polygon(Polygon &&other) : MultiPoint(std::move(other.points)) {}
	static Polygon new_scale(const std::vector<Vec2d> &points) { 
        Polygon pgn;
        pgn.points.reserve(points.size());
        for (const Vec2d &pt : points)
            pgn.points.emplace_back(Point::new_scale(pt(0), pt(1)));
		return pgn;
	}
    Polygon& operator=(const Polygon &other) { points = other.points; return *this; }
    Polygon& operator=(Polygon &&other) { points = std::move(other.points); return *this; }

    // last point == first point for polygons
    const Point& last_point() const override { return this->points.front(); }

    Lines lines() const override;
    Polyline split_at_vertex(const Point &point) const;
    // Split a closed polygon into an open polyline, with the split point duplicated at both ends.
    Polyline split_at_index(int index) const;
    // Split a closed polygon into an open polyline, with the split point duplicated at both ends.
    Polyline split_at_first_point() const { return this->split_at_index(0); }
    Points   equally_spaced_points(double distance) const { return this->split_at_first_point().equally_spaced_points(distance); }
    
    static double area(const Points &pts);
    double area() const;
    bool is_counter_clockwise() const;
    bool is_clockwise() const;
    bool make_counter_clockwise();
    bool make_clockwise();
    bool is_valid() const { return this->points.size() >= 3; }

    // Does an unoriented polygon contain a point?
    // Tested by counting intersections along a horizontal line.
    bool contains(const Point &point) const;
    Polygons simplify(double tolerance) const;
    void simplify(double tolerance, Polygons &polygons) const;
    void densify(float min_length, std::vector<float>* lengths = nullptr);
    void triangulate_convex(Polygons* polygons) const;
    Point centroid() const;
    Points concave_points(double angle = PI) const;
    Points convex_points(double angle = PI) const;
    // Projection of a point onto the polygon.
    Point point_projection(const Point &point) const;
    std::vector<float> parameter_by_length() const;
};

inline bool operator==(const Polygon &lhs, const Polygon &rhs) { return lhs.points == rhs.points; }
inline bool operator!=(const Polygon &lhs, const Polygon &rhs) { return lhs.points != rhs.points; }

extern BoundingBox get_extents(const Polygon &poly);
extern BoundingBox get_extents(const Polygons &polygons);
extern BoundingBox get_extents_rotated(const Polygon &poly, double angle);
extern BoundingBox get_extents_rotated(const Polygons &polygons, double angle);
extern std::vector<BoundingBox> get_extents_vector(const Polygons &polygons);

inline double total_length(const Polygons &polylines) {
    double total = 0;
    for (Polygons::const_iterator it = polylines.begin(); it != polylines.end(); ++it)
        total += it->length();
    return total;
}

inline double area(const Polygons &polys)
{
    double s = 0.;
    for (auto &p : polys) s += p.area();

    return s;
}

// Remove sticks (tentacles with zero area) from the polygon.
extern bool        remove_sticks(Polygon &poly);
extern bool        remove_sticks(Polygons &polys);

// Remove polygons with less than 3 edges.
extern bool        remove_degenerate(Polygons &polys);
extern bool        remove_small(Polygons &polys, double min_area);
extern void 	   remove_collinear(Polygon &poly);
extern void 	   remove_collinear(Polygons &polys);

// Append a vector of polygons at the end of another vector of polygons.
inline void        polygons_append(Polygons &dst, const Polygons &src) { dst.insert(dst.end(), src.begin(), src.end()); }

inline void        polygons_append(Polygons &dst, Polygons &&src) 
{
    if (dst.empty()) {
        dst = std::move(src);
    } else {
        std::move(std::begin(src), std::end(src), std::back_inserter(dst));
        src.clear();
    }
}

inline Polygons polygons_simplify(const Polygons &polys, double tolerance)
{
	Polygons out;
	out.reserve(polys.size());
	for (const Polygon &p : polys)
		polygons_append(out, p.simplify(tolerance));
	return out;
}

inline void polygons_rotate(Polygons &polys, double angle)
{
    const double cos_angle = cos(angle);
    const double sin_angle = sin(angle);
    for (Polygon &p : polys)
        p.rotate(cos_angle, sin_angle);
}

inline void polygons_reverse(Polygons &polys)
{
    for (Polygon &p : polys)
        p.reverse();
}

inline Points to_points(const Polygon &poly)
{
    return poly.points;
}

inline Points to_points(const Polygons &polys) 
{
    size_t n_points = 0;
    for (size_t i = 0; i < polys.size(); ++ i)
        n_points += polys[i].points.size();
    Points points;
    points.reserve(n_points);
    for (const Polygon &poly : polys)
        append(points, poly.points);
    return points;
}

inline Lines to_lines(const Polygon &poly) 
{
    Lines lines;
    lines.reserve(poly.points.size());
    if (poly.points.size() > 2) {
        for (Points::const_iterator it = poly.points.begin(); it != poly.points.end()-1; ++it)
            lines.push_back(Line(*it, *(it + 1)));
        lines.push_back(Line(poly.points.back(), poly.points.front()));
    }
    return lines;
}

inline Lines to_lines(const Polygons &polys) 
{
    size_t n_lines = 0;
    for (size_t i = 0; i < polys.size(); ++ i)
        n_lines += polys[i].points.size();
    Lines lines;
    lines.reserve(n_lines);
    for (size_t i = 0; i < polys.size(); ++ i) {
        const Polygon &poly = polys[i];
        for (Points::const_iterator it = poly.points.begin(); it != poly.points.end()-1; ++it)
            lines.push_back(Line(*it, *(it + 1)));
        lines.push_back(Line(poly.points.back(), poly.points.front()));
    }
    return lines;
}

inline Polylines to_polylines(const Polygons &polys)
{
    Polylines polylines;
    polylines.assign(polys.size(), Polyline());
    size_t idx = 0;
    for (Polygons::const_iterator it = polys.begin(); it != polys.end(); ++ it) {
        Polyline &pl = polylines[idx ++];
        pl.points = it->points;
        pl.points.push_back(it->points.front());
    }
    assert(idx == polylines.size());
    return polylines;
}

inline Polylines to_polylines(Polygons &&polys)
{
    Polylines polylines;
    polylines.assign(polys.size(), Polyline());
    size_t idx = 0;
    for (Polygons::const_iterator it = polys.begin(); it != polys.end(); ++ it) {
        Polyline &pl = polylines[idx ++];
        pl.points = std::move(it->points);
        pl.points.push_back(it->points.front());
    }
    assert(idx == polylines.size());
    return polylines;
}

} // Slic3r

// start Boost
#include <boost/polygon/polygon.hpp>
namespace boost { namespace polygon {
    template <>
    struct geometry_concept<Slic3r::Polygon>{ typedef polygon_concept type; };

    template <>
    struct polygon_traits<Slic3r::Polygon> {
        typedef coord_t coordinate_type;
        typedef Slic3r::Points::const_iterator iterator_type;
        typedef Slic3r::Point point_type;

        // Get the begin iterator
        static inline iterator_type begin_points(const Slic3r::Polygon& t) {
            return t.points.begin();
        }

        // Get the end iterator
        static inline iterator_type end_points(const Slic3r::Polygon& t) {
            return t.points.end();
        }

        // Get the number of sides of the polygon
        static inline std::size_t size(const Slic3r::Polygon& t) {
            return t.points.size();
        }

        // Get the winding direction of the polygon
        static inline winding_direction winding(const Slic3r::Polygon& /* t */) {
            return unknown_winding;
        }
    };

    template <>
    struct polygon_mutable_traits<Slic3r::Polygon> {
        // expects stl style iterators
        template <typename iT>
        static inline Slic3r::Polygon& set_points(Slic3r::Polygon& polygon, iT input_begin, iT input_end) {
            polygon.points.clear();
            while (input_begin != input_end) {
                polygon.points.push_back(Slic3r::Point());
                boost::polygon::assign(polygon.points.back(), *input_begin);
                ++input_begin;
            }
            // skip last point since Boost will set last point = first point
            polygon.points.pop_back();
            return polygon;
        }
    };
    
    template <>
    struct geometry_concept<Slic3r::Polygons> { typedef polygon_set_concept type; };

    //next we map to the concept through traits
    template <>
    struct polygon_set_traits<Slic3r::Polygons> {
        typedef coord_t coordinate_type;
        typedef Slic3r::Polygons::const_iterator iterator_type;
        typedef Slic3r::Polygons operator_arg_type;

        static inline iterator_type begin(const Slic3r::Polygons& polygon_set) {
            return polygon_set.begin();
        }

        static inline iterator_type end(const Slic3r::Polygons& polygon_set) {
            return polygon_set.end();
        }

        //don't worry about these, just return false from them
        static inline bool clean(const Slic3r::Polygons& /* polygon_set */) { return false; }
        static inline bool sorted(const Slic3r::Polygons& /* polygon_set */) { return false; }
    };

    template <>
    struct polygon_set_mutable_traits<Slic3r::Polygons> {
        template <typename input_iterator_type>
        static inline void set(Slic3r::Polygons& polygons, input_iterator_type input_begin, input_iterator_type input_end) {
          polygons.assign(input_begin, input_end);
        }
    };
} }
// end Boost

#endif
