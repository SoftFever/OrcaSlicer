#include <catch2/catch_all.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/CutObject.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

using namespace Slic3r;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

TriangleMesh make_test_cube()
{
    TriangleMesh mesh = make_cube(40.0, 40.0, 40.0);
    mesh.translate(Vec3f(-20.f, -20.f, -20.f));
    return mesh;
}

double measure_hole_depth(const indexed_triangle_set &its, bool upper_part)
{
    double max_depth = 0.0;
    for (const Vec3f &v : its.vertices) {
        const double r2 = v.x() * v.x() + v.y() * v.y();
        if (r2 <= 25.0) {
            if (upper_part && v.z() > 0.0f)
                max_depth = std::max(max_depth, static_cast<double>(v.z()));
            else if (!upper_part && v.z() < 0.0f)
                max_depth = std::max(max_depth, static_cast<double>(-v.z()));
        }
    }
    return max_depth;
}

double measure_protrusion_height(const indexed_triangle_set &its, bool upper_part)
{
    double max_protrusion = 0.0;
    for (const Vec3f &v : its.vertices) {
        const double r2 = v.x() * v.x() + v.y() * v.y();
        if (r2 <= 25.0) {
            if (upper_part && v.z() < 0.0f)
                max_protrusion = std::max(max_protrusion, static_cast<double>(-v.z()));
            else if (!upper_part && v.z() > 0.0f)
                max_protrusion = std::max(max_protrusion, static_cast<double>(v.z()));
        }
    }
    return max_protrusion;
}

CutConnector make_connector(CutConnectorType type, double depth, double depth_tolerance, double radius = 3.0)
{
    CutConnector c;
    c.type            = type;
    c.shape           = CutConnectorShape::Circle;
    c.pos             = Vec3d(0.0, 0.0, 0.0);
    c.radius          = radius;
    c.depth           = depth;
    c.depth_tolerance = depth_tolerance;
    return c;
}

}

TEST_CASE("Dowel connector cuts matching cavities in both parts with depth tolerance", "[CutConnectors]")
{
    const double nominal_depth = 10.0;
    const double tolerance     = 0.2;
    const Vec3d  cut_plane_pt(0.0, 0.0, 0.0);
    const Vec3d  cut_plane_normal(0.0, 0.0, 1.0);

    SECTION("nominal depth 10 mm with tolerance 0.2 mm expands both holes to 10.2 mm")
    {
        CutConnector dowel = make_connector(CutConnectorType::Dowel, nominal_depth, tolerance);

        CutObjectResult result = cut_mesh_with_connectors(make_test_cube(), cut_plane_pt, cut_plane_normal, { dowel });
        REQUIRE(result.upper_part.has_value());
        REQUIRE(result.lower_part.has_value());

        const double upper_depth = measure_hole_depth(result.upper_part->its, true);
        CHECK_THAT(upper_depth, WithinAbs(nominal_depth + tolerance, 1e-3));

        const double lower_depth = measure_hole_depth(result.lower_part->its, false);
        CHECK_THAT(lower_depth, WithinAbs(nominal_depth + tolerance, 1e-3));

        REQUIRE(result.connectors.size() == 1);
        const BoundingBox dowel_bb = result.connectors.front().bounding_box();
        CHECK_THAT(dowel_bb.size().z(), WithinAbs(2.0 * nominal_depth, 1e-3));
    }

    SECTION("zero depth tolerance keeps cavity depths strictly at nominal 10.0 mm")
    {
        CutConnector dowel_zero = make_connector(CutConnectorType::Dowel, nominal_depth, 0.0);

        CutObjectResult result = cut_mesh_with_connectors(make_test_cube(), cut_plane_pt, cut_plane_normal, { dowel_zero });
        REQUIRE(result.upper_part.has_value());
        REQUIRE(result.lower_part.has_value());

        const double upper_depth = measure_hole_depth(result.upper_part->its, true);
        const double lower_depth = measure_hole_depth(result.lower_part->its, false);

        CHECK_THAT(upper_depth, WithinAbs(nominal_depth, 1e-3));
        CHECK_THAT(lower_depth, WithinAbs(nominal_depth, 1e-3));
    }
}

