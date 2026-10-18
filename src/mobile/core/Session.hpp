#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// A slicing session: the presets, the model and the print of one project.
//
// One Session is one project. The bridge layer owns the instance, serializes every call
// on a single worker thread, and marshals progress callbacks to the UI thread; nothing
// here is thread-safe except cancel(), which may be called from any thread while
// slice() runs. Slicing is synchronous by design: the bridge decides how it runs.
//
// libslic3r types never appear in this header so the bridge and the renderer compile
// without the core's include tree.
namespace Slic3r {
class Model;
class Print;
class PresetBundle;
struct GCodeProcessorResult;
}

namespace Slic3r { namespace Mobile {

enum class PresetKind { Printer, Filament, Process };

struct PresetInfo
{
    std::string name;
    std::string vendor;       // vendor id for system presets, empty otherwise
    bool        is_system     = false;
    bool        is_user       = false;
    bool        is_visible    = false; // installed in the desktop sense; the app may ignore this
    bool        is_compatible = false; // with the selected printer
    bool        is_selected   = false;
};

// A preset named by kind, as the import / export and cloud calls address them.
struct PresetRef
{
    PresetKind  kind = PresetKind::Printer;
    std::string name;
};

// A preset an import or a cloud pull could not take. `reason` is one of
//   "system_name"    a system preset of that name exists (desktop skips these too)
//   "missing_parent" `inherits` names a preset that is not loaded; `vendor_hint` is the
//                    vendor whose profile ships that parent when the vendor is present in
//                    the resources directory, otherwise empty
//   "exists"         a user preset of that name exists and overwriting was off
//   "invalid"        not a preset file, or unreadable
struct SkippedPreset
{
    std::string name;
    std::string reason;
    std::string inherits;
    std::string vendor_hint;
};

struct ImportPresetsResult
{
    std::vector<PresetRef>     imported;
    std::vector<SkippedPreset> skipped;
    // User presets the archive would overwrite. Only filled when overwrite is off; the
    // caller asks, then calls again with overwrite on.
    std::vector<std::string>   conflicts;
};

// Cloud sync state of a user preset, mirroring the .info sidecar the desktop keeps.
struct CloudPresetState
{
    PresetKind  kind = PresetKind::Printer;
    std::string name;
    std::string setting_id;   // cloud id, empty before the first successful push
    std::string base_id;      // the parent's setting_id, when the parent is a cloud preset
    std::string sync_info;    // "create", "update", "hold", "will_not_sync" or empty (in sync)
    long long   updated_time = 0;
};

struct CloudApplyResult
{
    std::string                error;       // non-empty when the payload could not be read
    std::vector<SkippedPreset> unresolved;  // presets whose parent is not installed
};

// A deletion the cloud has not confirmed yet: the preset's file is gone, its .info
// sidecar stays with sync_info = delete until cloud_confirm_deleted().
struct PendingDelete
{
    std::string setting_id;
    std::string info_path;
};

using Vec3 = std::array<double, 3>;

struct ObjectInfo
{
    unsigned long id = 0;   // stable for the life of the session
    std::string   name;
    Vec3          position { 0, 0, 0 }; // instance offset, mm
    Vec3          rotation { 0, 0, 0 }; // Euler angles, radians
    Vec3          scale    { 1, 1, 1 };
    Vec3          size     { 0, 0, 0 }; // world-space bounding box, mm
    unsigned long triangles = 0;
    // Unconnected face edges summed over the model parts; 0 for a watertight mesh.
    // Non-zero is what the desktop object list flags with its warning icon.
    unsigned long open_edges = 0;
};

// Flat, non-indexed triangles in world space: nine floats per triangle in `positions`
// and the same layout of per-face normals in `normals`.
struct MeshData
{
    std::vector<float> positions;
    std::vector<float> normals;
    unsigned long      triangles = 0;
};

struct BedInfo
{
    std::vector<std::array<double, 2>> shape; // printable area polygon, mm
    double                             height = 0; // printable height, mm
    // Absolute paths to the printer's bed texture (PNG/SVG) and bed model (STL).
    // Empty when the selected printer preset does not customize them, in which
    // case the renderer falls back to a plain grid.
    std::string                        custom_texture;
    std::string                        custom_model;
    // Triangles for the printer's custom bed model, if one was configured and
    // loaded successfully. The renderer draws this instead of a flat quad when
    // present. Empty triangles list means "no model — use the plate polygon".
    MeshData                           model_mesh;
};

struct Progress
{
    int         percent = 0;
    std::string message;
};

// OutOfMemory is a cancellation the session raised itself through
// cancel_out_of_memory(), or a std::bad_alloc caught during processing; the app
// should offer to simplify or shrink the model rather than report a bug.
enum class SliceOutcome { Finished, Cancelled, Failed, OutOfMemory };

struct SliceWarning
{
    std::string text;
    bool        critical = false;
};

struct SliceResult
{
    SliceOutcome              outcome = SliceOutcome::Failed;
    std::string               error;      // set when outcome is Failed
    std::vector<SliceWarning> warnings;
};

struct SliceStatistics
{
    double        print_time_s      = 0; // normal mode estimate
    double        filament_mm3      = 0; // total extruded volume
    double        filament_g        = 0; // by filament density where known
    double        filament_cost     = 0;
    unsigned long layer_count       = 0;
};

// Geometry for the renderer. These are the only types the renderer sees; it never
// includes libslic3r.

enum class PreviewMoveType : unsigned char { Noop, Retract, Unretract, Seam, ToolChange, ColorChange, PausePrint, CustomGCode, Travel, Wipe, Extrude };

enum class PreviewRole : unsigned char {
    None, Perimeter, ExternalPerimeter, OverhangPerimeter, InternalInfill, SolidInfill, TopSolidInfill, BottomSurface,
    Ironing, BridgeInfill, InternalBridgeInfill, GapFill, Skirt, Brim, Support, SupportInterface, SupportTransition,
    WipeTower, Custom, Mixed
};

// One G-code move. Consecutive vertices form the toolpath; the segment ending at vertex i
// is drawn with vertex i's attributes, as libvgcode does.
struct PreviewVertex
{
    float           position[3] = { 0, 0, 0 }; // mm
    float           width       = 0;           // mm
    float           height      = 0;           // mm
    float           feedrate    = 0;           // mm/s
    float           fan_speed   = 0;           // percent
    float           temperature = 0;           // celsius
    float           volumetric_rate = 0;       // mm3/s
    float           time        = 0;           // s from print start, normal mode
    unsigned int    layer_id    = 0;
    unsigned char   extruder_id = 0;
    unsigned char   color_id    = 0;           // color-change index
    PreviewRole     role        = PreviewRole::None;
    PreviewMoveType type        = PreviewMoveType::Noop;
};

struct PreviewData
{
    std::vector<PreviewVertex>         vertices;
    std::vector<float>                 layer_zs;    // top z of each layer, by layer id
    std::vector<float>                 layer_times; // seconds spent on each layer, by layer id
    std::vector<std::array<float, 4>>  tool_colors; // RGBA per extruder, from the filament colours
    float                              print_time_s = 0;
};

class Session
{
public:
    // Nested types referenced by method signatures further down. Declared first so
    // the compiler sees them before use in open_project(), snapshot_history(), etc.
    struct PlateInfo
    {
        unsigned long id           = 0;
        unsigned long object_count = 0;
        bool          is_sliced    = false;
        bool          is_current   = false;
    };
    struct SnapshotInfo
    {
        std::string label;
        bool        current = false;
    };

    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Presets. load_presets() reads the vendor profiles from the resources directory and
    // the user presets from the data directory; returns an error text or an empty string.
    // reload_presets() first wipes data/system so a same-version reinstall of the resources
    // (e.g. after the app removed and re-fetched a vendor) really refreshes the mirror.
    std::string              load_presets();
    std::string              reload_presets();
    std::vector<PresetInfo>  presets(PresetKind kind) const;
    std::string              selected_preset(PresetKind kind) const;
    // Select by name. Filaments are per extruder; select_preset selects extruder 0.
    bool                     select_preset(PresetKind kind, const std::string& name);
    bool                     select_filament(size_t extruder, const std::string& name);
    BedInfo                  bed() const;

