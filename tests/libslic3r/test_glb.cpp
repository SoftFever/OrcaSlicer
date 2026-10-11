#include <catch2/catch_all.hpp>

#include "libslic3r/Format/GLB.hpp"
#include "test_utils.hpp"

#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace Slic3r;

namespace {
GLB::Scene sample_scene()
{
    GLB::Scene scene;
    scene.name = "Triangle";
    scene.vertices = {{{{0, 0, 0}}, {{0, 0, 1}}, {{0, 0}}},
                      {{{1, 0, 0}}, {{0, 0, 1}}, {{1, 0}}},
                      {{{0, 1, 0}}, {{0, 0, 1}}, {{0, 1}}}};
    scene.indices = {0, 1, 2};
    scene.primitives = {{0, 3, 0, {{"source", "test"}}}};
    scene.materials = {{"Red", {{255, 0, 0, 255}}, {{"id", 7}}}};
    scene.bounds_min = {{0, 0, 0}};
    scene.bounds_max = {{1, 1, 0}};
    scene.extras = {{"revision", 42}};
    scene.polylines = {{{{{0, 0, 0}}, {{1, 0, 0}}, {{1, 1, 0}}}, true, {{"kind", "boundary"}}}};
    return scene;
}

template<class T> void put_le(std::vector<uint8_t>& data, size_t offset, T value)
{
    using U = std::make_unsigned_t<T>;
    const U encoded = static_cast<U>(value);
    for (size_t i = 0; i < sizeof(T); ++i)
        data[offset + i] = uint8_t(encoded >> (8 * i));
}

template<class T> T get_le(const std::vector<uint8_t>& data, size_t offset)
{
    using U = std::make_unsigned_t<T>;
    U value{};
    for (size_t i = 0; i < sizeof(T); ++i)
        value |= U(data[offset + i]) << (8 * i);
    return static_cast<T>(value);
}

nlohmann::json glb_json(const std::vector<uint8_t>& bytes)
{
    return nlohmann::json::parse(bytes.begin() + 20, bytes.begin() + 20 + get_le<uint32_t>(bytes, 12));
}

std::vector<uint8_t> read_file(const boost::filesystem::path& path)
{
    std::ifstream input(path.string(), std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
} // namespace

TEST_CASE("GLB exports an indexed material scene", "[GLB]")
{
    const std::vector<uint8_t> bytes = GLB::serialize(sample_scene());
    const GLB::DocumentInfo info = GLB::validate(bytes);

    CHECK(info.version == 2);
    CHECK(info.file_size == bytes.size());
    CHECK(info.vertex_count == 3);
    CHECK(info.index_count == 3);
    CHECK(info.primitive_count == 1);
    CHECK(info.material_count == 1);
    CHECK(info.scene_extras["revision"] == 42);
}

TEST_CASE("GLB rejects malformed containers", "[GLB]")
{
    const std::vector<uint8_t> valid = GLB::serialize(sample_scene());

    auto bad_magic = valid;
    bad_magic[0] = 0;
    CHECK_THROWS(GLB::validate(bad_magic));

    auto bad_version = valid;
    put_le<uint32_t>(bad_version, 4, 1);
    CHECK_THROWS(GLB::validate(bad_version));

    auto truncated = valid;
    truncated.pop_back();
    CHECK_THROWS(GLB::validate(truncated));

    auto bad_json_chunk = valid;
    put_le<uint32_t>(bad_json_chunk, 16, 0);
    CHECK_THROWS(GLB::validate(bad_json_chunk));

    auto invalid_json = valid;
    invalid_json[20] = '!';
    CHECK_THROWS(GLB::validate(invalid_json));

    auto bad_binary_length = valid;
    const size_t binary_header = 20 + get_le<uint32_t>(bad_binary_length, 12);
    put_le<uint32_t>(bad_binary_length, binary_header, 0);
    CHECK_THROWS(GLB::validate(bad_binary_length));

    CHECK_THROWS(GLB::validate(valid, 3));
}

TEST_CASE("GLB exports geometry without a material", "[GLB]")
{
    GLB::Scene scene = sample_scene();
    scene.materials.clear();
    scene.primitives.front().material = -1;

    const auto bytes = GLB::serialize(scene);
    const GLB::DocumentInfo info = GLB::validate(bytes);

    CHECK(info.material_count == 0);
    CHECK(info.primitive_count == 1);
    CHECK_FALSE(glb_json(bytes).contains("materials"));
}

TEST_CASE("GLB includes position bounds for line primitives", "[GLB][Regression]")
{
    GLB::Scene scene = sample_scene();
    scene.polylines.front().points = {{{-2, 4, 1}}, {{3, -5, 2}}, {{1, 0, -3}}};
    const auto document = glb_json(GLB::serialize(scene));
    const size_t positions = document["meshes"][0]["primitives"][1]["attributes"]["POSITION"].get<size_t>();
    const auto& accessor = document["accessors"][positions];
    const std::array<double, 3> minimum{{-2, -5, -3}};
    const std::array<double, 3> maximum{{3, 4, 2}};
    for (size_t axis = 0; axis < 3; ++axis) {
        CHECK_THAT(accessor["min"][axis].get<double>(), Catch::Matchers::WithinAbs(minimum[axis], 1e-6));
        CHECK_THAT(accessor["max"][axis].get<double>(), Catch::Matchers::WithinAbs(maximum[axis], 1e-6));
    }
}

TEST_CASE("GLB rejects invalid scene scales", "[GLB]")
{
    GLB::Scene scene = sample_scene();
    scene.root_scale = GENERATE(0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN());
    CHECK_THROWS_AS(GLB::serialize(scene), std::runtime_error);
}

TEST_CASE("GLB stores a scene atomically", "[GLB]")
{
    ScopedTemporaryDir directory("glb-store");
    const boost::filesystem::path target = directory.path() / "scene.glb";

    store_glb(target.string().c_str(), sample_scene());

    REQUIRE(boost::filesystem::exists(target));
    CHECK(GLB::validate(read_file(target)).vertex_count == 3);
}

TEST_CASE("GLB stores a triangle mesh", "[GLB]")
{
    ScopedTemporaryDir directory("glb-mesh-store");
    const boost::filesystem::path target = directory.path() / "cube.glb";
    TriangleMesh mesh = make_cube(10.0, 20.0, 30.0);

    store_glb(target.string().c_str(), &mesh);

    const auto bytes = read_file(target);
    const GLB::DocumentInfo info = GLB::validate(bytes);
    CHECK(info.vertex_count == mesh.its.indices.size() * 3);
    CHECK(info.index_count == mesh.its.indices.size() * 3);
    CHECK(info.primitive_count == 1);
    const auto document = glb_json(bytes);
    const double scale = document["nodes"][0]["scale"][0].get<double>();
    const auto& positions = document["accessors"][0];
    const std::array<double, 3> dimensions_in_meters{{0.01, 0.02, 0.03}};
    for (size_t axis = 0; axis < 3; ++axis) {
        const double extent = positions["max"][axis].get<double>() - positions["min"][axis].get<double>();
        CHECK_THAT(extent * scale, Catch::Matchers::WithinAbs(dimensions_in_meters[axis], 1e-9));
        CHECK_THAT(document["nodes"][0]["scale"][axis].get<double>(), Catch::Matchers::WithinAbs(0.001, 1e-12));
    }
}

TEST_CASE("GLB skips degenerate triangle mesh facets", "[GLB][Regression]")
{
    ScopedTemporaryDir directory("glb-degenerate-facet");
    const boost::filesystem::path target = directory.path() / "mesh.glb";
    TriangleMesh mesh({{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
                      {{0, 1, 2}, {0, 1, 1}});

    store_glb(target.string().c_str(), &mesh);

    const GLB::DocumentInfo info = GLB::validate(read_file(target));
    CHECK(info.vertex_count == 3);
    CHECK(info.index_count == 3);
}

TEST_CASE("GLB rejects triangle meshes without exportable facets", "[GLB][Regression]")
{
    ScopedTemporaryDir directory("glb-empty-mesh");
    const boost::filesystem::path target = directory.path() / "mesh.glb";
    TriangleMesh empty;
    TriangleMesh degenerate({{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}}, {{0, 1, 1}});

    CHECK_THROWS_AS(store_glb(target.string().c_str(), &empty), std::invalid_argument);
    CHECK_THROWS_AS(store_glb(target.string().c_str(), &degenerate), std::invalid_argument);
    CHECK_FALSE(boost::filesystem::exists(target));
}
