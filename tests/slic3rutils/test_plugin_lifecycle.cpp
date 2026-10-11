#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <catch2/catch_all.hpp>

#include <ios>
#include <libslic3r/LifecycleEvents.hpp>
#include <libslic3r/GCode/GCodeProcessor.hpp>
#include <memory>
#include <slic3r/Utils/NetworkAgentFactory.hpp>
#include <slic3r/plugin/PluginAuditManager.hpp>
#include <slic3r/plugin/PluginDescriptor.hpp>
#include <slic3r/plugin/PluginManager.hpp>
#include <slic3r/plugin/PluginFsUtils.hpp>
#include <slic3r/plugin/PythonInterpreter.hpp>
#include <slic3r/plugin/host/PluginVisualizations.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include "plugin_test_utils.hpp"
#include "test_utils.hpp"

#include <boost/filesystem.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iterator>
#include <mutex>
#include "slic3r/plugin/PythonPluginInterface.hpp"
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <wx/app.h>
#include <wx/init.h>
#include <wx/thread.h>

namespace Slic3r {

// Exercise the real worker and queued dispatch without constructing an editor or an OpenGL context.
struct PluginVisualizationsTest
{
    using Request = PluginVisualizations::Request;
    using PendingResource = PluginVisualizations::PendingResource;
    PluginVisualizations host;

    void enqueue(Request request) { host.enqueue(std::move(request)); }
    bool has_queued_requests() const
    {
        std::lock_guard<std::mutex> lock(host.m_mutex);
        return !host.m_requests.empty();
    }
    bool worker_joinable() const { return host.m_worker.joinable(); }
    void dispatch(Request request)
    {
        {
            std::lock_guard<std::mutex> lock(host.m_mutex);
            request.generation = ++host.m_generations[request.id];
        }
        host.dispatch_request(std::move(request));
    }
};

} // namespace Slic3r

using namespace Slic3r;
namespace fs = boost::filesystem;

// Plugin load/unload lifecycle: discovery -> load -> capability materialization -> enable/disable
// -> unload.
//
// Each Catch2 test case runs in its own process (catch_discover_tests), so the PluginManager and
// interpreter singletons are brought up at most once per test.

namespace {

struct ScopedWxConsole
{
    bool initialized;

    ScopedWxConsole()
    {
        wxApp::SetInstance(new wxAppConsole);
        initialized = wxInitialize();
    }
    ~ScopedWxConsole()
    {
        if (initialized)
            wxUninitialize();
    }
};

// Brings the plugin system up, and tears it down explicitly at the end of the test.
//
// Shutting the interpreter down here, rather than leaving it to PythonInterpreter's static
// destructor, mirrors what the app does (GUI_App finalizes it before exit). Left to static
// destruction, shutdown()'s logging runs after boost::log has torn down its thread-local storage
// and throws, aborting the process after the tests have already passed.
//
// Declare this FIRST in a test so it is destroyed last.
struct ScopedPluginManager
{
    // Before initialize(): the interpreter creates {data_dir}/python/packages and {data_dir}/log,
    // which would otherwise land in the working directory.
    ScopedDataDir python_data_dir{"plugin-python"};
    bool initialized = false;

    ScopedPluginManager() { initialized = PluginManager::instance().initialize(); }
    ~ScopedPluginManager()
    {
        PluginManager::instance().shutdown();
        PythonInterpreter::instance().shutdown();
    }
};

class ConcurrentVisualization final : public VisualizationPluginCapability
{
public:
    std::string get_name() const override { return "Concurrent Visualization"; }
    std::vector<VisualizationInputSpec> get_supported_inputs() override
    {
        return {{VisualizationInputs::TOOLPATH, VisualizationInputs::GLTF_BINARY,
                 VisualizationInputs::FILE_TRANSPORT, 2, 0, 2, 0}};
    }
    ExecutionResult open(const VisualizationContext&) override
    {
        ++open_calls;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return ExecutionResult::success();
    }
    ExecutionResult update(const VisualizationContext&) override { return ExecutionResult::success(); }
    void close() override { ++close_calls; }

    std::atomic<int> open_calls{0};
    int close_calls{0};
};

class MultiResourceVisualization final : public VisualizationPluginCapability
{
public:
    std::string get_name() const override { return "Multi-resource Visualization"; }
    std::vector<VisualizationInputSpec> get_supported_inputs() override
    {
        return {{VisualizationInputs::TOOLPATH, VisualizationInputs::GLTF_BINARY,
                 VisualizationInputs::FILE_TRANSPORT, 2, 0, 2, 0},
                {VisualizationInputs::MODEL, VisualizationInputs::OBJ,
                 VisualizationInputs::FILE_TRANSPORT, 1, 0, 1, 0}};
    }
    std::vector<VisualizationResourceRequest> get_requested_resources() override
    {
        return {{VisualizationInputs::TOOLPATH, VisualizationInputs::CURRENT_PLATE},
                {VisualizationInputs::MODEL, VisualizationInputs::PROJECT}};
    }
    ExecutionResult open(const VisualizationContext& context) override
    {
        opened = context;
        return ExecutionResult::success();
    }
    ExecutionResult update(const VisualizationContext&) override { return ExecutionResult::success(); }

    VisualizationContext opened;
};

// A minimal script plugin exposing exactly one capability, "Echo".
const char* const ECHO_PLUGIN_SOURCE = R"PY(# /// script
# requires-python = ">=3.12"
#
# [tool.orcaslicer.plugin]
# name = "Echo Plugin"
# description = "Plugin lifecycle characterization fixture"
# author = "OrcaSlicer"
# version = "1.0"
# type = "script"
# ///
import orca

class Echo(orca.script.ScriptPluginCapabilityBase):
    def get_name(self):
        return "Echo"

    def execute(self, ctx):
        return orca.ExecutionResult.success()

@orca.plugin
class EchoPackage(orca.base):
    def register_capabilities(self):
        orca.register_capability(Echo)
)PY";

const char* const VISUALIZATION_PLUGIN_SOURCE = R"PY(# /// script
# requires-python = ">=3.12"
#
# [tool.orcaslicer.plugin]
# name = "Visualization Plugin"
# version = "1.0"
# type = "script"
# ///
from pathlib import Path

import orca

class DummyVisualization(orca.visualization.VisualizationPluginCapabilityBase):
    def get_name(self):
        return "Dummy Visualization"

    def get_supported_inputs(self):
        toolpath = orca.visualization.VisualizationInputSpec()
        toolpath.kind = orca.visualization.INPUT_TOOLPATH
        toolpath.format = orca.visualization.FORMAT_GLTF_BINARY
        toolpath.transport = orca.visualization.TRANSPORT_FILE
        toolpath.minimum_major = 2
        toolpath.maximum_major = 2
        model = orca.visualization.VisualizationInputSpec(
            orca.visualization.INPUT_MODEL,
            orca.visualization.FORMAT_OBJ,
        )
        model.minimum_major = 1
        model.maximum_major = 1
        return [toolpath, model]

    def get_requested_resources(self):
        return [
            orca.visualization.VisualizationResourceRequest(
                orca.visualization.INPUT_TOOLPATH,
                orca.visualization.SCOPE_CURRENT_PLATE,
            ),
            orca.visualization.VisualizationResourceRequest(
                orca.visualization.INPUT_MODEL,
                orca.visualization.SCOPE_PROJECT,
            ),
        ]

    def _record(self, event):
        with open(self.events_path, "a", encoding="utf-8") as output:
            output.write(event + "\n")

    def open(self, ctx):
        self.events_path = Path(ctx.resources[0].location).with_name("events.txt")
        try:
            ctx.resources[0].location = "plugin-controlled"
            mutability = "mutable"
        except AttributeError:
            mutability = "readonly"
        self._record(
            f"open:{ctx.revision}:{ctx.metadata['plate_index']}:{ctx.resources[0].kind}:{ctx.resources[0].format}:"
            f"{ctx.resources[0].major_version}.{ctx.resources[0].minor_version}:{len(ctx.resources)}:"
            f"{ctx.resources[0].scope}:{ctx.orca_version}:{mutability}"
        )
        return orca.ExecutionResult.success()

    def update(self, ctx):
        if ctx.revision == 99:
            raise RuntimeError("dummy update failure")
        if ctx.revision == 100:
            return orca.ExecutionResult.failure(orca.PluginResult.FatalError, "dummy fatal failure")
        self._record(f"update:{ctx.revision}")
        return orca.ExecutionResult.success()

    def close(self):
        self._record("close")

