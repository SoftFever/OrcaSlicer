#include "FilamentGroup.hpp"
#include "GCode/ToolOrdering.hpp"

namespace Slic3r
{
    int FilamentGroup::calc_filament_group(const std::vector<std::vector<unsigned int>>& layer_filaments)
    {
        std::set<unsigned int>used_filaments;
        for (const auto& lf : layer_filaments)
            for (const auto& extruder : lf)
                used_filaments.insert(extruder);

        m_filament_labels.resize(used_filaments.size());
        m_used_filaments = std::vector<unsigned int>(used_filaments.begin(), used_filaments.end());
        std::sort(m_used_filaments.begin(), m_used_filaments.end());

        if (m_filament_num <= 1)
            return 0;
        if (m_filament_num < 10)
            return calc_filament_group_by_enum(layer_filaments);
        else
            return calc_filament_group_by_pam(layer_filaments,300);

    }

    int FilamentGroup::calc_filament_group_by_enum(const std::vector<std::vector<unsigned int>>& layer_filaments)
    {
        auto bit_count_one = [](int n)
        {
            int count = 0;
            while (n != 0)
            {
                n &= n - 1;
                count++;
            }
            return count;
        };

        uint64_t max_group_num = (1 << m_filament_num);
        int best_cost = std::numeric_limits<int>::max();
        std::vector<int>best_label;

        for (uint64_t i = 0; i < max_group_num; ++i) {
            int num_to_group_1 = bit_count_one(i);
            if (num_to_group_1 > m_max_group_size[1] || (m_filament_num - num_to_group_1) > m_max_group_size[0])
                continue;
            std::set<int>group_0, group_1;
            for (int j = 0; j < m_filament_num; ++j) {
                if (i & (1 << j))
                    group_1.insert(m_used_filaments[j]);
                else
                    group_0.insert(m_used_filaments[j]);
            }

            if (group_0.size() < m_max_group_size[0] && group_1.size() < m_max_group_size[1]){

                std::vector<int>filament_maps(m_filament_num);
                for (int i = 0; i < m_filament_num; ++i) {
                    if (group_0.find(m_used_filaments[i]) != group_0.end())
                        filament_maps[i] = 0;
                    if (group_1.find(m_used_filaments[i]) != group_1.end())
                        filament_maps[i] = 1;
                }

                int total_cost = reorder_filaments_for_minimum_flush_volume(
                    m_used_filaments,
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
        }

        m_filament_labels = best_label;

        return best_cost;
    }

    int FilamentGroup::calc_filament_group_by_pam(const std::vector<std::vector<unsigned int>>& layer_filaments, int timeout_ms)
    {
        //calc pair counts
        std::vector<std::vector<int>>count_matrix(m_filament_num,std::vector<int>(m_filament_num));
        for (const auto& lf : layer_filaments) {
            for (auto iter = lf.begin(); iter != lf.end(); ++iter) {
                auto idx1 = std::find(m_used_filaments.begin(), m_used_filaments.end(), *iter)-m_used_filaments.begin();
                for (auto niter = std::next(iter); niter != lf.end(); ++niter) {
                    auto idx2 = std::find(m_used_filaments.begin(), m_used_filaments.end(), *niter) - m_used_filaments.begin();
                    count_matrix[idx1][idx2] += 1;
                    count_matrix[idx2][idx1] += 1;
                }
            }
        }

        //calc distance matrix
        std::vector<std::vector<float>>distance_matrix(m_filament_num, std::vector<float>(m_filament_num));
        for (size_t i = 0; i < m_used_filaments.size(); ++i) {
            for (size_t j = 0; j < m_used_filaments.size(); ++j) {
                if (i == j)
                    distance_matrix[i][j] = 0;
                else {
                    //TODO: check m_flush_matrix
                    float max_val = std::max(m_flush_matrix[0][m_used_filaments[i]][m_used_filaments[j]], m_flush_matrix[0][m_used_filaments[j]][m_used_filaments[i]]);
                    float min_val = std::min(m_flush_matrix[0][m_used_filaments[i]][m_used_filaments[j]], m_flush_matrix[0][m_used_filaments[j]][m_used_filaments[i]]);

                    double p = 0;
                    distance_matrix[i][j] = (max_val * p + min_val * (1 - p)) * count_matrix[i][j];
                }
            }
        }

        KMediods PAM(distance_matrix, m_filament_num,m_max_group_size);
        PAM.fit(timeout_ms);
        this->m_filament_labels = PAM.get_filament_labels();

        int cost = reorder_filaments_for_minimum_flush_volume(
            m_used_filaments,
            this->m_filament_labels,
            layer_filaments,
            m_flush_matrix,
            get_custom_seq,
            nullptr
        );

        return cost;
    }


    void KMediods::fit( int timeout_ms)
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

            labels = assign_label(medoids);
            int cost = calc_cost(labels, medoids);

            for (int i = 0; i < m_filament_num; ++i) {
                if (std::find(medoids.begin(), medoids.end(), i) != medoids.end())
                    continue;

                for (int j = 0; j < 2; ++j) {
                    std::vector<int> new_medoids = medoids;
                    new_medoids[j] = i;
                    std::vector<int> new_labels = assign_label(new_medoids);
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

            if (T.time_machine_end() > timeout_ms)
                break;
        }

        this->m_filament_labels = best_labels;
    }

    std::vector<int> KMediods::assign_label(const std::vector<int>& medoids) const
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
        while (!min_heap.empty()) {
            auto top = min_heap.top();
            min_heap.pop();
            if (group_0.size() < m_max_group_size[0] && (top.second <= 0 || group_1.size() >= m_max_group_size[1]))
                group_0.insert(top.first);
            else
                group_1.insert(top.first);

        }
        for (auto& item : group_0)
            labels[item] = 0;
        for (auto& item : group_1)
            labels[item] = 1;

        return labels;
    }

    int KMediods::calc_cost(const std::vector<int>& labels, const std::vector<int>& medoids) const
    {
        int total_cost = 0;
        for (int i = 0; i < m_filament_num; ++i)
            total_cost += m_distance_matrix[i][medoids[labels[i]]];
        return total_cost;
    }

    std::vector<int> KMediods::initialize(INIT_TYPE type) const
    {
        auto hash_func = [](int n1, int n2) {
            return n1 * 100 + n2;
        };
        srand(time(nullptr));
        std::vector<int>ret;
        if (type == INIT_TYPE::Farthest) {
            //get the farthest items
            int target_i=0,target_j=0,target_val=std::numeric_limits<int>::min();
            for(int i=0;i<m_distance_matrix.size();++i){
                for(int j=0;j<m_distance_matrix[0].size();++j){
                    if(i!=j &&m_distance_matrix[i][j]>target_val){
                        target_val=m_distance_matrix[i][j];
                        target_i=i;
                        target_j=j;
                    }
                }
            }
            ret.emplace_back(std::min(target_i, target_j));
            ret.emplace_back(std::max(target_i, target_j));
        }
        else if (type == INIT_TYPE::Random) {
            while (true) {
                std::vector<int>medoids;
                while (medoids.size() < 2)
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
}


