# Python plugin async bridge — High Level Design

## Purpose and scope

The embedded Python plugin runtime runs capability calls synchronously: a C++ call site acquires the
GIL and the capability's Python method runs to completion on the calling thread. That model cannot
express an operation that has to wait for the user — most visibly the staged preset update
(`begin_update()` in [preset-plugin-api.md](preset-plugin-api.md)), whose target selection can open
unsaved-edit dialogs and must not block Orca's UI thread while it waits.

This document describes the bridge that lets a Python plugin suspend and resume such an operation
without blocking the UI thread. The bridge is deliberately narrow: only the Script capability gains
an awaitable execution path. Every other capability — including the SlicingPipeline hooks — stays
synchronous.

## Execution baseline

### How a capability call runs

A capability instance is a Python object owned through `Slic3r::PluginInstanceHandle`
(`src/slic3r/plugin/PythonPluginBridge.cpp`) and handed to the rest of the program as a
`std::shared_ptr<Slic3r::PluginCapabilityInterface>`. Its identity (`name()`, `type()`,
`audit_plugin_key()`) is resolved once at materialization and cached in C++
(`PluginCapabilityInterface` in `src/slic3r/plugin/PythonPluginInterface.hpp`).

Every C++→Python call crosses a trampoline method. `PyPluginCommonTrampoline` and
`PyPluginInterfaceTrampoline` (`src/slic3r/plugin/PyPluginTrampoline.hpp`), and the per-type subclass
`PyScriptPluginCapabilityTrampoline`
(`src/slic3r/plugin/pluginTypes/script/ScriptPluginCapabilityTrampoline.hpp`), wrap the call in the
`ORCA_PY_OVERRIDE_AUDITED` macro. For one synchronous call that macro, in order:

1. constructs a `PluginCapabilityInterface::RefCounter`, which `increment()`s the capability's
   reference count for the call's duration;
2. constructs a `PythonGILState`, which first acquires a `PythonRuntimeLease` and then
   `PyGILState_Ensure()`;
3. opens a `ScopedPluginAuditContext(audit_plugin_key(), name())`;
4. dispatches into Python via `PYBIND11_OVERRIDE`/`PYBIND11_OVERRIDE_PURE`;

and unwinds those guards when the call returns.

`PythonGILState` and `PythonRuntimeLease` live in `src/slic3r/plugin/PythonInterpreter.hpp`. A lease is
a `shared_lock` on `PythonInterpreter::m_runtime_mutex`; `PythonInterpreter::shutdown()` takes the
exclusive side, so a thread that holds a lease can safely touch Python objects, and interpreter
finalization cannot run underneath it. `ScopedPluginAuditContext`
(`src/slic3r/plugin/PluginAuditManager.hpp`) stores the previous thread-local plugin/capability pair
and scoped filesystem roots, sets this call's, and restores them on destruction; the audit hook then
attributes every filesystem/socket/process event to the plugin currently executing on that thread.

### The Script capability

`Slic3r::ScriptPluginCapability` (`src/slic3r/plugin/pluginTypes/script/ScriptPluginCapability.hpp`)
declares one pure virtual method, `ExecutionResult execute()`. The async runtime invokes it through
`invoke_script_execute` (`src/slic3r/plugin/PyPluginTrampoline.hpp`), which replicates the audited
trampoline call (ref counter, audit scope, traceback-preserving rethrow) but returns a
`ScriptExecuteInvocation` instead of forcing a cast:

- a plain `def execute` runs to completion in `invoke_script_execute` and yields
  `ScriptExecuteInvocation::result`;
- an `async def execute` (or any override that returns a coroutine, detected with
  `inspect.iscoroutine`) yields the coroutine object in `ScriptExecuteInvocation::awaitable` without
  running it, so the runtime can drive it.

`ScriptExecuteInvocation` is defined in `PyPluginTrampoline.hpp`; `py_is_coroutine` is the detection
helper.

### UI marshalling

`PluginHostUi::run_on_ui_thread` (`src/slic3r/plugin/host/PluginHostUi.cpp`) forwards to the
file-local `run_on_ui_blocking`:

- if the caller is already on the wx main thread, it runs the callable inline after a
  `py::gil_scoped_release`;
- otherwise it sets up a `std::promise`/`std::future`, releases the GIL, posts the callable with
  `GUI::wxGetApp().CallAfter`, and blocks on `future.get()`.

That is a blocking handoff: the caller's stack is parked on a thread while the UI thread runs the
callable. It is the correct primitive for the synchronous `update()`/`commit()` path and for
`reload_local_bundle`/`list_bundle_ids`, but it cannot be used by an operation that must suspend.

### Slicing hooks

`Print::set_slicing_pipeline_hook_fn` is installed by `src/slic3r/plugin/PluginHooks.cpp`. The hook
runs on the slicing worker thread and dispatches to `SlicingPipelinePluginCapability::execute(ctx)`
per capability, acquiring a `PythonGILState` per capability. `SlicingPipelineContext`
(`SlicingPipelinePluginCapability.hpp`) holds raw pointers into the live slicing graph
(`ctx.print`, `ctx.object`) that are valid only for the duration of `execute(ctx)`. The UI thread can
be blocked waiting on the slicing worker (`BackgroundSlicingProcess::stop()` parks it), so a
marshalled UI call from a hook can deadlock; `PluginHostSlicing.cpp`, `SlicingPipelinePluginCapability.hpp`
and `PluginHostUi.hpp` all document this.

## Script-only async execution

### Per-invocation worker and loop

`ScriptAsyncRuntime` (declared in `src/slic3r/plugin/PluginManager.hpp`, implemented in
`PluginManager.cpp`) drives every Script invocation off the caller's thread. Its `start()` creates a
`Run` and launches a detached worker thread that runs `Impl::run_invocation`. There is one worker
thread per invocation, so no invocation ever blocks the UI thread and no invocation is queued behind
another plugin's.

`run_invocation` runs detection and execution on the worker:

- it re-checks `cancel_requested` before anything (a cancellation that arrived before the run
  started is honored without running the plugin);
- it calls `invoke_script_execute`; a plain `def execute` result is used directly, and an `async def`
  coroutine is driven on a loop created for that invocation:
  `asyncio.new_event_loop()` → `asyncio.set_event_loop(loop)` → `loop.create_task(coroutine)` →
  `loop.run_until_complete(task)` → `loop.close()`, then `asyncio.set_event_loop(None)`.

The loop is per invocation, not global, and no loop outlives its invocation. The loop for the
calling thread is published in the thread-local `tl_script_loop`, exposed to host awaitables through
`ScriptAsyncRuntime::current_loop()` (GIL held).

`PluginManager::launch_script_capability` is the entry point: it resolves the capability, hands it to
`ScriptAsyncRuntime::start`, and sets `ScriptLaunchResult::launched_async`. The completion callback
runs on the GUI thread (or inline when there is no wx app), and `launch_script_capability` records a
run error through `set_plugin_error` before invoking the caller's `ScriptCompletionFn`.
`PluginManager::run_script_capability` is kept as a compatibility wrapper: it waits for the
completion callback, pumping `wxYield()` when it is itself on the GUI thread, and returns the
`ExecutionResult`.

### Capability, runtime, GIL and audit lifetime across a suspension

For the whole invocation — including an `async def` suspended at an `await` — `run_invocation` holds:

- a `PluginCapabilityInterface::RefCounter` per invocation and a `shared_ptr` to the capability in the
  `Run`, so the capability and its module stay alive across awaits and unload cannot drop them;
- a `PythonRuntimeLease` (acquired before the GIL). `PythonInterpreter::shutdown()` cannot finalize
  the interpreter while any invocation worker holds a lease;
- the GIL, acquired with `pybind11::gil_scoped_acquire` on the worker;
- a `ScopedPluginAuditContext(audit_plugin_key(), name())`, so filesystem/socket/process events
  raised by the coroutine are attributed to the plugin.