@orca.plugin
class VisualizationPackage(orca.base):
    def register_capabilities(self):
        orca.register_capability(DummyVisualization)
)PY";

const char* const SECOND_VISUALIZATION_PLUGIN_SOURCE = R"PY(# /// script
# requires-python = ">=3.12"
#
# [tool.orcaslicer.plugin]
# name = "Second Visualization Plugin"
# version = "1.0"
# type = "script"
# ///
import orca

class SecondVisualization(orca.visualization.VisualizationPluginCapabilityBase):
    def get_name(self):
        return "Second Visualization"

    def get_supported_inputs(self):
        spec = orca.visualization.VisualizationInputSpec()
        spec.kind = orca.visualization.INPUT_TOOLPATH
        spec.format = orca.visualization.FORMAT_GLTF_BINARY
        spec.transport = orca.visualization.TRANSPORT_FILE
        spec.minimum_major = 2
        spec.maximum_major = 2
        return [spec]

    def open(self, ctx):
        return orca.ExecutionResult.success()

    def update(self, ctx):
        return orca.ExecutionResult.success()

@orca.plugin
class SecondVisualizationPackage(orca.base):
    def register_capabilities(self):
        orca.register_capability(SecondVisualization)
)PY";

// A minimal printer-agent plugin exposing exactly one PrinterConnection capability.
const char* const PRINTER_AGENT_PLUGIN_SOURCE = R"PY(# /// script
# requires-python = ">=3.12"
# dependencies = []
#
# [tool.orcaslicer.plugin]
# name = "Lifecycle Test Agent"
# description = "Minimal printer-agent plugin for the lifecycle test."
# author = "tests"
# version = "1.0.0"
# type = "printer-connection"
# ///
import orca


class LifecycleTestAgentCapability(orca.printer_agent.PrinterAgentBase):
    def get_name(self):
        return "Lifecycle Test Agent"

    def get_agent_info(self):
        return orca.printer_agent.AgentInfo(
            id="lifecycle-test-agent",
            name="Lifecycle Test Agent",
            version="1.0.0",
            description="Lifecycle test printer agent",
        )


@orca.plugin
class LifecycleTestPlugin(orca.base):
    def register_capabilities(self):
        orca.register_capability(LifecycleTestAgentCapability)
)PY";

// In production GUI_App::init_plugin_gui_wiring subscribes the agent-registry
// callbacks. The test binary has no GUI, so install the same unload-side wiring
// once so these tests exercise the production deregister-on-unload path.
//
// The load-side (register) wiring is deliberately not installed: duplicate-id
// coverage registers agents manually in a deterministic order instead.
void install_agent_registry_wiring()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;

    PluginManager& manager = PluginManager::instance();
    manager.subscribe_on_unload_callback(NetworkAgentFactory::deregister_python_plugin);
    manager.subscribe_on_capability_unload_callback([](const PluginCapabilityId& capability) {
        if (capability.type == PluginCapabilityType::PrinterConnection)
            NetworkAgentFactory::deregister_python_printer_agent(capability.plugin_key, capability.name);
    });
}

// Duplicate-id coverage needs two distinct plugins with different package keys
// and classes while both return the same AgentInfo.id.
std::string make_agent_plugin_source(const std::string& suffix,
                                     const std::string& display_name,
                                     const std::string& agent_id)
{
    return std::string{}
        + "# /// script\n"
        + "# requires-python = \">=3.12\"\n"
        + "# dependencies = []\n"
        + "#\n"
        + "# [tool.orcaslicer.plugin]\n"
        + "# name = \"" + display_name + "\"\n"
        + "# description = \"Duplicate-id fake printer-agent plugin.\"\n"
        + "# author = \"tests\"\n"
        + "# version = \"1.0.0\"\n"
        + "# type = \"printer-connection\"\n"
        + "# ///\n"
        + "import orca\n"
        + "\n\n"
        + "class Cap" + suffix + "(orca.printer_agent.PrinterAgentBase):\n"
        + "    def get_name(self):\n"
        + "        return \"" + display_name + "\"\n"
        + "\n"
        + "    def get_agent_info(self):\n"
        + "        return orca.printer_agent.AgentInfo(\n"
        + "            id=\"" + agent_id + "\", name=\"" + display_name + "\",\n"
        + "            version=\"1.0.0\", description=\"duplicate id test\")\n"
        + "\n\n"
        + "@orca.plugin\n"
        + "class Plugin" + suffix + "(orca.base):\n"
        + "    def register_capabilities(self):\n"
        + "        orca.register_capability(Cap" + suffix + ")\n";
}

// Writes {data_dir}/orca_plugins/<stem>/<stem>.py and returns the plugin directory.
fs::path write_plugin(const ScopedDataDir& data_dir_guard, const std::string& stem, const std::string& source)
{
    const fs::path plugin_dir = data_dir_guard.plugins_dir() / stem;
    fs::create_directories(plugin_dir);

    std::ofstream out((plugin_dir / (stem + ".py")).string(), std::ios::binary);
    out << source;
    out.close();

    return plugin_dir;
}

// install_hook() grants data_dir() as it stood when the plugin system came up. A test data dir set
// afterwards needs the same grant so its plugins may write to their storage folders, as in the app.
void allow_plugin_storage(const ScopedDataDir& data_dir_guard)
{
    PluginAuditManager::instance().add_global_allowed_root(data_dir_guard.dir);
}

// Loads a plugin and blocks until the detached worker thread is done with it.
bool load_and_wait(PluginManager&           manager,
                   const std::string&       plugin_key,
                   std::string&             error,
                   std::vector<std::string> capabilities_to_enable = {})
{
    manager.load_plugin(plugin_key, /*skip_deps=*/true, std::move(capabilities_to_enable));
    return manager.wait_for_plugin_load(plugin_key, std::chrono::seconds(120), error);
}

std::shared_ptr<PluginCapabilityInterface> find_capability(PluginManager& manager, const std::string& plugin_key,
                                                           const std::string& name)
{ return manager.get_plugin_capability({PluginCapabilityType::Unknown, name, plugin_key}, /*only_enabled=*/false); }

std::vector<std::shared_ptr<PluginCapabilityInterface>> capabilities_of(PluginManager& manager, const std::string& plugin_key)
{
    return manager.get_plugin_capabilities(plugin_key, PluginCapabilityType::Unknown, /*only_enabled=*/false);
}

PluginDescriptor descriptor_of(PluginManager& manager, const std::string& plugin_key)
{
    PluginDescriptor descriptor;
    manager.try_get_plugin_descriptor(plugin_key, descriptor);
    return descriptor;
}

} // namespace

TEST_CASE("Lifecycle hook shutdown drains concurrent dispatch", "[PluginLifecycle][LifecycleEvents]")
{
    std::mutex              mutex;
    std::condition_variable cv;
    bool                    callback_entered = false;
    bool                    release_callback = false;
    bool                    shutdown_started = false;
    bool                    shutdown_finished = false;

    set_lifecycle_hook_fn([&](LifecycleEvent, const LifecycleEventContext&) {
        std::unique_lock<std::mutex> lock(mutex);
        callback_entered = true;
        cv.notify_all();
        cv.wait(lock, [&] { return release_callback; });
    });

    std::thread dispatch_thread([] {
        LifecycleEventContext ctx;
        fire_lifecycle_event(LifecycleEvent::ProjectOpened, ctx);
    });

    bool callback_was_entered = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        callback_was_entered = cv.wait_for(lock, std::chrono::seconds(5), [&] { return callback_entered; });
    }
    if (!callback_was_entered) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            release_callback = true;
            cv.notify_all();
        }
        dispatch_thread.join();
        set_lifecycle_hook_fn(nullptr);
        FAIL("Lifecycle callback did not start");
    }

    std::thread shutdown_thread([&] {
        {
            std::lock_guard<std::mutex> lock(mutex);
            shutdown_started = true;
            cv.notify_all();
        }
        set_lifecycle_hook_fn(nullptr);
        std::lock_guard<std::mutex> lock(mutex);
        shutdown_finished = true;
        cv.notify_all();
    });

    bool shutdown_was_started = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        shutdown_was_started = cv.wait_for(lock, std::chrono::seconds(5), [&] { return shutdown_started; });
        if (shutdown_was_started)
            CHECK_FALSE(cv.wait_for(lock, std::chrono::milliseconds(100), [&] { return shutdown_finished; }));
        release_callback = true;
        cv.notify_all();
    }

    dispatch_thread.join();
    shutdown_thread.join();
    if (!shutdown_was_started)
        FAIL("Lifecycle hook shutdown did not start");
    CHECK(shutdown_finished);
}

