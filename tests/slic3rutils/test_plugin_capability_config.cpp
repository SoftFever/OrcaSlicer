#include <catch2/catch_all.hpp>

#include <exception>
#include <slic3r/plugin/PluginConfig.hpp>
#include <slic3r/plugin/PluginManager.hpp>
#include <slic3r/plugin/PythonInterpreter.hpp>
#include <slic3r/plugin/PythonPluginBridge.hpp>
#include <slic3r/plugin/PythonPluginInterface.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include "plugin_test_utils.hpp"
#include <pybind11/pytypes.h>
#include <pybind11/eval.h>
#include <pybind11/gil.h>

#include <nlohmann/json.hpp>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include <chrono>
#include <fstream>
#include <ios>
#include <memory>
#include <string>
#include <pybind11/cast.h>

namespace py = pybind11;
using namespace Slic3r;
using json = nlohmann::json;

namespace {

// Brings the plugin system up for the duration of one test and tears it down at the end — same
// idiom as ScopedPluginManager in test_plugin_lifecycle.cpp, and for the same reason: shutdown()
// logs via boost::log, so it must run before boost::log tears down its thread-local storage, not be
// left to a static destructor at process exit.
//
// Needed here (unlike a bare pybind11::scoped_interpreter): has_config_ui()/get_config_ui()/
// get_name()/etc. all cross PyPluginCommonTrampoline's ORCA_PY_OVERRIDE_AUDITED, which refuses to
// call into Python unless PythonInterpreter::instance() itself reports initialized (see
// PythonGILState). A bare interpreter never sets that flag, so every trampoline call would report
// "Python interpreter is shutting down" even though Python was perfectly alive.
//
// initialize() leaves the GIL released (production code re-acquires it per call via PythonGILState);
// every TEST_CASE below pairs this with a py::gil_scoped_acquire, declared second so it releases
// before this destructor's shutdown() runs.
struct ScopedPluginManager
{
    // Before initialize(): the interpreter creates {data_dir}/python/packages and {data_dir}/log,
    // which would otherwise land in the working directory.
    ScopedDataDir python_data_dir{"plugin-python"};
    bool initialized = PluginManager::instance().initialize();

    ~ScopedPluginManager()
    {
        PluginManager::instance().shutdown();
        PythonInterpreter::instance().shutdown();
    }
};

py::module_ import_orca_module()
{
    (void) PythonPluginBridge::instance(); // force the embedded module registration into the binary
    return py::module_::import("orca");
}

// Builds a Python capability the way PluginLoader does: the audit identity is stamped on by the
// host, never supplied by the plugin, and it scopes every config call to this one capability.
py::object make_capability(const std::string& class_name,
                           const std::string& body,
                           const std::string& plugin_key,
                           const std::string& capability_name,
                           PluginCapabilityType type = PluginCapabilityType::Script)
{
    // Import first: it brings the interpreter up, and any py:: object built before it would touch a
    // Python that does not exist yet.
    py::module_ orca = import_orca_module();

    py::dict globals;
    globals["orca"] = orca;

    py::exec("class " + class_name + "(orca.PythonPluginBase):\n" + body, globals);
    py::object instance = globals[class_name.c_str()]();

    if (!plugin_key.empty()) {
        auto iface = instance.cast<std::shared_ptr<PluginCapabilityInterface>>();
        iface->set_audit_plugin_key(plugin_key);
        iface->set_resolved_identity(capability_name, type);
    }
    return instance;
}

std::shared_ptr<PluginCapabilityInterface> as_interface(const py::object& instance)
{
    return instance.cast<std::shared_ptr<PluginCapabilityInterface>>();
}

// The Python API writes through the PluginManager singleton, so that is where assertions read from.
PluginConfig& host_config() { return PluginManager::instance().get_config(); }

// The Python config API speaks JSON text, not dicts; these helpers keep the tests in terms of values.
json py_get_config(const py::object& cap) { return json::parse(cap.attr("get_config")().cast<std::string>()); }

bool py_save_config(const py::object& cap, const json& value) { return cap.attr("save_config")(value.dump()).cast<bool>(); }

PluginCapabilityId capability_id(PluginCapabilityType type, const char* name, const char* plugin_key)
{
    return {type, name, plugin_key};
}

// restore_config_response looks the capability up in PluginManager, so the restore tests load a real
// plugin rather than building a detached capability.
const char* const RESTORE_PLUGIN_KEY = "Restore_Plugin";

const char* const RESTORE_PLUGIN_SOURCE = R"PY(# /// script
# requires-python = ">=3.12"
#
# [tool.orcaslicer.plugin]
# name = "Restore Plugin"
# description = "Restore defaults fixture"
# author = "OrcaSlicer"
# version = "1.0"
# type = "script"
# ///
import orca

class Target(orca.script.ScriptPluginCapabilityBase):
    def get_name(self): return "Target"
    def execute(self, ctx): return orca.ExecutionResult.success()
    def get_default_config(self): return {"speed": 1}

class Bystander(orca.script.ScriptPluginCapabilityBase):
    def get_name(self): return "Bystander"
    def execute(self, ctx): return orca.ExecutionResult.success()
    def get_default_config(self): return {"speed": 2}

# Loading seeds defaults through get_default_config and fails the plugin if it raises, so this one
# answers the load and raises from then on.
class Raising(orca.script.ScriptPluginCapabilityBase):
    calls = 0
    def get_name(self): return "Raising"
    def execute(self, ctx): return orca.ExecutionResult.success()
    def get_default_config(self):
        Raising.calls += 1
        if Raising.calls > 1:
            raise RuntimeError("boom")
        return {}

@orca.plugin
class RestorePackage(orca.base):
    def register_capabilities(self):
        orca.register_capability(Target)
        orca.register_capability(Bystander)
        orca.register_capability(Raising)
)PY";

