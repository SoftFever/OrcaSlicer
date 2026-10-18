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

using Slic3r::AFCMapping;
using Slic3r::AFCSelections;
using Slic3r::AFC_MAX_TOOLS;
using Slic3r::parse_afc_selection;
using Slic3r::serialize_afc_selection;
using nlohmann::json;

namespace {

json status_fixture()
{
    return {
        {"AFC", {{"lanes", {"CANVAS_1", "CANVAS_2", "CANVAS_3", "CANVAS_4"}},
                 {"current_state", "Idle"}, {"error_state", false}, {"multiple_tool_mapping", false}}},
        {"print_stats", {{"state", "standby"}}},
        {"AFC_lane CANVAS_1", {{"map", {"T1"}}, {"material", "PETG-CF"}, {"color", "#2E6194"}, {"load", true}, {"prep", true}, {"extruder_temp", 245.0}}},
        {"AFC_lane CANVAS_2", {{"map", {"T0"}}, {"material", "PLA"}, {"color", "#747C7E"}, {"load", true}, {"prep", true}}},
        {"AFC_lane CANVAS_3", {{"map", {"T2"}}, {"material", "PLA"}, {"color", "#FFFFFF"}, {"load", true}, {"prep", true}}},
        {"AFC_lane CANVAS_4", {{"map", {"T3"}}, {"material", nullptr}, {"color", ""}, {"load", false}, {"prep", false}}}
    };
}

AFCSelections selection(const AFCMapping&, const std::map<int, std::string>& desired)
{
    AFCSelections entries;
    for (const auto& [tool, name] : desired) entries.push_back({tool, name});
    return entries;
}

// Model AFC's documented SET_MAP semantics independently: legacy mode swaps
// lane assignments; multiple mapping moves only the requested tool alias.
void apply_script(AFCMapping& mapping, const std::string& script)
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
    AFCMapping parsed;
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
    AFCMapping parsed;
    REQUIRE(AFCMapping::parse(status, parsed));
    CHECK(parsed.multiple_mapping);
    CHECK(parsed.tools.at(1) == parsed.tools.at(2));
    CHECK(parsed.lanes.at("CANVAS_1").color.empty());
}

TEST_CASE("AFC status with no tool mappings parses as an empty unit", "[AFCMapping]")
{
    auto status = status_fixture();
    for (const auto* lane : {"CANVAS_1", "CANVAS_2", "CANVAS_3", "CANVAS_4"})
        status[std::string("AFC_lane ") + lane]["map"] = {"NONE"};
    AFCMapping parsed;
    REQUIRE(AFCMapping::parse(status, parsed));
    CHECK(parsed.tools.empty());
    CHECK(parsed.lanes.size() == 4);
    std::map<int, std::string> desired;
    CHECK_FALSE(parsed.plan(selection(parsed, {{0, "CANVAS_1"}}), desired));
}

TEST_CASE("AFC color encodings normalize to the same display and snapshot color", "[AFCMapping]")
{
    for (const auto& color : {"#aAbBcC", "AABBCC", "aabbccff", "0xaabbcc", "0XAABBCCFF"})
        CHECK(AFCMapping::normalize_color(color) == "#AABBCC");
    CHECK(AFCMapping::normalize_color("not a color").empty());
}

TEST_CASE("AFC status rejects malformed or ambiguous physical assignments", "[AFCMapping]")
{
    auto status = status_fixture();
    AFCMapping parsed;
    SECTION("duplicate tool") { status["AFC_lane CANVAS_1"]["map"] = {"T0"}; }
    SECTION("missing lane") { status.erase("AFC_lane CANVAS_2"); }
    SECTION("invalid tool") { status["AFC_lane CANVAS_1"]["map"] = {"T1evil"}; }
    SECTION("malformed readiness") { status["AFC_lane CANVAS_1"]["load"] = "yes"; }
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
    AFCMapping before;
    REQUIRE(AFCMapping::parse(status, before));
    const auto chosen = selection(before, {{0, "CANVAS_1"}, {1, "CANVAS_2"}});
    std::map<int, std::string> accepted;
    REQUIRE(before.plan(chosen, accepted));
    SECTION("printing") { status["print_stats"]["state"] = "printing"; }
    SECTION("paused") { status["print_stats"]["state"] = "paused"; }
    SECTION("changing tool") { status["AFC"]["current_state"] = "Loading"; }
    SECTION("AFC error") { status["AFC"]["error_state"] = true; }
    SECTION("unloaded") { status["AFC_lane CANVAS_1"]["load"] = false; }
    SECTION("unprepared") { status["AFC_lane CANVAS_1"]["prep"] = false; }
    AFCMapping now;
    REQUIRE(AFCMapping::parse(status, now));
    std::map<int, std::string> desired;
    CHECK_FALSE(now.plan(chosen, desired));
}

