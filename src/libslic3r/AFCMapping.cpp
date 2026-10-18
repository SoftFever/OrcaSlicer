#include "AFCMapping.hpp"
#include "LaneMapping.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <map>
#include <string>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace Slic3r {

bool AFCMapping::tool_index(const std::string& name, int& index)
{
    // from_chars accepts -0, but tool commands require an unsigned decimal suffix.
    if (name.size() < 2 || name.front() != 'T' || name[1] < '0' || name[1] > '9')
        return false;
    const auto parsed = std::from_chars(name.data() + 1, name.data() + name.size(), index);
    return parsed.ec == std::errc{} && parsed.ptr == name.data() + name.size() && index >= 0;
}

bool AFCMapping::valid_lane_name(const std::string& name)
{
    // AFC does not restrict names to ASCII identifiers. Reject only control
    // characters and Klipper shlex whitespace, quoting, escape, and comment syntax.
    return !name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return c > 0x20 && c != 0x7f && c != '#' && c != ';' && c != '\'' && c != '"' && c != '\\';
    });
}

bool AFCMapping::parse(const nlohmann::json& status, LaneMapping& parsed)
{
    LaneMapping result;
    // This is the one untrusted firmware-data boundary. Keep nullable optional
    // metadata permissive while rejecting malformed maps and readiness fields.
    try {
        if (!status.is_object() || !status.contains("AFC") || !status["AFC"].is_object())
            return false;
        const auto& afc = status["AFC"];
        if (!afc.contains("lanes") || !afc["lanes"].is_array())
            return false;
        result.multiple_mapping = afc.value("multiple_tool_mapping", false);
        const auto print_state = status.value("print_stats", nlohmann::json::object()).value("state", std::string{});
        result.idle = (print_state == "standby" || print_state == "complete" || print_state == "cancelled" || print_state == "error")
            && afc.value("current_state", std::string{}) == "Idle" && !afc.value("error_state", false);
        for (const auto& name_json : afc["lanes"]) {
            if (!name_json.is_string())
                return false;
            const auto name = name_json.get<std::string>();
            auto it = status.find("AFC_lane " + name);
            if (it == status.end())
                it = status.find("AFC_stepper " + name);
            if (!valid_lane_name(name) || it == status.end() || !it->is_object())
                return false;
            const auto& lane = *it;
            LaneMapping::Lane info;
            if (lane.contains("material") && lane["material"].is_string())
                info.material = lane["material"];
            if (lane.contains("color") && lane["color"].is_string())
                info.color = LaneMapping::normalize_color(lane["color"].get<std::string>());
            const auto filament_status = lane.value("filament_status", std::string{});
            info.ready = filament_status == "Ready" || filament_status == "In Tool";
            if (!result.lanes.emplace(name, std::move(info)).second || !lane.contains("map"))
                return false;
            const auto add_mapping = [&](const nlohmann::json& mapping) {
                if (!mapping.is_string())
                    return false;
                const auto command = mapping.get<std::string>();
                if (command == "NONE" || command.empty())
                    return true;
                int index;
                return tool_index(command, index) && index < AFC_MAX_TOOLS && result.tools.emplace(index, name).second;
            };
            const auto& mappings = lane["map"];
            if (mappings.is_array()) {
                for (const auto& mapping : mappings) {
                    if (!add_mapping(mapping))
                        return false;
                }
            } else if (!add_mapping(mappings)) {
                return false;
            }
        }
        // An idle AFC with nothing mapped is valid; callers inspect tools.empty().
        parsed = std::move(result);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

std::map<int, std::string> AFCMapping::expected_tools(const LaneMapping& mapping, const std::map<int, std::string>& desired)
{
    auto after = mapping.tools;
    for (const auto& [tool, lane] : desired) {
        const auto current = mapping.tools.find(tool);
        const std::string current_lane = current != mapping.tools.end() ? current->second : std::string{};
        if (current_lane == lane)
            continue;
        if (!mapping.multiple_mapping) {
            const auto previous = after.find(tool);
            if (previous != after.end()) {
                const std::string displaced = previous->second;
                for (auto& [other_tool, other_lane] : after) {
                    if (other_lane == lane)
                        other_lane = displaced;
                }
            }
        }
        after.insert_or_assign(tool, lane);
    }
    return after;
}

std::string AFCMapping::mapping_script(const LaneMapping& mapping, const std::map<int, std::string>& desired)
{
    std::string script;
    // All destinations are physical identities resolved before any swap.
    for (const auto& [tool, lane] : desired) {
        const auto current = mapping.tools.find(tool);
        const std::string current_lane = current != mapping.tools.end() ? current->second : std::string{};
        if (current_lane != lane)
            script += "SET_MAP LANE=" + lane + " MAP=T" + std::to_string(tool) + "\n";
    }
    return script;
}

} // namespace Slic3r