// Writes the restore fixture into {data_dir}/orca_plugins and loads it, returning the load error
// (empty on success). Call without holding the GIL: the load runs on a worker thread.
std::string load_restore_plugin(const ScopedDataDir& data_dir_guard)
{
    const boost::filesystem::path plugin_dir = data_dir_guard.plugins_dir() / RESTORE_PLUGIN_KEY;
    boost::filesystem::create_directories(plugin_dir);
    {
        std::ofstream out((plugin_dir / (std::string(RESTORE_PLUGIN_KEY) + ".py")).string(), std::ios::binary);
        out << RESTORE_PLUGIN_SOURCE;
    }

    PluginManager& manager = PluginManager::instance();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);
    manager.load_plugin(RESTORE_PLUGIN_KEY, /*skip_deps=*/true);
    std::string error;
    if (!manager.wait_for_plugin_load(RESTORE_PLUGIN_KEY, std::chrono::seconds(120), error) && error.empty())
        error = "the plugin did not load";
    return error;
}

} // namespace

TEST_CASE("Capability config API is exposed on every Python capability", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system; // declared first: destroyed last
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil; // released before plugin_system's destructor shuts Python down

    py::module_ orca = import_orca_module();
    REQUIRE(py::hasattr(orca, "PythonPluginBase"));

    py::object base = orca.attr("PythonPluginBase");
    // Host-provided: every capability has a config, so there is no hook to opt out of being
    // configurable.
    CHECK(py::hasattr(base, "get_config"));
    CHECK(py::hasattr(base, "save_config"));
    CHECK(py::hasattr(base, "get_config_version"));
    // Plugin-provided (the host calls these). All optional.
    CHECK(py::hasattr(base, "has_config_ui"));
    CHECK(py::hasattr(base, "get_config_ui"));
    CHECK(py::hasattr(base, "get_default_config"));

    // Config is reached only through the capability, never as a free orca.config.* function, so a
    // capability cannot name — and cannot touch — a config that is not its own.
    CHECK_FALSE(py::hasattr(orca, "config"));
}

