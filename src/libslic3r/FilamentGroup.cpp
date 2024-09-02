#include "FilamentGroup.hpp"
#include "GCode/ToolOrderUtils.hpp"
#include <queue>
#include <random>
#include <cassert>

namespace Slic3r
{
    static void remove_intersection(std::set<int>& a, std::set<int>& b) {
        std::vector<int>intersection;
        std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(intersection));
        for (auto& item : intersection) {
            a.erase(item);
            b.erase(item);
        }
    }

    static bool extract_indices(const std::vector<unsigned int>& used_filaments, const std::vector<std::set<int>>& physical_unprintable_elems, const std::vector<std::set<int>>& geometric_unprintable_elems,
        std::vector<std::set<int>>& physical_unprintable_idxs, std::vector<std::set<int>>& geometric_unprintable_idxs)
    {
        assert(physical_unprintable_elems.size() == geometric_unprintable_elems.size());
        std::vector<std::set<int>>(physical_unprintable_elems.size()).swap(physical_unprintable_idxs);
        std::vector<std::set<int>>(geometric_unprintable_elems.size()).swap(geometric_unprintable_idxs);

        for (size_t gid = 0; gid < physical_unprintable_elems.size(); ++gid) {
            for (auto& f : physical_unprintable_elems[gid]) {
                auto iter = std::find(used_filaments.begin(), used_filaments.end(), (unsigned)f);
                if (iter != used_filaments.end())
                    physical_unprintable_idxs[gid].insert(iter - used_filaments.begin());
            }
        }

        for (size_t gid = 0; gid < geometric_unprintable_elems.size(); ++gid) {
            for (auto& f : geometric_unprintable_elems[gid]) {
                auto iter = std::find(used_filaments.begin(), used_filaments.end(), (unsigned)f);
                if (iter != used_filaments.end())
                    geometric_unprintable_idxs[gid].insert(iter - used_filaments.begin());
            }
        }
        return true;
    }

    static bool check_printable(const std::vector<std::set<int>>& groups, const std::map<int,int>& unprintable)
    {
        for (size_t i = 0; i < groups.size(); ++i) {
            auto& group = groups[i];
            for (auto& filament : group) {
                if (auto iter = unprintable.find(filament); iter != unprintable.end() && i == iter->second)
                    return false;
            }
        }
        return true;
    }

    std::vector<unsigned int> collect_sorted_used_filaments(const std::vector<std::vector<unsigned int>>& layer_filaments)
    {
        std::set<unsigned int>used_filaments_set;
        for (const auto& lf : layer_filaments)
            for (const auto& f : lf)
                used_filaments_set.insert(f);
        std::vector<unsigned int>used_filaments(used_filaments_set.begin(), used_filaments_set.end());
        std::sort(used_filaments.begin(), used_filaments.end());
        return used_filaments;
    }

    FlushDistanceEvaluator::FlushDistanceEvaluator(const FlushMatrix& flush_matrix, const std::vector<unsigned int>& used_filaments, const std::vector<std::vector<unsigned int>>& layer_filaments, double p)
    {
        //calc pair counts
        std::vector<std::vector<int>>count_matrix(used_filaments.size(), std::vector<int>(used_filaments.size()));
        for (const auto& lf : layer_filaments) {
            for (auto iter = lf.begin(); iter != lf.end(); ++iter) {
                auto id_iter1 = std::find(used_filaments.begin(), used_filaments.end(), *iter);
                if (id_iter1 == used_filaments.end())
                    continue;
                auto idx1 = id_iter1 - used_filaments.begin();
                for (auto niter = std::next(iter); niter != lf.end(); ++niter) {
                    auto id_iter2 = std::find(used_filaments.begin(), used_filaments.end(), *niter);
                    if (id_iter2 == used_filaments.end())
                        continue;
                    auto idx2 = id_iter2 - used_filaments.begin();
                    count_matrix[idx1][idx2] += 1;
                    count_matrix[idx2][idx1] += 1;
                }
            }
        }

        m_distance_matrix.resize(used_filaments.size(), std::vector<float>(used_filaments.size()));

        for (size_t i = 0; i < used_filaments.size(); ++i) {
            for (size_t j = 0; j < used_filaments.size(); ++j) {
                if (i == j)
                    m_distance_matrix[i][j] = 0;
                else {
                    //TODO: check m_flush_matrix
                    float max_val = std::max(flush_matrix[used_filaments[i]][used_filaments[j]], flush_matrix[used_filaments[j]][used_filaments[i]]);
                    float min_val = std::min(flush_matrix[used_filaments[i]][used_filaments[j]], flush_matrix[used_filaments[j]][used_filaments[i]]);
                    m_distance_matrix[i][j] = (max_val * p + min_val * (1 - p)) * count_matrix[i][j];
                }
            }
        }
    }

    double FlushDistanceEvaluator::get_distance(int idx_a, int idx_b) const
    {
        assert(0 <= idx_a && idx_a < m_distance_matrix.size());
        assert(0 <= idx_b && idx_b < m_distance_matrix.size());

        return m_distance_matrix[idx_a][idx_b];
    }

    std::vector<int> KMediods2::cluster_small_data(const std::map<int, int>& unplaceable_limits, const std::vector<int>& group_size)
    {
        std::vector<int>labels(m_elem_count, -1);
        std::vector<int>new_group_size = group_size;

        for (auto& [elem, center] : unplaceable_limits) {
            if (labels[elem] == -1) {
                int gid = 1 - center;
                labels[elem] = gid;
                new_group_size[gid] -= 1;
            }
        }

        for (auto& label : labels) {
            if (label == -1) {
                int gid = -1;
                for (size_t idx = 0; idx < new_group_size.size(); ++idx) {
                    if (new_group_size[idx] > 0) {
                        gid = idx;
                        break;
                    }
                }
                if (gid != -1) {
                    label = gid;
                    new_group_size[gid] -= 1;
                }
                else {
                    label = 0;
                }
            }
        }

        return labels;
    }

    std::vector<int> KMediods2::assign_cluster_label(const std::vector<int>& center, const std::map<int, int>& unplaceable_limtis, const std::vector<int>& group_size, const FGStrategy& strategy)
    {
        struct Comp {
            bool operator()(const std::pair<int, int>& a, const std::pair<int, int>& b) {
                return a.second > b.second;
            }
        };

        std::vector<std::set<int>>groups(2);
        std::vector<int>new_max_group_size = group_size;
        // store filament idx and distance gap between center 0 and center 1
        std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>, Comp>min_heap;

        for (int i = 0; i < m_elem_count; ++i) {
            if (auto it = unplaceable_limtis.find(i); it != unplaceable_limtis.end()) {
                int gid = it->second;
                assert(gid == 0 || gid == 1);
                groups[1 - gid].insert(i);   // insert to group
                new_max_group_size[1 - gid] = std::max(new_max_group_size[1 - gid] - 1, 0); // decrease group_size
                continue;
            }
            int distance_to_0 = m_evaluator->get_distance(i, center[0]);
            int distance_to_1 = m_evaluator->get_distance(i, center[1]);
            min_heap.push({ i,distance_to_0 - distance_to_1 });
        }

        bool have_enough_size = (min_heap.size() <= (new_max_group_size[0] + new_max_group_size[1]));

        if (have_enough_size || strategy == FGStrategy::BestFit) {
            while (!min_heap.empty()) {
                auto top = min_heap.top();
                min_heap.pop();
                if (groups[0].size() < new_max_group_size[0] && (top.second <= 0 || groups[1].size() >= new_max_group_size[1]))
                    groups[0].insert(top.first);
                else if (groups[1].size() < new_max_group_size[1] && (top.second > 0 || groups[0].size() >= new_max_group_size[0]))
                    groups[1].insert(top.first);
                else {
                    if (top.second <= 0)
                        groups[0].insert(top.first);
                    else
                        groups[1].insert(top.first);
                }
            }
        }
        else {
            while (!min_heap.empty()) {
                auto top = min_heap.top();
                min_heap.pop();
                if (top.second <= 0)
                    groups[0].insert(top.first);
                else
                    groups[1].insert(top.first);
            }
        }

        std::vector<int>labels(m_elem_count);
        for (auto& f : groups[0])
            labels[f] = 0;
        for (auto& f : groups[1])
            labels[f] = 1;

        return labels;
    }

    int KMediods2::calc_cost(const std::vector<int>& labels, const std::vector<int>& medoids)
    {
        int total_cost = 0;
        for (int i = 0; i < m_elem_count; ++i)
            total_cost += m_evaluator->get_distance(i, medoids[labels[i]]);
        return total_cost;
    }

    void KMediods2::do_clustering(const FGStrategy& g_strategy, int timeout_ms)
    {
        FlushTimeMachine T;
        T.time_machine_start();

        if (m_elem_count < m_k) {
            m_cluster_labels = cluster_small_data(m_unplaceable_limits, m_max_cluster_size);
            return;
        }

        std::vector<int>best_labels;
        int best_cost = std::numeric_limits<int>::max();

        for (int center_0 = 0; center_0 < m_elem_count; ++center_0) {
            if (auto iter = m_unplaceable_limits.find(center_0); iter != m_unplaceable_limits.end() && iter->second == 0)
                continue;
            for (int center_1 = 0; center_1 < m_elem_count; ++center_1) {
                if (center_0 == center_1)
                    continue;
                if (auto iter = m_unplaceable_limits.find(center_1); iter != m_unplaceable_limits.end() && iter->second == 1)
                    continue;

                std::vector<int>new_centers = { center_0,center_1 };
                std::vector<int>new_labels = assign_cluster_label(new_centers, m_unplaceable_limits, m_max_cluster_size, g_strategy);

                int new_cost = calc_cost(new_labels, new_centers);
                if (new_cost < best_cost) {
                    best_cost = new_cost;
                    best_labels = new_labels;
                }
                if (T.time_machine_end() > timeout_ms)
                    break;
            }
            if (T.time_machine_end() > timeout_ms)
                break;
        }
        this->m_cluster_labels = best_labels;
    }

    FilamentGroup::FilamentGroup(const FilamentGroupContext& context)
    {
        assert(context.flush_matrix.size() == 2);
        assert(context.flush_matrix.size() == context.max_group_size.size());
        assert(context.max_group_size.size() == context.physical_unprintables.size());
        assert(context.physical_unprintables.size() == context.geometric_unprintables.size());

        m_context = context;
    }

    std::vector<int> FilamentGroup::calc_filament_group(const std::vector<std::vector<unsigned int>>& layer_filaments, const FGStrategy& g_strategy, int* cost)
    {
        std::vector<unsigned int> used_filaments = collect_sorted_used_filaments(layer_filaments);

        int used_filament_num = used_filaments.size();
        if (used_filament_num < 10)
            return calc_filament_group_by_enum(layer_filaments, used_filaments, g_strategy, cost);
        else
            return calc_filament_group_by_pam2(layer_filaments, used_filaments, g_strategy, cost, 100);
    }

    // sorted used_filaments
    std::vector<int> FilamentGroup::calc_filament_group_by_enum(const std::vector<std::vector<unsigned int>>& layer_filaments, const std::vector<unsigned int>& used_filaments, const FGStrategy& g_strategy,int*cost)
    {
        static constexpr int UNPLACEABLE_LIMIT_REWARD = 100;  // reward value if the group result follows the unprintable limit
        static constexpr int MAX_SIZE_LIMIT_REWARD = 10;    // reward value if the group result follows the max size per extruder
        static constexpr int BEST_FIT_LIMIT_REWARD = 1;     // reward value if the group result try to fill the max size per extruder
        auto bit_count_one = [](uint64_t n)
            {
                int count = 0;
                while (n != 0)
                {
                    n &= n - 1;
                    count++;
                }
                return count;
            };

        std::map<int, int>unplaceable_limits;
        {
            // if the filament cannot be placed in both extruder, we just ignore it
            std::vector<std::set<int>>physical_unprintables = m_context.physical_unprintables;
            std::vector<std::set<int>>geometric_unprintables = m_context.geometric_unprintables;
            // TODO: should we instantly fail here later?
            remove_intersection(physical_unprintables[0], physical_unprintables[1]);
            remove_intersection(geometric_unprintables[0], geometric_unprintables[1]);

            for (auto& unprintables : { physical_unprintables, geometric_unprintables }) {
                for (size_t group_id = 0; group_id < 2; ++group_id) {
                    for (size_t elem = 0; elem < used_filaments.size(); ++elem) {
                        for (auto f : unprintables[group_id]) {
                            if (unplaceable_limits.count(f) == 0)
                                unplaceable_limits[f] = group_id;
                        }
                    }
                }
            }
        }

        int used_filament_num = used_filaments.size();
        uint64_t max_group_num = (static_cast<uint64_t>(1) << used_filament_num);

        int best_cost = std::numeric_limits<int>::max();
        std::vector<int>best_label;
        int best_prefer_level = 0;

        for (uint64_t i = 0; i < max_group_num; ++i) {
            std::vector<std::set<int>>groups(2);
            for (int j = 0; j < used_filament_num; ++j) {
                if (i & (static_cast<uint64_t>(1) << j))
                    groups[1].insert(used_filaments[j]);
                else
                    groups[0].insert(used_filaments[j]);
            }

            int prefer_level = 0;

            if (check_printable(groups, unplaceable_limits))
                prefer_level += UNPLACEABLE_LIMIT_REWARD;
            if (groups[0].size() <= m_context.max_group_size[0] && groups[1].size() <= m_context.max_group_size[1])
                prefer_level += MAX_SIZE_LIMIT_REWARD;
            if (FGStrategy::BestFit == g_strategy && groups[0].size() >= m_context.max_group_size[0] && groups[1].size() >= m_context.max_group_size[1])
                prefer_level += BEST_FIT_LIMIT_REWARD;

            std::vector<int>filament_maps(used_filament_num);
            for (int i = 0; i < used_filament_num; ++i) {
                if (groups[0].find(used_filaments[i]) != groups[0].end())
                    filament_maps[i] = 0;
                if (groups[1].find(used_filaments[i]) != groups[1].end())
                    filament_maps[i] = 1;
            }

            int total_cost = reorder_filaments_for_minimum_flush_volume(
                used_filaments,
                filament_maps,
                layer_filaments,
                m_context.flush_matrix,
                get_custom_seq,
                nullptr
            );

            if (prefer_level > best_prefer_level || (prefer_level == best_prefer_level && total_cost < best_cost)) {
                best_prefer_level = prefer_level;
                best_cost = total_cost;
                best_label = filament_maps;
            }
        }

        if (cost)
            *cost = best_cost;

        std::vector<int> filament_labels(m_context.total_filament_num, 0);
        for (int i = 0; i < best_label.size(); ++i)
            filament_labels[used_filaments[i]] = best_label[i];

        return filament_labels;
    }

    // sorted used_filaments
    std::vector<int> FilamentGroup::calc_filament_group_by_pam2(const std::vector<std::vector<unsigned int>>& layer_filaments, const std::vector<unsigned int>& used_filaments, const FGStrategy& g_strategy, int*cost,int timeout_ms)
    {
        std::vector<int>filament_labels_ret(m_context.total_filament_num, 0);
        if (used_filaments.size() == 1)
            return filament_labels_ret;

        std::map<int, int>unplaceable_limits;
        {
            // map the unprintable filaments to idx of used filaments , if not used ,just ignore
            std::vector<std::set<int>> physical_unprintable_idxs, geometric_unprintable_idxs;
            extract_indices(used_filaments, m_context.physical_unprintables, m_context.geometric_unprintables, physical_unprintable_idxs, geometric_unprintable_idxs);
            remove_intersection(physical_unprintable_idxs[0], physical_unprintable_idxs[1]);
            remove_intersection(geometric_unprintable_idxs[0], geometric_unprintable_idxs[1]);
            for (auto& unprintables : { physical_unprintable_idxs, geometric_unprintable_idxs }) {
                for (size_t group_id = 0; group_id < 2; ++group_id) {
                    for(auto f:unprintables[group_id]){
                        if(unplaceable_limits.count(f)==0)
                            unplaceable_limits[f]=group_id;
                    }
                }
            }
        }

        auto distance_evaluator = std::make_shared<FlushDistanceEvaluator>(m_context.flush_matrix[0], used_filaments, layer_filaments);
        KMediods2 PAM((int)used_filaments.size(),distance_evaluator);
        PAM.set_max_cluster_size(m_context.max_group_size);
        PAM.set_unplaceable_limits(unplaceable_limits);
        PAM.do_clustering(g_strategy, timeout_ms);
        std::vector<int>filament_labels = PAM.get_cluster_labels();

        if(cost)
            *cost=reorder_filaments_for_minimum_flush_volume(used_filaments,filament_labels,layer_filaments,m_context.flush_matrix,std::nullopt,nullptr);

        for (int i = 0; i < filament_labels.size(); ++i)
            filament_labels_ret[used_filaments[i]] = filament_labels[i];
        return filament_labels_ret;
    }

}


