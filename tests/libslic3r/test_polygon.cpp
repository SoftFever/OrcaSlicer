#include <catch2/catch.hpp>

#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"

using namespace Slic3r;

// This test currently only covers remove_collinear_points.
// All remaining tests are to be ported from xs/t/06_polygon.t

Slic3r::Points collinear_circle({
    Slic3r::Point::new_scale(0, 0), // 3 collinear points at beginning
    Slic3r::Point::new_scale(10, 0),
    Slic3r::Point::new_scale(20, 0),
    Slic3r::Point::new_scale(30, 10),
    Slic3r::Point::new_scale(40, 20), // 2 collinear points
    Slic3r::Point::new_scale(40, 30),
    Slic3r::Point::new_scale(30, 40), // 3 collinear points
    Slic3r::Point::new_scale(20, 40),
    Slic3r::Point::new_scale(10, 40),
    Slic3r::Point::new_scale(-10, 20),
    Slic3r::Point::new_scale(-20, 10),
    Slic3r::Point::new_scale(-20, 0), // 3 collinear points at end
    Slic3r::Point::new_scale(-10, 0),
    Slic3r::Point::new_scale(-5, 0)
});

SCENARIO("Remove collinear points from Polygon") {
    GIVEN("Polygon with collinear points"){
        Slic3r::Polygon p(collinear_circle);
        WHEN("collinear points are removed") {
            remove_collinear(p);
            THEN("Leading collinear points are removed") {
                REQUIRE(p.points.front() == Slic3r::Point::new_scale(20, 0));
            }
            THEN("Trailing collinear points are removed") {
                REQUIRE(p.points.back() == Slic3r::Point::new_scale(-20, 0));
            }
            THEN("Number of remaining points is correct") {
                REQUIRE(p.points.size() == 7);
            }
        }
    }
}