TEST_CASE("get_config returns only cap_config and save_config persists it", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system; // declared first: destroyed last
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil; // released before plugin_system's destructor shuts Python down

    ScopedDataDir data_dir_guard("plugin-config-py-roundtrip");
    host_config().load(); // reset the singleton's in-memory store against the empty temp dir

    py::object cap = make_capability("RoundTripCap", "    def get_name(self): return 'cap_a'\n", "plugin_a", "cap_a");

    // Nothing stored yet: the JSON text of an empty object, not None, so a plugin can json.loads() it
    // unconditionally.
    py::object initial = cap.attr("get_config")();
    REQUIRE(py::isinstance<py::str>(initial));
    CHECK(json::parse(initial.cast<std::string>()) == json::object());
    CHECK(cap.attr("get_config_version")().cast<std::string>().empty());

    REQUIRE(py_save_config(cap, json{{"speed", 5}, {"name", "fast"}}));

    const auto stored = host_config().get_config(capability_id(PluginCapabilityType::Script, "cap_a", "plugin_a"));
    REQUIRE(stored);
    CHECK(stored->id == capability_id(PluginCapabilityType::Script, "cap_a", "plugin_a"));
    CHECK(stored->config == json{{"speed", 5}, {"name", "fast"}});

    // Python reads back exactly cap_config — no host metadata.
    const json reloaded = py_get_config(cap);
    CHECK(reloaded.size() == 2);
    CHECK(reloaded.contains("speed"));
    CHECK_FALSE(reloaded.contains("plugin_key"));
    CHECK_FALSE(reloaded.contains("capability"));
    CHECK_FALSE(reloaded.contains("cap_config"));
    CHECK_FALSE(reloaded.contains("plugin_version"));
}

TEST_CASE("save_config rejects a string that is not valid JSON", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system; // declared first: destroyed last
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil; // released before plugin_system's destructor shuts Python down

    ScopedDataDir data_dir_guard("plugin-config-py-badjson");
    host_config().load();

    py::object cap = make_capability("BadJsonCap", "    def get_name(self): return 'cap_a'\n", "plugin_a", "cap_a");

    REQUIRE(py_save_config(cap, json{{"keep", "me"}}));

    // Refusing unparseable text must leave the previously stored config alone.
    CHECK_FALSE(cap.attr("save_config")("{not json").cast<bool>());
    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Script, "cap_a", "plugin_a"))->config == json{{"keep", "me"}});
}

TEST_CASE("Saving one capability's config does not touch another's", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system; // declared first: destroyed last
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil; // released before plugin_system's destructor shuts Python down

    ScopedDataDir data_dir_guard("plugin-config-py-isolation");
    host_config().load();

    const std::string body = "    def get_name(self): return 'cap'\n";
    // Same capability name under two plugins, plus a second capability of plugin_a: each addresses
    // only the entry matching its own stamped identity.
    py::object a_cap1 = make_capability("IsoCapA1", body, "plugin_a", "cap_a");
    py::object a_cap2 = make_capability("IsoCapA2", body, "plugin_a", "cap_b");
    py::object b_cap1 = make_capability("IsoCapB1", body, "plugin_b", "cap_a");

    REQUIRE(py_save_config(a_cap1, json{{"value", 1}}));
    REQUIRE(py_save_config(a_cap2, json{{"value", 2}}));
    REQUIRE(py_save_config(b_cap1, json{{"value", 3}}));

    REQUIRE(py_save_config(a_cap1, json{{"value", 99}}));

    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Script, "cap_a", "plugin_a"))->config == json{{"value", 99}});
    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Script, "cap_b", "plugin_a"))->config == json{{"value", 2}});
    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Script, "cap_a", "plugin_b"))->config == json{{"value", 3}});

    // A shared name under one plugin must remain isolated when the capability type differs.
    py::object importer = make_capability("IsoCapImporter", body, "plugin_a", "cap_a", PluginCapabilityType::Importer);
    REQUIRE(py_save_config(importer, json{{"value", 4}}));
    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Script, "cap_a", "plugin_a"))->config == json{{"value", 99}});
    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Importer, "cap_a", "plugin_a"))->config == json{{"value", 4}});

    host_config().load();
    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Script, "cap_a", "plugin_a"))->config == json{{"value", 99}});
    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Importer, "cap_a", "plugin_a"))->config == json{{"value", 4}});

    CHECK(py_get_config(a_cap2).at("value") == 2);
    CHECK(py_get_config(b_cap1).at("value") == 3);
}