    // Preset lifecycle. save_preset_as forks the currently-edited preset of `kind` under
    // a new user preset name and writes it to disk (data_dir/user/...); the new preset
    // is selected. Returns false if `name` already exists on a preset that cannot be
    // overwritten (system, project-embedded, external). delete_preset removes a user
    // preset from the collection and unlinks its file; false if the preset is not user-
    // owned or does not exist. preset_file returns the absolute path of a preset's JSON
    // file, empty if the preset has no on-disk backing.
    bool                     save_preset_as(PresetKind kind, const std::string& name);
    bool                     delete_preset(PresetKind kind, const std::string& name);
    std::string              preset_file(PresetKind kind, const std::string& name) const;

    // Preset files. import_presets reads a desktop export — a single preset .json, a
    // .zip of presets, or an .orca_printer / .orca_filament / .orca_bundle archive — with
    // PresetBundle::import_presets, so classification, parent resolution and the diff-
    // against-parent save match the desktop. With `as_bundle` off (the default) archives
    // land as plain user presets; on, they keep the desktop's read-only bundle semantics.
    // With `overwrite` off, user presets of the same name are reported in `conflicts`
    // and left untouched. The caller should reload_presets() afterwards.
    // export_presets writes the named user presets as a zip at `out_path`; the
    // extension picks the layout: .orca_printer (one printer plus its filaments and
    // processes, bundle_structure.json included), .orca_bundle (any mix, same layout)
    // or .zip (flat, no structure file). Printer presets are written with their print
    // host and API key fields stripped, as the desktop does. Returns the path written;
    // throws std::runtime_error on failure or when a preset is not user-owned.
    ImportPresetsResult      import_presets(const std::string& path, bool overwrite, bool as_bundle);
    std::string              export_presets(const std::vector<PresetRef>& presets, const std::string& out_path);

