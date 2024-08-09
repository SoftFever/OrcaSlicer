#include "FilamentGroup.hpp"
#include "GCode/ToolOrderUtils.hpp"
#include <queue>

namespace Slic3r
{
    void KMediods::fit(const FGStrategy&g_strategy , int timeout_ms)
    {
        std::vector<int>best_medoids;
        std::vector<int>best_labels;
        int best_cost = std::numeric_limits<int>::max();

        FlushTimeMachine T;
        T.time_machine_start();

        int count = 0;
        while (true)
        {
            std::vector<int>medoids;
            std::vector<int>labels;
            if (count == 0)
                medoids = initialize(INIT_TYPE::Farthest);
            else
                medoids = initialize(INIT_TYPE::Random);

            labels = assign_label(medoids,g_strategy);
            int cost = calc_cost(labels, medoids);

            for (int i = 0; i < m_filament_num; ++i) {
                if (std::find(medoids.begin(), medoids.end(), i) != medoids.end())
                    continue;

                for (int j = 0; j < 2; ++j) {
                    std::vector<int> new_medoids = medoids;
                    new_medoids[j] = i;
                    std::vector<int> new_labels = assign_label(new_medoids,g_strategy);
                    int new_cost = calc_cost(new_labels, new_medoids);

                    if (new_cost < cost)
                    {
                        labels = new_labels;
                        cost = new_cost;
                        medoids = new_medoids;
                    }
                }
            }

            if (cost < best_cost)
            {
                best_cost = cost;
                best_labels = labels;
                best_medoids = medoids;
            }
            count += 1;

            if (T.time_machine_end() > timeout_ms || m_medoids_set.size() == (m_filament_num * (m_filament_num - 1) / 2))
                break;
        }

        this->m_filament_labels = best_labels;
    }

    std::vector<int> KMediods::assign_label(const std::vector<int>& medoids,const FGStrategy&g_strategy)
    {
        std::vector<int>labels(m_filament_num);
        struct Comp {
            bool operator()(const std::pair<int, int>& a, const std::pair<int, int>& b) {
                return a.second > b.second;
            }
        };
        std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>,Comp>min_heap;

        for (int i = 0; i < m_filament_num; ++i) {
            int distancec_to_0 = m_distance_matrix[i][medoids[0]];
            int distancec_to_1 = m_distance_matrix[i][medoids[1]];
            min_heap.push({ i,distancec_to_0 - distancec_to_1 });
        }
        std::set<int> group_0, group_1;
        bool have_enough_size = (m_filament_num <= (m_max_group_size[0] + m_max_group_size[1]));
        if (have_enough_size || g_strategy == FGStrategy::BestFit) {
            while (!min_heap.empty()) {
                auto top = min_heap.top();
                min_heap.pop();
                if (group_0.size() < m_max_group_size[0] && (top.second <= 0 || group_1.size() >= m_max_group_size[1]))
                    group_0.insert(top.first);
                else if (group_1.size() < m_max_group_size[1] && (top.second > 0 || group_0.size() >= m_max_group_size[0]))
                    group_1.insert(top.first);
                else {
                    if (top.second <= 0)
                        group_0.insert(top.first);
                    else
                        group_1.insert(top.first);
                }
            }
        }
        else if (g_strategy == FGStrategy::BestCost) {
            while (!min_heap.empty()) {
                auto top = min_heap.top();
                min_heap.pop();
                if (top.second <= 0)
                    group_0.insert(top.first);
                else
                    group_1.insert(top.first);
            }
        }

        for (auto& item : group_0)
            labels[item] = 0;
        for (auto& item : group_1)
            labels[item] = 1;

        return labels;
    }

    int KMediods::calc_cost(const std::vector<int>& labels, const std::vector<int>& medoids)
    {
        int total_cost = 0;
        for (int i = 0; i < m_filament_num; ++i)
            total_cost += m_distance_matrix[i][medoids[labels[i]]];
        return total_cost;
    }

    std::vector<int> KMediods::initialize(INIT_TYPE type)
    {
        auto hash_func = [](int n1, int n2) {
            return n1 * 100 + n2;
            };
        srand(time(nullptr));
        std::vector<int>ret;
        if (type == INIT_TYPE::Farthest) {
            //get the farthest items
            int target_i = 0, target_j = 0, target_val = std::numeric_limits<int>::min();
            for (int i = 0; i < m_distance_matrix.size(); ++i) {
                for (int j = 0; j < m_distance_matrix[0].size(); ++j) {
                    if (i != j && m_distance_matrix[i][j] > target_val) {
                        target_val = m_distance_matrix[i][j];
                        target_i = i;
                        target_j = j;
                    }
                }
            }
            ret.emplace_back(std::min(target_i, target_j));
            ret.emplace_back(std::max(target_i, target_j));
        }
        else if (type == INIT_TYPE::Random) {
            while (true) {
                std::vector<int>medoids;
                while (medoids.size() < k)
                {
                    int candidate = rand() % m_filament_num;
                    if (std::find(medoids.begin(), medoids.end(), candidate) == medoids.end())
                        medoids.push_back(candidate);
                }
                std::sort(medoids.begin(), medoids.end());

                if (m_medoids_set.find(hash_func(medoids[0], medoids[1])) != m_medoids_set.end() && m_medoids_set.size() != (m_filament_num * (m_filament_num - 1) / 2))
                    continue;
                else {
                    ret = medoids;
                    break;
                }
            }
        }
        m_medoids_set.insert(hash_func(ret[0],ret[1]));
        return ret;
    }


