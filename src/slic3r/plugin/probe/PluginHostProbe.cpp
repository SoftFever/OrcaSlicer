// Probe-only `orca.host` registration. The GUI-free domains use the production bindings; the
// domains that need a live GUI application, main window or preset bundle are exposed as explicit
// unsupported submodules instead of being compiled (and linked) into the probe.
//
// A plugin that touches one of those domains fails its own callback with a clear message; the
// failure is recorded by the probe's callback report rather than becoming a link dependency.

#include "slic3r/plugin/host/PluginHost.hpp"
#include "slic3r/plugin/host/PluginHostBindings.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>

#include <stdexcept>
#include <string>

namespace py = pybind11;

namespace Slic3r {
namespace {

void add_unavailable_domain(py::module_& host, const char* name, const char* description)
{
    py::module_ domain = host.def_submodule(name, description);
    const std::string prefix = std::string("orca.host.") + name + ".";
    domain.attr("__getattr__") = py::cpp_function([prefix](const std::string& attribute) -> py::object {
        throw std::runtime_error(prefix + attribute +
                                 " is not available in the headless plugin permission probe because it needs the GUI application");
    });
}

void add_unavailable_function(py::module_& host, const char* name)
{
    // Production registers these in PluginHostApp::register_app(). Do not overwrite a slot that
    // another registrar already claimed (the model domain binds the Model class through
    // register_model); the probe only fills the GUI-backed accessors that are absent here.
    if (py::hasattr(host, name))
        return;

    const std::string detail = std::string("orca.host.") + name +
                               " is not available in the headless plugin permission probe because it needs the GUI application";
    host.def(name, [detail](py::args, py::kwargs) -> py::object { throw std::runtime_error(detail); }, detail.c_str());
}

} // namespace

void PluginHost::RegisterBindings(py::module_& module)
{
    auto host = module.def_submodule("host", "Host application API");

    // Value types first so the docstring signatures of later registrars resolve to the bound names.
    host_bindings::register_geometry(host);
    host_bindings::register_mesh(host);
    host_bindings::register_model(host);
    host_bindings::register_slicing(host);

    // These domains are backed by the live GUI, its Plater/MainFrame and the preset bundle.
    add_unavailable_domain(host, "presets", "Preset bundle access (headless probe: unavailable)");
    add_unavailable_domain(host, "app", "Live application and Plater access (headless probe: unavailable)");
    add_unavailable_domain(host, "ui", "Native dialogs and HTML windows (headless probe: unavailable)");
    for (const char* name : {"plater", "model", "preset_bundle", "app_info", "gl_info", "selected_printer"})
        add_unavailable_function(host, name);

    // Storage needs the PluginManager registry, which is not part of the headless probe.
    auto plugin_host = host.def_submodule("plugin", "Plugin host API (headless probe: storage unavailable)");
    plugin_host.def("storage", []() -> std::string {
        throw std::runtime_error("orca.host.plugin.storage() is not available in the headless plugin permission probe");
    }, "Return the installed folder of the current plugin.");
    plugin_host.attr("__getattr__") = py::cpp_function([](const std::string& attribute) -> py::object {
        throw std::runtime_error("orca.host.plugin." + attribute +
                                 " is not available in the headless plugin permission probe");
    });
}

} // namespace Slic3r
