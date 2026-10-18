#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include "libslic3r/AFCMapping.hpp"
#include "libslic3r/LaneMapping.hpp"

using Slic3r::AFCMapping;
using Slic3r::LaneMapping;
using Slic3r::LaneSelections;
using Slic3r::AFC_MAX_TOOLS;
using Slic3r::parse_lane_selection;
using Slic3r::serialize_lane_selection;
using nlohmann::json;

namespace {

json status_fixture()
{
    return {
        {"AFC", {{"lanes", {"CANVAS_1", "CANVAS_2", "CANVAS_3", "CANVAS_4"}},
                 {"current_state", "Idle"}, {"error_state", false}, {"multiple_tool_mapping", false}}},
        {"print_stats", {{"state", "standby"}}},
        {"AFC_lane CANVAS_1", {{"map", {"T1"}}, {"material", "PETG-CF"}, {"color", "#2E6194"}, {"load", true}, {"prep", true}, {"filament_status", "Ready"}, {"extruder_temp", 245.0}}},
        {"AFC_lane CANVAS_2", {{"map", {"T0"}}, {"material", "PLA"}, {"color", "#747C7E"}, {"load", true}, {"prep", true}, {"filament_status", "Ready"}}},
        {"AFC_lane CANVAS_3", {{"map", {"T2"}}, {"material", "PLA"}, {"color", "#FFFFFF"}, {"load", true}, {"prep", true}, {"filament_status", "Ready"}}},
        {"AFC_lane CANVAS_4", {{"map", {"T3"}}, {"material", nullptr}, {"color", ""}, {"load", false}, {"prep", false}, {"filament_status", "Not Ready"}}}
    };
}

LaneSelections selection(const std::map<int, std::string>& desired)
{
    LaneSelections entries;
    for (const auto& [tool, name] : desired) entries.push_back({tool, name});
    return entries;
}

// Model AFC's documented SET_MAP semantics independently: legacy mode swaps
// lane assignments; multiple mapping moves only the requested tool alias.
void apply_script(LaneMapping& mapping, const std::string& script)
{
    std::istringstream lines(script);
    std::string command, lane_param, tool_param;
    while (lines >> command >> lane_param >> tool_param) {
        REQUIRE(command == "SET_MAP");
        REQUIRE(lane_param.rfind("LANE=", 0) == 0);
        REQUIRE(tool_param.rfind("MAP=T", 0) == 0);
        const auto name = lane_param.substr(5);
        const int tool = std::stoi(tool_param.substr(5));
        const auto old_lane = mapping.tools.at(tool);
        if (!mapping.multiple_mapping) {
            for (auto& [other_tool, other_lane] : mapping.tools)
                if (other_lane == name) other_lane = old_lane;
        }
        mapping.tools[tool] = name;
    }
}

} // namespace

TEST_CASE("AFC status preserves physical lane names and legacy or array tool mappings", "[AFCMapping]")
{
    auto status = status_fixture();
    LaneMapping parsed;
    REQUIRE(AFCMapping::parse(status, parsed));
    CHECK(parsed.tools.at(0) == "CANVAS_2");
    CHECK(parsed.tools.at(1) == "CANVAS_1");
    CHECK_FALSE(parsed.lanes.at("CANVAS_4").ready);
    CHECK(parsed.lanes.at("CANVAS_4").material.empty());
    status["AFC_lane CANVAS_1"]["map"] = "T1";
    REQUIRE(AFCMapping::parse(status, parsed));
    CHECK(parsed.tools.at(1) == "CANVAS_1");
}

TEST_CASE("AFC status tolerates unknown metadata and multiple tool aliases", "[AFCMapping]")
{
    auto status = status_fixture();
    status["AFC"]["multiple_tool_mapping"] = true;
    status["AFC_lane CANVAS_1"]["map"] = {"T1", "T2"};
    status["AFC_lane CANVAS_1"]["color"] = nullptr;
    status["AFC_lane CANVAS_1"]["bed_temp"] = nullptr;
    status["AFC_lane CANVAS_1"]["extruder_temp"] = nullptr;
    status["AFC_lane CANVAS_3"]["map"] = {"NONE"};
    LaneMapping parsed;
    REQUIRE(AFCMapping::parse(status, parsed));
    CHECK(parsed.multiple_mapping);
    CHECK(parsed.tools.at(1) == parsed.tools.at(2));
    CHECK(parsed.lanes.at("CANVAS_1").color.empty());
}