    // Orca Cloud sync, preset side only — the app does the HTTP. The data directory
    // keeps one user preset folder per cloud account (data/user/<user_id>) plus
    // data/user/default when logged out; set_cloud_user picks which one the next
    // load_presets() reads and every save writes to. An empty id means logged out.
    void                           set_cloud_user(const std::string& user_id);
    std::string                    cloud_user() const;
    // User presets that need a push: never synced, or marked create / update.
    std::vector<CloudPresetState>  cloud_dirty_presets();
    // JSON for one push: {"content":{...flat string map...},"updated_time":N,
    // "setting_id":"...","sync_info":"..."}. `content` is the preset's difference from its
    // parent plus name / type / version / inherits / base_id, exactly what the desktop
    // uploads. Empty when the preset is not a user preset.
    std::string                    cloud_push_payload(PresetKind kind, const std::string& name);
    // Record the outcome of a push: the (possibly new) setting_id, the server's
    // updated_time (0 keeps the local one) and the new sync_info ("" when in sync).
    void                           cloud_mark_synced(PresetKind kind, const std::string& name, const std::string& setting_id,
                                                     long long updated_time, const std::string& sync_info);
    // Apply a FULL pull: a JSON array of {id, name, updated_time, content}. Presets missing
    // from the array that were synced before are removed locally, so never pass a
    // partial list. Presets whose parent is not installed come back in `unresolved`.
    CloudApplyResult               cloud_apply_pull(const std::string& upserts_json);
    // Deletions awaiting the cloud's confirmation, and the call that drops the sidecar
    // once the server has deleted the preset.
    std::vector<PendingDelete>     cloud_pending_deletes() const;
    void                           cloud_confirm_deleted(const std::string& setting_id);

