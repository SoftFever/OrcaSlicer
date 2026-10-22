#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r { namespace GUI {

enum class ModelImportStatus { Success, Partial, Cancelled, Failed };

struct ModelImportResult
{
    ModelImportStatus               status {ModelImportStatus::Failed};
    std::vector<size_t>              object_ids;
    std::vector<std::vector<size_t>> object_ids_by_file;
    std::vector<size_t>              instance_ids;
    int                             plate_index {-1};
    bool                            arrange_started {false};
    std::vector<std::string>         errors;
};

// Meshes without source instances use the plater's normal empty-cell placement. Formats
// with an existing layout need one common XY shift to anchor that layout on the target.
inline bool center_prepositioned_model_for_import(Model& model, const Vec2d& target_center)
{
    if (std::none_of(model.objects.begin(), model.objects.end(),
                     [](const ModelObject* object) { return !object->instances.empty(); }))
        return false;
    return model.center_instances_around_point(target_center);
}

// These are the native geometry loaders, not the project/archive file-picker routes.
inline bool is_supported_model_import_extension(std::string extension)
{
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    constexpr std::array extensions {".stl", ".oltp", ".obj", ".amf", ".3mf", ".step", ".stp",
                                     ".svg", ".drc", ".glb", ".gltf", ".fbx"};
    if (std::find(extensions.begin(), extensions.end(), extension) != extensions.end())
        return true;
#ifdef __APPLE__
    constexpr std::array modelio_extensions {".usd", ".usda", ".usdc", ".usdz", ".abc", ".ply"};
    return std::find(modelio_extensions.begin(), modelio_extensions.end(), extension) != modelio_extensions.end();
#else
    return false;
#endif
}

inline bool is_supported_model_import_filename(std::string filename)
{
    std::transform(filename.begin(), filename.end(), filename.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    constexpr char gcode_suffix[] = ".gcode.3mf";
    constexpr size_t gcode_suffix_size = sizeof(gcode_suffix) - 1;
    if (filename.size() >= gcode_suffix_size &&
        filename.compare(filename.size() - gcode_suffix_size, gcode_suffix_size, gcode_suffix) == 0)
        return false;
    const size_t extension = filename.find_last_of('.');
    return extension != std::string::npos && is_supported_model_import_extension(filename.substr(extension));
}

}} // namespace Slic3r::GUI
