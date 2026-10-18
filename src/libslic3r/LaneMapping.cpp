#include "LaneMapping.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

namespace Slic3r {

std::string serialize_lane_selection(const LaneSelections& selection)
{
    auto entries = nlohmann::json::array();
    for (const auto& entry : selection)
        entries.push_back({{"tool", entry.tool}, {"lane", entry.lane}});
    return entries.dump();
}

bool parse_lane_selection(const std::string& serialized, LaneSelections& selection)
{
    return parse_lane_selection(nlohmann::json::parse(serialized, nullptr, false), selection);
}

bool parse_lane_selection(const nlohmann::json& entries, LaneSelections& selection)
{
    selection.clear();
    if (!entries.is_array() || entries.empty())
        return false;
    for (const auto& entry : entries) {
        if (!entry.is_object() || !entry.contains("tool") || !entry["tool"].is_number_integer()
            || !entry.contains("lane") || !entry["lane"].is_string())
            return false;
        const auto& tool = entry["tool"];
        if (tool < 0 || tool > std::numeric_limits<int>::max())
            return false;
        selection.push_back({tool.get<int>(), entry["lane"].get<std::string>()});
    }
    return true;
}

std::string LaneMapping::normalize_color(std::string color)
{
    if (color.rfind("#", 0) == 0)
        color.erase(0, 1);
    else if (color.rfind("0x", 0) == 0 || color.rfind("0X", 0) == 0)
        color.erase(0, 2);
    if (color.size() == 8)
        color.resize(6);
    if (color.size() != 6 || !std::all_of(color.begin(), color.end(), [](unsigned char c) { return std::isxdigit(c); }))
        return {};
    std::transform(color.begin(), color.end(), color.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return "#" + color;
}

std::string LaneMapping::normalize_material(std::string material)
{
    const auto first = material.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    material = material.substr(first, material.find_last_not_of(" \t\r\n") - first + 1);
    std::transform(material.begin(), material.end(), material.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return material;
}

bool LaneMapping::plan(const LaneSelections& selection, std::map<int, std::string>& desired) const
{
    desired.clear();
    if (selection.empty() || !idle)
        return false;
    std::set<std::string> selected;
    for (const auto& entry : selection) {
        const auto lane = lanes.find(entry.lane);
        if (entry.tool < 0 || !tools.count(entry.tool))
            return false;
        if (lane == lanes.end())
            return false;
        const auto& [lane_name, lane_info] = *lane;
        if (!lane_info.ready)
            return false;
        if (!selected.insert(entry.lane).second && !multiple_mapping)
            return false;
        if (!desired.emplace(entry.tool, entry.lane).second)
            return false;
    }
    return true;
}

} // namespace Slic3r
