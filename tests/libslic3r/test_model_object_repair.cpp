#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <libslic3r/BoundingBox.hpp>
#include <libslic3r/Exception.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/ModelObjectRepair.hpp>
#include <libslic3r/ObjectID.hpp>
#include <libslic3r/Point.hpp>
#include <libslic3r/TriangleMesh.hpp>

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r;
using Catch::Matchers::WithinRel;

namespace {

indexed_triangle_set cube_at(float size, const Vec3f &origin)
{
    indexed_triangle_set its = its_make_cube(size, size, size);
    for (Vec3f &v : its.vertices)
        v += origin;
    return its;
}

indexed_triangle_set open_cube_at(float size, const Vec3f &origin)
{
    indexed_triangle_set its = cube_at(size, origin);
    its.indices.pop_back();
    return its;
}

indexed_triangle_set sheet_at(const Vec3f &origin)
{
    indexed_triangle_set its;
    its.vertices = {origin, origin + Vec3f(10.f, 0.f, 0.f), origin + Vec3f(10.f, 10.f, 0.f), origin + Vec3f(0.f, 10.f, 0.f)};
    its.indices  = {{0, 1, 2}, {0, 2, 3}};
    return its;
}

indexed_triangle_set inverted(indexed_triangle_set its)
{
    for (auto &f : its.indices)
        std::swap(f[1], f[2]);
    return its;
}

// One closed shell that intersects itself: the top corner pushed through the bottom face; open when a side face is missing.
indexed_triangle_set pierced_cube(bool open)
{
    indexed_triangle_set its = cube_at(10.f, Vec3f::Zero());
    its.vertices[4]          = Vec3f(3.f, 3.f, -5.f);
    if (open)
        its.indices.erase(its.indices.begin() + 7);
    return its;
}

indexed_triangle_set merged(indexed_triangle_set a, const indexed_triangle_set &b)
{
    its_merge(a, b);
    return a;
}

ModelObject &object_with(Model &model, std::initializer_list<indexed_triangle_set> volumes)
{
    ModelObject *object = model.add_object();
    for (const indexed_triangle_set &its : volumes)
        object->add_volume(TriangleMesh(its));
    object->add_instance();
    return *object;
}

ModelObjectRepairReport repair(ModelObject &object, int volume_idx = -1, bool keep_painting = false)
{
    return repair_model_object(object, volume_idx, keep_painting, [](const std::string &, int) {}, [] {});
}

size_t open_edges(const ModelVolume &volume) { return its_num_open_edges(volume.mesh().its); }

double total_volume(const ModelObject &object)
{
    double sum = 0.;
    for (const ModelVolume *v : object.volumes)
        sum += its_volume(v->mesh().its);
    return sum;
}

BoundingBoxf3 world_bounding_box(const ModelObject &object)
{
    BoundingBoxf3  bbox;
    const Transform3d instance = object.instances.front()->get_matrix();
    for (const ModelVolume *v : object.volumes)
        bbox.merge(v->mesh().transformed_bounding_box(instance * v->get_matrix()));
    return bbox;
}

} // namespace

TEST_CASE("Repairing a whole object closes every broken volume", "[ModelObjectRepair]") {
    Model        model;
    ModelObject &object = object_with(model, {open_cube_at(10.f, Vec3f::Zero()), open_cube_at(10.f, Vec3f(20.f, 0.f, 0.f))});
    repair(object);
    REQUIRE(object.volumes.size() == 2);
    REQUIRE(open_edges(*object.volumes[0]) == 0);
    REQUIRE(open_edges(*object.volumes[1]) == 0);
    REQUIRE_THAT(total_volume(object), WithinRel(2000., 0.001));
}

TEST_CASE("Repairing one volume leaves the other volumes untouched", "[ModelObjectRepair]") {
    Model        model;
    ModelObject &object   = object_with(model, {open_cube_at(10.f, Vec3f::Zero()), open_cube_at(10.f, Vec3f(20.f, 0.f, 0.f))});
    const auto   untouched = object.volumes[1]->get_mesh_shared_ptr();
    repair(object, 0);
    REQUIRE(open_edges(*object.volumes[0]) == 0);
    REQUIRE(object.volumes[1]->get_mesh_shared_ptr() == untouched);
}