TEST_CASE("AFC status with no tool mappings parses as an empty unit", "[AFCMapping]")
{
    auto status = status_fixture();
    json unmapped = {"NONE"};
    SECTION("array placeholder") {}
    SECTION("legacy null") { unmapped = nullptr; }
    for (const auto* lane : {"CANVAS_1", "CANVAS_2", "CANVAS_3", "CANVAS_4"})
        status[std::string("AFC_lane ") + lane]["map"] = unmapped;
    LaneMapping parsed;
    REQUIRE(AFCMapping::parse(status, parsed));
    CHECK(parsed.tools.empty());
    CHECK(parsed.lanes.size() == 4);
    std::map<int, std::string> desired;
    CHECK_FALSE(parsed.plan(selection({{0, "CANVAS_1"}}), desired));
}

TEST_CASE("AFC null mappings preserve unassigned lanes as print destinations", "[AFCMapping]")
{
    auto status = status_fixture();
    status["AFC_lane CANVAS_4"]["map"] = nullptr;
    status["AFC_lane CANVAS_4"]["filament_status"] = "Ready";
    LaneMapping before;
    REQUIRE(AFCMapping::parse(status, before));
    CHECK(before.tools.size() == 3);
    CHECK(before.lanes.at("CANVAS_4").ready);
    CHECK(before.tools.at(0) == "CANVAS_2");
    std::map<int, std::string> desired;
    REQUIRE(before.plan(selection({{0, "CANVAS_4"}}), desired));
    const auto expected = AFCMapping::expected_tools(before, desired);

    // Legacy SET_MAP transfers the previously unassigned lane's null map
    // to the displaced lane, without creating another tool alias.
    status["AFC_lane CANVAS_2"]["map"] = nullptr;
    status["AFC_lane CANVAS_4"]["map"] = "T0";
    LaneMapping after;
    REQUIRE(AFCMapping::parse(status, after));
    CHECK(after.tools == expected);
    CHECK(after.tools.at(0) == "CANVAS_4");
    CHECK(after.tools.size() == 3);
    CHECK(after.lanes.count("CANVAS_2") == 1);
}

TEST_CASE("AFC status rejects malformed or ambiguous physical assignments", "[AFCMapping]")
{
    auto status = status_fixture();
    LaneMapping parsed;
    SECTION("duplicate tool") { status["AFC_lane CANVAS_1"]["map"] = {"T0"}; }
    SECTION("missing lane") { status.erase("AFC_lane CANVAS_2"); }
    SECTION("missing map") { status["AFC_lane CANVAS_1"].erase("map"); }
    SECTION("nonstring map") { status["AFC_lane CANVAS_1"]["map"] = 1; }
    SECTION("invalid tool") { status["AFC_lane CANVAS_1"]["map"] = {"T1evil"}; }
    SECTION("malformed readiness") { status["AFC_lane CANVAS_1"]["filament_status"] = true; }
    SECTION("unrepresentable sync slot") { status["AFC_lane CANVAS_1"]["map"] = {"T32"}; }
    CHECK_FALSE(AFCMapping::parse(status, parsed));
}

TEST_CASE("AFC lane names cannot introduce another G-code parameter or command", "[AFCMapping]")
{
    CHECK(AFCMapping::valid_lane_name("CANVAS_1"));
    CHECK(AFCMapping::valid_lane_name("box-2.lane3"));
    CHECK_FALSE(AFCMapping::valid_lane_name("lane1 MAP=T0"));
    CHECK_FALSE(AFCMapping::valid_lane_name("lane1\nG28"));
    CHECK_FALSE(AFCMapping::valid_lane_name("lane1;G28"));
    CHECK_FALSE(AFCMapping::valid_lane_name(R"("lane1")"));
}

