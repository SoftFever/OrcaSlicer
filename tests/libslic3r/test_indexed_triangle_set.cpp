#include <iostream>
#include <fstream>
#include <catch2/catch.hpp>

#include "libslic3r/TriangleMesh.hpp"

//#include "libnest2d/tools/benchmark.h"

TEST_CASE("Split empty mesh", "[its_split][its]") {
    using namespace Slic3r;

    indexed_triangle_set its;

    std::vector<indexed_triangle_set> res;
    its_split(its, std::back_inserter(res));

    REQUIRE(res.empty());
}

TEST_CASE("Split simple mesh consisting of one part", "[its_split][its]") {
    using namespace Slic3r;

    TriangleMesh cube = make_cube(10., 10., 10.);

    std::vector<indexed_triangle_set> res;
    its_split(cube.its, std::back_inserter(res));

    REQUIRE(res.size() == 1);
    REQUIRE(res.front().indices.size() == cube.its.indices.size());
    REQUIRE(res.front().vertices.size() == cube.its.vertices.size());
}

TEST_CASE("Split two merged spheres", "[its_split][its]") {
    using namespace Slic3r;

    TriangleMesh sphere1 = make_sphere(10., 2 * PI / 200.), sphere2 = sphere1;

    sphere1.translate(-5.f, 0.f, 0.f);
    sphere2.translate( 5.f, 0.f, 0.f);

    sphere1.merge(sphere2);
    sphere1.require_shared_vertices();

//    Benchmark bench;

//    bench.start();
    auto index = its_create_neighbors_index(sphere1.its);
    std::vector<indexed_triangle_set> parts = its_split(sphere1.its, index);
//    bench.stop();

//    std::cout << "split took " << bench.getElapsedSec() << " seconds." << std::endl;

    REQUIRE(parts.size() == 2);

#ifndef NDEBUG
    size_t part_idx = 0;
    for (auto &part_its : parts) {
        its_write_obj(part_its, (std::string("part_its") + std::to_string(part_idx++) + ".obj").c_str());
    }
#endif
}

//TEST_CASE("Split two merged spheres TriangleMesh", "[its_split][its]") {
//    using namespace Slic3r;

//    TriangleMesh sphere1 = make_sphere(10., 2 * PI / 200.), sphere2 = sphere1;

//    sphere1.translate(-5.f, 0.f, 0.f);
//    sphere2.translate( 5.f, 0.f, 0.f);

//    sphere1.merge(sphere2);
//    sphere1.require_shared_vertices();

//    Benchmark bench;

//    bench.start();
//    TriangleMeshPtrs parts = sphere1.split();
//    for (auto &part : parts) part->require_shared_vertices();
//    bench.stop();

//    std::cout << "split took " << bench.getElapsedSec() << " seconds." << std::endl;

//    REQUIRE(parts.size() == 2);

////#ifndef NDEBUG
////    size_t part_idx = 0;
////    for (auto &part : parts) {
////        its_write_obj(part->its, (std::string("part_its") + std::to_string(part_idx++) + ".obj").c_str());
////    }
////#endif
//}
