#pragma once

#include <map>
#include <string>

#include <nlohmann/json_fwd.hpp>

#include "LaneMapping.hpp"

namespace Slic3r {

// Supported AFC tool indices are 0 through 31.
inline constexpr int AFC_MAX_TOOLS = 32;

// AFC-specific status and SET_MAP semantics adapt to the shared lane snapshot.
struct AFCMapping
{
    static bool tool_index(const std::string& name, int& index);
    static bool valid_lane_name(const std::string& name);
    static bool parse(const nlohmann::json& status, LaneMapping& result);
    static std::map<int, std::string> expected_tools(const LaneMapping& mapping, const std::map<int, std::string>& desired);
    static std::string mapping_script(const LaneMapping& mapping, const std::map<int, std::string>& desired);
};

} // namespace Slic3r