TEST_CASE("Repairing a split volume leaves the volumes after it untouched", "[ModelObjectRepair]") {
    Model        model;
    ModelObject &object = object_with(model, {merged(open_cube_at(10.f, Vec3f::Zero()), open_cube_at(10.f, Vec3f(20.f, 0.f, 0.f))),
                                              open_cube_at(10.f, Vec3f(40.f, 0.f, 0.f))});
    const auto   untouched = object.volumes[1]->get_mesh_shared_ptr();
    repair(object, 0);
    REQUIRE(object.volumes.back()->get_mesh_shared_ptr() == untouched);
    for (size_t i = 0; i + 1 < object.volumes.size(); ++i)
        REQUIRE(open_edges(*object.volumes[i]) == 0);
}

TEST_CASE("Repairing a volume made of separate shells repairs each shell", "[ModelObjectRepair]") {
    Model        model;
    ModelObject &object = object_with(model, {merged(cube_at(10.f, Vec3f::Zero()), open_cube_at(10.f, Vec3f(20.f, 0.f, 0.f)))});
    repair(object);
    for (const ModelVolume *v : object.volumes)
        REQUIRE(open_edges(*v) == 0);
    REQUIRE_THAT(total_volume(object), WithinRel(2000., 0.001));
}

TEST_CASE("Repairing keeps the object in place", "[ModelObjectRepair]") {
    Model        model;
    ModelObject &object = object_with(model, {merged(open_cube_at(10.f, Vec3f::Zero()), open_cube_at(10.f, Vec3f(20.f, 0.f, 0.f))),
                                              cube_at(10.f, Vec3f(0.f, 20.f, 0.f))});
    object.volumes[0]->set_offset(Vec3d(3., -7., 11.));
    object.instances.front()->set_offset(Vec3d(100., 50., 0.));
    const BoundingBoxf3 before = world_bounding_box(object);
    repair(object);
    const BoundingBoxf3 after = world_bounding_box(object);
    REQUIRE(after.min.isApprox(before.min, 1e-5));
    REQUIRE(after.max.isApprox(before.max, 1e-5));
}

TEST_CASE("Repairing drops parts that enclose no volume", "[ModelObjectRepair]") {
    Model        model;
    ModelObject &object = object_with(model, {merged(open_cube_at(10.f, Vec3f::Zero()), sheet_at(Vec3f(20.f, 0.f, 0.f)))});
    repair(object);
    REQUIRE(object.volumes.size() == 1);
    REQUIRE(open_edges(*object.volumes.front()) == 0);
    REQUIRE_THAT(total_volume(object), WithinRel(1000., 0.001));
}

TEST_CASE("Repairing a selected volume repairs all its shells when splitting drops a flat volume before it", "[ModelObjectRepair]") {
    // ModelVolume::split() also deletes the other volumes of the object whose convex hull is degenerate, as a sheet's is.
    Model        model;
    ModelObject &object = object_with(model, {sheet_at(Vec3f(0.f, 0.f, 50.f)),
                                              merged(open_cube_at(10.f, Vec3f::Zero()), open_cube_at(10.f, Vec3f(20.f, 0.f, 0.f)))});
    repair(object, 1);
    size_t solids = 0;
    for (const ModelVolume *v : object.volumes)
        if (its_volume(v->mesh().its) > 1.f) {
            ++solids;
            REQUIRE(open_edges(*v) == 0);
        }
    REQUIRE(solids == 2);
}

TEST_CASE("Repairing keeps a clean volume whose tiny mesh is scaled up", "[ModelObjectRepair]") {
    // The mesh of the second volume encloses 1e-6 mm³, but scaled by 1000 it is a 10 mm cube.
    Model        model;
    ModelObject &object = object_with(model, {open_cube_at(10.f, Vec3f::Zero()), cube_at(0.01f, Vec3f::Zero())});
    object.volumes[1]->set_scaling_factor(Vec3d(1000., 1000., 1000.));
    const ModelVolume *scaled = object.volumes[1];
    repair(object);
    REQUIRE(object.volumes.size() == 2);
    REQUIRE(object.volumes[1] == scaled);
    REQUIRE(open_edges(*object.volumes[0]) == 0);
}

