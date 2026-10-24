// Minimal replacement for the config API PythonPluginBridge binds into `orca`.
//
// The production implementation (PluginConfig.cpp) resolves presets, reads the sidebar's effective
// config and writes config.json through PluginManager, all of which pull in the GUI and the
// registry. The probe has neither: a capability reports an empty config, an empty version and a
// refused save. That keeps the bridge's Python surface intact without pretending to reproduce
// application state the probe does not have.

#include "slic3r/plugin/PluginConfig.hpp"
#include "slic3r/plugin/PythonPluginInterface.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace Slic3r {

nlohmann::json capability_get_config(const PluginCapabilityInterface&)
{
    return nlohmann::json::object();
}

std::string capability_get_config_version(const PluginCapabilityInterface&) { return {}; }

bool capability_save_config(const PluginCapabilityInterface&, const nlohmann::json&) { return false; }

std::string PluginConfig::capability_type_label(PluginCapabilityType type)
{
    return plugin_capability_type_display_name(type);
}

} // namespace Slic3r
