# Moonraker filament mapping

Moonraker AFC mapping is available through the legacy print-host upload dialog.
It maps only the selected plate’s used filaments. Upload/Save only never changes
mappings or starts printing.

## Shared mapping controls

`PrintHostDialogs.cpp` shares material cards and the slot popup between Flashforge and AFC. Their small `MaterialMappingSlot` view keeps presentation
separate from printer identifiers and protocols. Flashforge retains four numbered
slots and its original card sizes. AFC uses wider cards and named lane badges,
with additional rows for larger filament systems. Source and target colors,
material matching, and manual selection use the same controls. AFC automatically
suggests the nearest color among loaded same-material lanes and reserves distinct
lanes when multiple tool mapping is disabled. Filename and storage controls remain
in the existing print-host send dialog.

## AFC status

`libslic3r/AFCMapping.hpp` declares the snapshot and typed `AFCSelection { tool, lane }`;
`libslic3r/AFCMapping.cpp` implements parsing, normalization, validation, and script generation.
`fetch_afc_status()` reads AFC lane and print status together through Moonraker.
Physical identity is independent of reassigned tool numbers. The snapshot supplies
the lane choices in the send dialog and supports pre-print mapping verification.
Supported tool indices are 0 through 31.

## Upload and print

Typed selections serialize into `extended_info["afc_mapping"]` at the upload job
boundary. `Moonraker::apply_afc_mapping()` runs between upload and print startup:

1. Upload the file without starting or queuing a mapped print.
2. Fetch fresh status, validate idle state and selected loaded lanes, then apply
   the required AFC `SET_MAP` assignments.
3. Fetch again to verify assignments and readiness before starting the returned path.

Without `multiple_tool_mapping`, AFC's `SET_MAP LANE=X MAP=Tn` swaps: lane X takes
Tn and the lane that held Tn takes X's previous tool, including tools this print
does not use. Planning models the full post-state, and verification compares the
whole tool map with that expected state rather than only the selected tools.
With `multiple_tool_mapping`, only the requested tool moves.

Automatic suggestions and manual selection can use any loaded lane of the same
material. Swaps may change persistent assignments for tools outside the print,
even when their current lanes are empty. An idle AFC with no tool mappings parses
successfully with an empty tool map; the send dialog then falls back to the plain
upload dialog.

If AFC detection fails because `printer/objects/list` is unavailable, the plain
upload dialog proceeds without a mapping (fail open), since a powered-off Klipper
must still accept queued uploads.

AFC saves assignments persistently; cancellation or failure after mapping does
not restore them. Errors prevent startup and explain that saved assignments may
remain. Ordinary uploads retain automatic-start/queue behavior.
Status checks and commands are not an atomic transaction.

The parsing, planning, and script logic is covered by `tests/libslic3r/test_afc_mapping.cpp`.
The HTTP and verification integration in `Moonraker.cpp` has no automated suite.

## Other filament changers

The controls, typed selection boundary, and sequencing provide reuse points. Happy
Hare already has read-only preparation sync through `fetch_hh_filament_info()`
and `mmu` gate arrays. Another changer needs its own physical-slot adapter,
readiness rules, commands, and assignment verification; AFC behavior does not
define its write interface. No general provider framework is required by this design.