TEST_CASE("A discovered script plugin loads and materializes its capability", "[PluginLifecycle][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    ScopedDataDir data_dir_guard("lifecycle-load");
    write_plugin(data_dir_guard, "Echo_Plugin", ECHO_PLUGIN_SOURCE);

    PluginManager& manager = PluginManager::instance();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);

    PluginDescriptor descriptor;
    REQUIRE(manager.try_get_valid_plugin_descriptor("Echo_Plugin", descriptor));
    CHECK(descriptor.name == "Echo Plugin");

    std::string error;
    REQUIRE(load_and_wait(manager, "Echo_Plugin", error));
    INFO("load error: " << error);
    CHECK(error.empty());

    CHECK(manager.is_plugin_loaded("Echo_Plugin"));
    CHECK(manager.get_plugin_load_error("Echo_Plugin").empty());

    const auto capabilities = capabilities_of(manager, "Echo_Plugin");
    REQUIRE(capabilities.size() == 1);

    const auto& echo = capabilities.front();
    CHECK(echo->name() == "Echo");
    CHECK(echo->type() == PluginCapabilityType::Script);
    CHECK(echo->is_enabled());
    CHECK(echo->audit_plugin_key() == "Echo_Plugin");

    CHECK(manager.get_plugin_capability({PluginCapabilityType::Script, "Echo", "Echo_Plugin"}) == echo);

    manager.unload_plugin("Echo_Plugin");
}

TEST_CASE("Concurrent visualization opens create one session", "[PluginLifecycle][Visualization]")
{
    auto capability = std::make_shared<ConcurrentVisualization>();
    capability->set_resolved_identity("Concurrent Visualization", PluginCapabilityType::Visualization);
    capability->set_audit_plugin_key("concurrent-visualization");
    capability->resolve_type_metadata();

    PluginVisualizationsTest visualizations;
    VisualizationContext context;
    context.revision = 1;
    context.resources = {{VisualizationInputs::TOOLPATH, VisualizationInputs::GLTF_BINARY,
                          VisualizationInputs::FILE_TRANSPORT, "snapshot.glb", 2, 0}};

    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::array<ExecutionResult, 2> results;
    std::array<std::thread, 2> callers;
    for (size_t index = 0; index < callers.size(); ++index) {
        callers[index] = std::thread([&, index] {
            ++ready;
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            results[index] = visualizations.host.open(capability, context);
        });
    }
    while (ready.load(std::memory_order_acquire) != int(callers.size()))
        std::this_thread::yield();
    start.store(true, std::memory_order_release);
    for (std::thread& caller : callers)
        caller.join();

    const int successes = std::count_if(results.begin(), results.end(), [](const ExecutionResult& result) {
        return result.status == PluginResult::Success;
    });
    const int skipped = std::count_if(results.begin(), results.end(), [](const ExecutionResult& result) {
        return result.status == PluginResult::Skipped;
    });
    CHECK(successes == 1);
    CHECK(skipped == 1);
    CHECK(capability->open_calls == 1);
    visualizations.host.close(capability->identity());
}

TEST_CASE("Visualization worker failures complete on the main thread and remove snapshots", "[PluginLifecycle][Visualization]")
{
    REQUIRE(wxApp::GetInstance() == nullptr);
    ScopedWxConsole app;
    REQUIRE(app.initialized);
    PluginVisualizationsTest visualizations;
    ScopedTemporaryFile snapshot(".glb");
    std::ofstream(snapshot.path().string()) << "partial snapshot";
    REQUIRE(fs::exists(snapshot.path()));

    std::atomic<bool> completed{false};
    bool on_main_thread = false;
    ExecutionResult result;
    PluginVisualizationsTest::Request request;
    request.id = {PluginCapabilityType::Visualization, "Worker failure", "worker-failure"};
    PluginVisualizationsTest::PendingResource resource;
    resource.snapshot_path = snapshot.path();
    request.resources.push_back(std::move(resource)); // Missing geometry fails inside the worker.
    request.completion = [&](ExecutionResult completion) {
        result = std::move(completion);
        on_main_thread = wxIsMainThread();
        completed = true;
    };
    visualizations.enqueue(std::move(request));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!completed && std::chrono::steady_clock::now() < deadline) {
        wxApp::GetInstance()->ProcessPendingEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(completed);
    CHECK(on_main_thread);
    CHECK(result.status == PluginResult::RecoverableError);
    CHECK_FALSE(result.message.empty());
    CHECK_FALSE(fs::exists(snapshot.path()));
}

TEST_CASE("Closing a visualization invalidates its queued dispatch", "[PluginLifecycle][Visualization]")
{
    REQUIRE(wxApp::GetInstance() == nullptr);
    ScopedWxConsole app;
    REQUIRE(app.initialized);
    PluginVisualizationsTest visualizations;
    ScopedTemporaryFile snapshot(".glb");
    std::ofstream(snapshot.path().string()) << "snapshot";
    REQUIRE(fs::exists(snapshot.path()));
    auto capability = std::make_shared<ConcurrentVisualization>();
    capability->set_resolved_identity("Queued Visualization", PluginCapabilityType::Visualization);
    capability->set_audit_plugin_key("queued-visualization");
    capability->resolve_type_metadata();

    bool completed = false;
    ExecutionResult result;
    PluginVisualizationsTest::Request request;
    request.id = capability->identity();
    request.capability = capability;
    PluginVisualizationsTest::PendingResource resource;
    resource.snapshot_path = snapshot.path();
    request.resources.push_back(std::move(resource));
    request.completion = [&](ExecutionResult completion) {
        result = std::move(completion);
        completed = true;
    };
    std::thread worker([&] { visualizations.dispatch(std::move(request)); });
    worker.join();
    REQUIRE_FALSE(completed);

    visualizations.host.close(capability->identity());
    wxApp::GetInstance()->ProcessPendingEvents();

    REQUIRE(completed);
    CHECK(result.status == PluginResult::Skipped);
    CHECK(result.message.empty());
    CHECK(capability->open_calls == 0);
    CHECK_FALSE(visualizations.host.is_active(capability->identity()));
    CHECK_FALSE(fs::exists(snapshot.path()));
}

