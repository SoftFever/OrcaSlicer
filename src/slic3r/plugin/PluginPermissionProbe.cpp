// Headless, record-only plugin permission probe.
//
// This is a developer/security-analysis tool, not a second production loader. It reuses the
// embedded interpreter, the production plugin_loader sequence and the GUI-free `orca` bindings
// (capability types plus the geometry/model/slicing host domains), so the observations come from
// the same audit classification and target extraction as the GUI. It then lets every audited
// operation continue instead of consulting permission grants or dialogs. Host domains that need
// the live GUI (app, presets, ui) and plugin.storage are registered as explicit unsupported
// errors; the config API reports an empty config and refuses writes.
//
// It is NOT a sandbox. Plugin code runs with the probe's filesystem, network and process
// privileges. The temporary data directory only keeps OrcaSlicer's own state out of the user's
// tree; it does not confine plugin-authored code. Use OS-level restrictions (container, user,
// seccomp/AppArmor, firewall) when the package is not trusted.
//
// The report describes only the paths this run exercised. CPython audit events are observations of
// particular operations, not a notification for every Python call, so absence of a category in the
// report is not evidence that the plugin cannot perform it.

#include "PluginAuditManager.hpp"
#include "PluginDescriptor.hpp"
#include "PluginFsUtils.hpp"
#include "PluginLoader.hpp"
#include "PluginManager.hpp"
#include "PluginProbeCollector.hpp"
#include "PythonInterpreter.hpp"
#include "PythonPluginInterface.hpp"

#include "libslic3r/LifecycleEvents.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>
#include <pybind11/embed.h>
#include <pybind11/gil.h>
#include <pybind11/pytypes.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fs = boost::filesystem;
namespace py = pybind11;

using namespace Slic3r;

namespace {

constexpr const char* kLoadPhase = "load:import+registration+on_load";

struct Options
{
    fs::path plugin_path;
    bool json = false;
};

void print_usage(std::ostream& out)
{
    out << "Usage: orca-plugin-probe [--json] <plugin.py|plugin.whl>\n"
           "\n"
           "Runs a plugin in a temporary data/working directory and reports the CPython audit\n"
           "events observed while its import, registration, on_load and discoverable callbacks\n"
           "execute. The probe does not prompt for permissions, write grants, install dependencies,\n"
           "or start the OrcaSlicer GUI.\n"
           "\n"
           "  --json   Emit the report as JSON (default: human-readable text)\n"
           "  --help   Show this message\n"
           "\n"
           "WARNING: this is not a sandbox. Plugin Python code runs with this process's privileges.\n"
           "Results are observational and non-exhaustive. Use OS-level restrictions for untrusted\n"
           "packages.\n"
           "\n"
           "Exit status: 0 plugin loaded, 1 initialization/load failure, 2 usage error.\n";
}

bool parse_options(int argc, char** argv, Options& options, std::string& error)
{
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            print_usage(std::cout);
            std::exit(0);
        }
        if (argument == "--json") {
            options.json = true;
            continue;
        }
        if (!argument.empty() && argument.front() == '-') {
            error = "Unknown option: " + argument;
            return false;
        }
        if (!options.plugin_path.empty()) {
            error = "Only one plugin package may be probed at a time.";
            return false;
        }
        options.plugin_path = fs::path(argument);
    }

    if (options.plugin_path.empty()) {
        error = "No plugin package given.";
        return false;
    }
    return true;
}

// Keep every write OrcaSlicer makes during the run inside a throwaway tree, and keep relative
// paths opened by plugin code out of the caller's working directory. This is hygiene, not
// confinement: an absolute path supplied by the plugin is still opened normally.
class ScopedProbeWorkspace
{
public:
    ScopedProbeWorkspace()
        : m_root(fs::temp_directory_path() / fs::unique_path("orca-plugin-probe-%%%%-%%%%-%%%%"))
        , m_previous_data_dir(data_dir())
        , m_previous_working_dir(fs::current_path())
    {
        fs::create_directories(m_root);
        set_data_dir(m_root.string());
        fs::current_path(m_root);
        m_moved = true;
    }

    ~ScopedProbeWorkspace()
    {
        boost::system::error_code ec;
        set_data_dir(m_previous_data_dir);
        if (m_moved)
            fs::current_path(m_previous_working_dir, ec);
        fs::remove_all(m_root, ec);
    }