TEST_CASE("AFC print selections reject busy printers and unloaded lanes", "[AFCMapping]")
{
    auto status = status_fixture();
    LaneMapping before;
    REQUIRE(AFCMapping::parse(status, before));
    const auto chosen = selection({{0, "CANVAS_1"}, {1, "CANVAS_2"}});
    std::map<int, std::string> accepted;
    REQUIRE(before.plan(chosen, accepted));
    SECTION("printing") { status["print_stats"]["state"] = "printing"; }
    SECTION("paused") { status["print_stats"]["state"] = "paused"; }
    SECTION("changing tool") { status["AFC"]["current_state"] = "Loading"; }
    SECTION("AFC error") { status["AFC"]["error_state"] = true; }
    SECTION("unloaded") { status["AFC_lane CANVAS_1"]["filament_status"] = "Not Ready"; }
    SECTION("unprepared") { status["AFC_lane CANVAS_1"]["filament_status"] = "Prep"; }
    LaneMapping now;
    REQUIRE(AFCMapping::parse(status, now));
    std::map<int, std::string> desired;
    CHECK_FALSE(now.plan(chosen, desired));
}

TEST_CASE("AFC selections retain physical destinations after an external remap", "[AFCMapping]")
{
    auto status = status_fixture();
    LaneMapping before;
    REQUIRE(AFCMapping::parse(status, before));
    const auto chosen = selection({{1, "CANVAS_1"}, {2, "CANVAS_3"}});
    status["AFC_lane CANVAS_1"]["map"] = {"T2"};
    status["AFC_lane CANVAS_3"]["map"] = {"T1"};
    LaneMapping now;
    REQUIRE(AFCMapping::parse(status, now));
    std::map<int, std::string> desired;
    REQUIRE(now.plan(chosen, desired));
    const auto expected = AFCMapping::expected_tools(now, desired);
    apply_script(now, AFCMapping::mapping_script(now, desired));
    CHECK(now.tools == expected);
    CHECK(now.tools.at(1) == "CANVAS_1");
    CHECK(now.tools.at(2) == "CANVAS_3");
    CHECK(now.tools.at(0) == "CANVAS_2");
    CHECK(now.tools.at(3) == "CANVAS_4");
}

TEST_CASE("AFC full single-mapping permutations reach the requested lanes", "[AFCMapping]")
{
    LaneMapping initial;
    REQUIRE(AFCMapping::parse(status_fixture(), initial));
    initial.lanes.at("CANVAS_4").ready = true;
    std::array<std::string, 4> names = {"CANVAS_1", "CANVAS_2", "CANVAS_3", "CANVAS_4"};
    do {
        std::map<int, std::string> desired;
        for (size_t i = 0; i < names.size(); ++i) desired[static_cast<int>(i)] = names[i];
        auto current = initial;
        REQUIRE(current.plan(selection(desired), desired));
        apply_script(current, AFCMapping::mapping_script(current, desired));
        CHECK(current.tools == desired);
    } while (std::next_permutation(names.begin(), names.end()));
}

TEST_CASE("AFC partial selections that swap among themselves leave unselected tools in place", "[AFCMapping]")
{
    LaneMapping mapping;
    REQUIRE(AFCMapping::parse(status_fixture(), mapping));
    const auto before = mapping.tools;
    std::map<int, std::string> desired;
    REQUIRE(mapping.plan(selection({{0, "CANVAS_1"}, {1, "CANVAS_2"}}), desired));
    const auto expected = AFCMapping::expected_tools(mapping, desired);
    apply_script(mapping, AFCMapping::mapping_script(mapping, desired));
    CHECK(mapping.tools == expected);
    CHECK(mapping.tools.at(0) == "CANVAS_1");
    CHECK(mapping.tools.at(1) == "CANVAS_2");
    CHECK(mapping.tools.at(2) == before.at(2));
    CHECK(mapping.tools.at(3) == before.at(3));
}

