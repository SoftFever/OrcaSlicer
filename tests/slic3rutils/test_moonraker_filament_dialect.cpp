#include <catch2/catch_all.hpp>

#include <nlohmann/json.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "slic3r/Utils/MoonrakerPrinterAgent.hpp"
#include "slic3r/Utils/PrintHost.hpp"

using Slic3r::IPrinterAgent;
using namespace Slic3r::MoonrakerFilamentDialect;

// Orca: the materials dialog writes a slot back through IPrinterAgent::push_filament_info. On a
// Moonraker printer the dialect is whichever filament changer reported the slots: AFC (the
// lane_data namespace) or Happy Hare (the mmu object). Both are plain g-code commands, so the
// rendering is pinned here as strings, like the Snapmaker and WonderMaker start scripts.

namespace {

IPrinterAgent::FilamentSlotInfo red_pla(int slot, const std::string& name = "")
{
    IPrinterAgent::FilamentSlotInfo info;
    info.slot       = slot;
    info.name       = name;
    info.vendor     = "Polymaker";
    info.type       = "PLA";
    info.sub_type   = "PolyTerra";
    info.color_rgba = "FF0000FF";
    return info;
}

} // namespace

TEST_CASE("AFC has no combined command, so a slot write is two commands", "[MoonrakerFilamentDialect]")
{
    // Colour goes without alpha (AFC stores RRGGBB); vendor and sub-type have no field in AFC
    // and are not sent.
    // The lane is addressed by the name the slot came with (AmsTrayData::slot_name), not by index.
    CHECK(afc_push_scripts(red_pla(2, "e1")) ==
          std::vector<std::string>{"SET_COLOR LANE=e1 COLOR=FF0000", "SET_MATERIAL LANE=e1 MATERIAL=PLA"});
}

TEST_CASE("An AFC write with no colour falls back to white", "[MoonrakerFilamentDialect]")
{
    IPrinterAgent::FilamentSlotInfo info = red_pla(0, "lane1");
    info.color_rgba.clear();
    CHECK(afc_push_scripts(info).front() == "SET_COLOR LANE=lane1 COLOR=FFFFFF");
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

// Print-time mapping. The map is reset first so the result does not depend on what the printer's
// own screen left behind, then each USED tool is assigned in ascending order (AFC's SET_MAP is a
// swap; ascending assignment settles), then the plain SD start the Moonraker agent always sends.
// tool_to_slot is the dialog's shape: 1-based slot per 0-based tool, 0 = unassigned.

TEST_CASE("AFC start script resets the map, assigns used tools by lane name, then starts", "[MoonrakerFilamentDialect]")
{
    const std::vector<std::string> names = {"lane1", "lane2", "lane3"};
    CHECK(afc_mapping_start_script("cube.gcode", {3, 1, 0}, names) ==
          "RESET_AFC_MAPPING\n"
          "SET_MAP LANE=lane3 MAP=T0\n"
          "SET_MAP LANE=lane1 MAP=T1\n"
          "SDCARD_PRINT_FILE FILENAME=\"cube.gcode\"");
}

TEST_CASE("An AFC tool mapped to a slot with no lane name renders no script", "[MoonrakerFilamentDialect]")
{
    // An empty script is what the send path treats as "the map never reached the printer" and
    // refuses to auto-start on; better than a SET_MAP naming nothing.
    CHECK(afc_mapping_start_script("cube.gcode", {2}, {"lane1", ""}).empty());
    CHECK(afc_mapping_start_script("cube.gcode", {2}, {"lane1"}).empty());
}

TEST_CASE("Happy Hare start script resets the tool-to-gate map and assigns used tools", "[MoonrakerFilamentDialect]")
{
    CHECK(happy_hare_mapping_start_script("cube.gcode", {3, 1, 0}) ==
          "MMU_TTG_MAP RESET=1\n"
          "MMU_TTG_MAP TOOL=0 GATE=2\n"
          "MMU_TTG_MAP TOOL=1 GATE=0\n"
          "SDCARD_PRINT_FILE FILENAME=\"cube.gcode\"");
}

TEST_CASE("The Klipper changer protocol renders in the dialect the printer reported", "[MoonrakerFilamentDialect]")
{
    Slic3r::DevicePrintJobInfo job;
    job.filament_map_1based = {2, 1};
    job.slot_names          = {"lane1", "lane2"};
    job.changer_dialect     = "afc";
    CHECK(Slic3r::build_device_start_script(Slic3r::FilamentMappingProtocol::fmpKlipperChanger, "cube.gcode", job) ==
          "RESET_AFC_MAPPING\nSET_MAP LANE=lane2 MAP=T0\nSET_MAP LANE=lane1 MAP=T1\nSDCARD_PRINT_FILE FILENAME=\"cube.gcode\"");
    job.changer_dialect = "happy_hare";
    CHECK(Slic3r::build_device_start_script(Slic3r::FilamentMappingProtocol::fmpKlipperChanger, "cube.gcode", job) ==
          "MMU_TTG_MAP RESET=1\nMMU_TTG_MAP TOOL=0 GATE=1\nMMU_TTG_MAP TOOL=1 GATE=0\nSDCARD_PRINT_FILE FILENAME=\"cube.gcode\"");
    // No changer reported: nothing to send, and the send path refuses to auto-start on that.
    job.changer_dialect.clear();
    CHECK(Slic3r::build_device_start_script(Slic3r::FilamentMappingProtocol::fmpKlipperChanger, "cube.gcode", job).empty());
    // The standard device dialog with a mapping and no printer-specific options.
    CHECK(Slic3r::device_print_spec(Slic3r::FilamentMappingProtocol::fmpKlipperChanger).supports_filament_mapping);
    CHECK(Slic3r::device_print_spec(Slic3r::FilamentMappingProtocol::fmpKlipperChanger).options.empty());
}
