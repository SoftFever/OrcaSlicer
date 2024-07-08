#ifndef FILAMENT_GROUP_HPP
#define FILAMENT_GROUP_HPP

#include<chrono>

namespace Slic3r
{
    using FlushMatrix = std::vector<std::vector<float>>;

    struct FlushTimeMachine
    {
    private:
        std::chrono::high_resolution_clock::time_point start;

    public:
        void time_machine_start()
        {
            start = std::chrono::high_resolution_clock::now();
        }

        int time_machine_end()
        {
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            return duration.count();
        }
    };


    class FilamentGroup
    {
    public:
        FilamentGroup(const std::vector<FlushMatrix>& flush_matrix, const int filament_num, const std::vector<int>& max_group_size) :
            m_flush_matrix{ flush_matrix },
            m_filament_num{ filament_num },
            m_max_group_size{ max_group_size }
        {}

        int calc_filament_group(const std::vector<std::vector<unsigned int>>& layer_filaments);
        int calc_filament_group_by_enum(const std::vector<std::vector<unsigned int>>& layer_filaments);
        int calc_filament_group_by_pam(const std::vector<std::vector<unsigned int>>& layer_filaments, int timeout_ms = 300);

        std::vector<int> get_filament_map() const {return m_filament_labels;}

    private:
        std::vector<FlushMatrix>m_flush_matrix;
        int m_filament_num;
        std::vector<int>m_max_group_size;
        std::vector<unsigned int>m_used_filaments;
    public:
        std::optional<std::function<bool(int, std::vector<int>&)>> get_custom_seq;
    private:
        std::vector<int>m_filament_labels;
        std::vector<std::vector<unsigned int>>m_filament_orders;

    };

    class KMediods
    {
        enum INIT_TYPE
        {
            Random = 0,
            Farthest
        };
    public:
        KMediods(const std::vector<std::vector<float>>& distance_matrix, const int filament_num, const std::vector<int>& max_group_size) :
            m_distance_matrix{ distance_matrix },
            m_filament_num{ filament_num },
            m_max_group_size{ max_group_size } {}

        void fit(int timeout_ms = 300);
        std::vector<int>get_filament_labels()const {
            return m_filament_labels;
        }

    private:
        std::vector<int>initialize(INIT_TYPE type)const;
        std::vector<int>assign_label(const std::vector<int>& medoids)const;
        int calc_cost(const std::vector<int>& labels, const std::vector<int>& medoids)const;
    private:
        std::vector<std::vector<float>>m_distance_matrix;
        int m_filament_num;
        std::vector<int>m_max_group_size;
        std::vector<int>m_used_filaments;
        mutable std::set<int>m_medoids_set;
    private:
        std::vector<int>m_filament_labels;
    };
}
#endif // !FILAMENT_GROUP_HPP