TEST_CASE("Repairing a volume whose first shell is flat repairs its solid and leaves the other volume alone", "[ModelObjectRepair]") {
    // ModelVolume::split() keeps the first shell in the split volume itself and then deletes it for being flat.
    Model        model;
    ModelObject &object = object_with(model, {merged(sheet_at(Vec3f(0.f, 0.f, 50.f)), open_cube_at(10.f, Vec3f::Zero())),
                                              open_cube_at(10.f, Vec3f(30.f, 0.f, 0.f))});
    const ModelVolume *other = object.volumes[1];
    repair(object, 0);
    REQUIRE(object.volumes.size() == 2);
    REQUIRE(object.volumes[1] == other);
    REQUIRE(open_edges(*other) == 3);
    REQUIRE(open_edges(*object.volumes[0]) == 0);
    REQUIRE_THAT(double(its_volume(object.volumes[0]->mesh().its)), WithinRel(1000., 0.001));
}

TEST_CASE("Repairing a volume whose shells are all flat leaves the only other volume alone", "[ModelObjectRepair]") {
    // split() deletes every part, and deleting volumes gives the last one left a new id.
    Model        model;
    ModelObject &object = object_with(model, {merged(sheet_at(Vec3f(0.f, 0.f, 50.f)), sheet_at(Vec3f(0.f, 0.f, 80.f))),
                                              open_cube_at(10.f, Vec3f(30.f, 0.f, 0.f))});
    const ModelVolume *other = object.volumes[1];
    repair(object, 0);
    REQUIRE(object.volumes.size() == 1);
    REQUIRE(object.volumes[0] == other);
    REQUIRE(open_edges(*other) == 3);
}

TEST_CASE("Repairing after dropping a flat first volume still repairs the next volume", "[ModelObjectRepair]") {
    Model        model;
    ModelObject &object = object_with(model, {sheet_at(Vec3f::Zero()), open_cube_at(10.f, Vec3f(20.f, 0.f, 0.f))});
    repair(object);
    REQUIRE(object.volumes.size() == 1);
    REQUIRE(open_edges(*object.volumes.front()) == 0);
}

TEST_CASE("Repairing never leaves an object without volumes", "[ModelObjectRepair]") {
    Model        model;
    ModelObject &object = object_with(model, {sheet_at(Vec3f::Zero())});
    REQUIRE_THROWS(repair(object));
    REQUIRE(object.volumes.size() == 1);
}

TEST_CASE("Repairing an object made only of flat parts reports failure without changing it", "[ModelObjectRepair]") {
    // Side by side, and stacked: together the stacked sheets span a box and have a non-zero signed volume.
    const Vec3f second = GENERATE(Vec3f(20.f, 0.f, 0.f), Vec3f(0.f, 0.f, 5.f));
    Model        model;
    ModelObject &object    = object_with(model, {merged(sheet_at(Vec3f::Zero()), sheet_at(second))});
    const auto   unchanged = object.volumes.front()->get_mesh_shared_ptr();
    REQUIRE_THROWS_AS(repair(object), Slic3r::RuntimeError);
    REQUIRE(object.volumes.size() == 1);
    REQUIRE(object.volumes.front()->get_mesh_shared_ptr() == unchanged);
}

TEST_CASE("Repairing a volume that does not exist is rejected", "[ModelObjectRepair]") {
    Model        model;
    ModelObject &object = object_with(model, {open_cube_at(10.f, Vec3f::Zero())});
    REQUIRE_THROWS_AS(repair(object, 1), Slic3r::InvalidArgument);
    REQUIRE_THROWS_AS(repair(object, -2), Slic3r::InvalidArgument);
    REQUIRE(open_edges(*object.volumes.front()) > 0);
}

TEST_CASE("Repairing stops when cancelled", "[ModelObjectRepair]") {
    Model        model;
    ModelObject &object = object_with(model, {open_cube_at(10.f, Vec3f::Zero())});
    struct Cancelled {};
    REQUIRE_THROWS_AS(repair_model_object(object, -1, false, [](const std::string &, int) {}, [] { throw Cancelled(); }), Cancelled);
    REQUIRE(open_edges(*object.volumes.front()) > 0);
}