TEST_CASE("Plug connector scales female hole depth by tolerance while male stays nominal", "[CutConnectors]")
{
    const double nominal_depth = 10.0;
    const double tolerance     = 0.2;
    const Vec3d  cut_plane_pt(0.0, 0.0, 0.0);
    const Vec3d  cut_plane_normal(0.0, 0.0, 1.0);

    SECTION("female cavity receives tolerance (10.2 mm) and male plug remains nominal (10.0 mm)")
    {
        CutConnector plug = make_connector(CutConnectorType::Plug, nominal_depth, tolerance);

        CutObjectResult result = cut_mesh_with_connectors(make_test_cube(), cut_plane_pt, cut_plane_normal, { plug });
        REQUIRE(result.upper_part.has_value());
        REQUIRE(result.lower_part.has_value());

        const double female_depth = measure_hole_depth(result.upper_part->its, true);
        CHECK_THAT(female_depth, WithinAbs(nominal_depth + tolerance, 1e-3));

        const double male_protrusion = measure_protrusion_height(result.lower_part->its, false);
        CHECK_THAT(male_protrusion, WithinAbs(nominal_depth, 1e-3));
    }

    SECTION("zero depth tolerance leaves female hole depth identical to male protrusion")
    {
        CutConnector plug_zero = make_connector(CutConnectorType::Plug, nominal_depth, 0.0);

        CutObjectResult result = cut_mesh_with_connectors(make_test_cube(), cut_plane_pt, cut_plane_normal, { plug_zero });
        REQUIRE(result.upper_part.has_value());
        REQUIRE(result.lower_part.has_value());

        const double female_depth    = measure_hole_depth(result.upper_part->its, true);
        const double male_protrusion = measure_protrusion_height(result.lower_part->its, false);

        CHECK_THAT(female_depth, WithinAbs(nominal_depth, 1e-3));
        CHECK_THAT(male_protrusion, WithinAbs(nominal_depth, 1e-3));
    }
}

TEST_CASE("Snap connector preserves clearance depth in the socket cavity", "[CutConnectors]")
{
    const double nominal_depth = 10.0;
    const double tolerance     = 0.2;
    const Vec3d  cut_plane_pt(0.0, 0.0, 0.0);
    const Vec3d  cut_plane_normal(0.0, 0.0, 1.0);

    SECTION("snap socket cavity expands to 10.2 mm with 0.2 mm tolerance")
    {
        CutConnector snap = make_connector(CutConnectorType::Snap, nominal_depth, tolerance);

        CutObjectResult result = cut_mesh_with_connectors(make_test_cube(), cut_plane_pt, cut_plane_normal, { snap });
        REQUIRE(result.upper_part.has_value());
        REQUIRE(result.lower_part.has_value());

        const double socket_depth = measure_hole_depth(result.upper_part->its, true);
        const double snap_height  = measure_protrusion_height(result.lower_part->its, false);

        CHECK_THAT(socket_depth, WithinAbs(nominal_depth + tolerance, 1e-3));
        CHECK_THAT(snap_height, WithinAbs(nominal_depth, 1e-3));
    }
}

TEST_CASE("Depth tolerance scales hole depth linearly across varying real-world offsets", "[CutConnectors]")
{
    const double nominal_depth = 10.0;
    const auto [tolerance] = GENERATE(table<double>({
        { 0.0 },
        { 0.1 },
        { 0.2 },
        { 0.35 },
        { 0.5 }
    }));

    DYNAMIC_SECTION("tolerance " << tolerance << " mm yields depth " << (nominal_depth + tolerance) << " mm")
    {
        CutConnector dowel = make_connector(CutConnectorType::Dowel, nominal_depth, tolerance);
        CutObjectResult result = cut_mesh_with_connectors(make_test_cube(), Vec3d::Zero(), Vec3d(0.0, 0.0, 1.0), { dowel });

        REQUIRE(result.upper_part.has_value());
        REQUIRE(result.lower_part.has_value());

        const double depth_a = measure_hole_depth(result.upper_part->its, true);
        const double depth_b = measure_hole_depth(result.lower_part->its, false);

        CHECK_THAT(depth_a, WithinAbs(nominal_depth + tolerance, 1e-3));
        CHECK_THAT(depth_b, WithinAbs(nominal_depth + tolerance, 1e-3));
    }
}

TEST_CASE("Issue #14512 regression: Connector hole is preserved when cut face rests on the bed", "[CutConnectors][Regression]")
{
    const double nominal_depth = 10.0;
    const double tolerance     = 0.2;
    const double expected_depth = nominal_depth + tolerance;

    CutConnector dowel = make_connector(CutConnectorType::Dowel, nominal_depth, tolerance);
    CutObjectResult result = cut_mesh_with_connectors(make_test_cube(), Vec3d::Zero(), Vec3d(0.0, 0.0, 1.0), { dowel });

    REQUIRE(result.upper_part.has_value());
    TriangleMesh cut_part = *result.upper_part;

    size_t bed_hole_vertices = 0;
    for (const Vec3f &v : cut_part.its.vertices) {
        if (std::abs(v.z()) < 1e-4f) {
            const double r2 = v.x() * v.x() + v.y() * v.y();
            if (r2 > 1.0 && r2 < 25.0)
                bed_hole_vertices++;
        }
    }
    CHECK(bed_hole_vertices >= 8);

    const double cavity_depth = measure_hole_depth(cut_part.its, true);
    CHECK_THAT(cavity_depth, WithinAbs(expected_depth, 1e-3));

    CHECK(its_num_open_edges(cut_part.its) == 0);
}