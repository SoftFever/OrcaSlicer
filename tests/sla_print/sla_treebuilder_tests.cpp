#include <catch2/catch.hpp>
#include <test_utils.hpp>

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/SLA/SupportTreeBuilder.hpp"

TEST_CASE("Test bridge_mesh_intersect on a cube's wall", "[SLABridgeMeshInters]") {
    using namespace Slic3r;

    TriangleMesh cube = make_cube(10., 10., 10.);

    sla::SupportConfig cfg = {}; // use default config
    sla::SupportPoints pts = {{10.f, 5.f, 5.f, float(cfg.head_front_radius_mm), false}};
    sla::SupportableMesh sm{cube, pts, cfg};

    SECTION("Bridge is straight horizontal and pointing away from the cube") {

        sla::Bridge bridge(pts[0].pos.cast<double>(), Vec3d{15., 5., 5.},
                           pts[0].head_front_radius);

        auto hit = sla::query_hit(sm, bridge);

        REQUIRE(std::isinf(hit.distance()));

        cube.merge(sla::to_triangle_mesh(bridge.mesh));
        cube.require_shared_vertices();
        cube.WriteOBJFile("cube1.obj");
    }

    SECTION("Bridge is tilted down in 45 degrees, pointing away from the cube") {
        sla::Bridge bridge(pts[0].pos.cast<double>(), Vec3d{15., 5., 0.},
                           pts[0].head_front_radius);

        auto hit = sla::query_hit(sm, bridge);

        REQUIRE(std::isinf(hit.distance()));

        cube.merge(sla::to_triangle_mesh(bridge.mesh));
        cube.require_shared_vertices();
        cube.WriteOBJFile("cube2.obj");
    }
}


TEST_CASE("Test bridge_mesh_intersect on a sphere", "[SLABridgeMeshInters]") {
    using namespace Slic3r;

    TriangleMesh sphere = make_sphere(1.);

    sla::SupportConfig cfg = {}; // use default config
    cfg.head_back_radius_mm = cfg.head_front_radius_mm;
    sla::SupportPoints pts = {{1.f, 0.f, 0.f, float(cfg.head_front_radius_mm), false}};
    sla::SupportableMesh sm{sphere, pts, cfg};

    SECTION("Bridge is straight horizontal and pointing away from the sphere") {

        sla::Bridge bridge(pts[0].pos.cast<double>(), Vec3d{2., 0., 0.},
                           pts[0].head_front_radius);

        auto hit = sla::query_hit(sm, bridge);

        sphere.merge(sla::to_triangle_mesh(bridge.mesh));
        sphere.require_shared_vertices();
        sphere.WriteOBJFile("sphere1.obj");

        REQUIRE(std::isinf(hit.distance()));
    }

    SECTION("Bridge is tilted down 45 deg and pointing away from the sphere") {

        sla::Bridge bridge(pts[0].pos.cast<double>(), Vec3d{2., 0., -2.},
                           pts[0].head_front_radius);

        auto hit = sla::query_hit(sm, bridge);

        sphere.merge(sla::to_triangle_mesh(bridge.mesh));
        sphere.require_shared_vertices();
        sphere.WriteOBJFile("sphere2.obj");

        REQUIRE(std::isinf(hit.distance()));
    }

    SECTION("Bridge is tilted down 90 deg and pointing away from the sphere") {

        sla::Bridge bridge(pts[0].pos.cast<double>(), Vec3d{1., 0., -2.},
                           pts[0].head_front_radius);

        auto hit = sla::query_hit(sm, bridge);

        sphere.merge(sla::to_triangle_mesh(bridge.mesh));
        sphere.require_shared_vertices();
        sphere.WriteOBJFile("sphere3.obj");

        REQUIRE(std::isinf(hit.distance()));
    }
}
