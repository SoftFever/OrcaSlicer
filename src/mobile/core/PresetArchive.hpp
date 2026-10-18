#pragma once

#include "Session.hpp"

#include <string>
#include <vector>

// Preset archives in the desktop's formats: the zip layouts File > Export > Export
// Preset Bundle writes (.orca_printer, .orca_bundle, plain .zip) and a peek into any of
// them, or into a single preset .json, that does not touch the preset bundle.
namespace Slic3r {
class PresetBundle;
}

namespace Slic3r { namespace Mobile {

// One preset file found in an archive (or the lone .json itself).
struct ArchiveEntry
{
    std::string file_name;   // base name inside the archive, "<name>.json"
    std::string name;        // the preset's "name" key
    std::string inherits;    // the preset's "inherits" key, empty for a root preset
    PresetKind  kind = PresetKind::Printer;
    bool        valid = false; // carries a *_settings_id key and a parseable version
};

// Lists the preset entries of `path` without importing anything. Returns an empty
// list when the file is neither a preset .json nor a readable zip.
std::vector<ArchiveEntry> scan_preset_archive(const std::string& path);

// Writes `presets` (all must be user presets) as a zip at `out_path`. The extension
// picks the layout; see Session::export_presets. Returns out_path; throws
// std::runtime_error with a user-readable message on failure.
std::string write_preset_archive(PresetBundle& bundle, const std::vector<PresetRef>& presets, const std::string& out_path);

}} // namespace Slic3r::Mobile