TEST_CASE("Repairing an object made only of flat parts stops when cancelled", "[ModelObjectRepair]") {
    Model        model;
    ModelObject &object = object_with(model, {merged(sheet_at(Vec3f::Zero()), sheet_at(Vec3f(0.f, 0.f, 5.f)))});
    struct Cancelled {};
    REQUIRE_THROWS_AS(repair_model_object(object, -1, false, [](const std::string &, int) {}, [] { throw Cancelled(); }), Cancelled);
}

TEST_CASE("Repairing reports progress from 0 to 100 percent", "[ModelObjectRepair]") {
    Model        model;
    ModelObject &object = object_with(model, {open_cube_at(10.f, Vec3f::Zero()), sheet_at(Vec3f(20.f, 0.f, 0.f)),
                                              merged(open_cube_at(10.f, Vec3f(40.f, 0.f, 0.f)), open_cube_at(10.f, Vec3f(60.f, 0.f, 0.f)))});
    std::vector<int> reported;
    repair_model_object(object, -1, false, [&reported](const std::string &, int percent) { reported.push_back(percent); }, [] {});
    REQUIRE_FALSE(reported.empty());
    for (int percent : reported) {
        REQUIRE(percent >= 0);
        REQUIRE(percent <= 100);
    }
    REQUIRE(reported.back() == 100);
}

TEST_CASE("Repairing leaves a closed volume made of several shells as it is", "[ModelObjectRepair]") {
    // Side by side, and a hollow cube.
    const indexed_triangle_set shells = GENERATE(merged(cube_at(10.f, Vec3f::Zero()), cube_at(10.f, Vec3f(20.f, 0.f, 0.f))),
                                                 merged(cube_at(10.f, Vec3f::Zero()), inverted(cube_at(4.f, Vec3f(3.f, 3.f, 3.f)))));
    Model        model;
    ModelObject &object = object_with(model, {shells});
    ModelVolume &volume = *object.volumes.front();
    volume.supported_facets.set_triangle_from_string(0, "4");
    volume.mmu_segmentation_facets.set_triangle_from_string(1, "8");
    const ObjectID id   = volume.id();
    const auto     mesh = volume.get_mesh_shared_ptr();
    REQUIRE_FALSE(repair(object).self_intersections_remain);
    REQUIRE(object.volumes.size() == 1);
    REQUIRE(object.volumes.front() == &volume);
    REQUIRE(volume.id() == id);
    REQUIRE(volume.get_mesh_shared_ptr() == mesh);
    REQUIRE_FALSE(volume.supported_facets.empty());
    REQUIRE_FALSE(volume.mmu_segmentation_facets.empty());
}

TEST_CASE("Repairing keeps the paint of a repaired part only with keep painting", "[ModelObjectRepair]") {
    const bool   keep_painting = GENERATE(false, true);
    Model        model;
    ModelObject &object = object_with(model, {open_cube_at(10.f, Vec3f::Zero())});
    object.volumes.front()->supported_facets.set_triangle_from_string(0, "4");
    repair(object, -1, keep_painting);
    REQUIRE(open_edges(*object.volumes.front()) == 0);
    REQUIRE(object.volumes.front()->supported_facets.empty() != keep_painting);
}

TEST_CASE("Repairing splits a closed volume whose shells overlap", "[ModelObjectRepair]") {
    Model        model;
    ModelObject &object = object_with(model, {merged(cube_at(10.f, Vec3f::Zero()), cube_at(10.f, Vec3f(5.f, 5.f, 0.f)))});
    repair(object);
    REQUIRE(object.volumes.size() == 2);
    REQUIRE_THAT(total_volume(object), WithinRel(2000., 0.001));
}

TEST_CASE("Repairing reports the self-intersections it cannot remove", "[ModelObjectRepair]") {
    const bool   open = GENERATE(false, true);
    Model        model;
    ModelObject &object = object_with(model, {pierced_cube(open)});
    REQUIRE(repair(object).self_intersections_remain);
    REQUIRE(object.volumes.size() == 1);
    REQUIRE(open_edges(*object.volumes.front()) == 0);
}

