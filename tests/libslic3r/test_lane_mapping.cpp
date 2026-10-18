#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "libslic3r/LaneMapping.hpp"

using Slic3r::LaneMapping;
using Slic3r::LaneSelections;
using Slic3r::parse_lane_selection;
using Slic3r::serialize_lane_selection;
using nlohmann::json;

TEST_CASE("Lane color encodings normalize to the same display and snapshot color", "[LaneMapping]")
{
    for (const auto& color : {"#aAbBcC", "AABBCC", "aabbccff", "0xaabbcc", "0XAABBCCFF"})
        CHECK(LaneMapping::normalize_color(color) == "#AABBCC");
    CHECK(LaneMapping::normalize_color("not a color").empty());
}

TEST_CASE("Typed lane selections round trip only tool and physical lane identity", "[LaneMapping]")
{
    const LaneSelections original = {{0, "CANVAS_1"}, {2, "CANVAS_3"}};
    LaneSelections decoded;
    REQUIRE(parse_lane_selection(serialize_lane_selection(original), decoded));
    REQUIRE(decoded.size() == 2);
    CHECK(decoded[0].tool == 0);
    CHECK(decoded[0].lane == "CANVAS_1");
    CHECK(decoded[1].tool == 2);
    CHECK(decoded[1].lane == "CANVAS_3");
}


TEST_CASE("Lane selections support backend tool ranges independently of AFC", "[LaneMapping]")
{
    LaneMapping mapping;
    mapping.idle = true;
    mapping.tools = {{100, "slot:1"}};
    mapping.lanes = {{"slot:1", {"PLA", "#FFFFFF", true}}};
    LaneSelections decoded;
    REQUIRE(parse_lane_selection(serialize_lane_selection({{100, "slot:1"}}), decoded));
    std::map<int, std::string> desired;
    REQUIRE(mapping.plan(decoded, desired));
    CHECK(desired.at(100) == "slot:1");
}

TEST_CASE("Lane selection decoding rejects tool numbers outside the integer range", "[LaneMapping]")
{
    for (const auto& tool : {json(-1), json(static_cast<std::uint64_t>(std::numeric_limits<int>::max()) + 1),
                              json(std::numeric_limits<std::uint64_t>::max())}) {
        LaneSelections decoded;
        CHECK_FALSE(parse_lane_selection(json::array({{{"tool", tool}, {"lane", "slot1"}}}), decoded));
    }
}