    // Options, read and written on the edited preset that owns the key. Values are in
    // the serialized form libslic3r uses in its config files ("0.2", "1,2,3", "nil").
    std::string              option(const std::string& key) const;
    bool                     set_option(const std::string& key, const std::string& value);
    // Keys whose edited value differs from the selected preset, per kind.
    std::vector<std::string> modified_options(PresetKind kind) const;
    void                     discard_modified_options(PresetKind kind);
    // Every print, filament and printer option definition as a JSON array: key, type,
    // label, category, tooltip, unit, mode, default, enum values and labels, min, max.
    static std::string       option_definitions_json();

    // Project I/O. save_project writes every plate + its objects as a BBS-style
    // multi-plate .3mf; the file re-opens with all plates restored in another
    // Orca instance or desktop Orca. open_project replaces the entire project
    // with the plates read from the file. import() is the additive form for STL,
    // OBJ, STEP, AMF and single-plate 3MFs — new objects land on the current
    // plate rather than clobbering the project. All three throw on failure.
    std::string              save_project(const std::string& path);
    std::vector<PlateInfo>   open_project(const std::string& path);
    std::vector<ObjectInfo>  import(const std::string& path);
    std::vector<ObjectInfo>  objects() const;
    bool                     remove_object(unsigned long id);
    bool                     set_transform(unsigned long id, const Vec3& position, const Vec3& rotation, const Vec3& scale);
    // Object manipulation shortcuts. Return the new / mutated object info.
    // `mirror_object` flips a scale axis to negative; the renderer picks
    // that up automatically. `lay_flat` and `auto_orient` are the same
    // libslic3r routine (`Slic3r::orient(ModelInstance*)`), exposed under
    // two names because desktop separates them in the UI.
    ObjectInfo               duplicate_object(unsigned long id);
    bool                     mirror_object(unsigned long id, char axis /* 'x','y','z' */);
    bool                     lay_flat(unsigned long id);
    bool                     auto_orient(unsigned long id);
    // Cuts an object horizontally at world-space Z = `z`, keeping both halves and
    // placing the lower part flat on the bed. Removes the original from the plate
    // and adds every fragment. Returns the new object ids in bed order.
    std::vector<unsigned long> cut_object(unsigned long id, double z);

    // Reduce the object's model parts to about `target_triangles` faces in total with
    // Garland-Heckbert quadric edge collapse (libslic3r's its_quadric_edge_collapse, the
    // desktop Simplify gizmo). The budget is shared between parts by their share of the
    // triangles. The main use is rescuing a mesh too heavy to slice in a phone's memory.
    // Returns false when the object is unknown, already under the target, or cancel()
    // was called while it ran, in which case nothing changed. Blocks for seconds on a
    // million-face mesh, so the bridge runs it like slice(). Takes one undo snapshot,
    // "Simplify mesh".
    bool                     simplify_object(unsigned long id, unsigned long target_triangles);

    // What repair_object() did. Counts are summed over the object's model parts.
    struct MeshRepairResult
    {
        bool          changed          = false; // a part's mesh was replaced
        unsigned long open_edges_before = 0;
        unsigned long open_edges_after  = 0;
        unsigned long vertices_merged  = 0;     // duplicate vertices united
        unsigned long degenerate_faces = 0;     // zero-area faces dropped
        unsigned long faces_removed    = 0;     // unconnected faces dropped by admesh
        unsigned long faces_reversed   = 0;     // winding flipped to agree with neighbours
        unsigned long edges_fixed      = 0;     // near-coincident edges snapped together
    };
    // Re-run on an object the repair libslic3r applies when it reads an STL: merge
    // duplicate vertices, drop degenerate faces, then admesh's nearby-edge snapping,
    // unconnected-face removal and normal fixing. It cannot fill holes (neither can the
    // desktop without Netfabb / Windows), so open_edges_after may stay non-zero. Takes
    // one undo snapshot, "Repair mesh", when anything changed.
    MeshRepairResult         repair_object(unsigned long id);
    // Ray-cast against every visible object instance. Returns the id of
    // the closest hit and the world-space intersection point, or 0 in
    // `hit_id` when nothing was picked.
    struct PickHit
    {
        unsigned long id       = 0;
        Vec3          position { 0, 0, 0 };
    };
    PickHit                  pick_at(const Vec3& ray_origin, const Vec3& ray_direction) const;
    void                     clear();
    void                     arrange();

