#ifndef slic3r_Geometry_MedialAxis_hpp_
#define slic3r_Geometry_MedialAxis_hpp_

#include "Voronoi.hpp"
#include "../ExPolygon.hpp"

namespace Slic3r { namespace Geometry {

class MedialAxis {
public:
    Lines lines;
    const ExPolygon* expolygon;
    double max_width;
    double min_width;
    MedialAxis(double _max_width, double _min_width, const ExPolygon* _expolygon = NULL)
        : expolygon(_expolygon), max_width(_max_width), min_width(_min_width) {};
    void build(ThickPolylines* polylines);
    void build(Polylines* polylines);
    
private:
    using VD = VoronoiDiagram;
    VD vd;
    std::set<const VD::edge_type*> edges, valid_edges;
    std::map<const VD::edge_type*, std::pair<coordf_t,coordf_t> > thickness;
    void process_edge_neighbors(const VD::edge_type* edge, ThickPolyline* polyline);
    bool validate_edge(const VD::edge_type* edge);
    const Line& retrieve_segment(const VD::cell_type* cell) const;
    const Point& retrieve_endpoint(const VD::cell_type* cell) const;
};

} } // namespace Slicer::Geometry

#endif // slic3r_Geometry_MedialAxis_hpp_
