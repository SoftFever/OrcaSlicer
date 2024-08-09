#include "ToolOrderUtils.hpp"
#include <queue>
#include <set>
#include <map>
#include <cmath>
#include <boost/multiprecision/cpp_int.hpp>

namespace Slic3r
{

    //solve the problem by searching the least flush of current filament
    static std::vector<unsigned int> solve_extruder_order_with_greedy(const std::vector<std::vector<float>>& wipe_volumes,
        const std::vector<unsigned int> curr_layer_extruders,
        const std::optional<unsigned int>& start_extruder_id,
        float* min_cost)
    {
        float cost = 0;
        std::vector<unsigned int> best_seq;
        std::vector<bool>is_visited(curr_layer_extruders.size(), false);
        std::optional<unsigned int>prev_filament = start_extruder_id;
        int idx = curr_layer_extruders.size();
        while (idx > 0) {
            if (!prev_filament) {
                auto iter = std::find_if(is_visited.begin(), is_visited.end(), [](auto item) {return item == 0; });
                assert(iter != is_visited.end());
                prev_filament = curr_layer_extruders[iter - is_visited.begin()];
            }
            int target_idx = -1;
            int target_cost = std::numeric_limits<int>::max();
            for (size_t k = 0; k < is_visited.size(); ++k) {
                if (!is_visited[k]) {
                    if (wipe_volumes[*prev_filament][curr_layer_extruders[k]] < target_cost) {
                        target_idx = k;
                        target_cost = wipe_volumes[*prev_filament][curr_layer_extruders[k]];
                    }
                }
            }
            assert(target_idx != -1);
            cost += target_cost;
            best_seq.emplace_back(curr_layer_extruders[target_idx]);
            prev_filament = curr_layer_extruders[target_idx];
            is_visited[target_idx] = true;
            idx -= 1;
        }
        if (min_cost)
            *min_cost = cost;
        return best_seq;
    }

    //solve the problem by forcasting one layer
    static std::vector<unsigned int> solve_extruder_order_with_forcast(const std::vector<std::vector<float>>& wipe_volumes,
        std::vector<unsigned int> curr_layer_extruders,
        std::vector<unsigned int> next_layer_extruders,
        const std::optional<unsigned int>& start_extruder_id,
        float* min_cost)
    {
        std::sort(curr_layer_extruders.begin(), curr_layer_extruders.end());
        std::sort(next_layer_extruders.begin(), next_layer_extruders.end());
        float best_cost = std::numeric_limits<float>::max();
        std::vector<unsigned int>best_seq;

        do {
            std::optional<unsigned int>prev_extruder_1 = start_extruder_id;
            float curr_layer_cost = 0;
            for (size_t idx = 0; idx < curr_layer_extruders.size(); ++idx) {
                if (prev_extruder_1)
                    curr_layer_cost += wipe_volumes[*prev_extruder_1][curr_layer_extruders[idx]];
                prev_extruder_1 = curr_layer_extruders[idx];
            }
            if (curr_layer_cost > best_cost)
                continue;
            do {
                std::optional<unsigned int>prev_extruder_2 = prev_extruder_1;
                float total_cost = curr_layer_cost;

                for (size_t idx = 0; idx < next_layer_extruders.size(); ++idx) {
                    if (prev_extruder_2)
                        total_cost += wipe_volumes[*prev_extruder_2][next_layer_extruders[idx]];
                    prev_extruder_2 = next_layer_extruders[idx];
                }

                if (total_cost < best_cost) {
                    best_cost = total_cost;
                    best_seq = curr_layer_extruders;
                }
            } while (std::next_permutation(next_layer_extruders.begin(), next_layer_extruders.end()));
        } while (std::next_permutation(curr_layer_extruders.begin(), curr_layer_extruders.end()));

        if (min_cost) {
            float real_cost = 0;
            std::optional<unsigned int>prev_extruder = start_extruder_id;
            for (size_t idx = 0; idx < best_seq.size(); ++idx) {
                if (prev_extruder)
                    real_cost += wipe_volumes[*prev_extruder][best_seq[idx]];
                prev_extruder = best_seq[idx];
            }
            *min_cost = real_cost;
        }
        return best_seq;
    }