TEST_CASE("Repairing reports no self-intersections when it leaves none", "[ModelObjectRepair]") {
    Model        model;
    ModelObject &object = object_with(model, {open_cube_at(10.f, Vec3f::Zero())});
    REQUIRE_FALSE(repair(object).self_intersections_remain);
}

TEST_CASE("Applying a repaired copy changes only the repaired volumes of the object", "[ModelObjectRepair]") {
    Model              model;
    ModelObject       &object    = object_with(model, {open_cube_at(10.f, Vec3f::Zero()), cube_at(10.f, Vec3f(20.f, 0.f, 0.f))});
    const ModelVolume *untouched = object.volumes[1];
    Model              scratch;
    ModelObject       &copy = *scratch.add_object(object);
    repair(copy);
    REQUIRE(open_edges(*object.volumes[0]) > 0);
    apply_repaired_volumes(object, copy);
    REQUIRE(object.volumes.size() == 2);
    REQUIRE(open_edges(*object.volumes[0]) == 0);
    REQUIRE(object.volumes[0]->get_object() == &object);
    REQUIRE(object.volumes[1] == untouched);
    REQUIRE(open_edges(*copy.volumes[0]) == 0);
}

TEST_CASE("Applying a repaired copy that dropped volumes keeps the object in place", "[ModelObjectRepair]") {
    Model        model;
    ModelObject &object = object_with(model, {sheet_at(Vec3f::Zero()), open_cube_at(10.f, Vec3f(20.f, 0.f, 0.f))});
    object.volumes[1]->set_offset(Vec3d(3., -7., 11.));
    object.instances.front()->set_offset(Vec3d(100., 50., 0.));
    const BoundingBoxf3 before = object.volumes[1]->mesh().transformed_bounding_box(object.instances.front()->get_matrix() *
                                                                                     object.volumes[1]->get_matrix());
    Model               scratch;
    ModelObject        &copy = *scratch.add_object(object);
    repair(copy);
    apply_repaired_volumes(object, copy);
    REQUIRE(object.volumes.size() == 1);
    REQUIRE(open_edges(*object.volumes.front()) == 0);
    const BoundingBoxf3 after = world_bounding_box(object);
    REQUIRE(after.min.isApprox(before.min, 1e-5));
    REQUIRE(after.max.isApprox(before.max, 1e-5));
}

TEST_CASE("Cancelling the repair of a copy leaves the object unchanged", "[ModelObjectRepair]") {
    Model        model;
    ModelObject &object = object_with(model, {merged(open_cube_at(10.f, Vec3f::Zero()), open_cube_at(10.f, Vec3f(20.f, 0.f, 0.f))),
                                              open_cube_at(10.f, Vec3f(40.f, 0.f, 0.f))});
    object.volumes[0]->supported_facets.set_triangle_from_string(0, "4");
    const ModelVolumePtrs                            volumes = object.volumes;
    std::vector<ObjectID>                            ids;
    std::vector<std::shared_ptr<const TriangleMesh>> meshes;
    for (const ModelVolume *v : volumes) {
        ids.push_back(v->id());
        meshes.push_back(v->get_mesh_shared_ptr());
    }
    const Transform3d instance = object.instances.front()->get_matrix();

    Model        scratch;
    ModelObject &copy = *scratch.add_object(object);
    struct Cancelled {};
    // Cancelled once the first volume of the copy has been split.
    REQUIRE_THROWS_AS(repair_model_object(copy, -1, false, [](const std::string &, int) {}, [&copy] { if (copy.volumes.size() > 2) throw Cancelled(); }),
                      Cancelled);
    REQUIRE(copy.volumes.size() == 3);

    REQUIRE(object.volumes == volumes);
    for (size_t i = 0; i < volumes.size(); ++i) {
        REQUIRE(volumes[i]->id() == ids[i]);
        REQUIRE(volumes[i]->get_mesh_shared_ptr() == meshes[i]);
        REQUIRE(volumes[i]->get_object() == &object);
    }
    REQUIRE_FALSE(volumes[0]->supported_facets.empty());
    REQUIRE(object.instances.front()->get_matrix().isApprox(instance));
}
