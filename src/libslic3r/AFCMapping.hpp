#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace Slic3r {

// Preparation sync projects AFC tools into a 32-bit tray-presence mask.
inline constexpr int AFC_MAX_TOOLS = 32;

struct AFCSelection
{
    int tool;
    std::string lane;
};
using AFCSelections = std::vector<AFCSelection>;

std::string serialize_afc_selection(const AFCSelections& selection);
bool parse_afc_selection(const std::string& serialized, AFCSelections& selection);
bool parse_afc_selection(const nlohmann::json& entries, AFCSelections& selection);

// The shared filament snapshot retains physical lane identity even after SET_MAP.
struct AFCMapping
{
    struct Lane {
        std::string material;
        std::string color;
        bool ready = false;
        int bed_temp = 0;
        int nozzle_temp = 0;
    };
    std::map<int, std::string> tools;
    std::map<std::string, Lane> lanes;
    std::string device_id;
    bool multiple_mapping = false;
    bool idle = false;

    static bool tool_index(const std::string& name, int& index);
    static bool valid_lane_name(const std::string& name);
    static std::string normalize_color(std::string color);
    static std::string normalize_material(std::string material);
    static bool parse(const nlohmann::json& status, AFCMapping& result);

    bool plan(const AFCSelections& selection, std::map<int, std::string>& desired) const;
    bool selection_from_tools(const std::string& slot_mapping, const std::string& filament_info,
                              AFCSelections& selection) const;
    std::map<int, std::string> expected_tools(const std::map<int, std::string>& desired) const;
    bool matches(const std::map<int, std::string>& expected) const;
    std::string mapping_script(const std::map<int, std::string>& desired) const;
};

} // namespace Slic3r