TEST_CASE("Visualization shutdown joins preparation and suppresses queued callbacks", "[PluginLifecycle][Visualization]")
{
    REQUIRE(wxApp::GetInstance() == nullptr);
    ScopedWxConsole app;
    REQUIRE(app.initialized);
    PluginVisualizationsTest visualizations;
    auto capability = std::make_shared<ConcurrentVisualization>();
    capability->set_resolved_identity("Shutdown session", PluginCapabilityType::Visualization);
    capability->set_audit_plugin_key("shutdown-session");
    capability->resolve_type_metadata();
    VisualizationContext context;
    context.resources = {{VisualizationInputs::TOOLPATH, VisualizationInputs::GLTF_BINARY,
                          VisualizationInputs::FILE_TRANSPORT, "session.glb", 2, 0}};
    REQUIRE(visualizations.host.open(capability, context).status == PluginResult::Success);

    auto source = std::make_shared<GCodeProcessorResult>();
    source->id = 1;
    source->filaments_count = 1;
    source->filament_densities = {1.0f};
    source->moves.resize(2001);
    for (size_t i = 0; i < source->moves.size(); ++i) {
        auto& move = source->moves[i];
        move.type = EMoveType::Extrude;
        move.extrusion_role = erPerimeter;
        move.width = 0.45f;
        move.height = 0.2f;
        move.position = Vec3f(float(i), 0.0f, 0.2f);
    }
    ScopedTemporaryFile snapshot(".glb");
    PluginVisualizationsTest::PendingResource resource;
    resource.input.format = VisualizationInputs::GLTF_BINARY;
    resource.snapshot_path = snapshot.path();
    resource.toolpath = std::move(source);
    resource.tool_colors = {"#123456"};
    PluginVisualizationsTest::Request request;
    request.id = {PluginCapabilityType::Visualization, "Shutdown build", "shutdown-build"};
    request.resources.push_back(std::move(resource));
    bool completed = false;
    request.completion = [&](ExecutionResult) { completed = true; };
    visualizations.enqueue(std::move(request));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (visualizations.has_queued_requests() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK_FALSE(visualizations.has_queued_requests());

    visualizations.host.shutdown();
    CHECK_FALSE(visualizations.worker_joinable());
    CHECK_FALSE(visualizations.host.is_active(capability->identity()));
    CHECK(capability->close_calls == 1);
    wxApp::GetInstance()->ProcessPendingEvents();
    CHECK_FALSE(completed);
    CHECK_FALSE(fs::exists(snapshot.path()));
    CHECK(visualizations.host.open(capability, context).status == PluginResult::Skipped);
    visualizations.host.shutdown();
    CHECK(capability->close_calls == 1);
}

TEST_CASE("Visualization contexts carry separately scoped resources", "[PluginLifecycle][Visualization]")
{
    auto capability = std::make_shared<MultiResourceVisualization>();
    capability->set_resolved_identity("Multi-resource Visualization", PluginCapabilityType::Visualization);
    capability->set_audit_plugin_key("multi-resource-visualization");
    capability->resolve_type_metadata();

    REQUIRE(capability->requested_resources().size() == 2);
    CHECK(capability->requested_resources()[0].kind == VisualizationInputs::TOOLPATH);
    CHECK(capability->requested_resources()[0].scope == VisualizationInputs::CURRENT_PLATE);
    CHECK(capability->requested_resources()[1].kind == VisualizationInputs::MODEL);
    CHECK(capability->requested_resources()[1].scope == VisualizationInputs::PROJECT);

    VisualizationContext context;
    context.revision = 7;
    context.resources = {
        {VisualizationInputs::TOOLPATH, VisualizationInputs::GLTF_BINARY,
         VisualizationInputs::FILE_TRANSPORT, "toolpath.glb", 2, 0,
         VisualizationInputs::CURRENT_PLATE, {{"plate_index", "1"}}},
        {VisualizationInputs::MODEL, VisualizationInputs::OBJ,
         VisualizationInputs::FILE_TRANSPORT, "model.obj", 1, 0,
         VisualizationInputs::PROJECT, {{"plate_index", "0"}}}
    };

    PluginVisualizationsTest isolated;
    PluginVisualizations& visualizations = isolated.host;
    REQUIRE(visualizations.open(capability, context).status == PluginResult::Success);
    REQUIRE(capability->opened.resources.size() == 2);
    CHECK(capability->opened.resources[0].location == "toolpath.glb");
    CHECK(capability->opened.resources[0].scope == VisualizationInputs::CURRENT_PLATE);
    CHECK(capability->opened.resources[1].scope == VisualizationInputs::PROJECT);
    CHECK(capability->opened.resources[1].metadata.at("plate_index") == "0");
    visualizations.close(capability->identity());
}

TEST_CASE("Visualization opens and updates require every requested resource", "[PluginLifecycle][Visualization]")
{
    auto capability = std::make_shared<MultiResourceVisualization>();
    capability->set_resolved_identity("Required resources", PluginCapabilityType::Visualization);
    capability->set_audit_plugin_key("required-resources");
    capability->resolve_type_metadata();
    PluginVisualizationsTest visualizations;
    VisualizationContext context;
    context.revision = 1;
    context.resources = {{VisualizationInputs::MODEL, VisualizationInputs::OBJ,
                          VisualizationInputs::FILE_TRANSPORT, "model.obj", 1, 0, VisualizationInputs::PROJECT}};
    CHECK(visualizations.host.open(capability, context).status == PluginResult::Skipped);
    CHECK_FALSE(visualizations.host.is_active(capability->identity()));
    CHECK(capability->opened.resources.empty());

    context.resources.push_back({VisualizationInputs::TOOLPATH, VisualizationInputs::GLTF_BINARY,
                                 VisualizationInputs::FILE_TRANSPORT, "toolpath.glb", 2, 0, VisualizationInputs::PROJECT});
    CHECK(visualizations.host.open(capability, context).status == PluginResult::Skipped);
    context.resources.back().scope = VisualizationInputs::CURRENT_PLATE;
    REQUIRE(visualizations.host.open(capability, context).status == PluginResult::Success);

    context.revision = 2;
    context.resources.pop_back();
    CHECK(visualizations.host.update(capability->identity(), context).status == PluginResult::Skipped);
    CHECK(visualizations.host.is_active(capability->identity()));
    visualizations.host.close(capability->identity());
}

TEST_CASE("Visualization sessions dispatch lifecycle calls and close when disabled", "[PluginLifecycle][Visualization][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    ScopedDataDir data_dir_guard("visualization-lifecycle");
    allow_plugin_storage(data_dir_guard);
    write_plugin(data_dir_guard, "Visualization_Plugin", VISUALIZATION_PLUGIN_SOURCE);

    PluginManager& manager = PluginManager::instance();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);

    std::string error;
    REQUIRE(load_and_wait(manager, "Visualization_Plugin", error));
    INFO("load error: " << error);
    REQUIRE(error.empty());

    auto capability = std::dynamic_pointer_cast<VisualizationPluginCapability>(
        manager.get_plugin_capability({PluginCapabilityType::Visualization, "Dummy Visualization", "Visualization_Plugin"}));
    REQUIRE(capability != nullptr);
    REQUIRE(capability->requested_resources().size() == 2);
    CHECK(capability->requested_resources()[0].scope == VisualizationInputs::CURRENT_PLATE);
    CHECK(capability->requested_resources()[1].kind == VisualizationInputs::MODEL);
    CHECK(capability->requested_resources()[1].scope == VisualizationInputs::PROJECT);

    const fs::path events_path = fs::path(manager.get_storage_dir("Visualization_Plugin")) / "events.txt";
    VisualizationContext ctx;
    ctx.orca_version  = "test";
    ctx.revision      = 1;
    ctx.metadata["plate_index"] = "0";
    ctx.resources = {{VisualizationInputs::TOOLPATH, VisualizationInputs::GLTF_BINARY,
                      VisualizationInputs::FILE_TRANSPORT, (events_path.parent_path() / "snapshot.glb").string(), 2, 0}};

    ctx.resources.push_back({VisualizationInputs::MODEL, VisualizationInputs::OBJ,
                             VisualizationInputs::FILE_TRANSPORT, "model.obj", 1, 0, VisualizationInputs::PROJECT});

    PluginVisualizations& visualizations = PluginVisualizations::instance();
    CHECK(visualizations.open(capability, ctx).status == PluginResult::Success);
    CHECK(visualizations.is_active(capability->identity()));

    CHECK(visualizations.update(capability->identity(), ctx).status == PluginResult::Skipped);

    ctx.revision = 99;
    CHECK(visualizations.update(capability->identity(), ctx).status == PluginResult::RecoverableError);
    CHECK(visualizations.is_active(capability->identity()));

    ctx.revision = 2;
    CHECK(visualizations.update(capability->identity(), ctx).status == PluginResult::Success);

    ctx.revision = 100;
    CHECK(visualizations.update(capability->identity(), ctx).status == PluginResult::FatalError);
    CHECK_FALSE(visualizations.is_active(capability->identity()));

    ctx.revision = 3;
    REQUIRE(visualizations.open(capability, ctx).status == PluginResult::Success);
    REQUIRE(visualizations.is_active(capability->identity()));
    manager.set_capability_enabled(capability->identity(), false);
    CHECK_FALSE(visualizations.is_active(capability->identity()));
    CHECK(manager.unload_plugin("Visualization_Plugin"));

    std::ifstream events(events_path.string());
    const std::string contents((std::istreambuf_iterator<char>(events)), std::istreambuf_iterator<char>());
    CHECK(contents == "open:1:0:toolpath:model/gltf-binary:2.0:2:current_plate:test:readonly\nupdate:2\nclose\n"
                      "open:3:0:toolpath:model/gltf-binary:2.0:2:current_plate:test:readonly\nclose\n");
}