    // Shortest hamilton path problem
    static std::vector<unsigned int> solve_extruder_order(const std::vector<std::vector<float>>& wipe_volumes,
        std::vector<unsigned int> all_extruders,
        std::optional<unsigned int> start_extruder_id,
        float* min_cost)
    {
        bool add_start_extruder_flag = false;

        if (start_extruder_id) {
            auto start_iter = std::find(all_extruders.begin(), all_extruders.end(), start_extruder_id);
            if (start_iter == all_extruders.end())
                all_extruders.insert(all_extruders.begin(), *start_extruder_id), add_start_extruder_flag = true;
            else
                std::swap(*all_extruders.begin(), *start_iter);
        }
        else {
            start_extruder_id = all_extruders.front();
        }

        unsigned int iterations = (1 << all_extruders.size());
        unsigned int final_state = iterations - 1;
        std::vector<std::vector<float>>cache(iterations, std::vector<float>(all_extruders.size(), 0x7fffffff));
        std::vector<std::vector<int>>prev(iterations, std::vector<int>(all_extruders.size(), -1));
        cache[1][0] = 0.;
        for (unsigned int state = 0; state < iterations; ++state) {
            if (state & 1) {
                for (unsigned int target = 0; target < all_extruders.size(); ++target) {
                    if (state >> target & 1) {
                        for (unsigned int mid_point = 0; mid_point < all_extruders.size(); ++mid_point) {
                            if (state >> mid_point & 1) {
                                auto tmp = cache[state - (1 << target)][mid_point] + wipe_volumes[all_extruders[mid_point]][all_extruders[target]];
                                if (cache[state][target] > tmp) {
                                    cache[state][target] = tmp;
                                    prev[state][target] = mid_point;
                                }
                            }
                        }
                    }
                }
            }
        }

        //get res
        float cost = std::numeric_limits<float>::max();
        int final_dst = 0;
        for (unsigned int dst = 0; dst < all_extruders.size(); ++dst) {
            if (all_extruders[dst] != start_extruder_id && cost > cache[final_state][dst]) {
                cost = cache[final_state][dst];
                if (min_cost)
                    *min_cost = cost;
                final_dst = dst;
            }
        }

        std::vector<unsigned int>path;
        unsigned int curr_state = final_state;
        int curr_point = final_dst;
        while (curr_point != -1) {
            path.emplace_back(all_extruders[curr_point]);
            auto mid_point = prev[curr_state][curr_point];
            curr_state -= (1 << curr_point);
            curr_point = mid_point;
        };

        if (add_start_extruder_flag)
            path.pop_back();

        std::reverse(path.begin(), path.end());
        return path;
    }


    // get best filament order of single nozzle
    std::vector<unsigned int> get_extruders_order(const std::vector<std::vector<float>>& wipe_volumes,
        const std::vector<unsigned int>& curr_layer_extruders,
        const std::vector<unsigned int>& next_layer_extruders,
        const std::optional<unsigned int>& start_extruder_id,
        bool use_forcast,
        float* cost)
    {
        if (curr_layer_extruders.empty()) {
            if (cost)
                *cost = 0;
            return curr_layer_extruders;
        }
        if (curr_layer_extruders.size() == 1) {
            if (cost) {
                *cost = 0;
                if (start_extruder_id)
                    *cost = wipe_volumes[*start_extruder_id][curr_layer_extruders[0]];
            }
            return curr_layer_extruders;
        }

        if (use_forcast)
            return solve_extruder_order_with_forcast(wipe_volumes, curr_layer_extruders, next_layer_extruders, start_extruder_id, cost);
        else if (curr_layer_extruders.size() <= 20)
            return solve_extruder_order(wipe_volumes, curr_layer_extruders, start_extruder_id, cost);
        else
            return solve_extruder_order_with_greedy(wipe_volumes, curr_layer_extruders, start_extruder_id, cost);
    }



