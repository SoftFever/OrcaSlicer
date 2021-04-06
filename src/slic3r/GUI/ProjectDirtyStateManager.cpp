#include "libslic3r/libslic3r.h"
#include "ProjectDirtyStateManager.hpp"
#include "ImGuiWrapper.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"

#if ENABLE_PROJECT_DIRTY_STATE

namespace Slic3r {
namespace GUI {

void ProjectDirtyStateManager::update_from_undo_redo_stack(const Slic3r::UndoRedo::Stack& main_stack, const Slic3r::UndoRedo::Stack& active_stack)
{
    if (!wxGetApp().initialized())
        return;

    wxGetApp().mainframe->update_title();
}

void ProjectDirtyStateManager::update_from_presets()
{
    m_state.presets = false;
    std::vector<std::pair<unsigned int, std::string>> selected_presets = wxGetApp().get_selected_presets();
    for (const auto& [type, name] : selected_presets) {
        m_state.presets |= !m_initial_presets[type].empty() && m_initial_presets[type] != name;
    }
    m_state.presets |= wxGetApp().has_unsaved_preset_changes();
    wxGetApp().mainframe->update_title();
}

void ProjectDirtyStateManager::reset_after_save()
{
    reset_initial_presets();

    m_state.reset();
    wxGetApp().mainframe->update_title();
}

void ProjectDirtyStateManager::reset_initial_presets()
{
    m_initial_presets = std::array<std::string, Preset::TYPE_COUNT>();
    std::vector<std::pair<unsigned int, std::string>> selected_presets = wxGetApp().get_selected_presets();
    for (const auto& [type, name] : selected_presets) {
        m_initial_presets[type] = name;
    }
}

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

