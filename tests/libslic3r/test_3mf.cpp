#include <catch2/catch.hpp>

#include "libslic3r/Model.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/Format/stl.hpp"

#include <boost/filesystem/operations.hpp>

using namespace Slic3r;

SCENARIO("Reading 3mf file", "[3mf]") {
    GIVEN("umlauts in the path of the file") {
        Model model;
        WHEN("3mf model is read") {
        	std::string path = std::string(TEST_DATA_DIR) + "/test_3mf/Geräte/Büchse.3mf";
        	DynamicPrintConfig config;
            bool ret = load_3mf(path.c_str(), &config, &model, false);
            THEN("load should succeed") {
                REQUIRE(ret);
            }
        }
    }
}

SCENARIO("Export+Import geometry to/from 3mf file cycle", "[3mf]") {
    GIVEN("world vertices coordinates before save") {
        // load a model from stl file
        Model src_model;
        std::string src_file = std::string(TEST_DATA_DIR) + "/test_3mf/prusa.stl";
        load_stl(src_file.c_str(), &src_model);
        src_model.add_default_instances();

        ModelObject* src_object = src_model.objects[0];

        // apply generic transformation to the 1st volume
        Geometry::Transformation src_volume_transform;
        src_volume_transform.set_offset(Vec3d(10.0, 20.0, 0.0));
        src_volume_transform.set_rotation(Vec3d(Geometry::deg2rad(25.0), Geometry::deg2rad(35.0), Geometry::deg2rad(45.0)));
        src_volume_transform.set_scaling_factor(Vec3d(1.1, 1.2, 1.3));
        src_volume_transform.set_mirror(Vec3d(-1.0, 1.0, -1.0));
        src_object->volumes[0]->set_transformation(src_volume_transform);

        // apply generic transformation to the 1st instance
        Geometry::Transformation src_instance_transform;
        src_instance_transform.set_offset(Vec3d(5.0, 10.0, 0.0));
        src_instance_transform.set_rotation(Vec3d(Geometry::deg2rad(12.0), Geometry::deg2rad(13.0), Geometry::deg2rad(14.0)));
        src_instance_transform.set_scaling_factor(Vec3d(0.9, 0.8, 0.7));
        src_instance_transform.set_mirror(Vec3d(1.0, -1.0, -1.0));
        src_object->instances[0]->set_transformation(src_instance_transform);

        WHEN("model is saved+loaded to/from 3mf file") {
            // save the model to 3mf file
            std::string test_file = std::string(TEST_DATA_DIR) + "/test_3mf/prusa.3mf";
            store_3mf(test_file.c_str(), &src_model, nullptr);

            // load back the model from the 3mf file
            Model dst_model;
            DynamicPrintConfig dst_config;
            load_3mf(test_file.c_str(), &dst_config, &dst_model, false);
            boost::filesystem::remove(test_file);

            // compare meshes
            TriangleMesh src_mesh = src_model.mesh();
            src_mesh.repair();

            TriangleMesh dst_mesh = dst_model.mesh();
            dst_mesh.repair();

            bool res = src_mesh.its.vertices.size() == dst_mesh.its.vertices.size();
            if (res)
            {
                for (size_t i = 0; i < dst_mesh.its.vertices.size(); ++i)
                {
                    res &= dst_mesh.its.vertices[i].isApprox(src_mesh.its.vertices[i]);
                    if (!res)
                    {
                        Vec3f diff = dst_mesh.its.vertices[i] - src_mesh.its.vertices[i];
                        std::cout << i << ": diff " << to_string((Vec3d)diff.cast<double>()) << "\n";
                    }
                }
            }
            THEN("world vertices coordinates after load match") {
                REQUIRE(res);
            }
        }
    }
}
