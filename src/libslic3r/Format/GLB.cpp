#include "GLB.hpp"

#include "libslic3r/TriangleMesh.hpp"

#ifdef _WIN32
#include "libslic3r/Utils.hpp"
#endif

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace Slic3r::GLB {
namespace {

using json = nlohmann::json;

constexpr uint32_t GLB_MAGIC = 0x46546c67;
constexpr uint32_t GLB_VERSION = 2;
constexpr uint32_t JSON_CHUNK = 0x4e4f534a;
constexpr uint32_t BIN_CHUNK = 0x004e4942;
constexpr uint32_t ARRAY_BUFFER = 34962;
constexpr uint32_t ELEMENT_ARRAY_BUFFER = 34963;
constexpr uint32_t FLOAT_COMPONENT = 5126;
constexpr uint32_t UNSIGNED_INT_COMPONENT = 5125;
constexpr uint32_t LINE_LOOP = 2;
constexpr uint32_t LINE_STRIP = 3;
constexpr uint32_t TRIANGLES = 4;
constexpr size_t VERTEX_STRIDE = sizeof(float) * 8;

size_t padded_size(size_t size) { return (size + 3) & ~size_t(3); }

double linear_channel(uint8_t value)
{
    // Filament swatches are sRGB; glTF material factors use linear RGB.
    const double srgb = value / 255.0;
    return srgb <= 0.04045 ? srgb / 12.92 : std::pow((srgb + 0.055) / 1.055, 2.4);
}

template<class T> void append_le(std::vector<uint8_t>& data, T value)
{
    static_assert(std::is_integral_v<T>);
    using U = std::make_unsigned_t<T>;
    const U encoded = static_cast<U>(value);
    for (size_t i = 0; i < sizeof(T); ++i)
        data.push_back(uint8_t(encoded >> (8 * i)));
}

void append_float(std::vector<uint8_t>& data, float value, const char* field)
{
    if (!std::isfinite(value))
        throw std::runtime_error(std::string("GLB contains non-finite ") + field);
    uint32_t encoded;
    std::memcpy(&encoded, &value, sizeof(value));
    append_le(data, encoded);
}

size_t append_buffer_view(json& document, size_t offset, size_t length, uint32_t target, size_t stride = 0)
{
    json view = {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", length}};
    if (target != 0)
        view["target"] = target;
    if (stride != 0)
        view["byteStride"] = stride;
    document["bufferViews"].push_back(std::move(view));
    return document["bufferViews"].size() - 1;
}

size_t append_accessor(json& document, size_t view, size_t offset, uint32_t component_type,
                       size_t count, const char* type)
{
    document["accessors"].push_back({{"bufferView", view}, {"byteOffset", offset},
                                      {"componentType", component_type}, {"count", count}, {"type", type}});
    return document["accessors"].size() - 1;
}

} // namespace

std::vector<uint8_t> serialize(const Scene& scene)
{
    if (scene.vertices.empty() || scene.indices.empty() || scene.primitives.empty())
        throw std::runtime_error("GLB scene has an empty required section");
    for (size_t i = 0; i < 3; ++i)
        if (!std::isfinite(scene.bounds_min[i]) || !std::isfinite(scene.bounds_max[i]) ||
            scene.bounds_min[i] > scene.bounds_max[i])
            throw std::runtime_error("GLB scene contains invalid bounds");

    json scene_object = {{"nodes", json::array({0})}};
    if (!scene.extras.is_null() && !scene.extras.empty())
        scene_object["extras"] = scene.extras;
    json node = {{"mesh", 0}};
    if (scene.root_rotation != std::array<double, 4>{{0, 0, 0, 1}})
        node["rotation"] = scene.root_rotation;
    if (!std::isfinite(scene.root_scale) || scene.root_scale <= 0)
        throw std::runtime_error("GLB scene contains an invalid unit scale");
    if (scene.root_scale != 1.0)
        node["scale"] = {scene.root_scale, scene.root_scale, scene.root_scale};
    json document = {
        {"asset", {{"version", "2.0"}, {"generator", "OrcaSlicer"}}}, {"scene", 0},
        {"scenes", json::array({std::move(scene_object)})}, {"nodes", json::array({std::move(node)})},
        {"meshes", json::array()}, {"accessors", json::array()},
        {"bufferViews", json::array()}, {"buffers", json::array()}
    };

    std::vector<uint8_t> binary;
    for (const Vertex& vertex : scene.vertices) {
        for (float value : vertex.position) append_float(binary, value, "position");
        float normal_length_squared = 0;
        for (float value : vertex.normal) {
            append_float(binary, value, "normal");
            normal_length_squared += value * value;
        }
        if (normal_length_squared < 0.25f || normal_length_squared > 2.25f)
            throw std::runtime_error("GLB scene contains an invalid normal");
        for (float value : vertex.uv) append_float(binary, value, "UV");
    }
    const size_t vertex_view = append_buffer_view(document, 0, binary.size(), ARRAY_BUFFER, VERTEX_STRIDE);
    const size_t positions = append_accessor(document, vertex_view, 0, FLOAT_COMPONENT, scene.vertices.size(), "VEC3");
    document["accessors"][positions]["min"] = scene.bounds_min;
    document["accessors"][positions]["max"] = scene.bounds_max;
    const size_t normals = append_accessor(document, vertex_view, 3 * sizeof(float), FLOAT_COMPONENT,
                                            scene.vertices.size(), "VEC3");
    const size_t texcoords = append_accessor(document, vertex_view, 6 * sizeof(float), FLOAT_COMPONENT,
                                              scene.vertices.size(), "VEC2");

    const size_t indices_offset = binary.size();
    for (uint32_t index : scene.indices) {
        if (index >= scene.vertices.size())
            throw std::runtime_error("GLB scene index out of range");
        append_le(binary, index);
    }
    const size_t index_view = append_buffer_view(document, indices_offset, binary.size() - indices_offset,
                                                  ELEMENT_ARRAY_BUFFER);

    if (!scene.materials.empty())
        document["materials"] = json::array();
    for (const Material& material : scene.materials) {
        json value = {{"pbrMetallicRoughness", {{"baseColorFactor", {
                           linear_channel(material.base_color[0]), linear_channel(material.base_color[1]),
                           linear_channel(material.base_color[2]), material.base_color[3] / 255.0}},
                                                  {"metallicFactor", 0.0}, {"roughnessFactor", 1.0}}},
                      {"doubleSided", true}};
        if (!material.name.empty()) value["name"] = material.name;
        if (!material.extras.is_null() && !material.extras.empty()) value["extras"] = material.extras;
        if (material.base_color[3] != 255) value["alphaMode"] = "BLEND";
        document["materials"].push_back(std::move(value));
    }

    json primitives = json::array();
    for (const Primitive& primitive : scene.primitives) {
        if (primitive.index_count == 0 || primitive.index_count % 3 != 0 ||
            uint64_t(primitive.first_index) + primitive.index_count > scene.indices.size() ||
            primitive.material < -1 || (primitive.material >= 0 && size_t(primitive.material) >= scene.materials.size()))
            throw std::runtime_error("GLB scene contains an invalid primitive");
        const size_t indices = append_accessor(document, index_view, size_t(primitive.first_index) * sizeof(uint32_t),
                                                UNSIGNED_INT_COMPONENT, primitive.index_count, "SCALAR");
        json value = {{"attributes", {{"POSITION", positions}, {"NORMAL", normals}, {"TEXCOORD_0", texcoords}}},
                      {"indices", indices}, {"mode", TRIANGLES}};
        if (primitive.material >= 0) value["material"] = primitive.material;
        if (!primitive.extras.is_null() && !primitive.extras.empty()) value["extras"] = primitive.extras;
        primitives.push_back(std::move(value));
    }

    for (const Polyline& polyline : scene.polylines) {
        if (polyline.points.size() < 2)
            continue;
        const size_t offset = binary.size();
        for (const auto& point : polyline.points)
            for (float value : point) append_float(binary, value, "polyline point");
        const size_t view = append_buffer_view(document, offset, binary.size() - offset, ARRAY_BUFFER);
        const size_t accessor = append_accessor(document, view, 0, FLOAT_COMPONENT, polyline.points.size(), "VEC3");
        auto minimum = polyline.points.front();
        auto maximum = minimum;
        for (const auto& point : polyline.points)
            for (size_t axis = 0; axis < 3; ++axis) {
                minimum[axis] = std::min(minimum[axis], point[axis]);
                maximum[axis] = std::max(maximum[axis], point[axis]);
            }
        document["accessors"][accessor]["min"] = minimum;
        document["accessors"][accessor]["max"] = maximum;
        json value = {{"attributes", {{"POSITION", accessor}}},
                      {"mode", polyline.closed ? LINE_LOOP : LINE_STRIP}};
        if (!polyline.extras.is_null() && !polyline.extras.empty()) value["extras"] = polyline.extras;
        primitives.push_back(std::move(value));
    }

    document["meshes"].push_back({{"name", scene.name}, {"primitives", std::move(primitives)}});
    document["buffers"].push_back({{"byteLength", binary.size()}});
    std::string json_text = document.dump();
    json_text.resize(padded_size(json_text.size()), ' ');
    binary.resize(padded_size(binary.size()), 0);
    const uint64_t total_size = 12 + 8 + json_text.size() + 8 + binary.size();
    if (total_size > DEFAULT_MAX_FILE_SIZE)
        throw std::runtime_error("GLB exceeds its 32-bit file-size limit");

    std::vector<uint8_t> output;
    output.reserve(size_t(total_size));
    append_le(output, GLB_MAGIC); append_le(output, GLB_VERSION); append_le(output, uint32_t(total_size));
    append_le(output, uint32_t(json_text.size())); append_le(output, JSON_CHUNK);
    output.insert(output.end(), json_text.begin(), json_text.end());
    append_le(output, uint32_t(binary.size())); append_le(output, BIN_CHUNK);
    output.insert(output.end(), binary.begin(), binary.end());
    return output;
}

} // namespace Slic3r::GLB

void Slic3r::store_glb(const char* path, const GLB::Scene& scene)
{
    if (path == nullptr)
        throw std::invalid_argument("GLB output path is null");
    namespace fs = boost::filesystem;
    const std::vector<uint8_t> bytes = GLB::serialize(scene);
    const fs::path target(path);
    if (!target.parent_path().empty()) fs::create_directories(target.parent_path());
    fs::path temp = target;
    temp += ".tmp-" + fs::unique_path("%%%%-%%%%-%%%%").string();
    try {
        boost::nowide::ofstream output(temp.string(), std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        output.flush();
        output.close();
        if (!output) throw std::runtime_error("Failed to write GLB");
#ifdef _WIN32
        const std::error_code error = rename_file(temp.string(), target.string());
        if (error) throw std::runtime_error("Failed to publish GLB: " + error.message());
#else
        boost::system::error_code error;
        fs::rename(temp, target, error);
        if (error) throw std::runtime_error("Failed to publish GLB: " + error.message());
#endif
    } catch (...) {
        boost::system::error_code ignored;
        fs::remove(temp, ignored);
        throw;
    }
}

void Slic3r::store_glb(const char* path, const TriangleMesh* mesh)
{
    if (mesh == nullptr)
        throw std::invalid_argument("GLB input mesh is null");
    if (mesh->its.vertices.empty() || mesh->its.indices.empty())
        throw std::invalid_argument("GLB input mesh is empty");

    GLB::Scene scene;
    scene.name = "OrcaSlicer model";
    scene.root_rotation = {{-0.7071067811865476, 0, 0, 0.7071067811865476}};
    scene.root_scale = 0.001;
    scene.vertices.reserve(mesh->its.indices.size() * 3);
    scene.indices.reserve(mesh->its.indices.size() * 3);
    for (const auto& face : mesh->its.indices) {
        for (size_t corner = 0; corner < 3; ++corner)
            if (face[int(corner)] < 0 || size_t(face[int(corner)]) >= mesh->its.vertices.size())
                throw std::invalid_argument("GLB input mesh contains an invalid index");
        const Vec3f& a = mesh->its.vertices[size_t(face[0])];
        const Vec3f& b = mesh->its.vertices[size_t(face[1])];
        const Vec3f& c = mesh->its.vertices[size_t(face[2])];
        if (!a.allFinite() || !b.allFinite() || !c.allFinite())
            throw std::invalid_argument("GLB input mesh contains a non-finite position");
        Vec3f normal = (b - a).cross(c - a);
        const float normal_length_squared = normal.squaredNorm();
        if (!std::isfinite(normal_length_squared))
            throw std::invalid_argument("GLB input mesh contains an invalid face");
        if (normal_length_squared == 0.0f)
            continue;
        normal /= std::sqrt(normal_length_squared);
        for (size_t corner = 0; corner < 3; ++corner) {
            const Vec3f& position = mesh->its.vertices[size_t(face[int(corner)])];
            scene.vertices.push_back({{{position.x(), position.y(), position.z()}},
                                      {{normal.x(), normal.y(), normal.z()}}, {}});
            scene.indices.push_back(uint32_t(scene.indices.size()));
        }
    }
    if (scene.vertices.empty())
        throw std::invalid_argument("GLB input mesh contains no non-degenerate faces");
    scene.bounds_min = scene.vertices.front().position;
    scene.bounds_max = scene.vertices.front().position;
    for (const GLB::Vertex& vertex : scene.vertices)
        for (size_t axis = 0; axis < 3; ++axis) {
            scene.bounds_min[axis] = std::min(scene.bounds_min[axis], vertex.position[axis]);
            scene.bounds_max[axis] = std::max(scene.bounds_max[axis], vertex.position[axis]);
        }
    scene.primitives.push_back({0, uint32_t(scene.indices.size()), -1, {}});
    store_glb(path, scene);
}
