#ifndef slic3r_ProjectDirtyStateManager_hpp_
#define slic3r_ProjectDirtyStateManager_hpp_

#include "libslic3r/Preset.hpp"

#if ENABLE_PROJECT_DIRTY_STATE

namespace Slic3r {
namespace UndoRedo {
class Stack;
} // namespace UndoRedo
namespace GUI {

class ProjectDirtyStateManager
{
    struct DirtyState
    {
        bool plater{ false };
        bool presets{ false };

        bool is_dirty() const { return plater || presets; }
        void reset() {
            plater = false;
            presets = false;
        }
    };

    DirtyState m_state;

    // keeps track of initial selected presets
    std::array<std::string, Preset::TYPE_COUNT> m_initial_presets;

public:
    bool is_dirty() const { return m_state.is_dirty(); }
    void update_from_undo_redo_stack(const Slic3r::UndoRedo::Stack& main_stack, const Slic3r::UndoRedo::Stack& active_stack);
    void update_from_presets();
    void reset_after_save();
    void reset_initial_presets();
#if ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW
    void render_debug_window() const;
#endif // ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW
};

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_PROJECT_DIRTY_STATE

#endif // slic3r_ProjectDirtyStateManager_hpp_

