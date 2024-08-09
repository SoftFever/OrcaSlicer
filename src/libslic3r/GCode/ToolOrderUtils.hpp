#ifndef TOOL_ORDER_UTILS_HPP
#define TOOL_ORDER_UTILS_HPP

#include<vector>
#include<optional>
#include<functional>

namespace Slic3r {

using FlushMatrix = std::vector<std::vector<float>>;


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
