#ifndef slic3r_Geometry_hpp_
#define slic3r_Geometry_hpp_

#include "BoundingBox.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"

#include "boost/polygon/voronoi.hpp"
using boost::polygon::voronoi_builder;
using boost::polygon::voronoi_diagram;

namespace Slic3r { namespace Geometry {

void convex_hull(Points &points, Polygon* hull);
void chained_path(Points &points, std::vector<Points::size_type> &retval, Point start_near);
void chained_path(Points &points, std::vector<Points::size_type> &retval);
template<class T> void chained_path_items(Points &points, T &items, T &retval);

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
    Line retrieve_segment(const voronoi_diagram<double>::cell_type& cell) const;
};

} }

#endif
