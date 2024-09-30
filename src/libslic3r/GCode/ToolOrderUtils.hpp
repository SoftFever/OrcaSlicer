#ifndef TOOL_ORDER_UTILS_HPP
#define TOOL_ORDER_UTILS_HPP

#include<vector>
#include<optional>
#include<functional>

namespace Slic3r {

using FlushMatrix = std::vector<std::vector<float>>;

class MaxFlow
{
private:
    const int INF = std::numeric_limits<int>::max();
    struct Edge {
        int from, to, capacity, flow;
        Edge(int u, int v, int cap) :from(u), to(v), capacity(cap), flow(0) {}
    };
public:
    MaxFlow(const std::vector<int>& u_nodes, const std::vector<int>& v_nodes,
        const std::unordered_map<int, std::vector<int>>& uv_link_limits = {},
        const std::unordered_map<int, std::vector<int>>& uv_unlink_limits = {},
        const std::vector<int>& u_capacity = {},
        const std::vector<int>& v_capacity = {}
    );
    std::vector<int> solve();

private:
    void add_edge(int from, int to, int capacity);


    int total_nodes;
    int source_id;
    int sink_id;
    std::vector<Edge>edges;
    std::vector<int>l_nodes;
    std::vector<int>r_nodes;
    std::vector<std::vector<int>>adj;
};

class MinCostMaxFlow
{
    const int INF = std::numeric_limits<int>::max();
    struct Edge
    {
        int from, to, capacity, cost, flow;
        Edge(int u, int v, int cap, int cst) : from(u), to(v), capacity(cap), cost(cst), flow(0) {}
    };

public:
    MinCostMaxFlow(const std::vector<std::vector<float>>& matrix_, const std::vector<int>& u_nodes, const std::vector<int>& v_nodes,
        const std::unordered_map<int, std::vector<int>>& uv_link_limits = {},
        const std::unordered_map<int, std::vector<int>>& uv_unlink_limits = {},
        const std::vector<int>& u_capacity = {},
        const std::vector<int>& v_capacity = {}
    );
    std::vector<int> solve();

private:
    void add_edge(int from, int to, int capacity, int cost);
    bool spfa(int source, int sink);
    int get_distance(int idx_in_left, int idx_in_right);

private:
    std::vector<std::vector<float>> matrix;
    std::vector<int> l_nodes;
    std::vector<int> r_nodes;
    std::vector<Edge> edges;
    std::vector<std::vector<int>> adj;

    int total_nodes;
    int source_id;
    int sink_id;
};

std::vector<unsigned int> get_extruders_order(const std::vector<std::vector<float>> &wipe_volumes,
                                              const std::vector<unsigned int> &curr_layer_extruders,
                                              const std::vector<unsigned int> &next_layer_extruders,
                                              const std::optional<unsigned int> &start_extruder_id,
                                              bool use_forcast = false,
                                              float *cost = nullptr);

int reorder_filaments_for_minimum_flush_volume(const std::vector<unsigned int> &filament_lists,
                                               const std::vector<int> &filament_maps,
                                               const std::vector<std::vector<unsigned int>> &layer_filaments,
                                               const std::vector<FlushMatrix> &flush_matrix,
                                               std::optional<std::function<bool(int, std::vector<int> &)>> get_custom_seq,
                                               std::vector<std::vector<unsigned int>> *filament_sequences);

}
#endif // !TOOL_ORDER_UTILS_HPP
