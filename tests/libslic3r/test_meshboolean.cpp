#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/MeshBoolean.hpp>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r;
using Catch::Matchers::WithinRel;

TEST_CASE("CGAL and TriangleMesh conversions", "[MeshBoolean]") {
    TriangleMesh sphere = make_sphere(1.);
    
    auto cgalmesh_ptr = MeshBoolean::cgal::triangle_mesh_to_cgal(sphere);
    
    REQUIRE(cgalmesh_ptr);
    REQUIRE(! MeshBoolean::cgal::does_self_intersect(*cgalmesh_ptr));
    
    TriangleMesh M = MeshBoolean::cgal::cgal_to_triangle_mesh(*cgalmesh_ptr);
    
    REQUIRE(M.its.vertices.size() == sphere.its.vertices.size());
    REQUIRE(M.its.indices.size() == sphere.its.indices.size());
    
    REQUIRE(M.volume() == Catch::Approx(sphere.volume()));
    
    REQUIRE(! MeshBoolean::cgal::does_self_intersect(M));
}

TEST_CASE("mcut difference handles source splits between cuts", "[MeshBoolean]") {
    TriangleMesh body = make_cube(30., 10., 10.);

    TriangleMesh tool;

    // First cut splits the source into two disconnected components.
    TriangleMesh slab = make_cube(2., 12., 20.);
    slab.translate(Vec3f(14.f, -1.f, -5.f));
    its_merge(tool.its, slab.its);

    // These cuts must still be applied after the source has been split.
    TriangleMesh left_hole = make_cube(4., 4., 20.);
    left_hole.translate(Vec3f(3.f, 3.f, -5.f));
    its_merge(tool.its, left_hole.its);

    TriangleMesh right_hole = make_cube(4., 4., 20.);
    right_hole.translate(Vec3f(21.f, 3.f, -5.f));
    its_merge(tool.its, right_hole.its);

    std::vector<TriangleMesh> result;
    MeshBoolean::mcut::make_boolean(body, tool, result, "A_NOT_B");

    REQUIRE(result.size() == 1);

    const std::vector<indexed_triangle_set> components =
        its_split(result.front().its);

    REQUIRE(components.size() == 2);

    // 3000 - 200 - 160 - 160 = 2480.
    REQUIRE_THAT(
        result.front().volume(),
        Catch::Matchers::WithinRel(2480., 1e-3)
    );
}

namespace {

indexed_triangle_set cube_at(float size, const Vec3f &origin)
{
    indexed_triangle_set its = its_make_cube(size, size, size);
    for (Vec3f &v : its.vertices)
        v += origin;
    return its;
}

indexed_triangle_set merged(indexed_triangle_set a, const indexed_triangle_set &b)
{
    its_merge(a, b);
    return a;
}

void flip(stl_triangle_vertex_indices &face) { std::swap(face[1], face[2]); }

// Drops the faces whose vertices all satisfy the predicate.
template<typename Pred> indexed_triangle_set without_faces(indexed_triangle_set its, Pred pred)
{
    std::vector<stl_triangle_vertex_indices> kept;
    for (const auto &f : its.indices)
        if (!(pred(its.vertices[f[0]]) && pred(its.vertices[f[1]]) && pred(its.vertices[f[2]])))
            kept.push_back(f);
    its.indices = std::move(kept);
    return its;
}

// Every face gets its own three vertices, as in a binary STL before vertex merging.
indexed_triangle_set unwelded(const indexed_triangle_set &its)
{
    indexed_triangle_set out;
    for (const auto &f : its.indices) {
        const int base = int(out.vertices.size());
        for (int i = 0; i < 3; ++i)
            out.vertices.push_back(its.vertices[f[i]]);
        out.indices.emplace_back(base, base + 1, base + 2);
    }
    return out;
}

// Copies of the same corner differ by sub-micron noise, as in the seams of meshes converted from STEP.
indexed_triangle_set with_noisy_seams(const indexed_triangle_set &its)
{
    indexed_triangle_set out = unwelded(its);
    for (size_t i = 0; i < out.vertices.size(); ++i)
        for (int axis = 0; axis < 3; ++axis) {
            float      &c     = out.vertices[i](axis);
            const float noise = c == 0.f ? 1e-12f : 1e-6f * c;
            c += noise * float(int(i % 3) - 1);
        }
    return out;
}

// Repairs the mesh and checks the result is closed and encloses a positive volume.
TriangleMesh repaired_closed(const indexed_triangle_set &its)
{
    TriangleMesh mesh(its);
    std::string  error;
    const bool   ok = MeshBoolean::cgal::repair(mesh, nullptr, &error);
    INFO("repair error: " << error);
    REQUIRE(ok);
    REQUIRE(its_num_open_edges(mesh.its) == 0);
    REQUIRE(its_volume(mesh.its) > 0.f);
    return mesh;
}

// As above, and also free of self-intersections.
TriangleMesh repaired_solid(const indexed_triangle_set &its)
{
    TriangleMesh mesh = repaired_closed(its);
    REQUIRE_FALSE(MeshBoolean::cgal::does_self_intersect(mesh));
    return mesh;
}

} // namespace

