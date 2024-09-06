#include "FilamentGroup.hpp"
#include "GCode/ToolOrderUtils.hpp"
#include <queue>
#include <random>
#include <cassert>
#include <sstream>

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

    static int calc_color_distance(const Color &src, const Color &dst)
    {
        double rmean = (src.r + dst.r) / 2.f;
        double dr = src.r - dst.r;
        double dg = src.g - dst.g;
        double db = src.b - dst.b;

        return sqrt((512 + rmean) / 256.f * dr * dr + 4 * dg * dg + (767 - rmean) / 256 * db * db);
    }

    // clear the array and heap,save the groups in heap to the array
    static void change_memoryed_heaps_to_arrays(FilamentGroupUtils::MemoryedGroupHeap& heap,const int total_filament_num,const std::vector<unsigned int>& used_filaments, std::vector<std::vector<int>>& arrs)
    {
        // switch the label idx
        arrs.clear();
        while (!heap.empty()) {
            auto top = heap.top();
            heap.pop();
            std::vector<int> labels_tmp(total_filament_num, 0);
            for (size_t idx = 0; idx < top.group.size(); ++idx)
                labels_tmp[used_filaments[idx]] = top.group[idx];
            arrs.emplace_back(std::move(labels_tmp));
        }
    }

    Color::Color(const std::string& hexstr) {
        if (hexstr.empty() || (hexstr.length() != 9 && hexstr.length() != 7) || hexstr[0] != '#')
        {
            assert(false);
            r = 0, g = 0, b = 0, a = 255;
            return;
        }

        auto hexToByte = [](const std::string& hex)->unsigned char
            {
                unsigned int byte;
                std::istringstream(hex) >> std::hex >> byte;
                return static_cast<unsigned char>(byte);
            };
        r = hexToByte(hexstr.substr(1, 2));
        g = hexToByte(hexstr.substr(3, 2));
        b = hexToByte(hexstr.substr(5, 2));
        if (hexstr.size() == 9)
            a = hexToByte(hexstr.substr(7, 2));
    }

    std::vector<int> select_best_group_for_ams(const std::vector<std::vector<int>>& map_lists, const std::vector<unsigned int>& used_filaments, const std::vector<std::string>& used_filament_colors_str, const std::vector<std::vector<std::string>>& ams_filament_colors_str)
    {
        assert(used_filaments.size() == ams_filament_colors_str.size());
        // change the color str to real colors
        std::vector<Color>used_filament_colors;
        std::vector<std::vector<Color>>ams_filament_colors;
        for (auto& item : used_filament_colors_str)
            used_filament_colors.emplace_back(Color(item));

        for (auto& arr : ams_filament_colors_str) {
            std::vector<Color>tmp;
            for (auto& item : arr)
                tmp.emplace_back(Color(item));
            ams_filament_colors.emplace_back(std::move(tmp));
        }


        int best_cost = std::numeric_limits<int>::max();
        std::vector<int>best_map;
        for (auto& map : map_lists) {
            std::vector<std::vector<Color>>group_colors(2);

            for (size_t i = 0; i < used_filaments.size(); ++i) {
                if (map[used_filaments[i]] == 0)
                    group_colors[0].emplace_back(used_filament_colors[i]);
                else
                    group_colors[1].emplace_back(used_filament_colors[i]);
            }
            int tmp_cost = 0;
            for (size_t i = 0; i < 2; ++i) {
                if (group_colors[i].empty() || ams_filament_colors[i].empty())
                    continue;
                std::vector<std::vector<float>>distance_matrix(group_colors[i].size(), std::vector<float>(ams_filament_colors[i].size()));

                // calculate color distance matrix
                for (size_t src = 0; src < group_colors[i].size(); ++src) {
                    for (size_t dst = 0; dst < ams_filament_colors[i].size(); ++dst)
                        distance_matrix[src][dst] = calc_color_distance(group_colors[i][src], ams_filament_colors[i][dst]);
                }

                // get min cost by min cost max flow
                std::vector<int>l_nodes(group_colors[i].size()), r_nodes(ams_filament_colors[i].size());
                std::iota(l_nodes.begin(), l_nodes.end(), 0);
                std::iota(r_nodes.begin(), r_nodes.end(), 0);
                MCMF mcmf(distance_matrix, l_nodes, r_nodes);
                auto ams_map = mcmf.solve();

                for (size_t idx = 0; idx < ams_map.size(); ++idx) {
                    if (ams_map[idx] == -1)
                        continue;
                    tmp_cost += distance_matrix[idx][ams_map[idx]];
                }
            }

            if (tmp_cost < best_cost) {
                best_cost = tmp_cost;
                best_map = map;
            }
        }

        return best_map;
    }


    void FilamentGroupUtils::update_memoryed_groups(const MemoryedGroup& item, const double gap_threshold, MemoryedGroupHeap& groups)
    {
        auto emplace_if_accepatle = [gap_threshold](MemoryedGroupHeap& heap, const MemoryedGroup& elem, const MemoryedGroup& best) {
            if (best.cost == 0) {
                if (std::abs(elem.cost - best.cost) <= ABSOLUTE_FLUSH_GAP_TOLERANCE)
                    heap.push(elem);
                return;
            }
            double gap_rate = (double)std::abs(elem.cost - best.cost) / (double)best.cost;
            if (gap_rate < gap_threshold)
                heap.push(elem);
            };

        if (groups.empty()) {
            groups.push(item);
        }
        else {
            auto top = groups.top();
            // we only memory items with the highest prefer level
            if (top.prefer_level > item.prefer_level)
                return;
            else if (top.prefer_level == item.prefer_level) {
                if (top.cost <= item.cost) {
                    emplace_if_accepatle(groups, item, top);
                }
                // find a group with lower cost, rebuild the heap
                else {
                    MemoryedGroupHeap new_heap;
                    new_heap.push(item);
                    while (!groups.empty()) {
                        auto top = groups.top();
                        groups.pop();
                        emplace_if_accepatle(new_heap, top, item);
                    }
                    groups = std::move(new_heap);
                }
            }
            // find a group with the higher prefer level, rebuild the heap
            else {
                groups = MemoryedGroupHeap();
                groups.push(item);
            }
        }
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
        FilamentGroupUtils::FlushTimeMachine T;
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

                {
                    MemoryedGroup g;
                    g.prefer_level = 1; // in non enum mode, we use the same prefer level
                    g.cost = new_cost;
                    g.group = new_labels;
                    update_memoryed_groups(g, memory_threshold, memoryed_groups);
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

        MemoryedGroupHeap memoryed_groups;

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

            {
                MemoryedGroup mg;
                mg.prefer_level = prefer_level;
                mg.cost = total_cost;
                mg.group = std::move(filament_maps);
                update_memoryed_groups(mg, memory_threshold, memoryed_groups);
            }
        }

        if (cost)
            *cost = best_cost;

        std::vector<int> filament_labels(m_context.total_filament_num, 0);
        for (size_t i = 0; i < best_label.size(); ++i)
            filament_labels[used_filaments[i]] = best_label[i];


        change_memoryed_heaps_to_arrays(memoryed_groups, m_context.total_filament_num, used_filaments, m_memoryed_groups);

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
        PAM.set_memory_threshold(memory_threshold);
        PAM.do_clustering(g_strategy, timeout_ms);
        std::vector<int>filament_labels = PAM.get_cluster_labels();


        {
            auto memoryed_groups = PAM.get_memoryed_groups();
            change_memoryed_heaps_to_arrays(memoryed_groups, m_context.total_filament_num, used_filaments, m_memoryed_groups);
        }

        if(cost)
            *cost=reorder_filaments_for_minimum_flush_volume(used_filaments,filament_labels,layer_filaments,m_context.flush_matrix,std::nullopt,nullptr);

        for (int i = 0; i < filament_labels.size(); ++i)
            filament_labels_ret[used_filaments[i]] = filament_labels[i];
        return filament_labels_ret;
    }

}