TEST_CASE("AFC selections can take a loaded lane from an unused tool when the current lane is empty", "[AFCMapping]")
{
    auto status = status_fixture();
    status["AFC_lane CANVAS_2"]["load"] = false;
    status["AFC_lane CANVAS_2"]["prep"] = false;
    status["AFC_lane CANVAS_2"]["filament_status"] = "Not Ready";
    SECTION("single mapping swaps the unused tool onto the empty lane") {}
    SECTION("multiple mapping retains the unused tool on the loaded lane") {
        status["AFC"]["multiple_tool_mapping"] = true;
    }
    LaneMapping mapping;
    REQUIRE(AFCMapping::parse(status, mapping));
    const auto chosen = selection({{0, "CANVAS_3"}});
    std::map<int, std::string> desired;
    REQUIRE(mapping.plan(chosen, desired));
    auto expected = mapping.tools;
    expected.at(0) = "CANVAS_3";
    if (!mapping.multiple_mapping) expected.at(2) = "CANVAS_2";
    CHECK(AFCMapping::expected_tools(mapping, desired) == expected);
    const auto script = AFCMapping::mapping_script(mapping, desired);
    CHECK(script == "SET_MAP LANE=CANVAS_3 MAP=T0\n");
    apply_script(mapping, script);
    CHECK(mapping.tools == expected);
    CHECK(mapping.tools.at(1) == "CANVAS_1");
    CHECK(mapping.tools.at(3) == "CANVAS_4");
    REQUIRE(mapping.plan(chosen, desired));
    CHECK(AFCMapping::mapping_script(mapping, desired).empty());
}

TEST_CASE("AFC verification requires the whole expected tool map", "[AFCMapping]")
{
    LaneMapping mapping;
    REQUIRE(AFCMapping::parse(status_fixture(), mapping));
    auto expected = mapping.tools;
    CHECK(mapping.tools == expected);
    expected.at(3) = "CANVAS_1";
    CHECK(mapping.tools != expected);
}

TEST_CASE("AFC unchanged assignments still require idle and loaded status before printing", "[AFCMapping]")
{
    LaneMapping mapping;
    REQUIRE(AFCMapping::parse(status_fixture(), mapping));
    const auto chosen = selection({{0, "CANVAS_1"}, {1, "CANVAS_2"}});
    std::map<int, std::string> desired;
    REQUIRE(mapping.plan(chosen, desired));
    const auto expected = AFCMapping::expected_tools(mapping, desired);
    apply_script(mapping, AFCMapping::mapping_script(mapping, desired));
    REQUIRE(mapping.plan(chosen, desired));
    SECTION("printer becomes busy") { mapping.idle = false; }
    SECTION("selected lane is unloaded") { mapping.lanes.at("CANVAS_1").ready = false; }
    CHECK(mapping.tools == expected);
    CHECK_FALSE(mapping.plan(chosen, desired));
}

TEST_CASE("AFC multiple mapping permits duplicate destinations without moving other aliases", "[AFCMapping]")
{
    LaneMapping mapping;
    REQUIRE(AFCMapping::parse(status_fixture(), mapping));
    const auto chosen = selection({{1, "CANVAS_1"}, {2, "CANVAS_1"}});
    std::map<int, std::string> desired;
    // T1 already serves CANVAS_1, so only the duplicate destination can reject this.
    CHECK_FALSE(mapping.plan(chosen, desired));
    mapping.multiple_mapping = true;
    REQUIRE(mapping.plan(chosen, desired));
    const auto expected = AFCMapping::expected_tools(mapping, desired);
    apply_script(mapping, AFCMapping::mapping_script(mapping, desired));
    CHECK(mapping.tools == expected);
    CHECK(mapping.tools.at(1) == "CANVAS_1");
    CHECK(mapping.tools.at(2) == "CANVAS_1");
    CHECK(mapping.tools.at(0) == "CANVAS_2");
    CHECK(mapping.tools.at(3) == "CANVAS_4");
}

TEST_CASE("AFC selection boundaries reject incomplete or malformed mappings", "[AFCMapping]")
{
    LaneMapping mapping;
    REQUIRE(AFCMapping::parse(status_fixture(), mapping));
    auto chosen = json::array({{{"tool", 0}, {"lane", "CANVAS_1"}}, {{"tool", 1}, {"lane", "CANVAS_2"}}});
    SECTION("missing lane") { chosen[0].erase("lane"); }
    SECTION("unknown tool") { chosen[0]["tool"] = 99; }
    SECTION("noninteger tool") { chosen[0]["tool"] = 0.0; }
    SECTION("duplicate tool") { chosen.push_back(chosen[0]); }
    SECTION("unknown lane") { chosen[0]["lane"] = "CANVAS_99"; }
    SECTION("empty selection") { chosen = json::array(); }
    LaneSelections decoded;
    std::map<int, std::string> desired;
    const bool accepted = parse_lane_selection(chosen, decoded) && mapping.plan(decoded, desired);
    CHECK_FALSE(accepted);
    CHECK_FALSE(parse_lane_selection(std::string("not json"), decoded));
}