// "Fix model" in the GUI runs this repair on every broken part.
TEST_CASE("CGAL repair leaves an empty mesh empty", "[MeshBoolean]") {
    TriangleMesh mesh;
    REQUIRE(MeshBoolean::cgal::repair(mesh));
    REQUIRE(mesh.empty());
}

TEST_CASE("CGAL repair keeps a closed solid unchanged", "[MeshBoolean]") {
    TriangleMesh mesh = repaired_solid(cube_at(10.f, Vec3f::Zero()));
    REQUIRE_THAT(mesh.volume(), WithinRel(1000., 1e-6));
    REQUIRE(mesh.bounding_box().min.isApprox(Vec3d::Zero()));
    REQUIRE(mesh.bounding_box().max.isApprox(Vec3d(10., 10., 10.)));
}

TEST_CASE("CGAL repair keeps the cavity of a hollow solid", "[MeshBoolean]") {
    indexed_triangle_set cavity = cube_at(5.f, Vec3f(2.5f, 2.5f, 2.5f));
    for (auto &f : cavity.indices)
        flip(f);
    indexed_triangle_set outer = cube_at(10.f, Vec3f::Zero());
    outer.indices.pop_back(); // and a broken outer wall, so there is something to repair
    TriangleMesh mesh = repaired_solid(merged(outer, cavity));
    REQUIRE_THAT(mesh.volume(), WithinRel(1000. - 125., 0.001));
}

TEST_CASE("CGAL repair closes a hole left by a missing face", "[MeshBoolean]") {
    indexed_triangle_set its = cube_at(10.f, Vec3f::Zero());
    its.indices.resize(its.indices.size() - 2);
    REQUIRE(its_num_open_edges(its) > 0);
    REQUIRE_THAT(repaired_solid(its).volume(), WithinRel(1000., 0.001));
}

TEST_CASE("CGAL repair closes several separate holes", "[MeshBoolean]") {
    // Two opposite sides missing: a square tube with two holes.
    const indexed_triangle_set its = without_faces(cube_at(10.f, Vec3f::Zero()), [](const Vec3f &v) { return v.z() == 0.f; });
    const indexed_triangle_set tube = without_faces(its, [](const Vec3f &v) { return v.z() == 10.f; });
    REQUIRE_THAT(repaired_solid(tube).volume(), WithinRel(1000., 0.001));
}