TEST_CASE("Background visualization unload closes on the main thread before teardown", "[PluginLifecycle][Visualization][Python]")
{
    REQUIRE(wxApp::GetInstance() == nullptr);
    ScopedWxConsole app;
    REQUIRE(app.initialized);
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());
    ScopedDataDir data_dir_guard("visualization-background-unload");
    allow_plugin_storage(data_dir_guard);
    write_plugin(data_dir_guard, "Visualization_Plugin", std::string(VISUALIZATION_PLUGIN_SOURCE) + R"PY(
import threading

def close_on_main(self):
    self._record("close-main" if threading.current_thread() is threading.main_thread() else "close-worker")

def record_unload(self):
    self._record("unload")

DummyVisualization.close = close_on_main
DummyVisualization.on_unload = record_unload
)PY");
    PluginManager& manager = PluginManager::instance();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);
    std::string error;
    REQUIRE(load_and_wait(manager, "Visualization_Plugin", error));
    auto capability = std::dynamic_pointer_cast<VisualizationPluginCapability>(
        manager.get_plugin_capability({PluginCapabilityType::Visualization, "Dummy Visualization", "Visualization_Plugin"}));
    REQUIRE(capability);
    const fs::path events_path = fs::path(manager.get_storage_dir("Visualization_Plugin")) / "events.txt";
    VisualizationContext context;
    context.metadata["plate_index"] = "0";
    context.resources = {
        {VisualizationInputs::TOOLPATH, VisualizationInputs::GLTF_BINARY, VisualizationInputs::FILE_TRANSPORT,
         (events_path.parent_path() / "snapshot.glb").string(), 2, 0},
        {VisualizationInputs::MODEL, VisualizationInputs::OBJ, VisualizationInputs::FILE_TRANSPORT,
         "model.obj", 1, 0, VisualizationInputs::PROJECT}
    };
    REQUIRE(PluginVisualizations::instance().open(capability, context).status == PluginResult::Success);
    std::atomic<bool> completed{false};
    bool unloaded = false;
    std::thread worker([&] {
        unloaded = manager.unload_plugin("Visualization_Plugin");
        completed.store(true, std::memory_order_release);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!completed.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
        wxApp::GetInstance()->ProcessPendingEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const bool finished_before_timeout = completed.load(std::memory_order_acquire);
    if (!finished_before_timeout)
        manager.set_shutting_down();
    worker.join();
    REQUIRE(finished_before_timeout);
    REQUIRE(unloaded);
    CHECK_FALSE(PluginVisualizations::instance().is_active(capability->identity()));
    std::ifstream events(events_path.string());
    const std::string contents((std::istreambuf_iterator<char>(events)), std::istreambuf_iterator<char>());
    CHECK(contents.find("close-main\nunload\n") != std::string::npos);
    CHECK(contents.find("close-worker") == std::string::npos);
}

TEST_CASE("Unrelated visualization plugins keep independent sessions", "[PluginLifecycle][Visualization][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    ScopedDataDir data_dir_guard("visualization-independent");
    allow_plugin_storage(data_dir_guard);
    write_plugin(data_dir_guard, "Visualization_First", VISUALIZATION_PLUGIN_SOURCE);
    write_plugin(data_dir_guard, "Visualization_Second", SECOND_VISUALIZATION_PLUGIN_SOURCE);

    PluginManager& manager = PluginManager::instance();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);
    std::string error;
    REQUIRE(load_and_wait(manager, "Visualization_First", error));
    REQUIRE(error.empty());
    REQUIRE(load_and_wait(manager, "Visualization_Second", error));
    REQUIRE(error.empty());

    auto first = std::dynamic_pointer_cast<VisualizationPluginCapability>(
        manager.get_plugin_capability({PluginCapabilityType::Visualization, "Dummy Visualization", "Visualization_First"}));
    auto second = std::dynamic_pointer_cast<VisualizationPluginCapability>(
        manager.get_plugin_capability({PluginCapabilityType::Visualization, "Second Visualization", "Visualization_Second"}));
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(manager.get_plugin_capabilities("", PluginCapabilityType::Visualization).size() == 2);

    VisualizationContext first_context;
    first_context.revision = 1;
    first_context.metadata["plate_index"] = "0";
    first_context.resources = {{VisualizationInputs::TOOLPATH, VisualizationInputs::GLTF_BINARY,
                                VisualizationInputs::FILE_TRANSPORT,
                                (fs::path(manager.get_storage_dir("Visualization_First")) / "first.glb").string(), 2, 0}};
    first_context.resources.push_back({VisualizationInputs::MODEL, VisualizationInputs::OBJ,
                                       VisualizationInputs::FILE_TRANSPORT, "model.obj", 1, 0, VisualizationInputs::PROJECT});
    VisualizationContext second_context = first_context;
    second_context.resources.resize(1);
    second_context.resources[0].location = (fs::path(manager.get_storage_dir("Visualization_Second")) / "second.glb").string();

    PluginVisualizations& visualizations = PluginVisualizations::instance();
    REQUIRE(visualizations.open(first, first_context).status == PluginResult::Success);
    REQUIRE(visualizations.open(second, second_context).status == PluginResult::Success);
    visualizations.close(first->identity());
    CHECK_FALSE(visualizations.is_active(first->identity()));
    CHECK(visualizations.is_active(second->identity()));
    visualizations.close(second->identity());

    CHECK(manager.unload_plugin("Visualization_First"));
    CHECK(manager.unload_plugin("Visualization_Second"));
}

TEST_CASE("A printer-agent plugin registers and deregisters with its lifecycle", "[PluginLifecycle][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    ScopedDataDir data_dir_guard("printer-agent-load");
    write_plugin(data_dir_guard, "LifecycleTestAgent", PRINTER_AGENT_PLUGIN_SOURCE);

    PluginManager& manager = PluginManager::instance();
    install_agent_registry_wiring();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);

    PluginDescriptor descriptor;
    REQUIRE(manager.try_get_valid_plugin_descriptor("LifecycleTestAgent", descriptor));

    std::string error;
    REQUIRE(load_and_wait(manager, "LifecycleTestAgent", error));
    INFO("load error: " << error);

    const auto capabilities = manager.get_plugin_capabilities(
        "LifecycleTestAgent", PluginCapabilityType::PrinterConnection);
    REQUIRE(capabilities.size() == 1);
    NetworkAgentFactory::register_python_printer_agent("LifecycleTestAgent", capabilities.front()->name());

    CHECK(NetworkAgentFactory::is_printer_agent_registered("lifecycle-test-agent"));
    REQUIRE(manager.unload_plugin("LifecycleTestAgent"));
    CHECK_FALSE(NetworkAgentFactory::is_printer_agent_registered("lifecycle-test-agent"));
}

TEST_CASE("A duplicate printer-agent id is rejected without clobbering its owner", "[PluginLifecycle][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    const std::string key_a  = "DuplicateIdAgentA";
    const std::string key_b  = "DuplicateIdAgentB";
    const std::string dup_id = "duplicate-id-agent";

    ScopedDataDir data_dir_guard("printer-agent-duplicate");
    write_plugin(data_dir_guard, key_a, make_agent_plugin_source("A", "Duplicate Id Agent A", dup_id));
    write_plugin(data_dir_guard, key_b, make_agent_plugin_source("B", "Duplicate Id Agent B", dup_id));

    PluginManager& manager = PluginManager::instance();
    install_agent_registry_wiring();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);

    PluginDescriptor descriptor_a;
    PluginDescriptor descriptor_b;
    REQUIRE(manager.try_get_valid_plugin_descriptor(key_a, descriptor_a));
    REQUIRE(manager.try_get_valid_plugin_descriptor(key_b, descriptor_b));

    std::string error;
    REQUIRE(load_and_wait(manager, key_a, error));
    REQUIRE(load_and_wait(manager, key_b, error));

    const auto capabilities_a = manager.get_plugin_capabilities(key_a, PluginCapabilityType::PrinterConnection);
    const auto capabilities_b = manager.get_plugin_capabilities(key_b, PluginCapabilityType::PrinterConnection);
    REQUIRE(capabilities_a.size() == 1);
    REQUIRE(capabilities_b.size() == 1);

    NetworkAgentFactory::register_python_printer_agent(key_a, capabilities_a.front()->name());
    NetworkAgentFactory::register_python_printer_agent(key_b, capabilities_b.front()->name());

    CHECK(NetworkAgentFactory::is_printer_agent_registered(dup_id));
    const PrinterAgentInfo* info = NetworkAgentFactory::get_printer_agent_info(dup_id);
    REQUIRE(info != nullptr);
    CHECK(info->plugin_identifier.find(key_a) != std::string::npos);
    CHECK(info->plugin_identifier.find(key_b) == std::string::npos);

    REQUIRE(manager.unload_plugin(key_a));
    CHECK_FALSE(NetworkAgentFactory::is_printer_agent_registered(dup_id));
    manager.unload_plugin(key_b);
}

