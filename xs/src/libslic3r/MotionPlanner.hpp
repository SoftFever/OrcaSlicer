#ifndef slic3r_MotionPlanner_hpp_
#define slic3r_MotionPlanner_hpp_

#include "libslic3r.h"
#include "ClipperUtils.hpp"
#include "ExPolygonCollection.hpp"
#include "Polyline.hpp"
#include <map>
#include <utility>
#include <vector>

#define MP_INNER_MARGIN scale_(1.0)
#define MP_OUTER_MARGIN scale_(2.0)

namespace Slic3r {

class MotionPlanner;

class MotionPlannerEnv
{
    friend class MotionPlanner;
    
    public:
    ExPolygon island;
    ExPolygonCollection env;
    MotionPlannerEnv() {};
    MotionPlannerEnv(const ExPolygon &island) : island(island) {};
    Point nearest_env_point(const Point &from, const Point &to) const;
};

class MotionPlannerGraph
{
    friend class MotionPlanner;
    
    private:
    typedef int node_t;
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

class MotionPlanner
{
    public:
    MotionPlanner(const ExPolygons &islands);
    ~MotionPlanner();
    Polyline shortest_path(const Point &from, const Point &to);
    size_t islands_count() const;
    
    private:
    bool initialized;
    std::vector<MotionPlannerEnv> islands;
    MotionPlannerEnv outer;
    std::vector<MotionPlannerGraph*> graphs;
    
    void initialize();
    MotionPlannerGraph* init_graph(int island_idx);
    const MotionPlannerEnv& get_env(int island_idx) const;
};

}

#endif
