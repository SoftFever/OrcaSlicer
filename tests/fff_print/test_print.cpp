#include <catch2/catch.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Print.hpp"

#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

SCENARIO("PrintObject: Perimeter generation", "[PrintObject]") {
    GIVEN("20mm cube and default config") {
        Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
        TestMesh m = TestMesh::cube_20x20x20;
        Slic3r::Model model;
        size_t event_counter = 0;
        std::string stage;
        int value = 0;
        auto callback = [&event_counter, &stage, &value] (int a, const char* b) { stage = std::string(b); ++ event_counter; value = a; };
        config.set_deserialize("fill_density", "0");

        WHEN("make_perimeters() is called")  {
            std::shared_ptr<Slic3r::Print> print = Slic3r::Test::init_print({m}, model, config);
			print->process();
			const PrintObject& object = *(print->objects().at(0));
			THEN("67 layers exist in the model") {
                REQUIRE(object.layers().size() == 66);
            }
            THEN("Every layer in region 0 has 1 island of perimeters") {
                for (const Layer *layer : object.layers()) {
                    REQUIRE(layer->regions().front()->perimeters.entities.size() == 1);
                }
            }
            THEN("Every layer in region 0 has 3 paths in its perimeters list.") {
                for (const Layer *layer : object.layers()) {
                    REQUIRE(layer->regions().front()->perimeters.items_count() == 3);
                }
            }
        }
    }
}

SCENARIO("Print: Skirt generation", "[Print]") {
    GIVEN("20mm cube and default config") {
        Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
        TestMesh m = TestMesh::cube_20x20x20;
        Slic3r::Model model;
        std::string stage;
        int value = 0;
        config.opt_int("skirt_height") = 1;
        config.opt_float("skirt_distance") = 1.f;
        WHEN("Skirts is set to 2 loops")  {
            config.opt_int("skirts") = 2;
            std::shared_ptr<Slic3r::Print> print = Slic3r::Test::init_print({m}, model, config);
			print->process();
            THEN("Skirt Extrusion collection has 2 loops in it") {
                REQUIRE(print->skirt().items_count() == 2);
                REQUIRE(print->skirt().flatten().entities.size() == 2);
            }
        }
    }
}

void test_is_solid_infill(std::shared_ptr<Slic3r::Print> p, size_t obj_id, size_t layer_id ) {
    const PrintObject &obj = *(p->objects().at(obj_id));
    const Layer       &layer = *(obj.get_layer((int)layer_id));

    // iterate over all of the regions in the layer
    for (const LayerRegion *reg : layer.regions()) {
        // for each region, iterate over the fill surfaces
        for (const Surface& s : reg->fill_surfaces.surfaces) {
            CHECK(s.is_solid());
        }
    }
}

SCENARIO("Print: Changing number of solid surfaces does not cause all surfaces to become internal.", "[Print]") {
    GIVEN("sliced 20mm cube and config with top_solid_surfaces = 2 and bottom_solid_surfaces = 1") {
        Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
        TestMesh m = TestMesh::cube_20x20x20;
        config.opt_int("top_solid_layers") = 2;
        config.opt_int("bottom_solid_layers") = 1;
        config.opt_float("layer_height") = 0.5; // get a known number of layers
        config.set_deserialize("first_layer_height", "0.5");
        Slic3r::Model model;
        std::string stage;
        std::shared_ptr<Slic3r::Print> print = Slic3r::Test::init_print({m}, model, config);
        print->process();
        // Precondition: Ensure that the model has 2 solid top layers (39, 38)
        // and one solid bottom layer (0).
        test_is_solid_infill(print, 0, 0); // should be solid
        test_is_solid_infill(print, 0, 39); // should be solid
        test_is_solid_infill(print, 0, 38); // should be solid
        WHEN("Model is re-sliced with top_solid_layers == 3") {
			config.opt_int("top_solid_layers") = 3;
			print->apply(model, config);
            print->process();
            THEN("Print object does not have 0 solid bottom layers.") {
                test_is_solid_infill(print, 0, 0);
            }
            AND_THEN("Print object has 3 top solid layers") {
                test_is_solid_infill(print, 0, 39);
                test_is_solid_infill(print, 0, 38);
                test_is_solid_infill(print, 0, 37);
            }
        }
    }
}

SCENARIO("Print: Brim generation", "[Print]") {
    GIVEN("20mm cube and default config, 1mm first layer width") {
        Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
        TestMesh m = TestMesh::cube_20x20x20;
        Slic3r::Model model;
        std::string stage;
        int value = 0;
        config.set_deserialize("first_layer_extrusion_width", "1");
        WHEN("Brim is set to 3mm")  {
            config.opt_float("brim_width") = 3;
            std::shared_ptr<Slic3r::Print> print = Slic3r::Test::init_print({m}, model, config);
            print->process();
            THEN("Brim Extrusion collection has 3 loops in it") {
                REQUIRE(print->brim().items_count() == 3);
            }
        }
        WHEN("Brim is set to 6mm")  {
            config.opt_float("brim_width") = 6;
            std::shared_ptr<Slic3r::Print> print = Slic3r::Test::init_print({m}, model, config);
			print->process();
            THEN("Brim Extrusion collection has 6 loops in it") {
                REQUIRE(print->brim().items_count() == 6);
            }
        }
        WHEN("Brim is set to 6mm, extrusion width 0.5mm")  {
            config.opt_float("brim_width") = 6;
            config.set_deserialize("first_layer_extrusion_width", "0.5");
            std::shared_ptr<Slic3r::Print> print = Slic3r::Test::init_print({m}, model, config);
			print->process();
            THEN("Brim Extrusion collection has 12 loops in it") {
                REQUIRE(print->brim().items_count() == 14);
            }
        }
    }
}