TEST_CASE("A printer-agent plugin cannot claim a built-in agent id", "[PluginLifecycle][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    NetworkAgentFactory::register_all_agents();
    REQUIRE(NetworkAgentFactory::is_printer_agent_registered(BBL_PRINTER_AGENT_ID));

    const std::string plugin_key = "BuiltinClashAgent";
    ScopedDataDir data_dir_guard("printer-agent-builtin-clash");
    write_plugin(data_dir_guard, plugin_key,
                 make_agent_plugin_source("Clash", "Builtin Clash", BBL_PRINTER_AGENT_ID));

    PluginManager& manager = PluginManager::instance();
    install_agent_registry_wiring();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);

    PluginDescriptor descriptor;
    REQUIRE(manager.try_get_valid_plugin_descriptor(plugin_key, descriptor));

    std::string error;
    REQUIRE(load_and_wait(manager, plugin_key, error));
    const auto capabilities = manager.get_plugin_capabilities(plugin_key, PluginCapabilityType::PrinterConnection);
    REQUIRE(capabilities.size() == 1);
    NetworkAgentFactory::register_python_printer_agent(plugin_key, capabilities.front()->name());

    const PrinterAgentInfo* info = NetworkAgentFactory::get_printer_agent_info(BBL_PRINTER_AGENT_ID);
    REQUIRE(info != nullptr);
    CHECK_FALSE(info->is_plugin());
    CHECK(info->plugin_identifier.find(plugin_key) == std::string::npos);

    manager.unload_plugin(plugin_key);
}

TEST_CASE("Re-registering a printer-agent capability preserves its registration", "[PluginLifecycle][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    const std::string plugin_key = "ReRegisterAgent";
    const std::string agent_id   = "re-register-agent";

    ScopedDataDir data_dir_guard("printer-agent-reregister");
    write_plugin(data_dir_guard, plugin_key,
                 make_agent_plugin_source("Re", "Re Register", agent_id));

    PluginManager& manager = PluginManager::instance();
    install_agent_registry_wiring();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);

    PluginDescriptor descriptor;
    REQUIRE(manager.try_get_valid_plugin_descriptor(plugin_key, descriptor));

    std::string error;
    REQUIRE(load_and_wait(manager, plugin_key, error));
    const auto capabilities = manager.get_plugin_capabilities(plugin_key, PluginCapabilityType::PrinterConnection);
    REQUIRE(capabilities.size() == 1);

    NetworkAgentFactory::register_python_printer_agent(plugin_key, capabilities.front()->name());
    REQUIRE(NetworkAgentFactory::is_printer_agent_registered(agent_id));
    const PrinterAgentInfo* first = NetworkAgentFactory::get_printer_agent_info(agent_id);
    REQUIRE(first != nullptr);
    const std::string owner = first->plugin_identifier;

    NetworkAgentFactory::register_python_printer_agent(plugin_key, capabilities.front()->name());
    CHECK(NetworkAgentFactory::is_printer_agent_registered(agent_id));
    const PrinterAgentInfo* second = NetworkAgentFactory::get_printer_agent_info(agent_id);
    REQUIRE(second != nullptr);
    CHECK(second->plugin_identifier == owner);

    manager.unload_plugin(plugin_key);
}

TEST_CASE("Plugin manager can initialize again after shutdown", "[PluginLifecycle][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    ScopedDataDir data_dir_guard("lifecycle-reinitialize");
    write_plugin(data_dir_guard, "Echo_Plugin", ECHO_PLUGIN_SOURCE);

    PluginManager& manager = PluginManager::instance();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);
    manager.shutdown();

    REQUIRE(manager.initialize());
    manager.discover_plugins(/*async=*/false, /*clear=*/true);

    std::string error;
    REQUIRE(load_and_wait(manager, "Echo_Plugin", error));
    CHECK(manager.is_plugin_loaded("Echo_Plugin"));

    manager.unload_plugin("Echo_Plugin");
}

TEST_CASE("Duplicate discovered plugin keys are reported", "[PluginLifecycle][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    ScopedDataDir data_dir_guard("lifecycle-duplicate-key");
    for (const char* directory_name : {"first", "second"}) {
        const fs::path plugin_dir = data_dir_guard.plugins_dir() / directory_name;
        fs::create_directories(plugin_dir);
        std::ofstream out((plugin_dir / "Shared.py").string(), std::ios::binary);
        out << ECHO_PLUGIN_SOURCE;
    }

    PluginManager& manager = PluginManager::instance();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);

    PluginDescriptor descriptor;
    REQUIRE(manager.try_get_plugin_descriptor("Shared", descriptor));
    CHECK(descriptor.has_error());
    CHECK(descriptor.normalized_error().find("Duplicate plugin key") != std::string::npos);
}

TEST_CASE("Unloading a plugin drops the package and its capabilities", "[PluginLifecycle][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    ScopedDataDir data_dir_guard("lifecycle-unload");
    write_plugin(data_dir_guard, "Echo_Plugin", ECHO_PLUGIN_SOURCE);

    PluginManager& manager = PluginManager::instance();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);

    std::string error;
    REQUIRE(load_and_wait(manager, "Echo_Plugin", error));
    REQUIRE(manager.is_plugin_loaded("Echo_Plugin"));

    CHECK(manager.unload_plugin("Echo_Plugin"));

    CHECK_FALSE(manager.is_plugin_loaded("Echo_Plugin"));
    CHECK(manager.get_plugin_capabilities("Echo_Plugin").empty());
    CHECK(manager.get_plugin_capability({PluginCapabilityType::Script, "Echo", "Echo_Plugin"}) == nullptr);

    // The package stays discovered, but nothing capability-shaped survives the unload.
    const PluginDescriptor descriptor = descriptor_of(manager, "Echo_Plugin");
    CHECK(descriptor.plugin_key == "Echo_Plugin");
    CHECK(capabilities_of(manager, "Echo_Plugin").empty());
}

TEST_CASE("Python module release removes package submodules and owned sys.path", "[PluginLifecycle][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    ScopedDataDir data_dir_guard("module-release");
    const fs::path package_root = data_dir_guard.dir / "reload_package";
    fs::create_directories(package_root);

    auto write_package = [&](const std::string& value) {
        std::ofstream init((package_root / "__init__.py").string());
        init << "import reload_helper\nfrom . import sub\nVALUE = sub.VALUE\n";
        std::ofstream sub((package_root / "sub.py").string());
        sub << "VALUE = " << value << "\n";
        std::ofstream helper((package_root.parent_path() / "reload_helper.py").string());
        helper << "VALUE = 'helper'\n";
    };

    write_package("'old'");

    PythonInterpreter& interpreter = PythonInterpreter::instance();
    std::vector<std::string> paths;
    std::vector<std::string> modules;
    std::string              error;
    PyObject*                module = interpreter.load_module_from_directory(
        package_root.parent_path().string(), "reload_package", error, &paths, &modules);
    REQUIRE(module != nullptr);
    INFO("module load error: " << error);
    REQUIRE(error.empty());
    REQUIRE(paths.size() == 1);

    {
        PythonGILState gil;
        REQUIRE(gil);
        PyObject* modules = PyImport_GetModuleDict();
        REQUIRE(modules != nullptr);
        CHECK(PyDict_GetItemString(modules, "reload_package") != nullptr);
        CHECK(PyDict_GetItemString(modules, "reload_package.sub") != nullptr);
        CHECK(PyDict_GetItemString(modules, "reload_helper") != nullptr);
    }

    Plugin loaded;
    loaded.module           = module;
    loaded.module_name      = "reload_package";
    loaded.plugin_sys_paths = paths;
    loaded.plugin_modules   = modules;
    loaded.release_module();

    {
        PythonGILState gil;
        REQUIRE(gil);
        PyObject* modules = PyImport_GetModuleDict();
        REQUIRE(modules != nullptr);
        CHECK(PyDict_GetItemString(modules, "reload_package") == nullptr);
        CHECK(PyDict_GetItemString(modules, "reload_package.sub") == nullptr);
        CHECK(PyDict_GetItemString(modules, "reload_helper") == nullptr);

        PyObject* sys_path = PySys_GetObject("path");
        REQUIRE(sys_path != nullptr);
        PyObjectPtr path(PyUnicode_DecodeFSDefault(paths.front().c_str()));
        REQUIRE(path);
        CHECK(PySequence_Contains(sys_path, path.get()) == 0);
    }

    // Ensure the next import executes the new submodule rather than reusing a stale package child.
    write_package("'new'");
    boost::system::error_code ec;
    fs::remove_all(package_root / "__pycache__", ec);

    paths.clear();
    modules.clear();
    module = interpreter.load_module_from_directory(
        package_root.parent_path().string(), "reload_package", error, &paths, &modules);
    REQUIRE(module != nullptr);
    REQUIRE(error.empty());

    {
        PythonGILState gil;
        REQUIRE(gil);
        PyObjectPtr value(PyObject_GetAttrString(module, "VALUE"));
        REQUIRE(value);
        CHECK(std::string(PyUnicode_AsUTF8(value.get())) == "new");
    }

    loaded.module           = module;
    loaded.module_name      = "reload_package";
    loaded.plugin_sys_paths = paths;
    loaded.plugin_modules   = modules;
    loaded.release_module();
}

