# Preset Plugin API — High Level Design

## Purpose and scope

The preset plugin API lets Python plugins update the presets OrcaSlicer is using,
so users do not have to perform every routine setting change by hand. It covers
process, printer, and filament presets. All three use one staged update lifecycle;
the way a target is selected is specific to the preset type.

The API edits Orca's managed edited preset, not a detached copy or a saved preset
behind the UI's back. It does not save to disk. Saving, discarding, and transferring
unsaved edits remain under Orca's existing preset workflow.

The API does not create or delete presets or provide off-tab editing. For filament,
choosing a different `preset_name` as the update target also selects that preset for
the requested slot, following the normal UI flow. A standalone slot-assignment or
preset-fork operation is outside this API's contract.

## Preset and slot model

Each preset collection maintains a managed edited copy for its selected preset.
Settings tabs apply changes to that copy and own the normal dirty-state, validation,
save/discard/transfer, and downstream refresh behavior. Plugin updates must use the
same path so that the settings tab, dirty indicators, and effective print
configuration observe a consistent state.

Filament selection adds a slot-to-preset mapping. `PresetBundle::filaments` is the
catalog, while `PresetBundle::filament_presets` maps each slot to a preset name.
Several slots may refer to the same preset. A filament update edits that shared
preset, so all slots referring to it are affected. Slot numbers exposed by the API
are 1-based, matching the UI.

## API shape

Each preset API exposes `begin_update()`, which selects and binds one target, and
returns a transaction. The transaction stages setting changes using `set()` and
applies them only on explicit `commit()`.

```python
# Filament is targeted by its 1-based slot. Omitting preset_name keeps the
# preset currently assigned to the slot; providing it selects that preset for
# the slot before staging updates.
tx = await orca.host.preset.filament.begin_update(
    slot_number=2,
    preset_name="Generic PLA"
)
tx.set("filament_flow_ratio", "0.98")
tx.set("filament_retract_length", "0.8")
result = tx.commit()

# Process and printer default to their active presets. A caller can request a
# different preset that is selectable in the corresponding UI dropdown.
tx = await orca.host.preset.process.begin_update(
    preset_name="0.20mm Standard"
)
tx.set("layer_height", "0.2")
tx.commit()
```

The example expresses the intended API contract. The current Python bridge does not
support this awaitable transaction shape; supporting it requires new coroutine or
continuation handling. Any wait for a UI decision must suspend the plugin operation
without blocking Orca's UI thread.

### Target resolution

- **Filament:** `slot_number` identifies the slot. If `preset_name` is omitted,
  the transaction targets the preset currently assigned to that slot. If supplied,
  it requests that preset for the slot and selects it through the normal UI flow;
  the name must be among the presets offered by that slot's dropdown. Selecting a
  different preset changes the slot-to-preset mapping. In both cases the transaction
  edits the shared preset, not an isolated slot override.
- **Process and printer:** omitting `preset_name` targets the currently active
  preset. Supplying `preset_name` requests the preset by the name shown in its
  dropdown. It must be one of the choices the dropdown currently offers, rather
  than merely any preset present in the collection. This is particularly important
  for process presets, whose selectable choices can depend on the current context.
- A transaction targets exactly one preset. Multi-preset transactions are not
  supported: applying them would require switching among targets while the
  application has only one managed edited copy per collection.

Targets are resolved through Orca's normal preset-selection model. The API should
not accept raw preset pointers or filesystem paths as target identities. A missing,
unavailable, or ambiguous target fails explicitly instead of silently selecting a
different preset.

## Selection and user interaction

`begin_update()` resolves its target, then delegates any required selection to the
shared underlying `switch_preset` operation. It calls `switch_preset` only when the
requested target differs from the active target; otherwise it returns a transaction
without switching. `switch_preset` is responsible for using Orca's normal preset
selection flow, including updating a filament slot assignment when requested. If
the normal selection flow requires user input, it opens the relevant existing
dialog(s), including unsaved-edit choices such as save, discard, or transfer. The
operation is awaitable and remains pending until those interactions finish; it
resolves with the switch result or cancellation, without applying staged settings.
A canceled switch does not change the requested target or filament slot assignment,
but it does not roll back decisions the user already accepted in an earlier dialog.
`switch_preset` is a proposed implementation-level coordinator, not an existing
function or a separate plugin API. It must use the normal type-specific selection
paths and confirm that the requested preset actually became active; a successful
selection request alone is not sufficient.

If the requested target is already active, `begin_update()` returns without a
selection prompt, apart from any UI-thread dispatch needed by the bridge. If a
different preset is requested, Orca selects it through the normal UI flow. For
filament, this also updates the requested slot's assignment when `preset_name`
differs from its current assignment. When the current preset has unsaved changes,
the plugin operation remains pending until the user chooses among Orca's existing
options, such as transfer, discard, or cancel.

The API never suppresses that warning, chooses an option for the user, or switches
away from dirty edits silently. Cancel aborts the operation before any staged target
changes are applied. Process and printer updates follow this rule too when the
plugin requests a different `preset_name`; they normally return immediately when
the active preset is the target.

## Staging and commit semantics

`set(key, value)` stages a serialized config value and does not mutate the preset.
The transaction is the boundary for a related group of changes, avoiding
intermediate partially-updated states. At commit, the API validates keys and values
against the target preset type and applies the batch through the existing tab and
settings-group update path. Existing setting-specific validation, correction, and
callbacks remain authoritative; plugin updates must not create a parallel setter
with different behavior.