TEST_CASE("AFC tool parsing requires a complete bounded decimal number", "[AFCMapping]")
{
    int tool = -1;
    REQUIRE(AFCMapping::tool_index("T31", tool));
    CHECK(tool == AFC_MAX_TOOLS - 1);
    for (const auto& invalid : {"T", "T-1", "T-0", "T+1", "T1tail", "T999999999999999999999", "X1"})
        CHECK_FALSE(AFCMapping::tool_index(invalid, tool));
}

TEST_CASE("AFC lane and stepper objects resolve the same physical lanes", "[AFCMapping]")
{
    auto status = status_fixture();
    SECTION("stepper objects only") {
        for (const auto* name : {"CANVAS_1", "CANVAS_2", "CANVAS_3", "CANVAS_4"}) {
            status[std::string("AFC_stepper ") + name] = status[std::string("AFC_lane ") + name];
            status.erase(std::string("AFC_lane ") + name);
        }
    }
    SECTION("mixed lane and stepper objects") {
        status["AFC_stepper CANVAS_1"] = status["AFC_lane CANVAS_1"];
        status.erase("AFC_lane CANVAS_1");
    }
    SECTION("logical lane takes precedence over its stepper") {
        status["AFC_stepper CANVAS_1"] = {{"map", "T0"}, {"filament_status", "Not Ready"}};
    }
    LaneMapping parsed;
    REQUIRE(AFCMapping::parse(status, parsed));
    CHECK(parsed.tools.at(1) == "CANVAS_1");
    CHECK(parsed.lanes.at("CANVAS_1").ready);
    CHECK(parsed.lanes.at("CANVAS_1").material == "PETG-CF");
    std::map<int, std::string> desired;
    REQUIRE(parsed.plan({{0, "CANVAS_1"}}, desired));
    apply_script(parsed, AFCMapping::mapping_script(parsed, desired));
    CHECK(parsed.tools.at(0) == "CANVAS_1");
}

TEST_CASE("AFC readiness follows filament status including the loaded tool", "[AFCMapping]")
{
    for (const auto* value : {"Ready", "In Tool", "Prep", "Not Ready", "unknown", ""}) {
        DYNAMIC_SECTION("filament_status=" << value) {
            auto status = status_fixture();
            auto& lane = status["AFC_lane CANVAS_1"];
            lane["filament_status"] = value;
            lane.erase("load");
            lane.erase("prep");
            LaneMapping parsed;
            REQUIRE(AFCMapping::parse(status, parsed));
            const bool ready = std::string(value) == "Ready" || std::string(value) == "In Tool";
            CHECK(parsed.lanes.at("CANVAS_1").ready == ready);
            std::map<int, std::string> desired;
            CHECK(parsed.plan({{1, "CANVAS_1"}}, desired) == ready);
        }
    }
}

TEST_CASE("AFC lanes without filament status remain unavailable for printing", "[AFCMapping]")
{
    auto status = status_fixture();
    status["AFC_lane CANVAS_1"].erase("filament_status");
    LaneMapping parsed;
    REQUIRE(AFCMapping::parse(status, parsed));
    CHECK_FALSE(parsed.lanes.at("CANVAS_1").ready);
}

TEST_CASE("AFC lane names preserve punctuation and Unicode without changing command syntax", "[AFCMapping]")
{
    for (const auto* name : {"unit:lane/1", "lane+2", "entrée_3", "lane=4"}) {
        DYNAMIC_SECTION(name) {
            REQUIRE(AFCMapping::valid_lane_name(name));
            auto status = status_fixture();
            status["AFC"]["lanes"][0] = name;
            status[std::string("AFC_lane ") + name] = status["AFC_lane CANVAS_1"];
            status.erase("AFC_lane CANVAS_1");
            LaneMapping parsed;
            REQUIRE(AFCMapping::parse(status, parsed));
            CHECK(parsed.tools.at(1) == name);
        }
    }
    for (const auto* name : {"lane#comment", "lane;comment", "lane\\1", "lane'1", "lane\t1", "lane\r1"})
        CHECK_FALSE(AFCMapping::valid_lane_name(name));
}
