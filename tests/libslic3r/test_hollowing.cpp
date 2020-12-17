#include <iostream>
#include <fstream>
#include <catch2/catch.hpp>

#include <libslic3r/TriangleMesh.hpp>
#include "libslic3r/SLA/Hollowing.hpp"
#include <openvdb/tools/Filter.h>
#include "libslic3r/Format/OBJ.hpp"

#include <libnest2d/tools/benchmark.h>

#include <libslic3r/SimplifyMesh.hpp>

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


TEST_CASE("Hollow two overlapping spheres") {
    using namespace Slic3r;

    TriangleMesh sphere1 = make_sphere(10., 2 * PI / 20.), sphere2 = sphere1;

    sphere1.translate(-5.f, 0.f, 0.f);
    sphere2.translate( 5.f, 0.f, 0.f);

    sphere1.merge(sphere2);
    sphere1.require_shared_vertices();

    sla::hollow_mesh(sphere1, sla::HollowingConfig{}, sla::HollowingFlags::hfRemoveInsideTriangles);

    sphere1.WriteOBJFile("twospheres.obj");
}

