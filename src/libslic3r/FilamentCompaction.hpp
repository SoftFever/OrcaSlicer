#ifndef slic3r_FilamentCompaction_hpp_
#define slic3r_FilamentCompaction_hpp_

#include "Exception.hpp"

#include <vector>
#include <cstddef>
#include <stdexcept>

namespace Slic3r {

class Model;
class DynamicPrintConfig;
struct ModelFilaments;

// Orca: dense logical tool numbering for printers whose firmware only accepts T0..T(n-1).
//
// A project may hold more filaments than the printer has tools (see
// device_resolves_filament_mapping), so a plate can legitimately print with project slots 3 and
// 6 while the machine has four heads. The Snapmaker U1 takes those slot numbers as-is -- its
// 32-entry extruder_map_table is indexed by them -- but the WonderMaker ZR Ultra S has no macro
// past T(tool_count-1), so slots 3 and 6 have to reach it as T0 and T1 with the mapping saying
// which box each of the two pulls from.
//
// Rather than translating tool numbers at every point that emits one (the toolchange command,
// M104's T parameter, every `[next_extruder]`-indexed template in the vendor profile,
// CoolingBuffer's and GCodeProcessor's parsers), the whole filament index space is renumbered
// once, at the top of Print::apply, before config normalization and variant expansion. Every
// consumer downstream indexes filaments by that same space, so all of them become dense by
// construction and none of them needs to know this exists.
//
// Gated on the namespace (build_filament_compaction): printers that do not resolve the map never
// build a compaction, so nothing below runs for them.
//
// It is also the plate's one record of which filaments it uses (used_slots): every consumer --
// the T numbering, validation, the send dialog, the map delivered to the printer -- reads it
// from Print::filament_compaction() instead of collecting its own.
//
// Mixed-colour filaments are virtual slots: ToolOrdering resolves each to its component
// filaments and only those are ever commanded as tools. The dense space still has to hold the
// mix itself -- objects reference it and its per-filament config rows travel with it -- so a
// used mix is numbered AFTER the physical tools. T0..T(n-1) stay exactly the filaments the
// printer loads, and the printer never sees a mix's number.
struct FilamentCompaction
{
    // The 0-based project filament slot each dense tool number prints: the used physical slots
    // surest first (build_filament_compaction), then any used mixed slots. Empty means "no
    // renumbering" -- either the printer doesn't ask for it or the plate already fits.
    std::vector<int> slot_of_tool;
    // The plate's PHYSICAL filaments (mixed slots expanded), 0-based, ascending. Filled for every
    // device-resolved printer, renumbered or not.
    std::vector<int> used_slots;
    // How many of used_slots are part filaments, which print for certain (the pre-slice refusal
    // counts these). A count, not a prefix of slot_of_tool: a demoted slot moves to its end.
    size_t           certain_count = 0;

    bool is_identity() const;
    // The dense tool number that prints a project slot, or -1 when the plate doesn't use it.
    int  tool_of_slot(int slot_0based) const;
    // The inverse: the 0-based project slot a g-code tool number prints. Everything derived from
    // the sliced g-code (PlateData::parse_filament_info, and the per-extruder statistics behind
    // it) is keyed by tool number, so any consumer that goes on to index a project-indexed vector
    // -- filament_colour, filament_ids, a per-project-filament weight array -- has to come back
    // through here first. Identity when there is no compaction, and a tool this one doesn't cover
    // is returned unchanged, so callers can apply it unconditionally and keep their own bounds
    // check against the project's filament count.
    int  project_slot_of_tool(int tool) const;
    // used_slots as 1-based filament numbers, for the GUI's lists.
    std::vector<int> used_filaments_1based() const;
};

// The plate's filament set and its T numbering, from the model's filaments by tier
// (Print::collect_model_filaments). used_slots/certain_count are always filled. slot_of_tool stays
// empty unless the plate's highest physical slot reaches past the namespace
// (filament_namespace_size); then the physical slots are packed tier by tier -- parts, features,
// wipe tower, tool changes, support -- each ascending, then the used mixed slots, so a filament
// that turns out not to print sits above every filament from a surer tier. Renumbering is safe on
// every device-resolved printer because Orca delivers the map at print time in those numbers.
// `demoted`: slots the last slice of this plate showed never print (Print::process); they move to
// the end of slot_of_tool, the rest keeping their order.
FilamentCompaction build_filament_compaction(const ModelFilaments& filaments, const std::vector<int>& demoted,
                                             const DynamicPrintConfig& config, size_t namespace_size);

// Renumber every filament reference in the model: object / volume / layer-range configs,
// multi-material painting, and the plate's custom tool changes. References to slots the
// compaction doesn't cover become 0 (inherit) -- they belong to features this plate doesn't print.
//
// `source` is the model `model` was copied from (same objects, same order -- Model's assign_copy
// preserves both). Every mutated config and painting annotation gets its timestamp MIRRORED from
// the source: the copy is a deterministic derivation, so change detection must key on the
// source's edit history. Without the mirror, each Print::apply rebuilds the copy with fresh
// timestamps, apply's timestamp-compared state (painting especially) reads as "changed", and the
// slice that just finished is silently invalidated -- an endless slice/discard loop in the GUI.
// Passing the same object as both arguments is allowed (in-place, used by tests); the mirror is
// then a no-op.
void apply_filament_compaction(Model& model, const Model& source, const FilamentCompaction& compaction);

// Gather every per-filament config vector down to the used slots, in dense order, and renumber
// the scalar 1-based filament references (support_filament, *_filament_id, wipe_tower_filament)
// and the component lists of the mixed slots.
void apply_filament_compaction(DynamicPrintConfig& config, const FilamentCompaction& compaction);

// Thrown by Print::process when the plate's printed filaments fit the printer's tools but were
// numbered past its last one (a filament that never printed was numbered below one that did). The
// Print has recorded what did not print; applying the same inputs again renumbers and the reslice
// fits. Callers that can re-apply (Plater, the CLI) do so once; any other caller shows the message.
class FilamentRenumberRequired : public SlicingError
{
public:
    using SlicingError::SlicingError;
};

} // namespace Slic3r

#endif // slic3r_FilamentCompaction_hpp_