TEST_CASE("AFC selections retain physical destinations after an external remap", "[AFCMapping]")
{
    auto status = status_fixture();
    AFCMapping before;
    REQUIRE(AFCMapping::parse(status, before));
    const auto chosen = selection(before, {{1, "CANVAS_1"}, {2, "CANVAS_3"}});
    status["AFC_lane CANVAS_1"]["map"] = {"T2"};
    status["AFC_lane CANVAS_3"]["map"] = {"T1"};
    AFCMapping now;
    REQUIRE(AFCMapping::parse(status, now));
    std::map<int, std::string> desired;
    REQUIRE(now.plan(chosen, desired));
    const auto expected = now.expected_tools(desired);
    apply_script(now, now.mapping_script(desired));
    CHECK(now.matches(expected));
    CHECK(now.tools.at(1) == "CANVAS_1");
    CHECK(now.tools.at(2) == "CANVAS_3");
    CHECK(now.tools.at(0) == "CANVAS_2");
    CHECK(now.tools.at(3) == "CANVAS_4");
}

TEST_CASE("AFC full single-mapping permutations reach the requested lanes", "[AFCMapping]")
{
    AFCMapping initial;
    REQUIRE(AFCMapping::parse(status_fixture(), initial));
    initial.lanes.at("CANVAS_4").ready = true;
    std::array<std::string, 4> names = {"CANVAS_1", "CANVAS_2", "CANVAS_3", "CANVAS_4"};
    do {
        std::map<int, std::string> desired;
        for (size_t i = 0; i < names.size(); ++i) desired[static_cast<int>(i)] = names[i];
        auto current = initial;
        REQUIRE(current.plan(selection(initial, desired), desired));
        apply_script(current, current.mapping_script(desired));
        CHECK(current.tools == desired);
    } while (std::next_permutation(names.begin(), names.end()));
}

TEST_CASE("AFC partial selections that swap among themselves leave unselected tools in place", "[AFCMapping]")
{
    AFCMapping mapping;
    REQUIRE(AFCMapping::parse(status_fixture(), mapping));
    const auto before = mapping.tools;
    std::map<int, std::string> desired;
    REQUIRE(mapping.plan(selection(mapping, {{0, "CANVAS_1"}, {1, "CANVAS_2"}}), desired));
    const auto expected = mapping.expected_tools(desired);
    apply_script(mapping, mapping.mapping_script(desired));
    CHECK(mapping.matches(expected));
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
    SECTION("single mapping swaps the unused tool onto the empty lane") {}
    SECTION("multiple mapping retains the unused tool on the loaded lane") {
        status["AFC"]["multiple_tool_mapping"] = true;
    }
    AFCMapping mapping;
    REQUIRE(AFCMapping::parse(status, mapping));
    const auto chosen = selection(mapping, {{0, "CANVAS_3"}});
    std::map<int, std::string> desired;
    REQUIRE(mapping.plan(chosen, desired));
    auto expected = mapping.tools;
    expected.at(0) = "CANVAS_3";
    if (!mapping.multiple_mapping) expected.at(2) = "CANVAS_2";
    CHECK(mapping.expected_tools(desired) == expected);
    const auto script = mapping.mapping_script(desired);
    CHECK(script == "SET_MAP LANE=CANVAS_3 MAP=T0\n");
    apply_script(mapping, script);
    CHECK(mapping.matches(expected));
    CHECK(mapping.tools.at(1) == "CANVAS_1");
    CHECK(mapping.tools.at(3) == "CANVAS_4");
    REQUIRE(mapping.plan(chosen, desired));
    CHECK(mapping.mapping_script(desired).empty());
}

TEST_CASE("AFC verification requires the whole expected tool map", "[AFCMapping]")
{
    AFCMapping mapping;
    REQUIRE(AFCMapping::parse(status_fixture(), mapping));
    auto expected = mapping.tools;
    CHECK(mapping.matches(expected));
    expected.at(3) = "CANVAS_1";
    CHECK_FALSE(mapping.matches(expected));
}

