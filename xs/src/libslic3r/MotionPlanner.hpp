#ifndef slic3r_MotionPlanner_hpp_
#define slic3r_MotionPlanner_hpp_

#include <myinit.h>
#include "ClipperUtils.hpp"
#include "ExPolygonCollection.hpp"
#include "Polyline.hpp"
#include <map>
#include <utility>
#include <vector>

#define MP_INNER_MARGIN scale_(1.0)
#define MP_OUTER_MARGIN scale_(2.0)

namespace Slic3r {

class MotionPlannerGraph;

class MotionPlanner
{
    public:
    MotionPlanner(const ExPolygons &islands);
    ~MotionPlanner();
    Polyline shortest_path(const Point &from, const Point &to);
    size_t islands_count() const;
    
    private:
    ExPolygons islands;
    bool initialized;
    ExPolygon outer;
    ExPolygonCollections inner;
    std::vector<MotionPlannerGraph*> graphs;
    
    void initialize();
    MotionPlannerGraph* init_graph(int island_idx);
    ExPolygonCollection get_env(size_t island_idx) const;
    Point nearest_env_point(const ExPolygonCollection &env, const Point &from, const Point &to) const;
};

class MotionPlannerGraph
{
    friend class MotionPlanner;
    
    private:
    typedef size_t node_t;
    typedef double weight_t;
    struct neighbor {
        node_t target;
        weight_t weight;
        neighbor(node_t arg_target, weight_t arg_weight)
            : target(arg_target), weight(arg_weight) { }
    };
    typedef std::vector< std::vector<neighbor> > adjacency_list_t;
    adjacency_list_t adjacency_list;
    
    public:
    Points nodes;
    //std::map<std::pair<size_t,size_t>, double> edges;
    void add_edge(size_t from, size_t to, double weight);
    size_t find_node(const Point &point) const;
    Polyline shortest_path(size_t from, size_t to);
};

}

#endif
