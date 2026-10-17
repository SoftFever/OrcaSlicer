#include "AFCMapping.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace Slic3r {

std::string serialize_afc_selection(const AFCSelections& selection)
{
    auto entries = nlohmann::json::array();
    for (const auto& entry : selection)
        entries.push_back({{"tool", entry.tool}, {"lane", entry.lane}});
    return entries.dump();
}

bool parse_afc_selection(const std::string& serialized, AFCSelections& selection)
{
    return parse_afc_selection(nlohmann::json::parse(serialized, nullptr, false), selection);
}

bool parse_afc_selection(const nlohmann::json& entries, AFCSelections& selection)
{
    selection.clear();
    if (!entries.is_array() || entries.empty()) return false;
    for (const auto& entry : entries) {
        if (!entry.is_object() || !entry.contains("tool") || !entry["tool"].is_number_integer()
            || !entry.contains("lane") || !entry["lane"].is_string()) return false;
        const auto tool = entry["tool"].get<long long>();
        if (tool < 0 || tool >= AFC_MAX_TOOLS) return false;
        selection.push_back({static_cast<int>(tool), entry["lane"].get<std::string>()});
    }
    return true;
}

bool AFCMapping::tool_index(const std::string& name, int& index)
{
    if (name.size() < 2 || name.front() != 'T' || name[1] < '0' || name[1] > '9') return false;
    const auto parsed = std::from_chars(name.data() + 1, name.data() + name.size(), index);
    return parsed.ec == std::errc{} && parsed.ptr == name.data() + name.size() && index >= 0;
}

bool AFCMapping::valid_lane_name(const std::string& name)
{
    // The name is emitted as a Klipper G-code parameter, never script.
    return !name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-' || c == '.';
    });
}