TEST_CASE("AFC unchanged assignments still require idle and loaded status before printing", "[AFCMapping]")
{
    AFCMapping mapping;
    REQUIRE(AFCMapping::parse(status_fixture(), mapping));
    const auto chosen = selection(mapping, {{0, "CANVAS_1"}, {1, "CANVAS_2"}});
    std::map<int, std::string> desired;
    REQUIRE(mapping.plan(chosen, desired));
    const auto expected = mapping.expected_tools(desired);
    apply_script(mapping, mapping.mapping_script(desired));
    REQUIRE(mapping.plan(chosen, desired));
    SECTION("printer becomes busy") { mapping.idle = false; }
    SECTION("selected lane is unloaded") { mapping.lanes.at("CANVAS_1").ready = false; }
    CHECK(mapping.matches(expected));
    CHECK_FALSE(mapping.plan(chosen, desired));
}

TEST_CASE("AFC multiple mapping permits duplicate destinations without moving other aliases", "[AFCMapping]")
{
    AFCMapping mapping;
    REQUIRE(AFCMapping::parse(status_fixture(), mapping));
    const auto chosen = selection(mapping, {{1, "CANVAS_1"}, {2, "CANVAS_1"}});
    std::map<int, std::string> desired;
    // T1 already serves CANVAS_1, so only the duplicate destination can reject this.
    CHECK_FALSE(mapping.plan(chosen, desired));
    mapping.multiple_mapping = true;
    REQUIRE(mapping.plan(chosen, desired));
    const auto expected = mapping.expected_tools(desired);
    apply_script(mapping, mapping.mapping_script(desired));
    CHECK(mapping.matches(expected));
    CHECK(mapping.tools.at(1) == "CANVAS_1");
    CHECK(mapping.tools.at(2) == "CANVAS_1");
    CHECK(mapping.tools.at(0) == "CANVAS_2");
    CHECK(mapping.tools.at(3) == "CANVAS_4");
}

TEST_CASE("AFC selection boundaries reject incomplete or malformed mappings", "[AFCMapping]")
{
    AFCMapping mapping;
    REQUIRE(AFCMapping::parse(status_fixture(), mapping));
    auto chosen = json::array({{{"tool", 0}, {"lane", "CANVAS_1"}}, {{"tool", 1}, {"lane", "CANVAS_2"}}});
    SECTION("missing lane") { chosen[0].erase("lane"); }
    SECTION("unknown tool") { chosen[0]["tool"] = 99; }
    SECTION("noninteger tool") { chosen[0]["tool"] = 0.0; }
    SECTION("duplicate tool") { chosen.push_back(chosen[0]); }
    SECTION("unknown lane") { chosen[0]["lane"] = "CANVAS_99"; }
    SECTION("empty selection") { chosen = json::array(); }
    AFCSelections decoded;
    std::map<int, std::string> desired;
    const bool accepted = parse_afc_selection(chosen, decoded) && mapping.plan(decoded, desired);
    CHECK_FALSE(accepted);
    CHECK_FALSE(parse_afc_selection(std::string("not json"), decoded));
}

TEST_CASE("AFC typed selections round trip only tool and physical lane identity", "[AFCMapping]")
{
    const AFCSelections original = {{0, "CANVAS_1"}, {2, "CANVAS_3"}};
    AFCSelections decoded;
    REQUIRE(parse_afc_selection(serialize_afc_selection(original), decoded));
    REQUIRE(decoded.size() == 2);
    CHECK(decoded[0].tool == 0);
    CHECK(decoded[0].lane == "CANVAS_1");
    CHECK(decoded[1].tool == 2);
    CHECK(decoded[1].lane == "CANVAS_3");
}

TEST_CASE("AFC tool parsing requires a complete bounded decimal number", "[AFCMapping]")
{
    int tool = -1;
    REQUIRE(AFCMapping::tool_index("T31", tool));
    CHECK(tool == AFC_MAX_TOOLS - 1);
    for (const auto& invalid : {"T", "T-1", "T-0", "T+1", "T1tail", "T999999999999999999999", "X1"})
        CHECK_FALSE(AFCMapping::tool_index(invalid, tool));
}
