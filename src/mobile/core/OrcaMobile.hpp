#pragma once

#include <string>

// Entry points of the mobile façade over libslic3r.
//
// The React Native bridge calls into this namespace only; it never includes libslic3r
// headers directly. Everything here is plain C++17 with no platform dependencies.
namespace Slic3r { namespace Mobile {

// Version of the slicing core compiled into this library (SoftFever_VERSION).
std::string core_version();

// Directories the core reads and writes. Both must exist and be writable by the app.
// resources_dir holds the fetched profiles and the bundled runtime data; data_dir holds
// the app config, user presets and logs. Call once, before any other façade function.
void set_directories(const std::string& resources_dir, const std::string& data_dir);

}} // namespace Slic3r::Mobile
