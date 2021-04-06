#include "libslic3r/libslic3r.h"
#include "ProjectDirtyStateManager.hpp"
#include "ImGuiWrapper.hpp"
#include "GUI_App.hpp"

#if ENABLE_PROJECT_DIRTY_STATE

namespace Slic3r {
namespace GUI {

#if ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW
void ProjectDirtyStateManager::render_debug_window() const
{
    auto color = [](bool value) {
        return value ? ImVec4(1.0f, 0.49f, 0.216f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    };
    auto text = [](bool value) {
        return value ? "true" : "false";
    };

    std::string title = "Project dirty state statistics";
    ImGuiWrapper& imgui = *wxGetApp().imgui();
    imgui.begin(title, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    bool dirty = is_dirty();
    imgui.text_colored(color(dirty), "State:");
    ImGui::SameLine();
    imgui.text_colored(color(dirty), text(dirty));

    ImGui::Separator();
    imgui.text_colored(color(m_state.plater), "Plater:");
    ImGui::SameLine();
    imgui.text_colored(color(m_state.plater), text(m_state.plater));

    imgui.text_colored(color(m_state.presets), "Presets:");
    ImGui::SameLine();
    imgui.text_colored(color(m_state.presets), text(m_state.presets));

    imgui.end();
}
#endif // ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_PROJECT_DIRTY_STATE