TEST_CASE("Config API refuses a capability the host never materialized", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system; // declared first: destroyed last
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil; // released before plugin_system's destructor shuts Python down

    ScopedDataDir data_dir_guard("plugin-config-py-unowned");
    host_config().load();

    // No audit identity: never loaded by the host, so it has no config to address. Refused rather
    // than served from, or written to, some arbitrary entry.
    py::object orphan = make_capability("OrphanCap", "    def get_name(self): return 'cap'\n", "", "");

    CHECK_THROWS(orphan.attr("get_config")());
    CHECK_THROWS(orphan.attr("get_config_version")());
    CHECK_THROWS(orphan.attr("save_config")(json::object().dump()));
}

TEST_CASE("Custom config UI hooks dispatch to the Python override", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system; // declared first: destroyed last
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil; // released before plugin_system's destructor shuts Python down

    py::object cap = make_capability("CustomUiCap",
                                     "    def get_name(self): return 'cap_a'\n"
                                     "    def has_config_ui(self): return True\n"
                                     "    def get_config_ui(self): return '<p>hello</p>'\n",
                                     "plugin_a", "cap_a");

    auto iface = as_interface(cap);
    REQUIRE(iface);
    CHECK(iface->has_config_ui());
    CHECK(iface->get_config_ui() == "<p>hello</p>");
}

TEST_CASE("A capability that omits the config UI hooks gets the default editor", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system; // declared first: destroyed last
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil; // released before plugin_system's destructor shuts Python down

    ScopedDataDir data_dir_guard("plugin-config-py-bare");
    host_config().load();

    // Both hooks are optional and only choose the editor: a capability that overrides neither is
    // still configurable, it just gets the host's JSON editor. There is no way to opt out.
    py::object bare = make_capability("BareCap", "    def get_name(self): return 'cap_a'\n", "plugin_a", "cap_a");

    auto iface = as_interface(bare);
    REQUIRE(iface);
    CHECK_FALSE(iface->has_config_ui()); // -> default JSON editor
    CHECK(iface->get_config_ui().empty());

    REQUIRE(py_save_config(bare, json{{"speed", 5}}));
    CHECK(host_config().get_config(capability_id(PluginCapabilityType::Script, "cap_a", "plugin_a"))->config == json{{"speed", 5}});
}

TEST_CASE("get_default_config supplies the value Restore defaults writes back", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system; // declared first: destroyed last
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil; // released before plugin_system's destructor shuts Python down

    SECTION("not overridden -> an empty config")
    {
        // Already "restore defaults" for a capability that keeps its stored config sparse and applies
        // its own defaults on read.
        py::object bare = make_capability("NoDefaultsCap", "    def get_name(self): return 'cap_a'\n", "plugin_a", "cap_a");

        auto iface = as_interface(bare);
        REQUIRE(iface);
        CHECK(iface->get_default_config() == json::object());
    }

    SECTION("overridden -> exactly what the plugin returns")
    {
        py::object cap = make_capability("DefaultsCap",
                                         "    def get_name(self): return 'cap_a'\n"
                                         "    def get_default_config(self):\n"
                                         "        return {'speed': 5, 'nested': {'on': True}, 'items': [1, 2]}\n",
                                         "plugin_a", "cap_a");

        auto iface = as_interface(cap);
        REQUIRE(iface);
        // Round-trips through py_to_json untouched: the host does not reshape or validate it.
        CHECK(iface->get_default_config() == json{{"speed", 5}, {"nested", {{"on", true}}}, {"items", {1, 2}}});
    }

    SECTION("overridden but returns None -> an empty config, never a null")
    {
        // `def get_default_config(self): pass` is the easy mistake, and it must not store
        // "cap_config": null.
        py::object cap = make_capability("NoneDefaultsCap",
                                         "    def get_name(self): return 'cap_a'\n"
                                         "    def get_default_config(self): pass\n",
                                         "plugin_a", "cap_a");

        auto iface = as_interface(cap);
        REQUIRE(iface);

        const json restored = iface->get_default_config();
        CHECK(restored == json::object());
        CHECK_FALSE(restored.is_null());
    }

    SECTION("overridden but returns a non-object -> an empty config")
    {
        py::object cap = make_capability("ScalarDefaultsCap",
                                         "    def get_name(self): return 'cap_a'\n"
                                         "    def get_default_config(self): return [1, 2, 3]\n",
                                         "plugin_a", "cap_a");

        auto iface = as_interface(cap);
        REQUIRE(iface);
        CHECK(iface->get_default_config() == json::object());
    }
}