TEST_CASE("A capability disabled in the sidecar loads disabled", "[PluginLifecycle][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    ScopedDataDir  data_dir_guard("lifecycle-disabled");
    const fs::path plugin_dir = write_plugin(data_dir_guard, "Echo_Plugin", ECHO_PLUGIN_SOURCE);

    // Pre-seed the sidecar with the capability disabled, as a previous session would have.
    PluginInstallState state;
    state.installed_from    = "local";
    state.installed_version = "1.0";
    state.plugin_name       = "Echo Plugin";
    state.enabled           = true;
    state.capabilities      = {{"Echo", false}};
    REQUIRE(write_install_state(plugin_dir, state));

    PluginManager& manager = PluginManager::instance();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);

    std::string error;
    REQUIRE(load_and_wait(manager, "Echo_Plugin", error));
    REQUIRE(manager.is_plugin_loaded("Echo_Plugin"));

    // The capability still materializes — it is loaded, but logically disabled, so consumers skip it.
    const auto echo = find_capability(manager, "Echo_Plugin", "Echo");
    REQUIRE(echo != nullptr);
    CHECK_FALSE(echo->is_enabled());

    CHECK(manager.get_plugin_capabilities("Echo_Plugin", PluginCapabilityType::Unknown, /*only_enabled=*/true).empty());
    CHECK(manager.get_plugin_capabilities("Echo_Plugin", PluginCapabilityType::Unknown, /*only_enabled=*/false).size() == 1);

    // An empty load request must preserve the persisted disabled state even when the package is
    // already loaded.
    std::string no_request_error;
    REQUIRE(load_and_wait(manager, "Echo_Plugin", no_request_error));
    CHECK_FALSE(find_capability(manager, "Echo_Plugin", "Echo")->is_enabled());

    manager.unload_plugin("Echo_Plugin");
}

TEST_CASE("Disabling a capability round-trips through the sidecar and survives a reload", "[PluginLifecycle][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    ScopedDataDir  data_dir_guard("lifecycle-roundtrip");
    const fs::path plugin_dir = write_plugin(data_dir_guard, "Echo_Plugin", ECHO_PLUGIN_SOURCE);

    PluginManager& manager = PluginManager::instance();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);

    std::string error;
    REQUIRE(load_and_wait(manager, "Echo_Plugin", error));
    REQUIRE(find_capability(manager, "Echo_Plugin", "Echo")->is_enabled());

    // Disabling writes the choice through to .install_state.json.
    manager.set_capability_enabled({PluginCapabilityType::Unknown, "Echo", "Echo_Plugin"}, false);
    CHECK_FALSE(find_capability(manager, "Echo_Plugin", "Echo")->is_enabled());

    PluginInstallState persisted;
    REQUIRE(read_install_state(plugin_dir, persisted));
    REQUIRE(persisted.capabilities.size() == 1);
    CHECK(persisted.capabilities.front().first == "Echo");
    CHECK_FALSE(persisted.capabilities.front().second);

    // Unload and reload: the user's choice must survive.
    REQUIRE(manager.unload_plugin("Echo_Plugin"));

    std::string reload_error;
    REQUIRE(load_and_wait(manager, "Echo_Plugin", reload_error));

    const auto echo = find_capability(manager, "Echo_Plugin", "Echo");
    REQUIRE(echo != nullptr);
    CHECK_FALSE(echo->is_enabled());

    manager.unload_plugin("Echo_Plugin");
}

TEST_CASE("A capability disabled after load stays disabled when rediscovered and reloaded", "[PluginLifecycle][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    ScopedDataDir data_dir_guard("lifecycle-reload-live");
    write_plugin(data_dir_guard, "Echo_Plugin", ECHO_PLUGIN_SOURCE);

    PluginManager& manager = PluginManager::instance();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);

    std::string error;
    REQUIRE(load_and_wait(manager, "Echo_Plugin", error));

    manager.set_capability_enabled({PluginCapabilityType::Unknown, "Echo", "Echo_Plugin"}, false);
    REQUIRE(manager.unload_plugin("Echo_Plugin"));

    // Rediscover, as the app does when a plugin is toggled off and back on. The enable flags the
    // loader seeds from must come from the sidecar just written, not from a stale cache.
    manager.discover_plugins(/*async=*/false, /*clear=*/false);

    std::string reload_error;
    REQUIRE(load_and_wait(manager, "Echo_Plugin", reload_error));

    const auto echo = find_capability(manager, "Echo_Plugin", "Echo");
    REQUIRE(echo != nullptr);
    CHECK_FALSE(echo->is_enabled());

    manager.unload_plugin("Echo_Plugin");
}

TEST_CASE("Re-enabling a disabled capability writes the sidecar back", "[PluginLifecycle][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    ScopedDataDir  data_dir_guard("lifecycle-reenable");
    const fs::path plugin_dir = write_plugin(data_dir_guard, "Echo_Plugin", ECHO_PLUGIN_SOURCE);

    PluginInstallState state;
    state.installed_from = "local";
    state.plugin_name    = "Echo Plugin";
    state.enabled        = true;
    state.capabilities   = {{"Echo", false}};
    REQUIRE(write_install_state(plugin_dir, state));

    PluginManager& manager = PluginManager::instance();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);

    std::string error;
    REQUIRE(load_and_wait(manager, "Echo_Plugin", error));

    // An explicit request overrides the persisted disabled state, including on a fresh load.
    REQUIRE(manager.unload_plugin("Echo_Plugin"));
    std::string enable_error;
    REQUIRE(load_and_wait(manager, "Echo_Plugin", enable_error, {"Echo"}));

    const auto echo = find_capability(manager, "Echo_Plugin", "Echo");
    REQUIRE(echo != nullptr);
    CHECK(echo->is_enabled());

    PluginInstallState persisted;
    REQUIRE(read_install_state(plugin_dir, persisted));
    REQUIRE(persisted.capabilities.size() == 1);
    CHECK(persisted.capabilities.front().first == "Echo");
    CHECK(persisted.capabilities.front().second);

    manager.unload_plugin("Echo_Plugin");
}

TEST_CASE("Overwriting a local plugin unloads its live module", "[PluginLifecycle][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    ScopedDataDir data_dir_guard("lifecycle-overwrite");
    const fs::path package_dir = data_dir_guard.dir / "packages";
    fs::create_directories(package_dir);
    const fs::path package = package_dir / "Echo_Plugin.py";
    {
        std::ofstream out(package.string(), std::ios::binary);
        out << ECHO_PLUGIN_SOURCE;
    }

    PluginManager& manager = PluginManager::instance();
    std::string error;
    REQUIRE(manager.install_plugin(package, error));
    manager.discover_plugins(/*async=*/false, /*clear=*/true);
    REQUIRE(load_and_wait(manager, "Echo_Plugin", error));
    REQUIRE(manager.is_plugin_loaded("Echo_Plugin"));

    REQUIRE(manager.install_plugin(package, error));
    CHECK_FALSE(manager.is_plugin_loaded("Echo_Plugin"));
}

