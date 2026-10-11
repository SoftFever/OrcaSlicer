#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

// Inspect exported files independently of the writer; this is not an import API.
namespace GLBTest {
using json = nlohmann::json;
constexpr uint32_t GLB_MAGIC = 0x46546c67;
constexpr uint32_t GLB_VERSION = 2;
constexpr uint32_t JSON_CHUNK = 0x4e4f534a;
constexpr uint32_t BIN_CHUNK = 0x004e4942;
constexpr uint32_t FLOAT_COMPONENT = 5126;
constexpr uint32_t TRIANGLES = 4;
constexpr uint64_t DEFAULT_MAX_FILE_SIZE = uint64_t{UINT32_MAX};

struct DocumentInfo {
    uint32_t version{0};
    uint32_t file_size{0};
    size_t vertex_count{0};
    size_t index_count{0};
    size_t primitive_count{0};
    size_t material_count{0};
    nlohmann::json scene_extras;
};

template<class T> T get_le(const uint8_t* data, size_t size, size_t offset)
{
    static_assert(std::is_integral_v<T>);
    if (offset > size || sizeof(T) > size - offset)
        throw std::runtime_error("GLB region out of bounds");
    using U = std::make_unsigned_t<T>;
    U value{};
    for (size_t i = 0; i < sizeof(T); ++i)
        value |= U(data[offset + i]) << (8 * i);
    return static_cast<T>(value);
}

inline const json& array_at(const json& document, const char* key, size_t index)
{
    if (!document.contains(key) || !document[key].is_array() || index >= document[key].size())
        throw std::runtime_error(std::string("Invalid GLB ") + key + " reference");
    return document[key][index];
}

inline size_t accessor_count(const json& document, size_t index)
{
    const json& accessor = array_at(document, "accessors", index);
    if (!accessor.contains("count") || !accessor["count"].is_number_unsigned())
        throw std::runtime_error("Invalid GLB accessor");
    return accessor["count"].get<size_t>();
}

inline size_t component_size(uint32_t type)
{
    switch (type) {
    case 5125: case 5126: return 4;
    default: throw std::runtime_error("Invalid GLB accessor component type");
    }
}

inline size_t component_count(const std::string& type)
{
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    throw std::runtime_error("Invalid GLB accessor type");
}

inline DocumentInfo validate(const uint8_t* data, size_t size, uint64_t max_file_size = DEFAULT_MAX_FILE_SIZE)
{
    if (data == nullptr || size < 28 || size > max_file_size || get_le<uint32_t>(data, size, 0) != GLB_MAGIC)
        throw std::runtime_error("Invalid GLB header");
    const uint32_t version = get_le<uint32_t>(data, size, 4);
    const uint32_t declared_size = get_le<uint32_t>(data, size, 8);
    if (version != GLB_VERSION || declared_size != size)
        throw std::runtime_error("Unsupported or truncated GLB");
    const uint32_t json_size = get_le<uint32_t>(data, size, 12);
    if (json_size == 0 || json_size % 4 != 0 || get_le<uint32_t>(data, size, 16) != JSON_CHUNK ||
        json_size > size - 28)
        throw std::runtime_error("Invalid GLB JSON chunk");
    const size_t binary_header = 20 + json_size;
    if (get_le<uint32_t>(data, size, binary_header + 4) != BIN_CHUNK)
        throw std::runtime_error("Invalid GLB binary chunk");
    const uint32_t binary_size = get_le<uint32_t>(data, size, binary_header);
    if (binary_size % 4 != 0 || binary_size != size - binary_header - 8)
        throw std::runtime_error("Invalid GLB binary chunk length");

    json document;
    try {
        document = json::parse(data + 20, data + 20 + json_size);
    } catch (const json::exception& error) {
        throw std::runtime_error(std::string("Invalid GLB JSON: ") + error.what());
    }
    if (!document.contains("asset") || document["asset"].value("version", "") != "2.0" ||
        !document.contains("buffers") || document["buffers"].size() != 1 ||
        document["buffers"][0].value("byteLength", size_t(binary_size + 1)) > binary_size)
        throw std::runtime_error("Invalid GLB document structure");

    const size_t declared_binary_size = document["buffers"][0]["byteLength"].get<size_t>();
    if (!document.contains("bufferViews") || !document["bufferViews"].is_array() ||
        !document.contains("accessors") || !document["accessors"].is_array())
        throw std::runtime_error("GLB is missing buffer declarations");
    for (const json& view : document["bufferViews"]) {
        const size_t offset = view.value("byteOffset", size_t(0));
        const size_t length = view.value("byteLength", size_t(0));
        if (view.value("buffer", size_t(1)) != 0 || offset > declared_binary_size ||
            length > declared_binary_size - offset)
            throw std::runtime_error("GLB buffer view is out of range");
    }
    for (const json& accessor : document["accessors"]) {
        const size_t view_index = accessor.value("bufferView", document["bufferViews"].size());
        const json& view = array_at(document, "bufferViews", view_index);
        const size_t count = accessor.value("count", size_t(0));
        const size_t element_size = component_size(accessor.value("componentType", uint32_t(0))) *
                                    component_count(accessor.value("type", std::string()));
        const size_t stride = view.value("byteStride", element_size);
        const size_t offset = accessor.value("byteOffset", size_t(0));
        const size_t view_length = view["byteLength"].get<size_t>();
        if (count == 0 || stride < element_size || offset > view_length ||
            (count - 1) > (view_length - offset) / stride ||
            element_size > view_length - offset - (count - 1) * stride)
            throw std::runtime_error("GLB accessor is out of range");
    }

    const json& mesh = array_at(document, "meshes", 0);
    if (!mesh.contains("primitives") || !mesh["primitives"].is_array() || mesh["primitives"].empty())
        throw std::runtime_error("GLB contains no primitives");
    const json& scene = array_at(document, "scenes", document.value("scene", size_t(0)));
    DocumentInfo info;
    info.version = version;
    info.file_size = declared_size;
    if (document.contains("materials") && (!document["materials"].is_array() || document["materials"].empty()))
        throw std::runtime_error("Invalid GLB materials array");
    info.material_count = document.value("materials", json::array()).size();
    info.scene_extras = scene.value("extras", json::object());
    for (const json& primitive : mesh["primitives"]) {
        if (!primitive.contains("attributes") || !primitive["attributes"].contains("POSITION"))
            throw std::runtime_error("GLB primitive has no positions");
        const json& positions = array_at(document, "accessors", primitive["attributes"]["POSITION"].get<size_t>());
        if (positions.value("type", "") != "VEC3" || positions.value("componentType", uint32_t(0)) != FLOAT_COMPONENT ||
            !positions.contains("min") || !positions["min"].is_array() || positions["min"].size() != 3 ||
            !positions.contains("max") || !positions["max"].is_array() || positions["max"].size() != 3)
            throw std::runtime_error("GLB positions are missing valid bounds");
        if (primitive.value("mode", TRIANGLES) == TRIANGLES) {
            if (!primitive.contains("indices"))
                throw std::runtime_error("GLB triangle primitive has no indices");
            info.index_count += accessor_count(document, primitive["indices"].get<size_t>());
            info.vertex_count = std::max(info.vertex_count,
                accessor_count(document, primitive["attributes"]["POSITION"].get<size_t>()));
            ++info.primitive_count;
        }
    }
    if (info.vertex_count == 0 || info.index_count == 0 || info.primitive_count == 0)
        throw std::runtime_error("GLB contains an empty triangle scene");
    return info;
}

inline DocumentInfo validate(const std::vector<uint8_t>& data, uint64_t max_file_size = DEFAULT_MAX_FILE_SIZE)
{
    return validate(data.data(), data.size(), max_file_size);
}

} // namespace GLBTest
