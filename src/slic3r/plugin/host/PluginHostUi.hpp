#pragma once

#include <pybind11/pybind11.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace Slic3r {

// One awaitable pending on a plugin worker's running asyncio loop, completed from the UI thread.
//
// This is the suspension primitive for host calls that must show a dialog before they can resolve
// (the awaitable preset begin_update(); see docs/HLSD/preset-plugin-api.md and the async-bridge
// architecture gate). It carries no GUI or preset state: only the Python loop, the Future created
// on it, and a Python-side resolver.
//
// Threading contract:
//   * create() runs on the plugin worker with the GIL held and an asyncio loop running; it binds
//     that loop and creates the Future, which is the awaitable handed back to Python.
//   * The host posts the UI work with PluginHostUi::post_to_ui_thread(); the UI callback then calls
//     complete()/fail() with the GIL held. Both marshal the outcome through
//     loop.call_soon_threadsafe, so the Future is resolved on its own loop thread, never from the
//     UI thread.
//   * A Future that a cancelled task or a closed loop already discarded is dropped silently, so a
//     late UI result cannot corrupt or resurrect a cancelled coroutine.
//   * abandon() stops delivery without touching Python, for lifecycle teardown that must not wait.
//     Python references are always released under the GIL, from whichever thread destroys the
//     handle; if the interpreter is already gone the references are intentionally leaked rather
//     than decref'd unsafely (same rule as the UI callable holders).
class AsyncFuture
{
public:
    // Requires the GIL and a running asyncio loop (pybind11::error_already_set otherwise).
    static std::shared_ptr<AsyncFuture> create();

    // The Python Future to return as the awaitable. Requires the GIL.
    pybind11::object future() const { return m_future; }

    // Resolve the awaitable. Requires the GIL. complete() sets the result, fail() raises `exc`.
    void complete(pybind11::object value);
    void fail(pybind11::object exc);

    // Stop delivery (thread-safe, GIL-free) and let a host UI callback notice it must not start or
    // continue. Set when the owning plugin unloads, and by the loop-thread done-callback when the
    // Future is cancelled. Idempotent.
    void abandon() { m_abandoned.store(true, std::memory_order_release); }
    bool abandoned() const { return m_abandoned.load(std::memory_order_acquire); }

    // True once the awaitable has been cancelled or abandoned. Unlike abandoned(), this also reads
    // the Python Future's own cancelled() under the GIL, so it observes a cancellation that
    // Future.cancel() applied synchronously even before the loop runs the done-callback that sets
    // the atomic. It never mutates the Future or the loop. Safe to call from the UI thread.
    bool is_cancelled() const;

    ~AsyncFuture();

private:
    AsyncFuture() = default;

    pybind11::object  m_loop;
    pybind11::object  m_future;
    pybind11::object  m_bind;
    std::atomic<bool> m_abandoned{false};
};

// Binds the `orca.host.ui` submodule: native message boxes, progress dialogs,
// and interactive HTML windows/dialogs for plugins. All calls run on the main/UI
// thread (marshaled from the plugin worker thread) and the host owns every window.
//
// Not safe to call from a slicing pipeline hook (SlicingPipelinePluginCapability):
// that hook runs on the slicing worker thread, which the UI thread can itself be
// blocked waiting on, so marshaling a UI call from there can deadlock. Plugin
// authors must not call orca.host.ui.* from pipeline hooks.
class PluginHostUi
{
public:
    static void RegisterBindings(pybind11::module_& host);

    static void run_on_ui_thread(std::function<void()> fn);

    // Fail synchronously (std::runtime_error) when there is no wx application, matching
    // run_on_ui_thread()'s first check, so an awaitable entry point can reject before it touches
    // asyncio.
    static void require_ui_application();

    // Post `fn` to run on the main/UI thread without blocking the caller. `fn` runs with the GIL
    // released and must touch only C++/wx state and host-owned handles. Returns false (and drops
    // `fn`) when the application is not initialized.
    static bool post_to_ui_thread(std::function<void()> fn);

    // Lifecycle tracking for awaitables posted from a plugin worker. track_pending_async() keys the
    // handle by the current plugin; the registration holds only a weak reference and expires when
    // the UI operation finishes. abandon_pending_async() (plugin unload / draining) stops every
    // still-pending awaitable for the plugin from resolving into an unloaded loop.
    static void track_pending_async(const std::shared_ptr<AsyncFuture>& handle);
    static void abandon_pending_async(const std::string& plugin_key);

    // Number of plugin-worker awaitables for plugin_key whose UI work has been posted but has not
    // finished yet. Expired registrations are pruned, so this reaches 0 once every posted UI
    // callback has returned. PluginManager drains on this before unloading a plugin's module: a
    // Tab::select_preset dialog accepted after that would otherwise mutate presets after unload.
    // GIL-free; safe from any thread.
    static std::size_t pending_async_count(const std::string& plugin_key);

    // Lifecycle hook: close and tear down every UI window owned by a plugin. PluginManager invokes
    // this after plugin teardown and also for bulk unload during application shutdown.
    static void close_windows_for_plugin(const std::string& plugin_key);
};

} // namespace Slic3r
