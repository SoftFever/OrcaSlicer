#include <iostream>
#include <fstream>
#include <catch2/catch.hpp>

#include "libslic3r/SLA/Hollowing.hpp"

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

TEST_CASE("Split its") {
    using namespace Slic3r;

    TriangleMesh sphere1 = make_sphere(10., 2 * PI / 20.), sphere2 = sphere1;

    sphere1.translate(-5.f, 0.f, 0.f);
    sphere2.translate( 5.f, 0.f, 0.f);

    sphere1.merge(sphere2);
    sphere1.require_shared_vertices();

    std::vector<indexed_triangle_set> parts;
    its_split(sphere1.its, std::back_inserter(parts));

    size_t part_idx = 0;
    for (auto &part_its : parts) {
        its_write_obj(part_its, (std::string("part_its") + std::to_string(part_idx++) + ".obj").c_str());
    }
}

