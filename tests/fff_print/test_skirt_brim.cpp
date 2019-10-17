#include <catch2/catch.hpp>

#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Geometry.hpp"

#include <boost/algorithm/string.hpp>

#include "test_data.hpp" // get access to init_print, etc

using namespace Slic3r::Test;
using namespace Slic3r;

/// Helper method to find the tool used for the brim (always the first extrusion)
int get_brim_tool(std::string &gcode, Slic3r::GCodeReader& parser) {
    int brim_tool = -1;
    int tool = -1;

    parser.parse_buffer(gcode, [&tool, &brim_tool] (Slic3r::GCodeReader& self, const Slic3r::GCodeReader::GCodeLine& line)
        {
            // if the command is a T command, set the the current tool
            if (boost::starts_with(line.cmd(), "T")) {
                tool = atoi(line.cmd().data() + 1);
            } else if (line.cmd() == "G1" && line.extruding(self) && line.dist_XY(self) > 0 && brim_tool < 0) {
                brim_tool = tool;
            }
        });

    return brim_tool;
}

TEST_CASE("Skirt height is honored") {
    DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
    config.opt_int("skirts") = 1;
    config.opt_int("skirt_height") = 5;
    config.opt_int("perimeters") = 0;
    config.opt_float("support_material_speed") = 99;

    // avoid altering speeds unexpectedly
    config.set_deserialize("cooling", "0");
    config.set_deserialize("first_layer_speed", "100%");
    double support_speed = config.opt<Slic3r::ConfigOptionFloat>("support_material_speed")->value * MM_PER_MIN;

    std::map<double, bool> layers_with_skirt;
	std::string gcode;
	GCodeReader parser;
    Slic3r::Model model;

    SECTION("printing a single object") {
        auto print = Slic3r::Test::init_print({TestMesh::cube_20x20x20}, model, config);
        gcode = Slic3r::Test::gcode(print);
    }

    SECTION("printing multiple objects") {
        auto print = Slic3r::Test::init_print({TestMesh::cube_20x20x20, TestMesh::cube_20x20x20}, model, config);
		gcode = Slic3r::Test::gcode(print);
    }
    parser.parse_buffer(gcode, [&layers_with_skirt, &support_speed] (Slic3r::GCodeReader& self, const Slic3r::GCodeReader::GCodeLine& line)
    {
        if (line.extruding(self) && self.f() == Approx(support_speed)) {
            layers_with_skirt[self.z()] = 1;
        }
    });

    REQUIRE(layers_with_skirt.size() == (size_t)config.opt_int("skirt_height"));
}