    std::vector<int> FilamentGroup::calc_filament_group(const std::vector<std::vector<unsigned int>>& layer_filaments, const FGStrategy& g_strategy,int* cost)
    {
        std::set<unsigned int>used_filaments_set;
        for (const auto& lf : layer_filaments)
            for (const auto& extruder : lf)
                used_filaments_set.insert(extruder);

        std::vector<unsigned int>used_filaments = std::vector<unsigned int>(used_filaments_set.begin(), used_filaments_set.end());
        std::sort(used_filaments.begin(), used_filaments.end());

        int used_filament_num = used_filaments.size();

        std::vector<int> filament_labels(m_total_filament_num, 0);

        if (used_filament_num <= 1) {
            if (cost)
                *cost = 0;
            return filament_labels;
        }
        if (used_filament_num < 10)
            return calc_filament_group_by_enum(layer_filaments, used_filaments, g_strategy, cost);
        else
            return calc_filament_group_by_pam(layer_filaments, used_filaments, g_strategy, cost, 100);

    }

    std::vector<int> FilamentGroup::calc_filament_group_by_enum(const std::vector<std::vector<unsigned int>>& layer_filaments, const std::vector<unsigned int>& used_filaments, const FGStrategy& g_strategy,int*cost)
    {
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

        int used_filament_num = used_filaments.size();
        bool have_enough_size = (used_filament_num <= (m_max_group_size[0] + m_max_group_size[1]));

        uint64_t max_group_num = static_cast<uint64_t>(1 << used_filament_num);
        int best_cost = std::numeric_limits<int>::max();
        std::vector<int>best_label;

        for (uint64_t i = 0; i < max_group_num; ++i) {
            int num_to_group_1 = bit_count_one(i);
            int num_to_group_0 = used_filament_num - num_to_group_1;
            bool should_accept = false;
            if (have_enough_size)
                should_accept = (num_to_group_0 <= m_max_group_size[0] && num_to_group_1 <= m_max_group_size[1]);
            else if (g_strategy == FGStrategy::BestCost)
                should_accept = true;
            else if (g_strategy == FGStrategy::BestFit)
                should_accept = (num_to_group_0 >= m_max_group_size[0] && num_to_group_1 >= m_max_group_size[1]);

            if (!should_accept)
                continue;

            std::set<int>group_0, group_1;
            for (int j = 0; j < used_filament_num; ++j) {
                if (i & static_cast<uint64_t>(1 << j))
                    group_1.insert(used_filaments[j]);
                else
                    group_0.insert(used_filaments[j]);
            }

            std::vector<int>filament_maps(used_filament_num);
            for (int i = 0; i < used_filament_num; ++i) {
                if (group_0.find(used_filaments[i]) != group_0.end())
                    filament_maps[i] = 0;
                if (group_1.find(used_filaments[i]) != group_1.end())
                    filament_maps[i] = 1;
            }

            int total_cost = reorder_filaments_for_minimum_flush_volume(
                used_filaments,
                filament_maps,
                layer_filaments,
                m_flush_matrix,
                get_custom_seq,
                nullptr
            );

            if (total_cost < best_cost) {
                best_cost = total_cost;
                best_label = filament_maps;
            }
        }

        if (cost)
            *cost = best_cost;

        std::vector<int> filament_labels(m_total_filament_num, 0);
        for (int i = 0; i < best_label.size(); ++i)
            filament_labels[used_filaments[i]] = best_label[i];

        return filament_labels;
    }

    std::vector<int> FilamentGroup::calc_filament_group_by_pam(const std::vector<std::vector<unsigned int>>& layer_filaments, const std::vector<unsigned int>& used_filaments, const FGStrategy& g_strategy, int*cost,int timeout_ms)
    {
        std::vector<int>filament_labels_ret(m_total_filament_num, 0);
        int used_filament_num = used_filaments.size();
        if (used_filaments.size() == 1)
            return filament_labels_ret;
        //calc pair counts
        std::vector<std::vector<int>>count_matrix(used_filament_num, std::vector<int>(used_filament_num));
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

        //calc distance matrix
        std::vector<std::vector<float>>distance_matrix(used_filament_num, std::vector<float>(used_filament_num));
        for (size_t i = 0; i < used_filaments.size(); ++i) {
            for (size_t j = 0; j < used_filaments.size(); ++j) {
                if (i == j)
                    distance_matrix[i][j] = 0;
                else {
                    //TODO: check m_flush_matrix
                    float max_val = std::max(m_flush_matrix[0][used_filaments[i]][used_filaments[j]], m_flush_matrix[0][used_filaments[j]][used_filaments[i]]);
                    float min_val = std::min(m_flush_matrix[0][used_filaments[i]][used_filaments[j]], m_flush_matrix[0][used_filaments[j]][used_filaments[i]]);

                    double p = 0.65;
                    distance_matrix[i][j] = (max_val * p + min_val * (1 - p)) * count_matrix[i][j];
                }
            }
        }

        KMediods PAM(distance_matrix, used_filament_num, m_max_group_size);
        PAM.fit(g_strategy, timeout_ms);
        std::vector<int>filament_labels = PAM.get_filament_labels();

        if(cost)
            *cost=reorder_filaments_for_minimum_flush_volume(used_filaments,filament_labels,layer_filaments,m_flush_matrix,std::nullopt,nullptr);

        for (int i = 0; i < filament_labels.size(); ++i)
            filament_labels_ret[used_filaments[i]] = filament_labels[i];
        return filament_labels_ret;
    }

}


