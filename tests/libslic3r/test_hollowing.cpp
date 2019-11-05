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

static Slic3r::TriangleMesh hollowed_interior(const Slic3r::TriangleMesh &mesh,
                                              double min_thickness)
{
    Slic3r::sla::Contour3D imesh = Slic3r::sla::Contour3D{mesh};
    
    double scale = std::max(1.0, 3. / min_thickness);
    double offset = scale * min_thickness;
    float range = float(std::max(2 * offset, scale));
    
    for (auto &p : imesh.points) p *= scale;
    auto ptr = Slic3r::meshToVolume(imesh, {}, 0.1f * float(offset), range);
    
    REQUIRE(ptr);
    
    openvdb::tools::Filter<openvdb::FloatGrid>{*ptr}.gaussian(int(scale), 1);
    
    double iso_surface = -offset;
    double adaptivity = 0.;
    Slic3r::sla::Contour3D omesh = Slic3r::volumeToMesh(*ptr, iso_surface, adaptivity);
    
    REQUIRE(!omesh.empty());
    
    for (auto &p : omesh.points) p /= scale;
    
    return to_triangle_mesh(omesh);
}

TEST_CASE("Negative 3D offset should produce smaller object.", "[Hollowing]")
{
    Slic3r::TriangleMesh in_mesh = load_model("20mm_cube.obj");
    Benchmark bench;
    bench.start();
    
    Slic3r::TriangleMesh out_mesh = hollowed_interior(in_mesh, 2);
    
    bench.stop();
    
    std::cout << "Elapsed processing time: " << bench.getElapsedSec() << std::endl;
    
    in_mesh.merge(out_mesh);
    in_mesh.require_shared_vertices();
    in_mesh.WriteOBJFile("merged_out.obj");
}