TEST_CASE("capabilities_to_enable selects which capabilities come up enabled", "[PluginLifecycle][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    // Two capabilities in one package; only the second is requested.
    const char* const two_cap_source = R"PY(# /// script
# requires-python = ">=3.12"
#
# [tool.orcaslicer.plugin]
# name = "Duo Plugin"
# version = "1.0"
# type = "script"
# ///
import orca

class Alpha(orca.script.ScriptPluginCapabilityBase):
    def get_name(self):
        return "Alpha"

    def execute(self, ctx):
        return orca.ExecutionResult.success()

class Beta(orca.script.ScriptPluginCapabilityBase):
    def get_name(self):
        return "Beta"

    def execute(self, ctx):
        return orca.ExecutionResult.success()

@orca.plugin
class DuoPackage(orca.base):
    def register_capabilities(self):
        orca.register_capability(Alpha)
        orca.register_capability(Beta)
)PY";

    ScopedDataDir data_dir_guard("lifecycle-select");
    write_plugin(data_dir_guard, "Duo_Plugin", two_cap_source);

    PluginManager& manager = PluginManager::instance();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);

    std::string error;
    REQUIRE(load_and_wait(manager, "Duo_Plugin", error, {"Beta"}));

    REQUIRE(capabilities_of(manager, "Duo_Plugin").size() == 2);

    const auto alpha = find_capability(manager, "Duo_Plugin", "Alpha");
    const auto beta  = find_capability(manager, "Duo_Plugin", "Beta");
    REQUIRE(alpha != nullptr);
    REQUIRE(beta != nullptr);

    CHECK_FALSE(alpha->is_enabled());
    CHECK(beta->is_enabled());

    manager.unload_plugin("Duo_Plugin");
}

TEST_CASE("A cancelled load keeps blocking wait_for_all_plugin_loads until the worker exits", "[PluginLifecycle][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    // Stalls inside the module import, so the detached load worker is still executing Python while
    // the test cancels it.
    const char* const slow_source = R"PY(# /// script
# requires-python = ">=3.12"
#
# [tool.orcaslicer.plugin]
# name = "Slow Load Plugin"
# version = "1.0"
# type = "script"
# ///
import time

import orca

time.sleep(2)

class Slow(orca.script.ScriptPluginCapabilityBase):
    def get_name(self):
        return "Slow"

    def execute(self, ctx):
        return orca.ExecutionResult.success()

@orca.plugin
class SlowPackage(orca.base):
    def register_capabilities(self):
        orca.register_capability(Slow)
)PY";

    ScopedDataDir data_dir_guard("lifecycle-cancel");
    write_plugin(data_dir_guard, "Slow_Load_Plugin", slow_source);

    PluginManager& manager = PluginManager::instance();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);

    manager.load_plugin("Slow_Load_Plugin", /*skip_deps=*/true);

    // The key is registered before the worker is spawned, so this is not a race.
    REQUIRE(manager.is_plugin_load_in_progress("Slow_Load_Plugin"));
    REQUIRE(manager.cancel_plugin_load("Slow_Load_Plugin"));

    // Cancelling must not release the worker's slot. shutdown() unloads everything and GUI_App
    // finalizes the interpreter as soon as this wait returns, so reporting "no loads in progress"
    // while the worker is still inside Python is how the app crashes on exit.
    CHECK(manager.is_plugin_load_in_progress("Slow_Load_Plugin"));
    CHECK_FALSE(manager.wait_for_all_plugin_loads(std::chrono::milliseconds(0)));

    // The worker releases the slot itself, once it has unwound.
    CHECK(manager.wait_for_all_plugin_loads(std::chrono::seconds(60)));
    CHECK_FALSE(manager.is_plugin_load_in_progress("Slow_Load_Plugin"));
    CHECK_FALSE(manager.is_plugin_loaded("Slow_Load_Plugin"));
}

TEST_CASE("Loading an unknown plugin key records an error instead of crashing", "[PluginLifecycle][Python]")
{
    // discover_plugins() initializes the plugin system (and with it the interpreter), so this
    // needs the same explicit teardown as the load tests.
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    ScopedDataDir data_dir_guard("lifecycle-missing");

    PluginManager& manager = PluginManager::instance();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);

    // Rejected synchronously: no worker thread is spawned for an unknown key.
    manager.load_plugin("No_Such_Plugin", /*skip_deps=*/true);

    CHECK_FALSE(manager.is_plugin_loaded("No_Such_Plugin"));
    CHECK(manager.get_plugin_load_error("No_Such_Plugin") == "Plugin not found: No_Such_Plugin");
    CHECK(manager.get_plugin_capabilities("No_Such_Plugin").empty());
}

TEST_CASE("The startup auto-load list only contains packages whose sidecar enables them", "[PluginLifecycle][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    ScopedDataDir data_dir_guard("lifecycle-autoload");

    // No sidecar at all: never installed through Orca, so it carries no auto-load intent.
    write_plugin(data_dir_guard, "Bare_Plugin", ECHO_PLUGIN_SOURCE);

    // Sidecar with enabled = true: auto-loads.
    const fs::path on_dir = write_plugin(data_dir_guard, "Enabled_Plugin", ECHO_PLUGIN_SOURCE);
    PluginInstallState on_state;
    on_state.installed_from = "local";
    on_state.enabled        = true;
    REQUIRE(write_install_state(on_dir, on_state));

    // Sidecar with enabled = false: the user turned it off.
    const fs::path off_dir = write_plugin(data_dir_guard, "Disabled_Plugin", ECHO_PLUGIN_SOURCE);
    PluginInstallState off_state;
    off_state.installed_from = "local";
    off_state.enabled        = false;
    REQUIRE(write_install_state(off_dir, off_state));

    PluginManager& manager = PluginManager::instance();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);

    const std::vector<std::string> keys = manager.get_enabled_plugin_keys();

    CHECK(std::find(keys.begin(), keys.end(), "Enabled_Plugin") != keys.end());
    CHECK(std::find(keys.begin(), keys.end(), "Disabled_Plugin") == keys.end());
    CHECK(std::find(keys.begin(), keys.end(), "Bare_Plugin") == keys.end());
}

TEST_CASE("Signing out drops every cloud plugin row, installed or not", "[PluginLifecycle][Python]")
{
    ScopedPluginManager plugin_system;
    if (!plugin_system.initialized)
        SKIP("Bundled Python interpreter unavailable: " + PythonInterpreter::instance().last_error());

    ScopedDataDir data_dir_guard("lifecycle-signout");

    // A local package, which must survive sign-out.
    write_plugin(data_dir_guard, "Echo_Plugin", ECHO_PLUGIN_SOURCE);

    PluginManager& manager = PluginManager::instance();
    manager.discover_plugins(/*async=*/false, /*clear=*/true);

    // Two cloud rows: one merely available (nothing installed), one with a local package behind it —
    // the case that used to linger, unloaded but still listed, until the user hit refresh.
    PluginDescriptor available;
    available.plugin_key = "11111111-1111-1111-1111-111111111111";
    available.name       = "Available Cloud Plugin";
    available.cloud      = CloudPluginState{available.plugin_key, /*installed=*/false, false, false, false};

    PluginDescriptor installed;
    installed.plugin_key  = "22222222-2222-2222-2222-222222222222";
    installed.name        = "Installed Cloud Plugin";
    installed.plugin_root = (data_dir_guard.plugins_dir() / "_subscribed" / "user" / installed.plugin_key).string();
    installed.cloud       = CloudPluginState{installed.plugin_key, /*installed=*/true, false, false, false};

    manager.update_cloud_metadata({available, installed});

    const auto has_key = [&manager](const std::string& key) {
        PluginDescriptor descriptor;
        return manager.try_get_plugin_descriptor(key, descriptor);
    };

    REQUIRE(has_key(available.plugin_key));
    REQUIRE(has_key(installed.plugin_key));
    REQUIRE(has_key("Echo_Plugin"));

    // Sign out. The per-user _subscribed directory stops being scanned, so both cloud rows are now
    // stale and must go — not just the one with nothing installed behind it.
    manager.unload_cloud_plugins();
    manager.clear_cloud_plugin_metadata();
    manager.set_cloud_user("");

    CHECK_FALSE(has_key(available.plugin_key));
    CHECK_FALSE(has_key(installed.plugin_key));
    CHECK(has_key("Echo_Plugin"));
}

TEST_CASE("Unloading a plugin that is not loaded is a no-op", "[PluginLifecycle]")
{
    ScopedDataDir data_dir_guard("lifecycle-noop-unload");

    PluginManager& manager = PluginManager::instance();

    // Current behavior: unloading an unknown key succeeds (it fires the unload callbacks and
    // reports success) rather than reporting "nothing to unload".
    CHECK(manager.unload_plugin("No_Such_Plugin"));
    CHECK_FALSE(manager.is_plugin_loaded("No_Such_Plugin"));
}
