#include <iostream>
#include <fstream>
#include <catch2/catch.hpp>

#include "libslic3r/SLA/Hollowing.hpp"
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
    Benchmark bench;
    bench.start();
    
    Slic3r::TriangleMesh out_mesh = Slic3r::sla::generate_interior(in_mesh, 0.5);
    
    bench.stop();
    
    std::cout << "Elapsed processing time: " << bench.getElapsedSec() << std::endl;
    
    in_mesh.merge(out_mesh);
    in_mesh.require_shared_vertices();
    in_mesh.WriteOBJFile("merged_out.obj");
}