The capability reference is released inside the GIL scope (`run->capability.reset()`), because
dropping the pybind holder from a non-Python thread would `DECREF` without the GIL. The loop is
closed and `tl_script_loop` cleared before the worker exits.

A consequence of running Script on a non-main thread: the audit permission prompt is fail-closed off
the main thread. `request_permission` in `src/slic3r/plugin/PluginAuditManager.cpp` posts the dialog
with `CallAfter` and denies the current event, persisting the grant when the user accepts, so a
Script sees a denial on the first attempt and can succeed on a later one. This matches the existing
behavior for the plugin-load worker and the slicing worker.

### Awaitable UI continuation

`AsyncFuture` (`src/slic3r/plugin/host/PluginHostUi.hpp`, implemented in `PluginHostUi.cpp`) is the
suspension primitive for a host call that must show a dialog before it can resolve. It carries no GUI
or preset state — only the loop, the Future created on it, and a Python-side resolver:

- `AsyncFuture::create()` requires the GIL and a running asyncio loop. It binds
  `asyncio.get_running_loop()`, creates a Future with `create_future()`, and installs the resolver
  `make_future_resolver` and the cancellation watcher `make_cancellation_watcher` on the Future.
- `complete(value)`/`fail(exc)` marshal the outcome with `loop.call_soon_threadsafe(m_bind, …)`, so
  the Future is resolved on its own loop thread, never from the UI thread. The resolver drops an
  outcome whose Future is already cancelled (`set_result`/`set_exception` on a cancelled Future would
  raise `InvalidStateError`).
- Cancellation is observed two ways, so the UI thread can decide without touching the loop. The
  loop-thread done-callback `make_cancellation_watcher` sets an atomic (`abandon()`); `is_cancelled()`
  additionally reads the Python Future's own `cancelled()` under the GIL, so a cancellation that
  `Future.cancel()` applied synchronously is seen even before the loop runs the done-callback.
  `is_cancelled()` never mutates the Future or the loop and is safe from the UI thread. A queued
  selection therefore never starts, and one inside a modal stops when the modal returns.
  `~AsyncFuture` releases its Python references under the GIL, or intentionally leaks them if the
  interpreter is already gone (same rule as the UI callable holders).

`PluginHostUi::post_to_ui_thread(fn)` posts work to the UI thread without blocking (returns false when
the application is not initialized); `require_ui_application()` fails synchronously so an awaitable
entry point can reject before touching asyncio.

### Awaitable `begin_update()`

`begin_update()` is a real awaitable. `begin_preset_update_async` and `begin_filament_update_async`
(`src/slic3r/plugin/host/PluginHostPresets.cpp`) run on the plugin worker with the GIL held and a
running loop:

1. validate the application synchronously (`require_ui_application()`), so a call without a wx app
   fails exactly like `update()`;
2. create an `AsyncFuture` on the running loop and register it with
   `PluginHostUi::track_pending_async(handle)`;
3. post the selection to the UI thread with `PluginHostUi::post_to_ui_thread`;
4. return the Future at once.

The UI callback first skips the selection entirely when `handle->is_cancelled()`, then runs the
target binding with the GIL released — `bind_preset_target` for process/printer, `bind_filament_target`
for filament — passing a `CancelProbe` that also reads `handle->is_cancelled()`. It reuses the normal
selection flow `Tab::select_preset()`, including the unsaved-edit prompt. On success it resolves the
Future with the name-based `PresetTransaction`; on failure it fails the Future with the mapped Python
exception. Nothing is staged until the caller's later `set()`/`commit()`. An unavailable target is
rejected before selection and changes no preset. If the user declines selection, decisions already
accepted in an earlier dialog are not rolled back. Coroutine cancellation does not undo an accepted
preset selection; for filament, slot reassignment is skipped when cancellation is observed before the
mapping check, but may remain if cancellation arrives after that check, even when no transaction is
delivered. No GUI or preset pointer crosses the suspension — the awaitable holds only copied
category/name/slot values.

