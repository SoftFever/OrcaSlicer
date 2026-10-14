#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <catch2/catch_message.hpp>
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ModelArrange.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "test_helpers.hpp"
#include "test_utils.hpp"

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Point.hpp"
#include <fstream>
#include <ios>
#include <string>
#include <vector>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace Slic3r;

TEST_CASE("Klipper object labels name each copy without the characters Klipper cannot parse", "[GCode]")
{
    const auto [name, label] = GENERATE(table<std::string, std::string>({
        {"my part (2)", "my_part_2"},
        {"(cube)", "cube"},
    }));
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({{"gcode_flavor", "klipper"}, {"exclude_object", "1"}});
    Print print;
    Model model;
    Test::init_print(std::vector<TriangleMesh>{Test::cube(20.)}, print, model, config, nullptr, false, 2);
    model.objects.front()->name = name;
    arrange_objects(model, BoundingBox{Point::new_scale(0., 0.), Point::new_scale(500., 500.)},
                    ArrangeParams{scaled(min_object_distance(config))});
    print.apply(model, config);

    const std::string gcode = Test::gcode(print);
    for (const char *copy : {"0", "1"}) {
        const std::string instance_label = label + "_id_0_copy_" + copy;
        INFO(instance_label);
        CHECK(gcode.find("EXCLUDE_OBJECT_DEFINE NAME=" + instance_label + " ") != std::string::npos);
        CHECK(gcode.find("EXCLUDE_OBJECT_START NAME=" + instance_label + "\n") != std::string::npos);
    }
}

TEST_CASE("Object label comments number each object and copy with or without exclude object", "[GCode]")
{
    // repetier has no exclude-object commands, so set_object_info() writes nothing for it.
    const auto [flavor, exclude_object] = GENERATE(table<std::string, std::string>({
        {"marlin2", "0"},
        {"klipper", "0"},
        {"klipper", "1"},
        {"repetier", "1"},
    }));
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({{"gcode_flavor", flavor}, {"exclude_object", exclude_object}, {"gcode_label_objects", "1"}});
    Print print;
    Model model;
    Test::init_print(std::vector<TriangleMesh>{Test::cube(20.), Test::cube(20.)}, print, model, config, nullptr, false, 2);
    arrange_objects(model, BoundingBox{Point::new_scale(0., 0.), Point::new_scale(500., 500.)},
                    ArrangeParams{scaled(min_object_distance(config))});
    print.apply(model, config);

    const std::string gcode = Test::gcode(print);
    for (const char *label : {"id:0 copy 0", "id:0 copy 1", "id:1 copy 0", "id:1 copy 1"}) {
        INFO(flavor << ", exclude_object " << exclude_object << ": " << label);
        CHECK(gcode.find("; printing object object.stl " + std::string(label) + "\n") != std::string::npos);
    }
}

TEST_CASE("An exported G-code's settings load back as the settings it was sliced with", "[GCode]")
{
    // Two filaments, so the config block carries the flush matrix already multiplied by flush_multiplier.
    const DynamicPrintConfig config = Test::multifilament_config(2, {
        { "flush_multiplier", "0.5" },
        { "printable_area", "0x0,250x0,250x210,0x210" },
        { "printable_height", "180" },
    });
    Print print;
    Model model;
    Test::init_print(std::vector<TriangleMesh>{ Test::cube(20.) }, print, model, config, nullptr, false);
    ScopedTemporaryFile file(".gcode");
    {
        std::ofstream os(file.string(), std::ios::binary);
        os << Test::gcode(print);
        REQUIRE(os.good());
    }

    PresetBundle bundle;
    // The config block leaves prime_volume_mode out, so the receiver's choice has to survive the load.
    bundle.project_config.option<ConfigOptionEnum<PrimeVolumeMode>>("prime_volume_mode")->value = PrimeVolumeMode::pvmFast;
    ConfigSubstitutions substitutions;
    bundle.load_config_model(file.string(), bundle.config_from_gcode(file.string(), ForwardCompatibilitySubstitutionRule::Enable, substitutions));

    const DynamicPrintConfig &printer = bundle.printers.get_edited_preset().config;
    CHECK(printer.opt_serialize("printable_area") == config.opt_serialize("printable_area"));
    CHECK_THAT(printer.opt_float("printable_height"), Catch::Matchers::WithinAbs(180., 1e-6));
    // The purge between the two filaments is what the slice used: 280 at a 0.5 multiplier.
    const std::vector<double> &matrix     = bundle.project_config.option<ConfigOptionFloats>("flush_volumes_matrix")->values;
    const std::vector<double> &multiplier = bundle.project_config.option<ConfigOptionFloats>("flush_multiplier")->values;
    REQUIRE(matrix.size() == 4);
    REQUIRE(multiplier.size() == 1);
    CHECK_THAT(matrix[1] * multiplier[0], Catch::Matchers::WithinAbs(140., 1e-6));
    CHECK(bundle.project_config.option<ConfigOptionEnum<PrimeVolumeMode>>("prime_volume_mode")->value == PrimeVolumeMode::pvmFast);
}