TEST_CASE("Restoring defaults overwrites only the target capability", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system; // declared first: destroyed last
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    ScopedDataDir data_dir_guard("plugin-config-py-restore");
    host_config().load();
    const std::string load_error = load_restore_plugin(data_dir_guard);
    INFO("load error: " << load_error);
    REQUIRE(load_error.empty());

    const PluginCapabilityId target    = capability_id(PluginCapabilityType::Script, "Target", RESTORE_PLUGIN_KEY);
    const PluginCapabilityId bystander = capability_id(PluginCapabilityType::Script, "Bystander", RESTORE_PLUGIN_KEY);
    const json edited = json{{"speed", 99}};
    REQUIRE(host_config().store_capability_config(target, edited));
    REQUIRE(host_config().store_capability_config(bystander, edited));

    const json response = PluginConfig::restore_config_response(target);

    CHECK(response["ok"] == true);
    CHECK(response["config"] == json{{"speed", 1}});
    CHECK(host_config().get_config(target)->config == json{{"speed", 1}});
    CHECK(host_config().get_config(bystander)->config == edited);

    PluginManager::instance().unload_plugin(RESTORE_PLUGIN_KEY);
}

TEST_CASE("A raising get_default_config leaves the stored config untouched", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system; // declared first: destroyed last
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    ScopedDataDir data_dir_guard("plugin-config-py-restore-raise");
    host_config().load();
    const std::string load_error = load_restore_plugin(data_dir_guard);
    INFO("load error: " << load_error);
    REQUIRE(load_error.empty());

    const PluginCapabilityId raising = capability_id(PluginCapabilityType::Script, "Raising", RESTORE_PLUGIN_KEY);
    REQUIRE(host_config().store_capability_config(raising, json{{"keep", "me"}}));

    const json response = PluginConfig::restore_config_response(raising);

    CHECK(response["ok"] == false);
    CHECK_FALSE(response["error"].get<std::string>().empty());
    CHECK(host_config().get_config(raising)->config == json{{"keep", "me"}});

    PluginManager::instance().unload_plugin(RESTORE_PLUGIN_KEY);
}

TEST_CASE("A raising config UI hook surfaces as an exception the host can catch", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system; // declared first: destroyed last
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil; // released before plugin_system's destructor shuts Python down

    py::object cap = make_capability("RaisingCap",
                                     "    def get_name(self): return 'cap_a'\n"
                                     "    def has_config_ui(self): return True\n"
                                     "    def get_config_ui(self): raise RuntimeError('boom')\n",
                                     "plugin_a", "cap_a");

    auto iface = as_interface(cap);
    REQUIRE(iface);

    // The trampoline rethrows; callers catch it and fall back to the default JSON editor.
    CHECK_THROWS_AS(iface->get_config_ui(), py::error_already_set);

    // Catching it leaves the interpreter usable.
    CHECK(iface->get_name() == "cap_a");
}

TEST_CASE("A config UI hook returning the wrong type does not crash the host", "[PluginConfig][Python]")
{
    ScopedPluginManager plugin_system; // declared first: destroyed last
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    py::gil_scoped_acquire gil; // released before plugin_system's destructor shuts Python down

    // has_config_ui() is plugin-authored, so it can return anything; the host must survive the call.
    py::object cap = make_capability("BadTypeCap",
                                     "    def get_name(self): return 'cap_a'\n"
                                     "    def has_config_ui(self): return 'not a bool'\n",
                                     "plugin_a", "cap_a");

    auto iface = as_interface(cap);
    REQUIRE(iface);

    // Deliberately not REQUIRE_THROWS: pybind may coerce or reject the value, and both are fine.
    // What must hold is that the call is survivable — PluginLoader's guard turns a throw into
    // "no custom UI".
    try {
        (void) iface->has_config_ui();
    } catch (const std::exception&) {
    }

    // The capability is still usable afterwards.
    CHECK(iface->get_name() == "cap_a");
    CHECK(iface->get_config_ui().empty());
}