`set()` stages a serialized value without mutating the preset, and `commit()` applies the batch
through `Tab::apply_preset_config_values` on the UI thread; both are synchronous and, per their
bindings, valid only on the background Script worker (they call `PluginHostUi::run_on_ui_thread`,
which blocks the worker, or runs inline if entered from the UI thread).

### Cancellation and accepted-selection boundary

Two distinct cancellations exist:

- **User declined the selection** — `Tab::select_preset()` returned false. `bind_*_target` throws
  `SelectionCancelled`, which the awaitable path maps to `orca.host.preset.PresetSelectionCancelled`
  (a `RuntimeError` subclass). The synchronous `update()` path keeps raising plain `RuntimeError`.
- **The awaiting coroutine was cancelled** — task cancellation or plugin unload. `AsyncFuture`
  observes this two ways: the loop-thread done-callback sets an atomic, and `is_cancelled()` also
  reads the Future's own `cancelled()` under the GIL (see above). `bind_*_target` checks its
  `CancelProbe` after `select_preset()` returns and after the modal-free paths, throwing
  `SelectionAbandoned`; the outcome is dropped because the handle is cancelled/abandoned, so the
  coroutine sees `asyncio.CancelledError`. A queued selection never starts.

`ScriptAsyncRuntime::cancel_plugin(plugin_key)` sets `cancel_requested` on every active run for the
plugin and, when a loop is live (`has_loop`), cancels the task with
`loop.call_soon_threadsafe(task.cancel)` while holding the GIL. A synchronous `def execute`
invocation has no loop, so only the flag is set (checked before the run starts).

The accepted-selection boundary is deliberate and cannot be fully enforced once the UI flow has
begun:

- A selection the user already accepted — including any unsaved-edit dialog decision and the switch
  itself — is a UI decision and is **not** reversed. A cancellation that lands after the modal has
  been answered leaves that accepted selection in place.
- Cancellation only skips the remaining work. In the filament path, the slot assignment and the
  transaction creation are skipped only when the cancellation is observed **before** the slot mapping
  (`bind_filament_target` checks the `CancelProbe` immediately before it reassigns the slot); the
  transaction is likewise skipped when cancellation is observed before it is created.
- A narrow window remains where a cancellation and the operation's start race: if the cancel lands
  just as the UI callback begins, the selection may still start. Whichever side wins, the coroutine
  observes `asyncio.CancelledError`, never a plugin error.

A finished `commit()` is unaffected by a later cancellation.

A plugin's own `asyncio.CancelledError` is treated as a cancellation only when `cancel_requested` was
set; otherwise it stays a genuine error (see `run_invocation`), so a plugin that cancels its own task
does not masquerade as an unload cancellation.

### Unload and shutdown drain

Unloading or shutting down must guarantee that no plugin Python code runs after its module is
unloaded, and that a posted UI selection cannot mutate presets after teardown.

- `PluginManager::unload_plugin` and `unload_all_plugins` call
  `PluginHostUi::abandon_pending_async(plugin_key)` first, so the earliest possible UI callback
  cannot resume the plugin's coroutine into a run that is being cancelled.
- `PluginManager::cancel_and_wait_for_capabilities` calls `on_cancelled()` first (so the plugin can
  observe state while the run is intact), then `ScriptAsyncRuntime::cancel_plugin`, then drains.
  The drain is **unbounded while a Script invocation worker or a posted host UI callback for the
  plugin is outstanding** — its loop-exit condition is `all_released && !invocation_active &&
  !ui_pending`. `has_active_run()` distinguishes a run that was cancelled before it started from one
  whose worker is still live; `pending_async_count()` counts posted UI callbacks whose weak
  registration has not expired. On the GUI thread the drain pumps `wxYield()` so a posted callback
  (possibly a modal) can settle instead of being starved by the blocking wait. Only the non-Script
  path keeps the historical bounded 2-second grace: when neither an invocation nor a UI callback is
  pending, the drain stops waiting for capability references at the grace deadline and logs any
  remaining ones.
