#ifndef slic3r_Geometry_hpp_
#define slic3r_Geometry_hpp_

#include "BoundingBox.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"

#include "boost/polygon/voronoi.hpp"
using boost::polygon::voronoi_builder;
using boost::polygon::voronoi_diagram;

namespace Slic3r { namespace Geometry {

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

class ArrangeItem {
    public:
    Pointf pos;
    size_t index_x, index_y;
    coordf_t dist;
};
class ArrangeItemIndex {
    public:
    coordf_t index;
    ArrangeItem item;
    ArrangeItemIndex(coordf_t _index, ArrangeItem _item) : index(_index), item(_item) {};
};
double linint(double value, double oldmin, double oldmax, double newmin, double newmax);
Pointfs arrange(size_t total_parts, Pointf part, coordf_t dist, const BoundingBoxf &bb = BoundingBoxf());

class MedialAxis {
    public:
    Points points;
    Lines lines;
    double max_width;
    double min_width;
    MedialAxis(double _max_width, double _min_width) : max_width(_max_width), min_width(_min_width) {};
    void build(Polylines* polylines);
    
    private:
    typedef voronoi_diagram<double> VD;
    VD vd;
    std::set<const VD::edge_type*> edges;
    Line edge_to_line(const VD::edge_type &edge) const;
    void process_edge_neighbors(const voronoi_diagram<double>::edge_type& edge, Points* points);
    bool is_valid_edge(const voronoi_diagram<double>::edge_type& edge) const;
    const Line& retrieve_segment(const voronoi_diagram<double>::cell_type& cell) const;
};

} }

#endif