std::string AFCMapping::normalize_color(std::string color)
{
    if (color.rfind("#", 0) == 0) color.erase(0, 1);
    else if (color.rfind("0x", 0) == 0 || color.rfind("0X", 0) == 0) color.erase(0, 2);
    if (color.size() == 8) color.resize(6);
    if (color.size() != 6 || !std::all_of(color.begin(), color.end(), [](unsigned char c) { return std::isxdigit(c); })) return {};
    std::transform(color.begin(), color.end(), color.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return "#" + color;
}

std::string AFCMapping::normalize_material(std::string material)
{
    const auto first = material.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    material = material.substr(first, material.find_last_not_of(" \t\r\n") - first + 1);
    std::transform(material.begin(), material.end(), material.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return material;
}

bool AFCMapping::parse(const nlohmann::json& status, AFCMapping& result)
{
    result = {};
    // This is the one untrusted firmware-data boundary. Keep nullable optional
    // metadata permissive while rejecting malformed maps and readiness fields.
    try {
        if (!status.is_object() || !status.contains("AFC") || !status["AFC"].is_object()) return false;
        const auto& afc = status["AFC"];
        if (!afc.contains("lanes") || !afc["lanes"].is_array()) return false;
        result.multiple_mapping = afc.value("multiple_tool_mapping", false);
        const auto print_state = status.value("print_stats", nlohmann::json::object()).value("state", std::string{});
        result.idle = (print_state == "standby" || print_state == "complete" || print_state == "cancelled" || print_state == "error")
            && afc.value("current_state", std::string{}) == "Idle" && !afc.value("error_state", false);
        for (const auto& name_json : afc["lanes"]) {
            if (!name_json.is_string()) return false;
            const auto name = name_json.get<std::string>();
            const auto it = status.find("AFC_lane " + name);
            if (!valid_lane_name(name) || it == status.end() || !it->is_object()) return false;
            const auto& lane = *it;
            Lane info;
            if (lane.contains("material") && lane["material"].is_string()) info.material = lane["material"];
            if (lane.contains("color") && lane["color"].is_string()) info.color = normalize_color(lane["color"].get<std::string>());
            info.ready = lane.value("load", false) && lane.value("prep", false);
            if (lane.contains("bed_temp") && lane["bed_temp"].is_number()) info.bed_temp = lane["bed_temp"];
            if (lane.contains("extruder_temp") && lane["extruder_temp"].is_number()) info.nozzle_temp = lane["extruder_temp"];
            if (!result.lanes.emplace(name, std::move(info)).second || !lane.contains("map")) return false;
            const auto add_mapping = [&](const nlohmann::json& mapping) {
                if (!mapping.is_string()) return false;
                const auto command = mapping.get<std::string>();
                if (command == "NONE" || command.empty()) return true;
                int index;
                return tool_index(command, index) && index < AFC_MAX_TOOLS && result.tools.emplace(index, name).second;
            };
            const auto& mappings = lane["map"];
            if (mappings.is_array()) {
                for (const auto& mapping : mappings)
                    if (!add_mapping(mapping)) return false;
            } else if (!add_mapping(mappings)) return false;
        }
        return !result.tools.empty();
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

bool AFCMapping::plan(const AFCSelections& selection, std::map<int, std::string>& desired) const
{
    desired.clear();
    if (selection.empty() || !idle) return false;
    std::set<std::string> selected;
    for (const auto& entry : selection) {
        const auto lane = lanes.find(entry.lane);
        if (entry.tool < 0 || entry.tool >= AFC_MAX_TOOLS || !tools.count(entry.tool)
            || lane == lanes.end() || !lane->second.ready || !valid_lane_name(entry.lane)) return false;
        if (!selected.insert(entry.lane).second && !multiple_mapping) return false;
        if (!desired.emplace(entry.tool, entry.lane).second) return false;
    }
    return true;
}

bool AFCMapping::selection_from_tools(const std::string& slot_mapping, const std::string& filament_info,
                                     AFCSelections& selection) const
{
    selection.clear();
    const auto slots = nlohmann::json::parse(slot_mapping, nullptr, false);
    const auto info = nlohmann::json::parse(filament_info, nullptr, false);
    if (!slots.is_array() || slots.empty() || slots.size() > AFC_MAX_TOOLS) return false;
    for (size_t tool = 0; tool < slots.size(); ++tool) {
        if (!slots[tool].is_number_integer()) return false;
        const auto target = slots[tool].get<long long>();
        const nlohmann::json* source = info.is_array() && tool < info.size() && info[tool].is_object() ? &info[tool] : nullptr;
        const bool has_type = source && source->contains("filamentType") && (*source)["filamentType"].is_string();
        const auto source_material = has_type ? normalize_material((*source)["filamentType"].get<std::string>()) : std::string{};
        if (target == -1) {
            if (!source || !source_material.empty() || source->contains("sourceColor")) return false;
            continue;
        }
        if (target < 0 || target >= AFC_MAX_TOOLS) return false;
        const auto selected = tools.find(static_cast<int>(target));
        if (selected == tools.end()) return false;
        if (info.is_array() && !info.empty() && (source_material.empty()
            || source_material != normalize_material(lanes.at(selected->second).material))) return false;
        selection.push_back({static_cast<int>(tool), selected->second});
    }
    std::map<int, std::string> desired;
    return plan(selection, desired);
}

bool AFCMapping::matches(const std::map<int, std::string>& desired) const
{
    return std::all_of(desired.begin(), desired.end(), [this](const auto& item) {
        const auto it = tools.find(item.first);
        return it != tools.end() && it->second == item.second;
    });
}

std::string AFCMapping::mapping_script(const std::map<int, std::string>& desired) const
{
    std::string script;
    // All destinations are physical identities resolved before any swap.
    for (const auto& [tool, lane] : desired) {
        const auto current = tools.find(tool);
        if (current == tools.end() || current->second != lane)
            script += "SET_MAP LANE=" + lane + " MAP=T" + std::to_string(tool) + "\n";
    }
    return script;
}

} // namespace Slic3r
