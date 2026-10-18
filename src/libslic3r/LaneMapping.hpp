#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace Slic3r {

struct LaneSelection
{
    int tool;
    std::string lane;
};
using LaneSelections = std::vector<LaneSelection>;

std::string serialize_lane_selection(const LaneSelections& selection);
bool parse_lane_selection(const std::string& serialized, LaneSelections& selection);
bool parse_lane_selection(const nlohmann::json& entries, LaneSelections& selection);

// Physical lane identities are independent of the tools currently assigned to them.
struct LaneMapping
{
    struct Lane {
        std::string material;
        std::string color;
        bool ready = false;
    };
    std::map<int, std::string> tools;
    std::map<std::string, Lane> lanes;
    bool multiple_mapping = false;
    bool idle = false;

    static std::string normalize_color(std::string color);
    static std::string normalize_material(std::string material);
    bool plan(const LaneSelections& selection, std::map<int, std::string>& desired) const;
};

} // namespace Slic3r
