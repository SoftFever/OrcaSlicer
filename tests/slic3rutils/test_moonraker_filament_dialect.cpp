#include <catch2/catch_all.hpp>

#include <nlohmann/json.hpp>

#include "slic3r/Utils/MoonrakerPrinterAgent.hpp"

using Slic3r::IPrinterAgent;
using namespace Slic3r::MoonrakerFilamentDialect;

// Orca: the materials dialog writes a slot back through IPrinterAgent::push_filament_info. On a
// Moonraker printer the dialect is whichever filament changer reported the slots: AFC (the
// lane_data namespace) or Happy Hare (the mmu object). Both are plain g-code commands, so the
// rendering is pinned here as strings, like the Snapmaker and WonderMaker start scripts.

namespace {

IPrinterAgent::FilamentSlotInfo red_pla(int slot)
{
    IPrinterAgent::FilamentSlotInfo info;
    info.slot       = slot;
    info.vendor     = "Polymaker";
    info.type       = "PLA";
    info.sub_type   = "PolyTerra";
    info.color_rgba = "FF0000FF";
    return info;
}

} // namespace

TEST_CASE("AFC lane keys are recovered by slot from the lane_data namespace", "[MoonrakerFilamentDialect]")
{
    // The flat namespace is keyed by the lane NAME, which is what every AFC command wants; the
    // "lane" field carries the slot number the reader turns into a tray index.
    const nlohmann::json value = nlohmann::json::parse(R"({
        "lane1": {"lane": "0", "material": "PLA", "color": "#FF0000"},
        "e1":    {"lane": "2", "material": "ABS", "color": "#00FF00"},
        "junk":  {"material": "PETG"},
        "lane3": "not an object"
    })");
    const std::map<int, std::string> keys = afc_lane_keys(value);
    REQUIRE(keys.size() == 2);
    CHECK(keys.at(0) == "lane1");
    CHECK(keys.at(2) == "e1");
}

TEST_CASE("AFC has no combined command, so a slot write is two commands", "[MoonrakerFilamentDialect]")
{
    // Colour goes without alpha (AFC stores RRGGBB); vendor and sub-type have no field in AFC
    // and are not sent.
    CHECK(afc_push_scripts("e1", red_pla(2)) ==
          std::vector<std::string>{"SET_COLOR LANE=e1 COLOR=FF0000", "SET_MATERIAL LANE=e1 MATERIAL=PLA"});
}

TEST_CASE("An AFC write with no colour falls back to white", "[MoonrakerFilamentDialect]")
{
    IPrinterAgent::FilamentSlotInfo info = red_pla(0);
    info.color_rgba.clear();
    CHECK(afc_push_scripts("lane1", info).front() == "SET_COLOR LANE=lane1 COLOR=FFFFFF");
}

TEST_CASE("Happy Hare writes a gate in one MMU_GATE_MAP command", "[MoonrakerFilamentDialect]")
{
    // The gate is the slot index; MMU_GATE_MAP takes material and colour together.
    CHECK(happy_hare_push_script(red_pla(3)) == "MMU_GATE_MAP GATE=3 MATERIAL=PLA COLOR=FF0000");
}

TEST_CASE("Only the two writable dialects support a push", "[MoonrakerFilamentDialect]")
{
    CHECK(dialect_supports_push(Dialect::afc_lane_data));
    CHECK(dialect_supports_push(Dialect::happy_hare));
    CHECK_FALSE(dialect_supports_push(Dialect::none));
}
