#include <iostream>
#include <fstream>
#include <catch2/catch.hpp>

#include "libslic3r/OpenVDBUtils.hpp"
#include <openvdb/tools/Filter.h>
#include "libslic3r/Format/OBJ.hpp"

#include <libnest2d/tools/benchmark.h>

#if defined(WIN32) || defined(_WIN32)
#define PATH_SEPARATOR R"(\)"
#else
#define PATH_SEPARATOR R"(/)"
#endif

static Slic3r::TriangleMesh load_model(const std::string &obj_filename)
{
    Slic3r::TriangleMesh mesh;
    auto fpath = TEST_DATA_DIR PATH_SEPARATOR + obj_filename;
    Slic3r::load_obj(fpath.c_str(), &mesh);
    return mesh;
}

TEST_CASE("Negative 3D offset should produce smaller object.", "[Hollowing]")
{
    Slic3r::TriangleMesh in_mesh = load_model("20mm_cube.obj");
    in_mesh.scale(3.);
    Slic3r::sla::Contour3D imesh = Slic3r::sla::Contour3D{in_mesh};
    
    Benchmark bench;
    bench.start();
    
    openvdb::math::Transform tr;
    auto ptr = Slic3r::meshToVolume(imesh, {}, 0.0f, 10.0f);
    
    REQUIRE(ptr);
    
    openvdb::tools::Filter<openvdb::FloatGrid>{*ptr}.gaussian(1, 3);
    
    
    double iso_surface = -3.0;
    double adaptivity = 0.5;
    Slic3r::sla::Contour3D omesh = Slic3r::volumeToMesh(*ptr, iso_surface, adaptivity);
    
    REQUIRE(!omesh.empty());
    
    imesh.merge(omesh);
    
    for (auto &p : imesh.points) p /= 3.;
    
    bench.stop();
    
    std::cout << "Elapsed processing time: " << bench.getElapsedSec() << std::endl;
    std::fstream merged_outfile("merged_out.obj", std::ios::out);
    imesh.to_obj(merged_outfile);
    
    std::fstream outfile("out.obj", std::ios::out);
    omesh.to_obj(outfile);
}