    int reorder_filaments_for_minimum_flush_volume(const std::vector<unsigned int>& filament_lists,
        const std::vector<int>& filament_maps,
        const std::vector<std::vector<unsigned int>>& layer_filaments,
        const std::vector<FlushMatrix>& flush_matrix,
        std::optional<std::function<bool(int, std::vector<int>&)>> get_custom_seq,
        std::vector<std::vector<unsigned int>>* filament_sequences)
    {
        //only when layer filament num <= 5,we do forcast
        constexpr int max_n_with_forcast = 5;
        int cost = 0;
        std::vector<std::set<unsigned int>>groups(2); //save the grouped filaments
        std::vector<std::vector<std::vector<unsigned int>>> layer_sequences(2); //save the reordered filament sequence by group
        std::map<size_t, std::vector<int>> custom_layer_filament_map; //save the custom layers,second key stores the last extruder of that layer by group
        std::map<size_t, std::vector<unsigned int>> custom_layer_sequence_map; // save the filament sequences of custom layer

        // group the filament
        for (int i = 0; i < filament_maps.size(); ++i) {
            if (filament_maps[i] == 0)
                groups[0].insert(filament_lists[i]);
            if (filament_maps[i] == 1)
                groups[1].insert(filament_lists[i]);
        }

        // store custom layer sequence
        for (size_t layer = 0; layer < layer_filaments.size(); ++layer) {
            const auto& curr_lf = layer_filaments[layer];

            std::vector<int>custom_filament_seq;
            if (get_custom_seq && (*get_custom_seq)(layer, custom_filament_seq) && !custom_filament_seq.empty()) {
                std::vector<unsigned int> unsign_custom_extruder_seq;
                for (int extruder : custom_filament_seq) {
                    unsigned int unsign_extruder = static_cast<unsigned int>(extruder) - 1;
                    auto it = std::find(curr_lf.begin(), curr_lf.end(), unsign_extruder);
                    if (it != curr_lf.end())
                        unsign_custom_extruder_seq.emplace_back(unsign_extruder);
                }
                assert(curr_lf.size() == unsign_custom_extruder_seq.size());

                custom_layer_sequence_map[layer] = unsign_custom_extruder_seq;
                custom_layer_filament_map[layer].resize(2, -1);

                for (auto iter = unsign_custom_extruder_seq.rbegin(); iter != unsign_custom_extruder_seq.rend(); ++iter) {
                    if (groups[0].find(*iter) != groups[0].end() && custom_layer_filament_map[layer][0] == -1)
                        custom_layer_filament_map[layer][0] = *iter;
                    if (groups[1].find(*iter) != groups[1].end() && custom_layer_filament_map[layer][1] == -1)
                        custom_layer_filament_map[layer][1] = *iter;
                }
            }
        }

        using uint128_t = boost::multiprecision::uint128_t;
        auto extruders_to_hash_key = [](const std::vector<unsigned int>& curr_layer_extruders,
            const std::vector<unsigned int>& next_layer_extruders,
            const std::optional<unsigned int>& prev_extruder,
            bool use_forcast)->uint128_t
            {
                uint128_t hash_key = 0;
                //31-0 bit define current layer extruder,63-32 bit define next layer extruder,95~64 define prev extruder
                if (prev_extruder)
                    hash_key |= (uint128_t(1) << (64 + *prev_extruder));

                if (use_forcast) {
                    for (auto item : next_layer_extruders)
                        hash_key |= (uint128_t(1) << (32 + item));
                }

                for (auto item : curr_layer_extruders)
                    hash_key |= (uint128_t(1) << item);
                return hash_key;
            };


        // get best layer sequence by group
        for (size_t idx = 0; idx < groups.size(); ++idx) {
            // case with one group
            if (groups[idx].empty())
                continue;
            std::optional<unsigned int>current_extruder_id;

            std::unordered_map<uint128_t, std::pair<float, std::vector<unsigned int>>> caches;

            for (size_t layer = 0; layer < layer_filaments.size(); ++layer) {
                const auto& curr_lf = layer_filaments[layer];
                std::vector<int>custom_filament_seq;
                if (get_custom_seq && (*get_custom_seq)(layer, custom_filament_seq) && !custom_filament_seq.empty()) {
                    if (custom_layer_filament_map[layer][idx] != -1)
                        current_extruder_id = (unsigned int)(custom_layer_filament_map[layer][idx]);
                    //insert an empty array
                    if (filament_sequences)
                        layer_sequences[idx].emplace_back(std::vector<unsigned int>());
                    continue;
                }

                std::vector<unsigned int>filament_used_in_group;
                for (const auto& filament : curr_lf) {
                    if (groups[idx].find(filament) != groups[idx].end())
                        filament_used_in_group.emplace_back(filament);
                }

                std::vector<unsigned int>filament_used_in_group_next_layer;
                {
                    std::vector<unsigned int>next_lf;
                    if (layer + 1 < layer_filaments.size())
                        next_lf = layer_filaments[layer + 1];
                    for (const auto& filament : next_lf) {
                        if (groups[idx].find(filament) != groups[idx].end())
                            filament_used_in_group_next_layer.emplace_back(filament);
                    }
                }

                bool use_forcast = (filament_used_in_group.size() <= max_n_with_forcast && filament_used_in_group_next_layer.size() <= max_n_with_forcast);
                float tmp_cost = 0;
                std::vector<unsigned int>sequence;
                uint128_t hash_key = extruders_to_hash_key(filament_used_in_group, filament_used_in_group_next_layer, current_extruder_id, use_forcast);
                if (auto iter = caches.find(hash_key); iter != caches.end()) {
                    tmp_cost = iter->second.first;
                    sequence = iter->second.second;
                }
                else {
                    sequence = get_extruders_order(flush_matrix[idx], filament_used_in_group, filament_used_in_group_next_layer, current_extruder_id, use_forcast, &tmp_cost);
                    caches[hash_key] = { tmp_cost,sequence };
                }

                assert(sequence.size() == filament_used_in_group.size());

                if (filament_sequences)
                    layer_sequences[idx].emplace_back(sequence);

                if (!sequence.empty())
                    current_extruder_id = sequence.back();
                cost += tmp_cost;
            }
        }

        // get the final layer sequences
        // if only have one group,we need to check whether layer sequence[idx] is valid
        if (filament_sequences) {
            filament_sequences->clear();
            filament_sequences->resize(layer_filaments.size());

            bool last_group = 0;
            //if last_group == 0,print group 0 first ,else print group 1 first
            if (!custom_layer_sequence_map.empty()) {
                int custom_first_layer = custom_layer_sequence_map.begin()->first;
                bool custom_first_group = groups[0].count(custom_first_layer) ? 0 : 1;
                last_group = (custom_first_layer & 1) ? !custom_first_group : custom_first_group;
            }

            for (size_t layer = 0; layer < layer_filaments.size(); ++layer) {
                auto& curr_layer_seq = (*filament_sequences)[layer];
                if (custom_layer_sequence_map.find(layer) != custom_layer_sequence_map.end()) {
                    curr_layer_seq = custom_layer_sequence_map[layer];
                    if (!curr_layer_seq.empty()) {
                        last_group = groups[0].count(curr_layer_seq.back()) ? 0 : 1;
                    }
                    continue;
                }
                if (last_group) {
                    if (!layer_sequences[1].empty())
                        curr_layer_seq.insert(curr_layer_seq.end(), layer_sequences[1][layer].begin(), layer_sequences[1][layer].end());
                    if (!layer_sequences[0].empty())
                        curr_layer_seq.insert(curr_layer_seq.end(), layer_sequences[0][layer].begin(), layer_sequences[0][layer].end());
                }
                else {
                    if (!layer_sequences[0].empty())
                        curr_layer_seq.insert(curr_layer_seq.end(), layer_sequences[0][layer].begin(), layer_sequences[0][layer].end());
                    if (!layer_sequences[1].empty())
                        curr_layer_seq.insert(curr_layer_seq.end(), layer_sequences[1][layer].begin(), layer_sequences[1][layer].end());
                }
                last_group = !last_group;
            }
        }

        return cost;
    }

}