- `PluginManager::shutdown` detaches the libslic3r hooks (`plugin_hooks::uninstall`), stops new
  loads, waits for in-flight loads, unloads every plugin (draining the async runs above), then calls
  `ScriptAsyncRuntime::instance().shutdown()`, which refuses new runs and waits without a deadline
  for every invocation worker to finish — nothing is torn down by force. `ScriptAsyncRuntime::reset()`
  re-arms the runtime so a later `initialize()` in the same process can start runs again.

Cancellation triggered by unload or shutdown is a separate, expected completion, not a plugin
failure. `run_invocation` marks a run whose `cancel_requested` was set as `completion.cancelled` and
clears any error, whatever the coroutine body did. `launch_script_capability` records a plugin error
only when `!completion.cancelled && !completion.error.empty()`; `run_script_capability` reports an
empty error for a cancelled completion; and the two GUI launchers treat `cancelled` as neither error
nor success (the speed dial stays silent, the Plugins dialog updates no status). A plugin's own
`CancelledError` is not counted as this expected termination unless `cancel_requested` is set, so a
self-cancelled plugin still records its failure.

## Supported and unsupported capabilities

| Capability | Async support | Reason |
| --- | --- | --- |
| Script | `def execute` and `async def execute` both run on the per-invocation worker | The runtime owns the worker and, for a coroutine, a per-invocation loop; nothing about a Script invocation borrows C++ state. |
| SlicingPipeline | Synchronous only | `execute(ctx)` borrows `Print`/`PrintObject` for the callback's duration, and the UI thread can be blocked on the slicing worker. `PluginHooks.cpp` rejects an async override: `cap->execute()` casts its Python result to `ExecutionResult`, and a coroutine object cannot be cast, so it surfaces as a `SlicingError` rather than being scheduled. |
| PrinterConnection, Pages, Analysis, Importer, Exporter, Visualization, ResourceTexture | Unchanged | Not part of this bridge; they keep their existing synchronous contract. |

## Deadlock constraints

- **Never block the UI thread on plugin work.** Every Script launch path goes through
  `launch_script_capability`; the speed-dial `ActionRegistry` action and the Plugins dialog launcher
  return immediately and report the outcome later (see Examples).
- **Never call `orca.host.ui.*` from a slicing hook.** The slicing worker runs while the UI thread
  can be parked on it.
- **Resolve an awaitable's Future only with `loop.call_soon_threadsafe`.** `AsyncFuture::complete`/
  `fail` do this; the UI thread never touches the loop or the Future directly.
- **Create host awaitables only on the worker with a running loop.** `AsyncFuture::create()` binds
  `asyncio.get_running_loop()`; `begin_update()`'s binding requires this.
- **No pointer crosses a suspension.** Awaitables carry copied values; the `SlicingPipelineContext`
  graph is valid only within one synchronous `execute(ctx)`.

## File and symbol map

Script execution runtime:

- `src/slic3r/plugin/PluginManager.hpp` / `.cpp` — `ScriptAsyncRuntime` (`instance`, `start`,
  `cancel_plugin`, `has_active_run`, `shutdown`, `reset`, `current_loop`), `ScriptCompletion`,
  `ScriptCompletionFn`, `ScriptLaunchResult`, `launch_script_capability`, `run_script_capability`.
- `src/slic3r/plugin/PyPluginTrampoline.hpp` — `invoke_script_execute`, `ScriptExecuteInvocation`,
  `py_is_coroutine`.
- `src/slic3r/plugin/pluginTypes/script/ScriptPluginCapability.hpp` / `ScriptPluginCapabilityTrampoline.hpp`
  — `ScriptPluginCapability::execute`, `PyScriptPluginCapabilityTrampoline`.

Lifetime and GIL primitives:

- `src/slic3r/plugin/PythonInterpreter.hpp` — `PythonInterpreter`, `PythonRuntimeLease`,
  `PythonGILState`.
- `src/slic3r/plugin/PythonPluginInterface.hpp` — `PluginCapabilityInterface`, `RefCounter`,
  `increment`/`decrement`/`ref_count`.
- `src/slic3r/plugin/PluginAuditManager.hpp` / `.cpp` — `ScopedPluginAuditContext`,
  `request_filesystem_read_permissions`, `request_permission` (fail-closed off the main thread).

