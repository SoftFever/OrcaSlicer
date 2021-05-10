#ifndef slic3r_ProjectDirtyStateManager_hpp_
#define slic3r_ProjectDirtyStateManager_hpp_

#include "libslic3r/Preset.hpp"

#if ENABLE_PROJECT_DIRTY_STATE

namespace Slic3r {
namespace UndoRedo {
class Stack;
struct Snapshot;
} // namespace UndoRedo

namespace GUI {
class ProjectDirtyStateManager
{
public:
    enum class UpdateType : unsigned char
    {
        TakeSnapshot,
        UndoRedoTo
    };

    struct DirtyState
    {
        struct Gizmos
        {
            struct Gizmo
            {
                std::vector<size_t> modified_timestamps;
            };

            bool current{ false };
            std::map<std::string, Gizmo> used;

            void add_used(const UndoRedo::Snapshot& snapshot);
            void remove_obsolete_used(const Slic3r::UndoRedo::Stack& main_stack);
#if ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW
            bool any_used_modified() const;
#endif // ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW
            bool is_used_and_modified(const UndoRedo::Snapshot& snapshot) const;
            void reset();
        };

        bool plater{ false };
        bool presets{ false };
        Gizmos gizmos;

        bool is_dirty() const { return plater || presets || gizmos.current; }
        void reset() {
            plater = false;
            presets = false;
            gizmos.current = false;
        }
    };

private:
    struct LastSaveTimestamps
    {
        size_t main{ 0 };
        size_t gizmo{ 0 };

        void reset() {
            main = 0;
            gizmo = 0;
        }
    };

    DirtyState m_state;
    LastSaveTimestamps m_last_save;

    // keeps track of initial selected presets
    std::array<std::string, Preset::TYPE_COUNT> m_initial_presets;

public:
    bool is_dirty() const { return m_state.is_dirty(); }
    void update_from_undo_redo_stack(UpdateType type);
    void update_from_presets();
    void reset_after_save();
    void reset_initial_presets();
#if ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW
    void render_debug_window() const;
#endif // ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW

private:
    void update_from_undo_redo_main_stack(UpdateType type, const Slic3r::UndoRedo::Stack& stack);
    void update_from_undo_redo_gizmo_stack(UpdateType type, const Slic3r::UndoRedo::Stack& stack);
};

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_PROJECT_DIRTY_STATE

#endif // slic3r_ProjectDirtyStateManager_hpp_

