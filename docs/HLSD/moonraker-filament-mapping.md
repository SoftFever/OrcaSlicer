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

`Utils/AFCMapping.hpp` declares the snapshot and typed `AFCSelection { tool, lane }`;
`AFCMapping.cpp` implements parsing, normalization, validation, and script generation.
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

AFC saves assignments persistently; cancellation or failure after mapping does
not restore them. Errors prevent startup and explain that saved assignments may
remain. Ordinary legacy uploads retain automatic-start/queue behavior; agents retain
`SDCARD_PRINT_FILE` startup. Status checks and commands are not an atomic transaction.

## Other filament changers

The controls, typed selection boundary, and sequencing provide reuse points. Happy
Hare already has read-only preparation sync through `fetch_hh_filament_info()`
and `mmu` gate arrays. Another changer needs its own physical-slot adapter,
readiness rules, commands, and assignment verification; AFC behavior does not
define its write interface. No general provider framework is required by this design.