UI suspension and lifecycle tracking:

- `src/slic3r/plugin/host/PluginHostUi.hpp` / `.cpp` — `AsyncFuture` (`create`, `complete`, `fail`,
  `abandon`, `is_cancelled`), `make_future_resolver`, `make_cancellation_watcher`,
  `PluginHostUi::post_to_ui_thread`, `require_ui_application`, `track_pending_async`,
  `abandon_pending_async`, `pending_async_count`, `run_on_ui_thread`/`run_on_ui_blocking`.
- `GUI::wxGetApp().CallAfter` — the post-to-UI-thread primitive.

Preset targets:

- `src/slic3r/plugin/host/PluginHostPresets.cpp` — `PresetEditor`, `PresetTransaction`,
  `bind_preset_target`, `bind_filament_target`, `begin_preset_update_async`,
  `begin_filament_update_async`, `PresetSelectionCancelled`, `SelectionCancelled`,
  `SelectionAbandoned`, `CancelProbe`, `resolve_combo_preset`/`resolve_preset_in_combo`.
- `src/slic3r/GUI/Tab.hpp` / `.cpp` — `Tab::select_preset`, `Tab::apply_preset_config_values`,
  `Tab::commit_preset_config_value`.

Launch paths:

- `src/slic3r/GUI/ActionRegistry.cpp` — `PluginScriptAction::run`, `notify_script_outcome`.
- `src/slic3r/GUI/PluginsDialog.cpp` — `PluginsDialog::run_script_plugin_capability`.

Slicing (synchronous only):

- `src/slic3r/plugin/PluginHooks.cpp` — `install_slicing_pipeline_hook`.
- `src/slic3r/plugin/pluginTypes/slicingPipeline/SlicingPipelinePluginCapability.hpp` —
  `SlicingPipelineContext`, `execute`.

Unload/shutdown:

- `src/slic3r/plugin/PluginManager.cpp` — `unload_plugin`, `unload_all_plugins`,
  `cancel_and_wait_for_capabilities`, `shutdown`.

## Examples

An `async def execute` that awaits a preset update. `execute` runs on the background Script worker
with its own asyncio loop; `begin_update()` suspends the coroutine until any selection / unsaved-edit
dialog is answered, without blocking the UI thread; `set()`/`commit()` run synchronously on the
worker. This is the shape used by `sandboxes/orca_preset_transaction_demo.py`:

```python
import asyncio

import orca


class AutoTune(orca.script.ScriptPluginCapabilityBase):
    def get_name(self):
        return "Auto tune"

    async def execute(self):
        # begin_update() is awaitable: it resolves once the user answers any
        # selection / unsaved-edit dialog. The UI thread stays responsive.
        tx = await orca.host.preset.process.begin_update(preset_name="0.20mm Standard")
        tx.set("layer_height", "0.2")
        tx.commit()
        return orca.ExecutionResult.success("process preset updated")
```

The window-driven demo shows the same pattern where the plugin's own UI must hand work to the worker:
`on_message` fires on the UI thread and only enqueues onto the worker loop with
`loop.call_soon_threadsafe`; `execute` consumes that queue and awaits `begin_update()` per command.

Continuation sequence for one `await begin_update(...)`:

```text
Script worker (owned, per invocation)        UI thread
  loop.run_until_complete(task)
    await begin_update()  ─────────────────►  post_to_ui_thread
    (suspended)                               bind_*_target →
                                              Tab::select_preset() … dialog
                                              settles
    loop.call_soon_threadsafe(fut.set_result) ◄──── AsyncFuture::complete
    coroutine resumes with PresetTransaction
  execute() returns ExecutionResult
```

A plain `def execute` needs no loop and runs to completion on the same worker; both kinds are
launched with `launch_script_capability`, so the caller (UI thread) never blocks. A launch failure is
reported inline; a started run reports its outcome through the completion callback — the speed dial
shows "started" and pushes a completion notification, and the Plugins dialog updates its status when
`finish` runs.