A successful commit updates dirty state and downstream configuration/Plater state
once for the batch where possible. The edited preset remains unsaved; persistence
continues through Orca's normal save action. A validation or commit failure must
not leave only some staged target settings applied. This all-or-nothing batch
guarantee requires a batch-aware commit path; the current setter applies one option
at a time and cannot provide it by simply looping over existing calls. A filament slot assignment
made as part of target selection is not a staged setting: after the user accepts
the selection flow, it may remain changed even if a later commit fails. The API
does not automatically switch back or reverse the user's selection or any accepted
transfer/discard decision for the previous preset.

## Outcomes and consistency

The API distinguishes target resolution failures, invalid setting/value, user
cancellation, stale filament-slot mapping, and commit failure. A result identifies
the target preset, whether preset selection changed, and (for filament) whether the
slot assignment changed and which slots share the edited preset. A canceled
selection applies neither the requested slot assignment nor staged target changes;
a failed commit applies none of the staged target settings.

Filament slot assignments can change while a plugin operation is pending. The API
must detect that and reject a stale operation instead of silently editing the new
slot assignment. It must not hold raw preset pointers across reloads. Conflicting
duplicate writes to one setting in a transaction are rejected rather than resolved
by incidental call order.

The existing one-setting filament call,
`orca.host.preset.filament.update(slot_number, key, value)`, can remain as a
compatibility shorthand. It shares the staged API's target resolution, selection,
validation, and commit path.

## Design principles

- **Reduce routine interaction, preserve agency.** Clean switches should be
  seamless; unsaved user work must still receive the normal warning and decision.
- **One source of truth.** UI and plugins share the managed edited copy and setting
  update path, including validation and callbacks.
- **The UI workflow is authoritative.** The API must enter through the same
  selection and setting-change workflows as the UI, not reproduce their logic or
  write config values directly. This keeps existing and future side effects active:
  `on_change` handling for mutually exclusive, invalid, or out-of-range settings;
  compatibility and dependent-preset updates; and printer-agent changes when the
  selected printer changes.
- **Explicit target and explicit commit.** Plugins know which preset they edit and
  when staged changes become visible.
- **No hidden persistence.** Plugin edits remain unsaved until the user saves them
  through Orca.
- **Shared means shared.** Editing a filament preset through one slot affects every
  slot assigned to it. Choosing another preset for a slot is explicit in
  `preset_name`; slot-specific divergence would require a separate fork operation.
- **Consistent lifecycle, type-specific targeting.** Process, printer, and
  filament share transaction and error semantics while respecting their existing
  selection models.

## Feasibility against the current codebase

The design builds on existing preset and tab behavior, but the transaction API and
some target-selection behavior are new. The current plugin surface exposes
per-setting `update()` calls. For filament, `update_filament_slot()` resolves the
preset already assigned to a slot, selects it through `Tab::select_preset()`, and
applies one setting through `Tab::apply_preset_config_value()`. It does not change
the slot assignment. There is no current `begin_update()`, transaction type, or
`switch_preset()` helper.

`Tab::select_preset()` is the existing basis for switching to process, printer, or
filament presets and for invoking the normal unsaved-edit flow. The flow is
type-specific: a process selection can trigger compatibility handling for
filaments, and a printer selection can involve technology changes and dependent
preset compatibility. It may therefore show more than one dialog. Cancellation of
a later step does not necessarily undo decisions the user already accepted in an
earlier dialog. The plugin operation must preserve those side effects and verify
the resulting edited target.

The filament `preset_name` behavior is an additional integration, not a capability
of `Tab::select_preset()`: selecting a different name for a slot must also update
the slot-to-preset mapping through the Plater's normal selection behavior. That
path updates more than the vector entry, including dependent UI/configuration state.
The API should reuse or factor that path rather than call
`PresetBundle::set_filament_preset()` alone. It must also define which UI-visible
slots are valid, including whether mixed-color virtual slots are targetable.

Process and printer dropdown choices are constrained by visibility and
compatibility, and compatibility may depend on the active context. `Tab::select_preset()`
performs its own compatibility and technology checks; merely finding a name in the
collection does not prove it is an allowed dropdown choice. Target validation must
follow the same rules as the UI.

The current `PluginHostUi::run_on_ui_thread()` path marshals work synchronously and
waits for completion; modal selection dialogs block that operation until answered.
It is not an asyncio/coroutine bridge. Thus the user-facing wait exists today, but
the proposed Python `await` behavior does not. An awaitable `switch_preset()` needs
new plugin scheduling/continuation support and must avoid blocking the UI thread or
deadlocking callers running on threads that the UI itself waits for.

Finally, `Tab::apply_preset_config_value()` validates and commits one setting at a
time. It backs up one option and invokes callbacks that can adjust related settings.
A transaction that promises all-or-nothing updates and one downstream refresh needs
a batch-aware prepare/commit mechanism that accounts for those callback effects;
repeating the current single-setting method would allow partial updates and repeated
refreshes. Since `begin_update()` performs target selection before the caller stages
values, invalid staged values can still follow a user-visible selection prompt. That
is a consequence of this API shape and should remain explicit in its behavior.

## Integration boundary

The plugin API coordinates target resolution, preset selection, staged values, and
the settings commit path. It does not own or duplicate preset dirty-state logic,
selection prompts, setting validation, or persistence. Target changes must flow
through the same UI selection path so dependent behavior—such as selecting the
printer agent—still runs. Setting changes must flow through the same UI change path
so `on_change` and related validation/correction behavior still runs. The
integration must bridge potential UI prompts to the Python operation
asynchronously, reuse the normal Plater slot-selection behavior for explicit
filament targets, then commit a batch without losing existing per-setting behavior.