TEST_CASE("CGAL repair closes a large hole in a curved surface", "[MeshBoolean]") {
    const indexed_triangle_set sphere   = its_make_sphere(5., PI / 30.);
    const double               expected = its_volume(sphere);
    const indexed_triangle_set capless  = without_faces(sphere, [](const Vec3f &v) { return v.z() > 4.f; });
    REQUIRE(its_num_open_edges(capless) > 0);
    // The hole is bounded by a ring of latitude. The patch spans the ring and does not rebuild the cap above it, so
    // the result loses at most the volume of the spherical cap above the ring.
    float ring_z = -5.f;
    for (const auto &f : capless.indices)
        for (int i = 0; i < 3; ++i)
            ring_z = std::max(ring_z, capless.vertices[f[i]].z());
    const double h   = 5. - ring_z;
    const double cap = PI * h * h * (3. * 5. - h) / 3.;
    const double volume = repaired_solid(capless).volume();
    REQUIRE(volume <= expected * 1.001);
    REQUIRE(volume >= expected - cap);
}

TEST_CASE("CGAL repair closes many small holes", "[MeshBoolean]") {
    indexed_triangle_set sphere = its_make_sphere(5., PI / 30.);
    const double expected = its_volume(sphere);
    std::vector<stl_triangle_vertex_indices> kept;
    for (size_t i = 0; i < sphere.indices.size(); ++i)
        if (i % 11 != 0)
            kept.push_back(sphere.indices[i]);
    sphere.indices = std::move(kept);
    REQUIRE_THAT(repaired_solid(sphere).volume(), WithinRel(expected, 0.01));
}

TEST_CASE("CGAL repair orients an inside-out open mesh outwards", "[MeshBoolean]") {
    indexed_triangle_set its = cube_at(10.f, Vec3f::Zero());
    its.indices.pop_back();
    for (auto &f : its.indices)
        flip(f);
    REQUIRE_THAT(repaired_solid(its).volume(), WithinRel(1000., 0.001));
}

TEST_CASE("CGAL repair fixes a single inverted face", "[MeshBoolean]") {
    indexed_triangle_set its = cube_at(10.f, Vec3f::Zero());
    flip(its.indices.front());
    REQUIRE_THAT(repaired_solid(its).volume(), WithinRel(1000., 0.001));
}

TEST_CASE("CGAL repair welds an unindexed triangle soup", "[MeshBoolean]") {
    const indexed_triangle_set soup = unwelded(cube_at(10.f, Vec3f::Zero()));
    REQUIRE(its_num_open_edges(soup) == soup.indices.size() * 3);
    REQUIRE_THAT(repaired_solid(soup).volume(), WithinRel(1000., 0.001));
}

TEST_CASE("CGAL repair welds seams split by sub-micron coordinate noise", "[MeshBoolean]") {
    const indexed_triangle_set soup = with_noisy_seams(cube_at(10.f, Vec3f::Zero()));
    REQUIRE(its_num_open_edges(soup) == soup.indices.size() * 3);
    TriangleMesh mesh = repaired_solid(soup);
    REQUIRE_THAT(mesh.volume(), WithinRel(1000., 0.001));
    // Welding closes it; filling the slits would add faces.
    REQUIRE(mesh.its.indices.size() == 12);
}

TEST_CASE("CGAL repair removes duplicated faces", "[MeshBoolean]") {
    indexed_triangle_set its = cube_at(10.f, Vec3f::Zero());
    its.indices.push_back(its.indices.front());
    REQUIRE_THAT(repaired_solid(its).volume(), WithinRel(1000., 0.001));
}

TEST_CASE("CGAL repair removes degenerate faces", "[MeshBoolean]") {
    indexed_triangle_set its = cube_at(10.f, Vec3f::Zero());
    its.indices.pop_back();
    // A zero-area sliver on the open border.
    its.indices.emplace_back(its.indices.back()[0], its.indices.back()[1], its.indices.back()[1]);
    REQUIRE_THAT(repaired_solid(its).volume(), WithinRel(1000., 0.001));
}

