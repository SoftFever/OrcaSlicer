#include "PreviewGeometrySnapshot.hpp"
#include "../pluginTypes/visualization/VisualizationPluginCapability.hpp"

#include "slic3r/GUI/ToolpathMeshBuilder.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/Format/GLB.hpp"
#include "libslic3r/Format/DRC.hpp"
#include "libslic3r/Format/OBJ.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace Slic3r::PreviewGeometrySnapshot {
namespace {

uint8_t channel(float value)
{
    return uint8_t(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

GLB::Scene make_scene(Snapshot&& snapshot)
{
    GLB::Scene scene;
    scene.name = "OrcaSlicer preview";
    scene.bounds_min = snapshot.bounds_min;
    scene.bounds_max = snapshot.bounds_max;
    scene.root_rotation = {{-0.7071067811865476, 0, 0, 0.7071067811865476}};
    scene.root_scale = 0.001;
    scene.extras = {{"orca", {{"sceneId", snapshot.scene_id},
                               {"plateIndex", snapshot.plate_index},
                               {"metadataLengthUnit", "mm"},
                               {"spiralVase", (snapshot.flags & FLAG_SPIRAL_VASE) != 0},
                               {"zOffset", snapshot.z_offset},
                               {"printableHeight", snapshot.printable_height}}}};

    scene.primitives.reserve(snapshot.groups.size());
    uint64_t covered_indices = 0;
    for (const Group& group : snapshot.groups) {
        if (group.first_index != covered_indices || group.index_count == 0 || group.index_count % 3 != 0 ||
            uint64_t(group.first_index) + group.index_count > snapshot.indices.size())
            throw std::runtime_error("Preview groups do not cover the index buffer");
        scene.primitives.push_back({group.first_index, group.index_count, group.material_slot,
                                    {{"orca", {{"layerId", group.layer_id},
                                                {"extrusionRole", group.extrusion_role},
                                                {"extruderId", group.extruder_id},
                                                {"colourId", group.colour_id}}}}});
        covered_indices += group.index_count;
    }
    if (covered_indices != snapshot.indices.size())
        throw std::runtime_error("Preview groups do not cover the index buffer");
    scene.vertices = std::move(snapshot.vertices);
    scene.indices = std::move(snapshot.indices);
    scene.materials.reserve(snapshot.materials.size());
    for (const MaterialSlot& material : snapshot.materials)
        scene.materials.push_back({material.display_name, material.rgba,
                                   {{"orca", {{"extruderId", material.extruder_id},
                                               {"presetId", material.preset_id}}}}});

    const auto add_area = [&](const std::vector<Point>& points, const char* kind) {
        if (points.size() < 2)
            return;
        GLB::Polyline polyline;
        polyline.closed = true;
        polyline.extras = {{"orca", {{"areaKind", kind}}}};
        polyline.points.reserve(points.size());
        for (const Point& point : points)
            polyline.points.push_back({point.x, point.y, snapshot.z_offset});
        scene.polylines.push_back(std::move(polyline));
    };
    add_area(snapshot.printable_area, "printable");
    add_area(snapshot.bed_excluded_area, "bed-excluded");
    add_area(snapshot.wrapping_excluded_area, "wrapping-excluded");
    return scene;
}

TriangleMesh make_mesh(const Snapshot& snapshot)
{
    if (snapshot.indices.empty() || snapshot.indices.size() % 3 != 0)
        throw std::runtime_error("Preview index buffer is not triangular");
    indexed_triangle_set its;
    its.vertices.reserve(snapshot.vertices.size());
    for (const Vertex& vertex : snapshot.vertices)
        its.vertices.emplace_back(vertex.position[0], vertex.position[1], vertex.position[2]);
    its.indices.reserve(snapshot.indices.size() / 3);
    for (size_t i = 0; i < snapshot.indices.size(); i += 3) {
        if (snapshot.indices[i] >= its.vertices.size() || snapshot.indices[i + 1] >= its.vertices.size() ||
            snapshot.indices[i + 2] >= its.vertices.size())
            throw std::runtime_error("Preview index is out of range");
        its.indices.emplace_back(snapshot.indices[i], snapshot.indices[i + 1], snapshot.indices[i + 2]);
    }
    return TriangleMesh(std::move(its));
}

} // namespace

TriangleMesh capture_model(const Model& model, const std::vector<std::pair<size_t, size_t>>& instances)
{
    TriangleMesh mesh;
    for (const auto& [object_index, instance_index] : instances) {
        if (object_index >= model.objects.size())
            continue;
        const ModelObject* object = model.objects[object_index];
        if (instance_index >= object->instances.size())
            continue;
        for (const ModelVolume* volume : object->volumes) {
            if (!volume->is_model_part())
                continue;
            TriangleMesh part(volume->mesh());
            part.transform(volume->get_matrix(), true);
            part.transform(object->instances[instance_index]->get_matrix(), true);
            mesh.merge(part);
        }
    }
    return mesh;
}

Snapshot capture(const GUI::PreviewTriangleMesh& mesh, const GCodeProcessorResult& result, int plate_index,
                 uint64_t expected_scene_id, const std::vector<std::string>& tool_colors, const Pointfs& fallback_printable_area)
{
    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.groups.empty())
        throw std::runtime_error("Preview mesh is empty");

    Snapshot out;
    out.scene_id = expected_scene_id;
    out.plate_index = plate_index;
    std::vector<std::string> filament_ids;
    size_t filament_count{};
    {
        std::lock_guard<std::mutex> lock(result.result_mutex);
        if (result.id != expected_scene_id)
            throw std::runtime_error("Slice result changed before preview capture");
        out.flags = result.spiral_vase_mode ? FLAG_SPIRAL_VASE : 0;
        out.z_offset = result.z_offset;
        out.printable_height = result.printable_height;
        filament_ids = result.settings_ids.filament;
        filament_count = result.filaments_count;
        const auto copy = [](const Pointfs& source, std::vector<Point>& target) {
            target.reserve(source.size());
            for (const Vec2d& point : source)
                target.push_back({float(point.x()), float(point.y())});
        };
        copy(result.printable_area.empty() ? fallback_printable_area : result.printable_area, out.printable_area);
        copy(result.bed_exclude_area, out.bed_excluded_area);
        copy(result.wrapping_exclude_area, out.wrapping_excluded_area);
    }

    out.vertices.reserve(mesh.vertices.size());
    for (const auto& vertex : mesh.vertices)
        out.vertices.push_back({{vertex.position.x(), vertex.position.y(), vertex.position.z()},
                                {vertex.normal.x(), vertex.normal.y(), vertex.normal.z()},
                                {vertex.uv.x(), vertex.uv.y()}});
    out.indices = mesh.indices;
    uint16_t max_slot = 0;
    for (const auto& group : mesh.groups) {
        out.groups.push_back({group.first_index, group.index_count, group.material_slot, group.extrusion_role,
                              group.extruder_id, group.colour_id, group.layer_id});
        max_slot = std::max(max_slot, group.material_slot);
    }

    const size_t material_count = std::max({tool_colors.size(), filament_ids.size(), filament_count, size_t(max_slot) + 1});
    for (size_t i = 0; i < material_count; ++i) {
        MaterialSlot material;
        material.extruder_id = uint8_t(std::min<size_t>(i, 255));
        ColorRGBA color;
        if (i >= tool_colors.size() || !decode_color(tool_colors[i], color))
            color = ColorRGBA::GRAY();
        material.rgba = {{channel(color.r()), channel(color.g()), channel(color.b()), channel(color.a())}};
        if (i < filament_ids.size()) {
            material.preset_id = filament_ids[i];
            material.display_name = filament_ids[i];
        }
        out.materials.push_back(std::move(material));
    }
    if (mesh.bounds.defined)
        for (size_t i = 0; i < 3; ++i) {
            out.bounds_min[i] = mesh.bounds.min[int(i)];
            out.bounds_max[i] = mesh.bounds.max[int(i)];
        }
    return out;
}

std::vector<uint8_t> serialize(Snapshot snapshot)
{
    return GLB::serialize(make_scene(std::move(snapshot)));
}

void write_atomic(Snapshot snapshot, const boost::filesystem::path& target)
{
    store_glb(target.string().c_str(), make_scene(std::move(snapshot)));
}

const std::vector<Format>& supported_formats()
{
    static const std::vector<Format> formats = {
        {VisualizationInputs::GLTF_BINARY, ".glb", GLB::FORMAT_MAJOR, GLB::FORMAT_MINOR},
        {VisualizationInputs::STL, ".stl", 1, 0},
        {VisualizationInputs::OBJ, ".obj", 1, 0},
        {VisualizationInputs::DRACO, ".drc", 1, 0},
    };
    return formats;
}

const Format* find_format(const std::string& media_type)
{
    const auto& formats = supported_formats();
    const auto it = std::find_if(formats.begin(), formats.end(), [&media_type](const Format& format) {
        return media_type == format.media_type;
    });
    return it == formats.end() ? nullptr : &*it;
}

void write_atomic(Snapshot snapshot, const boost::filesystem::path& target, const std::string& media_type)
{
    if (media_type == VisualizationInputs::GLTF_BINARY) {
        write_atomic(std::move(snapshot), target);
        return;
    }
    write_atomic(make_mesh(snapshot), target, media_type);
}

void write_atomic(const TriangleMesh& source, const boost::filesystem::path& target, const std::string& media_type)
{
    if (media_type == VisualizationInputs::GLTF_BINARY) {
        store_glb(target.string().c_str(), &source);
        return;
    }
    if (find_format(media_type) == nullptr)
        throw std::runtime_error("Unsupported visualization format: " + media_type);

    namespace fs = boost::filesystem;
    fs::path temp = target;
    temp += ".tmp-" + fs::unique_path("%%%%-%%%%-%%%%").string();
    TriangleMesh mesh(source);
    bool written = false;
    try {
        if (media_type == VisualizationInputs::STL)
            written = store_stl(temp.string().c_str(), &mesh, true);
        else if (media_type == VisualizationInputs::OBJ)
            written = store_obj(temp.string().c_str(), &mesh);
        else if (media_type == VisualizationInputs::DRACO)
            written = store_drc(temp.string().c_str(), &mesh, DRC_BITS_DEFAULT);
        if (!written)
            throw std::runtime_error("Failed to write visualization resource");
        boost::system::error_code error;
        fs::rename(temp, target, error);
        if (error)
            throw std::runtime_error("Failed to publish visualization resource: " + error.message());
    } catch (...) {
        boost::system::error_code ignored;
        fs::remove(temp, ignored);
        throw;
    }
}

} // namespace Slic3r::PreviewGeometrySnapshot
