// Plugin module ownership is part of the loader's contract, not of the registry's GUI-facing
// surface. Kept in its own translation unit so the headless probe can link the loader without
// pulling PluginManager.cpp (and therefore wx/GUI) into the probe.

#include "PluginManager.hpp"
#include "PythonInterpreter.hpp"

namespace Slic3r {

void Plugin::release_module()
{
    if (module == nullptr && plugin_sys_paths.empty() && plugin_modules.empty())
        return;

    PythonInterpreter::instance().unload_module(module, module_name, plugin_sys_paths, plugin_modules);
    module = nullptr;
    module_name.clear();
    plugin_sys_paths.clear();
    plugin_modules.clear();
}

} // namespace Slic3r
