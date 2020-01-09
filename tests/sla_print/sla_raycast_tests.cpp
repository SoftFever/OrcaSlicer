#include <catch2/catch.hpp>
#include <test_utils.hpp>

#include <libslic3r/SLA/EigenMesh3D.hpp>
#include <libslic3r/SLA/Hollowing.hpp>

#include "sla_test_utils.hpp"

using namespace Slic3r;

// Create a simple scene with a 20mm cube and a big hole in the front wall 
// with 5mm radius. Then shoot rays from interesting positions and see where
// they land.
TEST_CASE("Raycaster with loaded drillholes", "[sla_raycast]") 
{
    // Load the cube and make it hollow.
    TriangleMesh cube = load_model("20mm_cube.obj");
    sla::HollowingConfig hcfg;
    std::unique_ptr<TriangleMesh> cube_inside = sla::generate_interior(cube, hcfg);
    REQUIRE(cube_inside);
    
    // Helper bb
    auto boxbb = cube.bounding_box();
    
    // Create the big 10mm long drainhole in the front wall.
    Vec3f center = boxbb.center().cast<float>();
    Vec3f p = {center.x(), 0., center.z()};
    Vec3f normal = {0.f, 1.f, 0.f};
    float radius = 5.f;
    float hole_length = 10.;
    sla::DrainHoles holes = { sla::DrainHole{p, normal, radius, hole_length} };
    
    cube.merge(*cube_inside);
    cube.require_shared_vertices();
    
    sla::EigenMesh3D emesh{cube};
    emesh.load_holes(holes);
    
    Vec3d s = center.cast<double>();
    SECTION("Fire from center, should hit the interior wall") {
        auto hit = emesh.query_ray_hit(s, {0, 1., 0.});
        REQUIRE(hit.distance() == Approx(boxbb.size().x() / 2 - hcfg.min_thickness));
    }
    
    SECTION("Fire upward from hole center, hit distance equals the radius") {
        s.y() = hcfg.min_thickness / 2;
        auto hit = emesh.query_ray_hit(s, {0, 0., 1.});
        REQUIRE(hit.distance() == Approx(radius));
    }
    
    SECTION("Fire from outside, hit the back side of the hole cylinder.") {
        s.y() = -1.;
        auto hit = emesh.query_ray_hit(s, {0, 1., 0.});
        REQUIRE(hit.distance() == Approx(boxbb.size().y() - hcfg.min_thickness + 1.));
    }
    
    SECTION("Check for support tree correctness") {
        test_support_model_collision("20mm_cube.obj", {}, hcfg, holes);
    }
}