    const fs::path& root() const { return m_root; }

private:
    fs::path m_root;
    std::string m_previous_data_dir;
    fs::path m_previous_working_dir;
    bool m_moved = false;
};

class ScopedObservationObserver
{
public:
    explicit ScopedObservationObserver(PluginProbeCollector& collector) : m_collector(collector)
    {
        PluginAuditManager::instance().set_observation_observer(
            [this](const AuditObservation& observation) { m_collector.record(observation); });
    }

    ~ScopedObservationObserver() { PluginAuditManager::instance().clear_observation_observer(); }

    ScopedObservationObserver(const ScopedObservationObserver&)            = delete;
    ScopedObservationObserver& operator=(const ScopedObservationObserver&) = delete;

private:
    PluginProbeCollector& m_collector;
};

bool prepare_descriptor(const fs::path& plugin_path, PluginDescriptor& descriptor, std::string& error)
{
    boost::system::error_code ec;
    const fs::path canonical = fs::weakly_canonical(plugin_path, ec);
    if (ec || !fs::exists(canonical) || !fs::is_regular_file(canonical)) {
        error = "Plugin package is not a readable file: " + plugin_path.string();
        return false;
    }

    std::string extension = canonical.extension().string();
    boost::algorithm::to_lower(extension);
    if (extension != ".py" && extension != ".whl") {
        error = "Plugin package must be a .py or .whl file, got: " + extension;
        return false;
    }

    if (extension == ".whl") {
        if (!read_wheel_plugin_metadata(canonical, descriptor, error))
            return false;
    } else {
        if (!read_python_plugin_metadata(canonical, descriptor, error))
            return false;
    }

    descriptor.entry_path     = canonical.string();
    descriptor.plugin_key     = make_local_plugin_key(canonical.stem().string());
    descriptor.metadata_valid = true;
    // Leaving plugin_root empty keeps the probe independent of any .install_state.json next to
    // the package and avoids the production sidecar path.
    descriptor.plugin_root.clear();
    if (descriptor.name.empty())
        descriptor.name = descriptor.plugin_key;
    return true;
}

void redirect_python_stdout()
{
    py::gil_scoped_acquire gil;
    py::object sys = py::module_::import("sys");
    // Keep plugin prints from corrupting the C++ report on stdout. sys.stderr is the interpreter's
    // tee to the temporary log, so plugin output remains available there.
    sys.attr("stdout") = sys.attr("stderr");
}

template<class Function>
void invoke_callback(const std::shared_ptr<PluginCapabilityInterface>& capability,
                     const std::string& phase,
                     const std::string& callback,
                     PluginProbeCollector& collector,
                     Function&& function)
{
    ScopedPluginAuditPhase phase_scope(phase);
    try {
        std::forward<Function>(function)();
        collector.record_callback({capability ? capability->name() : std::string{}, phase, callback, "invoked", {}});
    } catch (const std::exception& error) {
        collector.record_callback({capability ? capability->name() : std::string{}, phase, callback, "failed", error.what()});
    } catch (...) {
        collector.record_callback({capability ? capability->name() : std::string{}, phase, callback, "failed", "non-standard exception"});
    }
}

struct ProbeCapability
{
    std::string name;
    std::string type;
    bool enabled = true;
};

std::vector<ProbeCapability> describe_capabilities(const Plugin& plugin)
{
    std::vector<ProbeCapability> capabilities;
    capabilities.reserve(plugin.capabilities.size());
    for (const auto& capability : plugin.capabilities) {
        if (!capability)
            continue;
        capabilities.push_back({capability->name(), plugin_capability_type_to_string(capability->type()), capability->is_enabled()});
    }
    return capabilities;
}

