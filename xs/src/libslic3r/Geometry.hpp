#ifndef slic3r_Geometry_hpp_
#define slic3r_Geometry_hpp_

#include "libslic3r.h"
#include "BoundingBox.hpp"
#include "ExPolygon.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"

#include "boost/polygon/voronoi.hpp"
using boost::polygon::voronoi_builder;
using boost::polygon::voronoi_diagram;

namespace Slic3r { namespace Geometry {

inline bool ray_ray_intersection(const Pointf &p1, const Vectorf &v1, const Pointf &p2, const Vectorf &v2, Pointf &res)
{
    double denom = v1.x * v2.y - v2.x * v1.y;
    if (std::abs(denom) < EPSILON)
        return false;
    double t = (v2.x * (p1.y - p2.y) - v2.y * (p1.x - p2.x)) / denom;
    res.x = p1.x + t * v1.x;
    res.y = p1.y + t * v1.y;
    return true;
}

inline bool segment_segment_intersection(const Pointf &p1, const Vectorf &v1, const Pointf &p2, const Vectorf &v2, Pointf &res)
{
    double denom = v1.x * v2.y - v2.x * v1.y;
    if (std::abs(denom) < EPSILON)
        // Lines are collinear.
        return false;
    double s12_x = p1.x - p2.x;
    double s12_y = p1.y - p2.y;
    double s_numer = v1.x * s12_y - v1.y * s12_x;
    bool   denom_is_positive = false;
    if (denom < 0.) {
        denom_is_positive = true;
        denom   = - denom;
        s_numer = - s_numer;
    }
    if (s_numer < 0.)
        // Intersection outside of the 1st segment.
        return false;
    double t_numer = v2.x * s12_y - v2.y * s12_x;
    if (! denom_is_positive)
        t_numer = - t_numer;
    if (t_numer < 0. || s_numer > denom || t_numer > denom)
        // Intersection outside of the 1st or 2nd segment.
        return false;
    // Intersection inside both of the segments.
    double t = t_numer / denom;
    res.x = p1.x + t * v1.x;
    res.y = p1.y + t * v1.y;
    return true;
}

Polygon convex_hull(Points points);
Polygon convex_hull(const Polygons &polygons);
void chained_path(const Points &points, std::vector<Points::size_type> &retval, Point start_near);
void chained_path(const Points &points, std::vector<Points::size_type> &retval);
template<class T> void chained_path_items(Points &points, T &items, T &retval);
bool directions_parallel(double angle1, double angle2, double max_diff = 0);
template<class T> bool contains(const std::vector<T> &vector, const Point &point);
double rad2deg(double angle);
double rad2deg_dir(double angle);
double deg2rad(double angle);
void simplify_polygons(const Polygons &polygons, double tolerance, Polygons* retval);

double linint(double value, double oldmin, double oldmax, double newmin, double newmax);
bool arrange(
    // input
    size_t num_parts, const Pointf &part_size, coordf_t gap, const BoundingBoxf* bed_bounding_box, 
    // output
    Pointfs &positions);

class MedialAxis {
    public:
    Lines lines;
    const ExPolygon* expolygon;
    double max_width;
    double min_width;
    MedialAxis(double _max_width, double _min_width, const ExPolygon* _expolygon = NULL)
        : max_width(_max_width), min_width(_min_width), expolygon(_expolygon) {};
    void build(ThickPolylines* polylines);
    void build(Polylines* polylines);
    
    private:
    class VD : public voronoi_diagram<double> {
    public:
        typedef double                                          coord_type;
        typedef boost::polygon::point_data<coordinate_type>     point_type;
        typedef boost::polygon::segment_data<coordinate_type>   segment_type;
        typedef boost::polygon::rectangle_data<coordinate_type> rect_type;
    };
    VD vd;
    std::set<const VD::edge_type*> edges, valid_edges;
    std::map<const VD::edge_type*, std::pair<coordf_t,coordf_t> > thickness;
    void process_edge_neighbors(const VD::edge_type* edge, ThickPolyline* polyline);
    bool validate_edge(const VD::edge_type* edge);
    const Line& retrieve_segment(const VD::cell_type* cell) const;
    const Point& retrieve_endpoint(const VD::cell_type* cell) const;
};

} }

#endif
