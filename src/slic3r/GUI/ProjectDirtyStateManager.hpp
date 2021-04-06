#ifndef slic3r_ProjectDirtyStateManager_hpp_
#define slic3r_ProjectDirtyStateManager_hpp_

#if ENABLE_PROJECT_DIRTY_STATE

namespace Slic3r {
namespace GUI {

class ProjectDirtyStateManager
{
    struct DirtyState
    {
        bool plater{ false };
        bool presets{ false };

        bool is_dirty() const { return plater || presets; }
    };

    DirtyState m_state;

public:
    bool is_dirty() const { return m_state.is_dirty(); }

#if ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW
    void render_debug_window() const;
#endif // ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW
};

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_PROJECT_DIRTY_STATE

#endif // slic3r_ProjectDirtyStateManager_hpp_