TEST_CASE("CGAL repair closes solids touching at a single vertex", "[MeshBoolean]") {
    indexed_triangle_set its = merged(cube_at(10.f, Vec3f::Zero()), cube_at(10.f, Vec3f(10.f, 10.f, 10.f)));
    its_merge_vertices(its);
    its.indices.pop_back();
    REQUIRE_THAT(repaired_closed(its).volume(), WithinRel(2000., 0.001));
}

TEST_CASE("CGAL repair closes solids sharing an edge", "[MeshBoolean]") {
    indexed_triangle_set its = merged(cube_at(10.f, Vec3f::Zero()), cube_at(10.f, Vec3f(10.f, 10.f, 0.f)));
    its_merge_vertices(its);
    its.indices.pop_back();
    REQUIRE_THAT(repaired_closed(its).volume(), WithinRel(2000., 0.001));
}

TEST_CASE("CGAL repair merges overlapping closed shells", "[MeshBoolean]") {
    // Overlap is 5 x 5 x 10.
    const indexed_triangle_set its = merged(cube_at(10.f, Vec3f::Zero()), cube_at(10.f, Vec3f(5.f, 5.f, 0.f)));
    REQUIRE(MeshBoolean::cgal::does_self_intersect(TriangleMesh(its)));
    REQUIRE_THAT(repaired_solid(its).volume(), WithinRel(2000. - 250., 0.001));
}

TEST_CASE("CGAL repair merges overlapping shells when one of them is open", "[MeshBoolean]") {
    indexed_triangle_set open = cube_at(10.f, Vec3f(5.f, 5.f, 0.f));
    open.indices.pop_back();
    const indexed_triangle_set its = merged(cube_at(10.f, Vec3f::Zero()), open);
    REQUIRE_THAT(repaired_solid(its).volume(), WithinRel(2000. - 250., 0.001));
}

TEST_CASE("CGAL repair keeps the cavity of a hollow solid while merging an overlapping shell", "[MeshBoolean]") {
    indexed_triangle_set cavity = cube_at(5.f, Vec3f(2.5f, 2.5f, 2.5f));
    for (auto &f : cavity.indices)
        flip(f);
    // Overlaps the outer wall by 2 x 10 x 10 and stays clear of the cavity.
    indexed_triangle_set overlapping = cube_at(10.f, Vec3f(8.f, 0.f, 0.f));
    overlapping.indices.pop_back();
    const indexed_triangle_set its = merged(merged(cube_at(10.f, Vec3f::Zero()), cavity), overlapping);
    REQUIRE_THAT(repaired_solid(its).volume(), WithinRel(2000. - 200. - 125., 0.001));
}

TEST_CASE("CGAL repair keeps a solid inside the cavity of a hollow solid while merging an overlapping shell", "[MeshBoolean]") {
    indexed_triangle_set cavity = cube_at(10.f, Vec3f(5.f, 5.f, 5.f));
    for (auto &f : cavity.indices)
        flip(f);
    const indexed_triangle_set island = cube_at(4.f, Vec3f(8.f, 8.f, 8.f));
    // Overlaps the outer wall by 4 x 20 x 20 and stays clear of the cavity.
    indexed_triangle_set overlapping = cube_at(20.f, Vec3f(16.f, 0.f, 0.f));
    overlapping.indices.pop_back();
    const indexed_triangle_set its = merged(merged(merged(cube_at(20.f, Vec3f::Zero()), cavity), island), overlapping);
    REQUIRE_THAT(repaired_solid(its).volume(), WithinRel(8000. + 8000. - 1600. - 1000. + 64., 0.001));
}

