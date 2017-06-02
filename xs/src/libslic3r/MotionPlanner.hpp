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
    MotionPlannerEnv() {};
    MotionPlannerEnv(const ExPolygon &island) : m_island(island), m_island_bbox(get_extents(island)) {};
    Point nearest_env_point(const Point &from, const Point &to) const;
    bool  island_contains(const Point &pt) const
        { return m_island_bbox.contains(pt) && m_island.contains(pt); }
    bool  island_contains_b(const Point &pt) const
        { return m_island_bbox.contains(pt) && m_island.contains_b(pt); }

private:
    ExPolygon           m_island;
    BoundingBox         m_island_bbox;
    // Region, where the travel is allowed.
    ExPolygonCollection m_env;
};

// A 2D directed graph for searching a shortest path using the famous Dijkstra algorithm.
class MotionPlannerGraph
{    
public:
    // Add a directed edge into the graph.
    size_t   add_node(const Point &p) { m_nodes.emplace_back(p); return m_nodes.size() - 1; }
    void     add_edge(size_t from, size_t to, double weight);
    size_t   find_closest_node(const Point &point) const { return point.nearest_point_index(m_nodes); }

    bool     empty() const { return m_adjacency_list.empty(); }
    Polyline shortest_path(size_t from, size_t to) const;
    Polyline shortest_path(const Point &from, const Point &to) const
        { return this->shortest_path(this->find_closest_node(from), this->find_closest_node(to)); }

private:
    typedef int     node_t;
    typedef double  weight_t;
    struct Neighbor {
        Neighbor(node_t target, weight_t weight) : target(target), weight(weight) {}
        node_t   target;
        weight_t weight;
    };
    Points                              m_nodes;
    std::vector<std::vector<Neighbor>>  m_adjacency_list;
};

class MotionPlanner
{
public:
    MotionPlanner(const ExPolygons &islands);
    ~MotionPlanner() {}

    Polyline    shortest_path(const Point &from, const Point &to);
    size_t      islands_count() const { return m_islands.size(); }

private:
    bool                                m_initialized;
    std::vector<MotionPlannerEnv>       m_islands;
    MotionPlannerEnv                    m_outer;
    // 0th graph is the graph for m_outer. Other graphs are 1 indexed.
    std::vector<std::unique_ptr<MotionPlannerGraph>> m_graphs;
    
    void                      initialize();
    const MotionPlannerGraph& init_graph(int island_idx);
    const MotionPlannerEnv&   get_env(int island_idx) const
        { return (island_idx == -1) ? m_outer : m_islands[island_idx]; }
};

}

#endif
