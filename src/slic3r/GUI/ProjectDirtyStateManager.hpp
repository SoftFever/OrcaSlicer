#ifndef slic3r_ProjectDirtyStateManager_hpp_
#define slic3r_ProjectDirtyStateManager_hpp_

#include "libslic3r/Preset.hpp"

namespace Slic3r {
namespace GUI {

class ProjectDirtyStateManager
{
public:    
    void update_from_undo_redo_stack(bool dirty);
    void update_from_presets();
    void reset_after_save();
    void reset_initial_presets();

    bool is_dirty() const { return m_plater_dirty || m_project_config_dirty || m_presets_dirty; }

#if ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW
    void render_debug_window() const;
#endif // ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW

private:
    // Does the Undo / Redo stack indicate the project is dirty?
    bool                                        m_plater_dirty { false };
    // Do the presets indicate the project is dirty?
    bool                                        m_presets_dirty { false };
    // Is the project config dirty?
    bool                                        m_project_config_dirty { false };
    // Keeps track of preset names selected at the time of last project save.
    std::array<std::string, Preset::TYPE_COUNT> m_initial_presets;
    DynamicPrintConfig                          m_initial_project_config;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_ProjectDirtyStateManager_hpp_
