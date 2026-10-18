# Moonraker filament mapping

Moonraker AFC mapping is available through the legacy print-host upload dialog
and the experimental printer-agent route. Both map only the selected plate’s used
filaments. Upload/Save only never changes mappings or starts printing.

## Shared mapping controls

`PrintHostDialogs.cpp` shares material cards and the slot popup between Flashforge and AFC. Their small `MaterialMappingSlot` view keeps presentation
separate from printer identifiers and protocols. Flashforge retains four numbered
slots and its original card sizes. AFC uses wider cards and named lane badges,
with additional rows for larger filament systems. Source and target colors,
material matching, and manual selection use the same controls. AFC automatically
suggests the nearest color among loaded same-material lanes and reserves distinct
lanes when multiple tool mapping is disabled. Filename and storage controls remain
in the existing print-host send dialog.

## Shared status and preparation sync

`libslic3r/AFCMapping.hpp` declares the snapshot and typed `AFCSelection { tool, lane }`;
`libslic3r/AFCMapping.cpp` implements parsing, normalization, validation, and script generation.
`fetch_afc_status()` reads AFC lane and print status together through Moonraker.
Physical identity is independent of reassigned tool numbers. Preparation sync
adapts it to existing `AmsTrayData`/`build_ams_payload()` trays and
never issues mapping commands. The same snapshot supplies the legacy popup.
The experimental `SelectMachineDialog` retains its existing printer/filament UI.
Pull-mode agents refresh through preparation sync; the default agent interface
hook converts its tool-indexed choices before dispatch. The current tray model
represents tool indices 0–31 in groups of four. Unmapped physical lanes have no
tray, and identical colors can produce duplicate automatic suggestions that need
manual correction when AFC multiple tool mapping is disabled.

## Upload and print

Typed selections serialize only at string-based job boundaries: legacy
`extended_info["afc_mapping"]` and the agent's existing `ams_mapping_info` field.
The agent's `set_gcode_path` hook receives the selected plate's actual raw file,
including G-code already extracted from a sliced 3MF. Default hooks leave other
agents unchanged and preserve the `PrintParams` layout used by binary plugins.

Each route owns its upload/start sequence and invokes shared
`Moonraker::apply_afc_mapping()` between upload and print startup:

1. Upload the file without starting or queuing a mapped print.
2. Fetch fresh status, validate idle state and selected loaded lanes, then apply
   the required AFC `SET_MAP` assignments.
3. Fetch again to verify assignments and readiness before starting the returned path.

Without `multiple_tool_mapping`, AFC's `SET_MAP LANE=X MAP=Tn` swaps: lane X takes
Tn and the lane that held Tn takes X's previous tool. Planning models the full
post-state and rejects a selection that would move any unselected tool, and
verification compares the whole tool map with that expected state rather than only
the selected tools. With `multiple_tool_mapping`, only the requested tool moves.

A lane serves exactly one tool, so taking a lane that serves a tool this print does
not use necessarily moves that tool. Legacy mode rejects such a selection instead of
performing the move silently, because the printer's persistent assignments for other
tools would change as a side effect. The send dialog excludes those lanes from its
automatic suggestion and, on confirmation, validates the choice with `plan()`; it
stays open and names the lane and the tool it would move. The user either picks a
different lane or assigns the displaced tool to one of the print's filaments. The
agent route reports the same rule as an invalid mapping. An idle AFC with no tool
mappings parses successfully with an empty tool map; the send dialog then falls back
to the plain upload dialog.

If AFC detection fails because `printer/objects/list` is unavailable, the plain
upload dialog proceeds without a mapping (fail open), since a powered-off Klipper
must still accept queued uploads. Agent-based prints fail closed once AFC has
been detected for the device.

AFC saves assignments persistently; cancellation or failure after mapping does
not restore them. Errors prevent startup and explain that saved assignments may
remain. Ordinary legacy uploads retain automatic-start/queue behavior; agents retain
`SDCARD_PRINT_FILE` startup. Status checks and commands are not an atomic transaction.

The parsing, planning, and script logic is covered by `tests/libslic3r/test_afc_mapping.cpp`.
The HTTP and verification integration in `Moonraker.cpp` has no automated suite.

## Other filament changers

The controls, typed selection boundary, and sequencing provide reuse points. Happy
Hare already has read-only preparation sync through `fetch_hh_filament_info()`
and `mmu` gate arrays. Another changer needs its own physical-slot adapter,
readiness rules, commands, and assignment verification; AFC behavior does not
define its write interface. No general provider framework is required by this design.