SCENARIO("Original Slic3r Skirt/Brim tests", "[!mayfail]") {
    Slic3r::GCodeReader parser;
    Slic3r::Model model;
	std::string gcode;
    GIVEN("A default configuration") {
	    DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
		config.set_num_extruders(4);
		config.opt_float("support_material_speed") = 99;
		config.set_deserialize("first_layer_height", "0.3");
        config.set_deserialize("gcode_comments", "1");

        // avoid altering speeds unexpectedly
        config.set_deserialize("cooling", "0");
        config.set_deserialize("first_layer_speed", "100%");
        // remove noise from top/solid layers
        config.opt_int("top_solid_layers") = 0;
        config.opt_int("bottom_solid_layers") = 1;

        WHEN("Brim width is set to 5") {
			config.opt_int("perimeters") = 0;
			config.opt_int("skirts") = 0;
            config.opt_float("brim_width") = 5;
            THEN("Brim is generated") {
                auto print = Slic3r::Test::init_print({TestMesh::cube_20x20x20}, model, config);
                gcode = Slic3r::Test::gcode(print);
                bool brim_generated = false;
                double support_speed = config.opt<Slic3r::ConfigOptionFloat>("support_material_speed")->value * MM_PER_MIN;
                parser.parse_buffer(gcode, [&brim_generated, support_speed] (Slic3r::GCodeReader& self, const Slic3r::GCodeReader::GCodeLine& line)
                    {
                        if (self.z() == Approx(0.3) || line.new_Z(self) == Approx(0.3)) {
                            if (line.extruding(self) && self.f() == Approx(support_speed)) {
                                brim_generated = true;
                            }
                        }
                    });
                REQUIRE(brim_generated);
            }
        }

        WHEN("Skirt area is smaller than the brim") {
            config.opt_int("skirts") = 1;
            config.opt_float("brim_width") = 10;
            auto print = Slic3r::Test::init_print({TestMesh::cube_20x20x20}, model, config);
            THEN("Gcode generates") {
                REQUIRE(! Slic3r::Test::gcode(print).empty());
            }
        }

        WHEN("Skirt height is 0 and skirts > 0") {
			config.opt_int("skirts") = 2;
			config.opt_int("skirt_height") = 0;

            auto print = Slic3r::Test::init_print({TestMesh::cube_20x20x20}, model, config);
            THEN("Gcode generates") {
                REQUIRE(! Slic3r::Test::gcode(print).empty());
            }
        }

        WHEN("Perimeter extruder = 2 and support extruders = 3") {
			config.opt_int("skirts") = 0;
			config.opt_float("brim_width") = 5;
			config.opt_int("perimeter_extruder") = 2;
			config.opt_int("support_material_extruder") = 3;
            THEN("Brim is printed with the extruder used for the perimeters of first object") {
                auto print = Slic3r::Test::init_print({TestMesh::cube_20x20x20}, model, config);
                gcode = Slic3r::Test::gcode(print);
                int tool = get_brim_tool(gcode, parser);
                REQUIRE(tool == config.opt_int("perimeter_extruder") - 1);
            }
        }
        WHEN("Perimeter extruder = 2, support extruders = 3, raft is enabled") {
            config.opt_int("skirts") = 0;
            config.opt_float("brim_width") = 5;
            config.opt_int("perimeter_extruder") = 2;
            config.opt_int("support_material_extruder") = 3;
            config.opt_int("raft_layers") = 1;
            THEN("brim is printed with same extruder as skirt") {
                auto print = Slic3r::Test::init_print({TestMesh::cube_20x20x20}, model, config);
                gcode = Slic3r::Test::gcode(print);
                int tool = get_brim_tool(gcode, parser);
                REQUIRE(tool == config.opt_int("support_material_extruder") - 1);
            }
        }
        WHEN("brim width to 1 with layer_width of 0.5") {
			config.opt_int("skirts") = 0;
			config.set_deserialize("first_layer_extrusion_width", "0.5");
			config.opt_float("brim_width") = 1;
			
            THEN("2 brim lines") {
                Slic3r::Model model;
                auto print = Slic3r::Test::init_print({TestMesh::cube_20x20x20}, model, config);
                print->process();
                REQUIRE(print->brim().entities.size() == 2);
            }
        }

#if 0
        WHEN("brim ears on a square") {
			config.opt_int("skirts") = 0);
			config.set_deserialize("first_layer_extrusion_width", "0.5");
			config.opt_float("brim_width") = 1;
            config.set("brim_ears", true);
            config.set("brim_ears_max_angle", 91);
			
            Slic3r::Model model;
            auto print = Slic3r::Test::init_print({TestMesh::cube_20x20x20}, model, config);
            print->process();

            THEN("Four brim ears") {
                REQUIRE(print->brim.size() == 4);
            }
        }

        WHEN("brim ears on a square but with a too small max angle") {
            config.set("skirts", 0);
            config.set("first_layer_extrusion_width", 0.5);
            config.set("brim_width", 1);
            config.set("brim_ears", true);
            config.set("brim_ears_max_angle", 89);
			
            THEN("no brim") {
                Slic3r::Model model;
                auto print = Slic3r::Test::init_print({TestMesh::cube_20x20x20}, model, config);
                print->process();
                REQUIRE(print->brim.size() == 0);
            }
        }
#endif

        WHEN("Object is plated with overhang support and a brim") {
            config.opt_float("layer_height") = 0.4;
            config.set_deserialize("first_layer_height", "0.4");
            config.opt_int("skirts") = 1;
            config.opt_float("skirt_distance") = 0;
            config.opt_float("support_material_speed") = 99;
            config.opt_int("perimeter_extruder") = 1;
            config.opt_int("support_material_extruder") = 2;
            config.opt_int("infill_extruder") = 3;						// ensure that a tool command gets emitted.
            config.set_deserialize("cooling", "0");					// to prevent speeds to be altered
            config.set_deserialize("first_layer_speed", "100%");		// to prevent speeds to be altered

            Slic3r::Model model;
            auto print = Slic3r::Test::init_print({TestMesh::overhang}, model, config);
            print->process();

            // config.set("support_material", true);      // to prevent speeds to be altered

            THEN("skirt length is large enough to contain object with support") {
                CHECK(config.opt_bool("support_material")); // test is not valid if support material is off
                double skirt_length = 0.0;
                Points extrusion_points;
                int tool = -1;

                auto print = Slic3r::Test::init_print({TestMesh::cube_20x20x20}, model, config);
                std::string gcode = Slic3r::Test::gcode(print);

                double support_speed = config.opt<ConfigOptionFloat>("support_material_speed")->value * MM_PER_MIN;
                parser.parse_buffer(gcode, [config, &extrusion_points, &tool, &skirt_length, support_speed] (Slic3r::GCodeReader& self, const Slic3r::GCodeReader::GCodeLine& line)
                    {
                        // std::cerr << line.cmd() << "\n";
						if (boost::starts_with(line.cmd(), "T")) {
							tool = atoi(line.cmd().data() + 1);
						} else if (self.z() == Approx(config.opt<ConfigOptionFloat>("first_layer_height")->value)) {
                            // on first layer
							if (line.extruding(self) && line.dist_XY(self) > 0) {
                                float speed = ( self.f() > 0 ?  self.f() : line.new_F(self));
                                // std::cerr << "Tool " << tool << "\n";
                                if (speed == Approx(support_speed) && tool == config.opt_int("perimeter_extruder") - 1) {
                                    // Skirt uses first material extruder, support material speed.
                                    skirt_length += line.dist_XY(self);
                                } else {
                                    extrusion_points.push_back(Slic3r::Point::new_scale(line.new_X(self), line.new_Y(self)));
                                }
                            }
                        }

                        if (self.z() == Approx(0.3) || line.new_Z(self) == Approx(0.3)) {
                            if (line.extruding(self) && self.f() == Approx(support_speed)) {
                            }
                        }
                    });
                Slic3r::Polygon convex_hull = Slic3r::Geometry::convex_hull(extrusion_points);
                double hull_perimeter = unscale<double>(convex_hull.split_at_first_point().length());
                REQUIRE(skirt_length > hull_perimeter);
            }
        }
        WHEN("Large minimum skirt length is used.") {
            config.opt_float("min_skirt_length") = 20;
            Slic3r::Model model;
            auto print = Slic3r::Test::init_print({TestMesh::cube_20x20x20}, model, config);
            THEN("Gcode generation doesn't crash") {
                REQUIRE(! Slic3r::Test::gcode(print).empty());
            }
        }
    }
}
