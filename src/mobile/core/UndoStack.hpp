#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <vector>

// Mobile-only undo/redo. Holds live Slic3r::Model copies rather than serializing to
// Cereal blobs the way desktop's UndoRedo::Stack does: ModelVolume shares its
// TriangleMesh through a shared_ptr, so N snapshots of the same scene cost N object /
// instance / config copies but only one mesh per unique geometry. This keeps the mobile
// core off libslic3r_gui (Selection, GLGizmosManager and PartPlateList would drag wx +
// OpenGL into the xcframework), and it fits how the Nitro façade already owns a single
// Slic3r::Model per session.
//
// Semantics match the desktop pattern: each snapshot represents the state *after* the
// named action, and the cursor points at the entry matching the current live state.
// take_snapshot pushes {label, current model} at cursor+1, dropping any redo tail; undo
// steps the cursor back and copies that snapshot's model back into the live one; redo
// steps forward. Callers seed an initial "New project" snapshot on session start so undo
// after a first import returns to an empty scene.

namespace Slic3r {
class Model;
}

namespace Slic3r { namespace Mobile {

class UndoStack
{
public:
    // 32 entries is generous for a mobile session; instance/config copies stay cheap
    // while mesh memory is dominated by the unique-mesh pool, not the snapshot count.
    static constexpr std::size_t kDefaultMaxDepth = 32;

    struct SnapshotInfo
    {
        std::string label;
        bool        current = false;
    };

    explicit UndoStack(std::size_t max_depth = kDefaultMaxDepth);
    ~UndoStack();

    UndoStack(const UndoStack&) = delete;
    UndoStack& operator=(const UndoStack&) = delete;

    // Record `model` under `label` at cursor+1, truncating any redo tail beyond the
    // cursor and dropping the oldest snapshot when the stack exceeds max_depth.
    void take_snapshot(std::string label, const Model& model);

    // Step the cursor back / forward and copy that snapshot's model into `out`. Returns
    // false when there is no snapshot to undo / redo, leaving `out` unchanged.
    bool undo(Model& out);
    bool redo(Model& out);

    bool can_undo() const;
    bool can_redo() const;

    // Description of the action that would be undone / redone. Empty when unavailable.
    std::string undo_label() const;
    std::string redo_label() const;

    // Every snapshot, oldest first, with `current` marking the cursor.
    std::vector<SnapshotInfo> snapshots() const;
    void                      clear();

private:
    struct Snapshot
    {
        std::string            label;
        std::unique_ptr<Model> model;

        Snapshot();
        ~Snapshot();
        Snapshot(Snapshot&&) noexcept;
        Snapshot& operator=(Snapshot&&) noexcept;
        Snapshot(const Snapshot&)            = delete;
        Snapshot& operator=(const Snapshot&) = delete;
    };

    std::deque<Snapshot> m_stack;
    // Index of the snapshot that reflects the current live state; -1 means the stack is
    // empty. take_snapshot always leaves the cursor on the entry it just pushed.
    long                 m_cursor = -1;
    std::size_t          m_max_depth;
};

}} // namespace Slic3r::Mobile
