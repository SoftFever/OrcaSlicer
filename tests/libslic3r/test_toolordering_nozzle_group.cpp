#include <catch2/catch_all.hpp>

#include "libslic3r/GCode/ToolOrdering.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <set>
#include <string>
#include <vector>

using namespace Slic3r;

TEST_CASE("Grouping context spans the filament count with mis-sized config arrays", "[ToolOrdering]")
{
    // FilamentGroup indexes the grouping context's filament_info by filament id, so a short
    // per-filament array must not shorten it: the reads run off the end.
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    // Two extruders on a BBL printer is what opens the grouping engine on this branch (H2D).
    config.option<ConfigOptionFloats>("nozzle_diameter", true)->values = {0.4, 0.4};
    config.option<ConfigOptionStrings>("extruder_ams_count", true)->values = {"1#4", "1#4"};

    // Four filaments, with filament_type / filament_is_support left short on purpose.
    config.option<ConfigOptionStrings>("filament_colour", true)->values = {"#FF0000", "#00FF00", "#0000FF", "#FFFF00"};
    config.option<ConfigOptionStrings>("filament_type", true)->values = {"PLA"};
    config.option<ConfigOptionBools>("filament_is_support", true)->values = {0};
    config.option<ConfigOptionFloats>("filament_diameter", true)->values = {1.75, 1.75, 1.75, 1.75};
    config.option<ConfigOptionInts>("filament_map", true)->values = {1, 1, 2, 2};
    config.option<ConfigOptionFloats>("flush_volumes_matrix", true)->values = std::vector<double>(32, 140.);
    config.option<ConfigOptionFloats>("flush_multiplier", true)->values = {1., 1.};

    Model model;
    model.add_object("cube", "", make_cube(20, 20, 20))->add_instance();

    Print print;
    print.apply(model, config);
    print.is_BBL_printer() = true;
    // apply() does not pad the per-filament arrays, so the mis-sizing survives into the engine.
    REQUIRE(print.config().filament_type.values.size() < print.config().filament_colour.values.size());

    std::vector<std::vector<unsigned int>> layer_filaments = {{0, 1}, {1, 2}, {2, 3}};

    SECTION("short per-filament arrays still yield one entry per filament") {
        auto result = ToolOrdering::get_recommended_filament_maps(layer_filaments, &print, FilamentMapMode::fmmAutoForFlush, {}, {});
        REQUIRE(result.size() == 4);
        for (int extruder : result)
            CHECK((extruder == 0 || extruder == 1));
    }

    SECTION("filament_ids longer than the filament count is truncated, not paired past the end") {
        config.option<ConfigOptionStrings>("filament_ids", true)->values = {"a", "b", "c", "d", "e", "f"};
        print.apply(model, config);
        auto result = ToolOrdering::get_recommended_filament_maps(layer_filaments, &print, FilamentMapMode::fmmAutoForFlush, {}, {});
        REQUIRE(result.size() == 4);
    }
}