void exercise_callbacks(const Plugin& plugin, PluginProbeCollector& collector)
{
    // on_load() already ran inside plugin_loader::load. The callbacks below are the additional
    // entry points a loaded capability exposes to the host. Synthetic inputs are used; a callback
    // may behave differently with a real application state, which the report cannot capture.
    static constexpr LifecycleEvent lifecycle_events[] = {
        LifecycleEvent::NewProject,          LifecycleEvent::ProjectOpened,         LifecycleEvent::ProjectBeforeSave,
        LifecycleEvent::ProjectAfterSave,    LifecycleEvent::ProjectClosed,         LifecycleEvent::ProjectDirtyChanged,
        LifecycleEvent::SliceStarted,        LifecycleEvent::SliceGeometryFinished, LifecycleEvent::GCodeExportStarted,
        LifecycleEvent::GCodeExportFinished, LifecycleEvent::SlicingJobComplete,    LifecycleEvent::ObjectAdded,
        LifecycleEvent::ObjectDeleted,       LifecycleEvent::ObjectTransformed,     LifecycleEvent::ObjectChanged,
        LifecycleEvent::ObjectRenamed,       LifecycleEvent::PlateCreated,          LifecycleEvent::PlateDeleted,
        LifecycleEvent::PlateSelected,       LifecycleEvent::PlateRenamed,          LifecycleEvent::PresetSelected,
        LifecycleEvent::PresetSaved,         LifecycleEvent::PrintStateChanged,     LifecycleEvent::DeviceOnline,
        LifecycleEvent::DeviceOffline,       LifecycleEvent::DeviceDiscovered,      LifecycleEvent::DeviceSelected,
        LifecycleEvent::UploadStarted,       LifecycleEvent::UploadFinished,        LifecycleEvent::PrintJobStarted,
        LifecycleEvent::PrintJobFinished,    LifecycleEvent::SendJobStarted,        LifecycleEvent::SendJobFinished,
    };

    for (const auto& capability : plugin.capabilities) {
        if (!capability)
            continue;

        invoke_callback(capability, "callback:get_default_config", "get_default_config", collector,
                        [&capability]() { (void) capability->get_default_config(); });

        if (capability->config_ui_available()) {
            invoke_callback(capability, "callback:get_config_ui", "get_config_ui", collector,
                            [&capability]() { (void) capability->get_config_ui(); });
        } else {
            collector.record_callback(
                {capability->name(), "callback:get_config_ui", "get_config_ui", "skipped", "has_config_ui() is false"});
        }

        for (const LifecycleEvent event : lifecycle_events) {
            const std::string event_name = lifecycle_event_to_string(event);
            const std::string phase      = "callback:on_lifecycle_event(" + event_name + ")";
            LifecycleEventContext context;
            context.source = "orca-plugin-probe";
            context.msg    = "synthetic event generated by the permission probe";
            invoke_callback(capability, phase, "on_lifecycle_event(" + event_name + ")", collector,
                            [&capability, event, &context]() { capability->on_lifecycle_event(event, context); });
        }

        // Cancellation is a load-time path; calling it after a successful load would misrepresent
        // the capability's real lifecycle. Report it as skipped instead.
        collector.record_callback(
            {capability->name(), "callback:on_cancelled", "on_cancelled", "skipped", "not invoked after a successful load"});
    }
}

nlohmann::json observation_json(const AuditObservation& observation)
{
    return {
        {"plugin", observation.plugin_key},
        {"capability", observation.capability},
        {"phase", observation.phase.empty() ? "unattributed" : observation.phase},
        {"event", observation.event_name},
        {"category", observation.category},
        {"targets", observation.targets},
        {"continued", observation.continued},
    };
}

nlohmann::json callback_json(const ProbeCallbackRecord& callback)
{
    return {
        {"capability", callback.capability}, {"phase", callback.phase},   {"callback", callback.callback},
        {"status", callback.status},         {"detail", callback.detail},
    };
}

nlohmann::json capability_json(const ProbeCapability& capability)
{
    return {
        {"name", capability.name},
        {"type", capability.type},
        {"enabled", capability.enabled},
    };
}

