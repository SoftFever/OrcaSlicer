#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCode/GCodeStatistics.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"

#include "test_helpers.hpp"
#include "test_utils.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <vector>

using namespace Slic3r;

TEST_CASE("The statistics of a sliced plate add up per layer, feature and object instance", "[GCodeStatistics]")
{
    // A 20 mm cube at 0.2 mm layers prints 100 of them; without skirt or brim all its filament goes to its instances.
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({ { "layer_height", 0.2 },
                                    { "initial_layer_print_height", 0.2 },
                                    { "skirt_loops", 0 },
                                    { "brim_type", "no_brim" },
                                    { "filament_density", 1.25 } });
    Print print;
    Model model;
    Test::init_print({ Test::cube(20) }, print, model, config, nullptr, true, 2);
    GCodeProcessorResult result;
    Test::gcode(print, &result);

    ScopedTemporaryFile file(".json");
    export_gcode_statistics(result, file.string());
    std::string text;
    load_string_file(file.path(), text);
    const nlohmann::json statistics = nlohmann::json::parse(text);

    const nlohmann::json &summary = statistics["summary"];
    const nlohmann::json &layers  = statistics["layers"];
    REQUIRE(layers.size() == 100);
    CHECK(summary["layer_count"] == 100);
    CHECK_THAT(layers.front()["z"].get<double>(), Catch::Matchers::WithinAbs(0.2, 1e-3));
    CHECK_THAT(layers.back()["z"].get<double>(), Catch::Matchers::WithinAbs(20., 1e-3));

    double time = 0., volume = 0.;
    for (const nlohmann::json &layer : layers) {
        time += layer["time"].get<double>();
        volume += layer["volume"].get<double>();
        double features_time = 0.;
        for (const auto &[name, feature] : layer["features"].items())
            features_time += feature["time"].get<double>();
        CHECK(features_time <= layer["time"].get<double>() + 1e-3);
    }
    // Each value is rounded to the millisecond and the cubic millimeter.
    CHECK_THAT(time, Catch::Matchers::WithinAbs(summary["time"].get<double>(), 0.1));
    CHECK_THAT(volume, Catch::Matchers::WithinAbs(summary["volume"].get<double>(), 0.1));
    CHECK_THAT(summary["weight"].get<double>(), Catch::Matchers::WithinRel(summary["volume"].get<double>() * 1.25 / 1000., 1e-4));

    const nlohmann::json &features = summary["features"];
    CHECK(features.contains("Outer wall"));
    CHECK(features.contains("Sparse infill"));
    double model_volume = 0.;
    for (const auto &[name, feature] : features.items())
        if (name != "Custom")
            model_volume += feature["volume"].get<double>();

    const nlohmann::json &objects = statistics["objects"];
    REQUIRE(objects.size() == 2);
    double objects_volume = 0.;
    for (size_t i = 0; i < objects.size(); ++i) {
        CHECK(objects[i]["name"] == model.objects.front()->name);
        CHECK(objects[i]["instance"] == int(i));
        CHECK(objects[i]["support_volume"].get<double>() == 0.);
        objects_volume += objects[i]["volume"].get<double>();
    }
    CHECK_THAT(objects_volume, Catch::Matchers::WithinAbs(model_volume, 0.1));
}

TEST_CASE("The statistics file takes the G-code's name with the statistics extension", "[GCodeStatistics]")
{
    CHECK(gcode_statistics_path("plate_1.gcode") == "plate_1.statistics.json");
    CHECK(gcode_statistics_path("out/Cube_PLA_1h.gcode") == "out/Cube_PLA_1h.statistics.json");
}
