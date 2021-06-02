#include <iostream>
#include <fstream>
#include <catch2/catch.hpp>

#include "libslic3r/TriangleMesh.hpp"

TEST_CASE("Split empty mesh", "[its_split][its]") {
    using namespace Slic3r;

    indexed_triangle_set its;

    std::vector<indexed_triangle_set> res = its_split(its);

    REQUIRE(res.empty());
}

TEST_CASE("Split simple mesh consisting of one part", "[its_split][its]") {
    using namespace Slic3r;

    auto cube = its_make_cube(10., 10., 10.);

    std::vector<indexed_triangle_set> res = its_split(cube);

    REQUIRE(res.size() == 1);
    REQUIRE(res.front().indices.size() == cube.indices.size());
    REQUIRE(res.front().vertices.size() == cube.vertices.size());
}

void debug_write_obj(const std::vector<indexed_triangle_set> &res, const std::string &name)
{
#ifndef NDEBUG
    size_t part_idx = 0;
    for (auto &part_its : res) {
        its_write_obj(part_its, (name + std::to_string(part_idx++) + ".obj").c_str());
    }
#endif
}

TEST_CASE("Split two non-watertight mesh", "[its_split][its]") {
    using namespace Slic3r;

    auto cube1 = its_make_cube(10., 10., 10.);
    cube1.indices.pop_back();
    auto cube2 = cube1;

    its_transform(cube1, identity3f().translate(Vec3f{-5.f, 0.f, 0.f}));
    its_transform(cube2, identity3f().translate(Vec3f{5.f, 0.f, 0.f}));

    its_merge(cube1, cube2);

    std::vector<indexed_triangle_set> res = its_split(cube1);

    REQUIRE(res.size() == 2);
    REQUIRE(res[0].indices.size() == res[1].indices.size());
    REQUIRE(res[0].indices.size() == cube2.indices.size());
    REQUIRE(res[0].vertices.size() == res[1].vertices.size());
    REQUIRE(res[0].vertices.size() == cube2.vertices.size());

    debug_write_obj(res, "parts_non_watertight");
}

TEST_CASE("Split non-manifold mesh", "[its_split][its]") {
    using namespace Slic3r;

    auto cube = its_make_cube(10., 10., 10.), cube_low = cube;

    its_transform(cube_low, identity3f().translate(Vec3f{10.f, 10.f, 10.f}));
    its_merge(cube, cube_low);
    its_merge_vertices(cube);

    std::vector<indexed_triangle_set> res = its_split(cube);

    REQUIRE(res.size() == 2);
    REQUIRE(res[0].indices.size() == res[1].indices.size());
    REQUIRE(res[0].indices.size() == cube_low.indices.size());
    REQUIRE(res[0].vertices.size() == res[1].vertices.size());
    REQUIRE(res[0].vertices.size() == cube_low.vertices.size());

    debug_write_obj(res, "cubes_non_manifold");
}

TEST_CASE("Split two watertight meshes", "[its_split][its]") {
    using namespace Slic3r;

    auto sphere1 = its_make_sphere(10., 2 * PI / 200.), sphere2 = sphere1;

    its_transform(sphere1, identity3f().translate(Vec3f{-5.f, 0.f, 0.f}));
    its_transform(sphere2, identity3f().translate(Vec3f{5.f, 0.f, 0.f}));

    its_merge(sphere1, sphere2);

    std::vector<indexed_triangle_set> res = its_split(sphere1);

    REQUIRE(res.size() == 2);
    REQUIRE(res[0].indices.size() == res[1].indices.size());
    REQUIRE(res[0].indices.size() == sphere2.indices.size());
    REQUIRE(res[0].vertices.size() == res[1].vertices.size());
    REQUIRE(res[0].vertices.size() == sphere2.vertices.size());

    debug_write_obj(res, "parts_watertight");
}