nlohmann::json build_report(const Options& options,
                            const PluginProbeCollector& collector,
                            const std::vector<ProbeCapability>& capabilities,
                            const std::string& plugin_key,
                            bool python_initialized,
                            const std::string& python_error,
                            bool loaded,
                            const std::string& load_error,
                            const std::string& data_dir)
{
    nlohmann::json observations = nlohmann::json::array();
    for (const AuditObservation& observation : collector.observations())
        observations.push_back(observation_json(observation));

    nlohmann::json callbacks = nlohmann::json::array();
    for (const ProbeCallbackRecord& callback : collector.callbacks())
        callbacks.push_back(callback_json(callback));

    nlohmann::json capability_list = nlohmann::json::array();
    for (const ProbeCapability& capability : capabilities)
        capability_list.push_back(capability_json(capability));

    return {
        {"tool", "orca-plugin-permission-probe"},
        {"mode", "record-only"},
        {"observed_by_this_run_only", true},
        {"plugin",
         {
             {"path", options.plugin_path.string()},
             {"key", plugin_key},
         }},
        {"environment",
         {
             {"python_initialized", python_initialized},
             {"python_error", python_error},
             {"temporary_data_dir", data_dir},
             {"temporary_data_dir_removed_on_exit", true},
             {"working_directory", data_dir},
             {"interactive_prompts", false},
             {"grants_persisted", false},
         }},
        {"load", {{"ok", loaded}, {"error", load_error}, {"capabilities", capability_list}}},
        {"callbacks", callbacks},
        {"observations", observations},
        {"summary", collector.summary()},
        {"coverage",
         {
             {"exhaustive", false},
             {"sandbox", false},
             {"plugin_code_executed", true},
             {"notes",
              {
                  "Only the code paths this probe exercised are represented. Audit events cover "
                  "particular operations, not every Python call.",
                  "The probe does not start the GUI. Host APIs that require a live application or "
                  "UI (orca.host.app/presets/ui) and plugin.storage report an explicit unsupported "
                  "error and are reported as failed callbacks.",
                  "The probe links libslic3r for core geometry/print types, but not libslic3r_gui "
                  "or wx.",
                  "The temporary data directory isolates OrcaSlicer state, not plugin-authored file, "
                  "network or process access. Use OS-level restrictions for untrusted packages.",
                  "The loader may extract adjacent .whl dependencies into the package's directory; "
                  "probe a copy of the package when that side effect is unwanted.",
                  "Import, registration and on_load share one loader phase; the production loader "
                  "does not expose a finer boundary without changing its behavior.",
                  "Callbacks run with synthetic inputs, which can change plugin control flow "
                  "compared with a real application session.",
                  "continued=true means the audit hook did not block the operation. A surrounding "
                  "callback or load step can still fail afterwards, as reported in callbacks/load.",
                  "Events raised on a plugin-spawned thread carry no phase label and are reported "
                  "as unattributed.",
                  "Plugin code can spawn threads/processes, write anywhere this process can, or "
                  "terminate the process. None of that is contained by the probe.",
              }},
         }},
    };
}