TEST_CASE("CGAL repair keeps a cavity inside overlapping solids and a solid inside that cavity", "[MeshBoolean]") {
    // The solids overlap by 10 x 20 x 20; the cavity lies inside both, so two shells contain it.
    indexed_triangle_set cavity = cube_at(6.f, Vec3f(12.f, 7.f, 7.f));
    for (auto &f : cavity.indices)
        flip(f);
    const indexed_triangle_set island = cube_at(2.f, Vec3f(14.f, 9.f, 9.f));
    const indexed_triangle_set its    = merged(merged(merged(cube_at(20.f, Vec3f::Zero()), cube_at(20.f, Vec3f(10.f, 0.f, 0.f))), cavity),
                                               island);
    REQUIRE_THAT(repaired_solid(its).volume(), WithinRel(8000. + 8000. - 4000. - 216. + 8., 0.001));
}

TEST_CASE("CGAL repair reports the self-intersections it cannot remove", "[MeshBoolean]") {
    // One closed shell: the top corner pushed through the bottom face.
    indexed_triangle_set its = cube_at(10.f, Vec3f::Zero());
    its.vertices[4] = Vec3f(3.f, 3.f, -5.f);
    REQUIRE(its_num_open_edges(its) == 0);
    REQUIRE(MeshBoolean::cgal::does_self_intersect(TriangleMesh(its)));
    TriangleMesh mesh(its);
    std::string  error;
    bool         self_intersections_remain = false;
    REQUIRE(MeshBoolean::cgal::repair(mesh, nullptr, &error, &self_intersections_remain));
    REQUIRE(self_intersections_remain);
    REQUIRE(MeshBoolean::cgal::does_self_intersect(mesh));
}

TEST_CASE("CGAL repair reports no self-intersections after resolving them", "[MeshBoolean]") {
    TriangleMesh mesh(merged(cube_at(10.f, Vec3f::Zero()), cube_at(10.f, Vec3f(5.f, 5.f, 0.f))));
    bool         self_intersections_remain = true;
    REQUIRE(MeshBoolean::cgal::repair(mesh, nullptr, nullptr, &self_intersections_remain));
    REQUIRE_FALSE(self_intersections_remain);
}

TEST_CASE("CGAL repair closes a hole in a sub-millimetre part", "[MeshBoolean]") {
    indexed_triangle_set its = cube_at(0.2f, Vec3f::Zero());
    its.indices.pop_back();
    REQUIRE_THAT(repaired_solid(its).volume(), WithinRel(0.008, 0.001));
}

TEST_CASE("CGAL repair closes a hole in a part too far from the origin to weld", "[MeshBoolean]") {
    // Powers of two, exact in float. The welding grid would need cells beyond int64_t this far out, so welding is skipped.
    const float size = 2147483648.f;     // 2^31
    const float pos  = 18014398509481984.f; // 2^54
    indexed_triangle_set its = cube_at(size, Vec3f(pos, pos, pos));
    its.indices.pop_back();
    REQUIRE_THAT(repaired_solid(its).volume(), WithinRel(double(size) * size * size, 0.001));
}

TEST_CASE("CGAL repair closes a hole in a part far from the origin", "[MeshBoolean]") {
    indexed_triangle_set its = cube_at(10.f, Vec3f(1000.f, -1000.f, 500.f));
    its.indices.pop_back();
    TriangleMesh mesh = repaired_solid(its);
    REQUIRE_THAT(mesh.volume(), WithinRel(1000., 0.001));
    REQUIRE(mesh.bounding_box().min.isApprox(Vec3d(1000., -1000., 500.)));
}

TEST_CASE("CGAL repair reports failure for a surface that encloses no volume", "[MeshBoolean]") {
    indexed_triangle_set sheet;
    sheet.vertices = {{0.f, 0.f, 0.f}, {10.f, 0.f, 0.f}, {10.f, 10.f, 0.f}, {0.f, 10.f, 0.f}};
    sheet.indices  = {{0, 1, 2}, {0, 2, 3}};
    TriangleMesh mesh(sheet);
    std::string  error;
    REQUIRE_FALSE(MeshBoolean::cgal::repair(mesh, nullptr, &error));
    REQUIRE_FALSE(error.empty());
}