    // Plates. A project holds one or more plates; every model / print method above
    // operates on the current one. Presets are shared across plates — switching plates
    // does not change the selected printer or filament. IDs are monotonic and stable
    // across other-plate removals so the bridge can hold on to a "focused plate" id
    // through a delete elsewhere in the project. Removing the last plate is refused.
    unsigned long              add_plate();
    /** Clone the current plate — same objects, same instance transforms, fresh
     * undo history seeded with a "Duplicated plate" snapshot. Returns the new
     * plate's id and switches focus to it. */
    unsigned long              duplicate_plate();
    bool                       remove_plate(unsigned long id);
    bool                       select_plate(unsigned long id);
    unsigned long              current_plate() const;
    std::vector<PlateInfo>     plates() const;

    // Multi-material / AMS. A printer preset lists N nozzles (usually 1); each slot
    // holds one filament. `extruder_count()` is the size of `selected_filaments()`
    // and matches how many <select_filament> pickers the JS side should show. Per-
    // object assignment writes to ModelObject::config's `extruder` key, one-based
    // per libslic3r convention; 0 means "inherit from the process preset".
    std::size_t                extruder_count() const;
    std::vector<std::string>   selected_filaments() const;
    int                        object_extruder(unsigned long id) const;
    bool                       set_object_extruder(unsigned long id, int extruder);

    // Undo / redo. The discrete mutations above (import, remove_object, duplicate_object,
    // mirror_object, lay_flat, auto_orient, arrange, clear) snapshot themselves. Continuous
    // interactions the bridge drives frame-by-frame — currently only set_transform — do
    // not; the caller records a single snapshot at the end of the gesture with
    // take_snapshot("Move object" / "Rotate" / "Scale"), matching desktop's label set.
    // Undo history is per-plate: switching plates preserves each plate's stack.
    void                       take_snapshot(std::string label);
    // Restore the previous / next snapshot into the live model. Returns false when there
    // is nothing to undo / redo. Invalidates any current slice.
    bool                       undo();
    bool                       redo();
    bool                       can_undo() const;
    bool                       can_redo() const;
    // Description of the action that would be undone / redone; empty when unavailable.
    std::string                undo_label() const;
    std::string                redo_label() const;
    // Every snapshot, oldest first, with `current` marking the entry that matches the
    // live model.
    std::vector<SnapshotInfo>  snapshot_history() const;

    // Slicing. slice() blocks until the print is processed, cancelled or fails and
    // invalidates any earlier result. It first frees the previous plate result so the
    // old and new toolpaths never coexist at peak. cancel() may be called from another
    // thread; it also stops a running simplify_object().
    using ProgressFn = std::function<void(const Progress&)>;
    SliceResult              slice(const ProgressFn& on_progress);
    void                     cancel();
    // cancel() plus the reason: the slice reports SliceOutcome::OutOfMemory instead of
    // Cancelled. For the bridge's memory watchdog, which sees the platform's headroom
    // figure (the façade has no platform includes). Safe from any thread.
    void                     cancel_out_of_memory();
    // Cap TBB's worker count for the slice. libslic3r's peak memory grows with the
    // worker count, since every worker holds its own layer intermediates; the bridge
    // sets this from the device's memory. 0 (the default) leaves TBB at the hardware
    // concurrency, as the desktop runs.
    void                     set_thread_limit(unsigned threads);
    bool                     is_sliced() const;

    // Export the processed print. Returns the path written; throws std::runtime_error.
    std::string              export_gcode(const std::string& path);
    SliceStatistics          statistics() const;

    // Renderer input. mesh() is the object's triangles in world space (empty for an unknown
    // id); preview() is the toolpath of the last exported G-code (empty before export).
    MeshData                 mesh(unsigned long id) const;
    PreviewData              preview() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}} // namespace Slic3r::Mobile