void print_text_report(const nlohmann::json& report)
{
    std::cout << "OrcaSlicer plugin permission probe (record-only)\n"
                 "WARNING: observational only; not exhaustive and not a sandbox.\n\n";
    std::cout << "Plugin: " << report["plugin"]["path"].get<std::string>() << "\n";
    std::cout << "Python initialized: " << (report["environment"]["python_initialized"].get<bool>() ? "yes" : "no") << "\n";
    if (!report["environment"]["python_error"].get<std::string>().empty())
        std::cout << "Python error: " << report["environment"]["python_error"].get<std::string>() << "\n";
    std::cout << "Load: " << (report["load"]["ok"].get<bool>() ? "ok" : "failed") << "\n";
    if (!report["load"]["error"].get<std::string>().empty())
        std::cout << "Load error: " << report["load"]["error"].get<std::string>() << "\n";

    std::cout << "\nCapabilities (" << report["load"]["capabilities"].size() << "):\n";
    for (const auto& capability : report["load"]["capabilities"])
        std::cout << "  " << capability["name"].get<std::string>() << " [" << capability["type"].get<std::string>()
                  << "] enabled=" << (capability["enabled"].get<bool>() ? "yes" : "no") << "\n";

    std::cout << "\nCallbacks (" << report["callbacks"].size() << "):\n";
    for (const auto& callback : report["callbacks"]) {
        std::cout << "  " << callback["status"].get<std::string>() << " " << callback["callback"].get<std::string>();
        if (!callback["capability"].get<std::string>().empty())
            std::cout << " capability=" << callback["capability"].get<std::string>();
        if (!callback["detail"].get<std::string>().empty())
            std::cout << " - " << callback["detail"].get<std::string>();
        std::cout << "\n";
    }

    std::cout << "\nObserved audit events (" << report["observations"].size() << "):\n";
    for (const auto& observation : report["observations"]) {
        std::cout << "  phase=" << observation["phase"].get<std::string>() << " event=" << observation["event"].get<std::string>()
                  << " category=" << observation["category"].get<std::string>();
        if (!observation["capability"].get<std::string>().empty())
            std::cout << " capability=" << observation["capability"].get<std::string>();
        if (!observation["targets"].empty()) {
            std::cout << " targets=";
            bool first = true;
            for (const auto& target : observation["targets"]) {
                if (!first)
                    std::cout << ",";
                std::cout << target.get<std::string>();
                first = false;
            }
        }
        std::cout << " continued=" << (observation["continued"].get<bool>() ? "yes" : "no") << "\n";
    }

    std::cout << "\nObserved permission categories:\n";
    for (const auto& [category, detail] : report["summary"]["categories"].items()) {
        std::cout << "  " << category << ":";
        for (const auto& event : detail["events"])
            std::cout << " " << event.get<std::string>();
        if (!detail["targets"].empty()) {
            std::cout << " -> ";
            bool first = true;
            for (const auto& target : detail["targets"]) {
                if (!first)
                    std::cout << ", ";
                std::cout << target.get<std::string>();
                first = false;
            }
        }
        std::cout << "\n";
    }
    if (report["summary"]["categories"].empty())
        std::cout << "  (none observed)\n";

    std::cout << "\nCoverage limits:\n";
    for (const auto& note : report["coverage"]["notes"])
        std::cout << "  - " << note.get<std::string>() << "\n";
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    std::string option_error;
    if (!parse_options(argc, argv, options, option_error)) {
        std::cerr << "orca-plugin-probe: " << option_error << "\n\n";
        print_usage(std::cerr);
        return 2;
    }

    boost::system::error_code ec;
    const fs::path canonical_plugin = fs::weakly_canonical(options.plugin_path, ec);
    if (!ec)
        options.plugin_path = canonical_plugin;

    ScopedProbeWorkspace workspace;
    PluginProbeCollector collector;
    ScopedObservationObserver observer(collector);

    PluginDescriptor descriptor;
    std::string descriptor_error;
    const bool descriptor_ready = prepare_descriptor(options.plugin_path, descriptor, descriptor_error);

    PythonInterpreter& interpreter = PythonInterpreter::instance();
    bool python_initialized        = false;
    std::string python_error;
    if (descriptor_ready) {
        python_initialized = interpreter.initialize();
        if (!python_initialized)
            python_error = interpreter.last_error();
    }

    if (python_initialized) {
        try {
            redirect_python_stdout();
        } catch (const std::exception& error) {
            python_error = std::string("Could not redirect Python stdout: ") + error.what();
        }
    }

    bool loaded            = false;
    std::string load_error = descriptor_error;
    Plugin plugin;
    std::vector<ProbeCapability> capabilities;

    if (python_initialized && descriptor_ready) {
        // The outer context covers import, registration and on_load in plugin_loader::load; the
        // per-callback scopes below cover the follow-up calls. The production loader is unchanged.
        ScopedPluginAuditContext audit_context(descriptor.plugin_key);
        {
            ScopedPluginAuditPhase load_phase(kLoadPhase);
            loaded = plugin_loader::load(descriptor, /*skip_deps=*/true, /*capabilities_to_enable=*/{}, nullptr, plugin, load_error);
            collector.record_callback({std::string{}, kLoadPhase, "plugin_loader::load", loaded ? "invoked" : "failed", load_error});
            collector.record_callback({std::string{}, "callback:on_load", "on_load", loaded ? "invoked" : "skipped",
                                       loaded ? "invoked by plugin_loader::load for each materialized capability" :
                                                "plugin_loader::load failed before completing on_load"});
        }

        capabilities = describe_capabilities(plugin);

        if (loaded)
            exercise_callbacks(plugin, collector);

        if (loaded) {
            ScopedPluginAuditPhase unload_phase("callback:on_unload");
            plugin_loader::unload(plugin);
            collector.record_callback({std::string{}, "callback:on_unload", "on_unload", "invoked",
                                       "invoked by plugin_loader::unload; per-capability failures are logged by the loader"});
        }
    }

    if (interpreter.is_initialized())
        interpreter.shutdown();

    const nlohmann::json report = build_report(options, collector, capabilities,
                                               descriptor.plugin_key.empty() ? make_local_plugin_key(options.plugin_path.stem().string()) :
                                                                               descriptor.plugin_key,
                                               python_initialized, python_error, loaded, load_error, workspace.root().string());

    if (options.json)
        std::cout << report.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
    else
        print_text_report(report);

    return loaded ? 0 : 1;
}
