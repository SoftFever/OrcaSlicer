#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <catch2/catch_message.hpp>
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ModelArrange.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "test_helpers.hpp"

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Point.hpp"
#include <string>
#include <vector>

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

TEST_CASE("x_after_toolchange and position placeholders use machine coordinates on non-first plates", "[GCode]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        {"gcode_flavor",                    "marlin"},
        {"single_extruder_multi_material",  "1"},
        {"enable_prime_tower",              "0"},
        {"change_filament_gcode",
            ";TC_PLACEHOLDER X[x_after_toolchange] Y[y_after_toolchange]\n"}
    });

    Print print;
    Model model;

    Test::init_print(
        std::vector<TriangleMesh>{Test::cube(20.), Test::cube(20.)},
        print, model, config, nullptr, false, 1);

    REQUIRE(model.objects.size() >= 2);
    model.objects[0]->volumes.front()->config.set_key_value("extruder", new ConfigOptionInt(1));
    model.objects[1]->volumes.front()->config.set_key_value("extruder", new ConfigOptionInt(2));

    constexpr double plate_offset_x = 350.0;
    print.set_plate_origin(Vec3d(plate_offset_x, 0.0, 0.0));

    model.objects[0]->instances.front()->set_offset(Vec3d(plate_offset_x + 50.0, 50.0, 0.0));
    model.objects[1]->instances.front()->set_offset(Vec3d(plate_offset_x + 80.0, 50.0, 0.0));

    arrange_objects(model,
        BoundingBox{Point::new_scale(0., 0.), Point::new_scale(500., 500.)},
        ArrangeParams{scaled(min_object_distance(config))});
    print.apply(model, config);

    const std::string gcode = Test::gcode(print);

    const std::string marker = ";TC_PLACEHOLDER X";
    const size_t marker_pos = gcode.find(marker);
    REQUIRE(marker_pos != std::string::npos);

    const size_t x_start = marker_pos + marker.length();
    const size_t x_end   = gcode.find(" Y", x_start);
    REQUIRE(x_end != std::string::npos);
    const double x_val = std::stod(gcode.substr(x_start, x_end - x_start));

    constexpr double bed_width = 250.0;
    INFO("x_after_toolchange = " << x_val << " (must be in [0, " << bed_width << "])");
    CHECK(x_val >= 0.0);
    CHECK(x_val <  bed_width);
}