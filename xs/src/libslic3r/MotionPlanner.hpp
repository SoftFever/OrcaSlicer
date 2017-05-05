#ifndef slic3r_MotionPlanner_hpp_
#define slic3r_MotionPlanner_hpp_

#include "libslic3r.h"
#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "ExPolygonCollection.hpp"
#include "Polyline.hpp"
#include <map>
#include <utility>
#include <memory>
#include <vector>

#define MP_INNER_MARGIN scale_(1.0)
#define MP_OUTER_MARGIN scale_(2.0)

namespace Slic3r {

class MotionPlanner;

class MotionPlannerEnv
{
    friend class MotionPlanner;
    
public:
    ExPolygon           island;
    BoundingBox         island_bbox;
    ExPolygonCollection env;
    MotionPlannerEnv() {};
    MotionPlannerEnv(const ExPolygon &island) : island(island), island_bbox(get_extents(island)) {};
    Point nearest_env_point(const Point &from, const Point &to) const;
};

class MotionPlannerGraph
{
    friend class MotionPlanner;
    
private:
    typedef int     node_t;
    typedef double  weight_t;
    struct Neighbor {
        node_t   target;
        weight_t weight;
        Neighbor(node_t arg_target, weight_t arg_weight) : target(arg_target), weight(arg_weight) {}
    };
    typedef std::vector<std::vector<Neighbor>> adjacency_list_t;
    adjacency_list_t adjacency_list;
    
public:
    Points   nodes;
    void     add_edge(size_t from, size_t to, double weight);
    size_t   find_closest_node(const Point &point) const { return point.nearest_point_index(this->nodes); }
    Polyline shortest_path(size_t from, size_t to) const;
};

class MotionPlanner
{
public:
    MotionPlanner(const ExPolygons &islands);
    ~MotionPlanner() {}

    Polyline    shortest_path(const Point &from, const Point &to);
    size_t      islands_count() const { return this->islands.size(); }

private:
    bool                                initialized;
    std::vector<MotionPlannerEnv>       islands;
    MotionPlannerEnv                    outer;
    std::vector<std::unique_ptr<MotionPlannerGraph>> graphs;
    
    void                      initialize();
    const MotionPlannerGraph& init_graph(int island_idx);
    const MotionPlannerEnv&   get_env(int island_idx) const
        { return (island_idx == -1) ? this->outer : this->islands[island_idx]; }
};

}

#endif
