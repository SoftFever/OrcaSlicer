#include "libslic3r/libslic3r.h"

#include "ProjectDirtyStateManager.hpp"
#include "ImGuiWrapper.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "I18N.hpp"
#include "Plater.hpp"
#include "../Utils/UndoRedo.hpp"

#include <boost/algorithm/string/predicate.hpp>

#include <algorithm>
#include <assert.h>

#if ENABLE_PROJECT_DIRTY_STATE

namespace Slic3r {
namespace GUI {

enum class EStackType
{
    Main,
    Gizmo
};

static const UndoRedo::Snapshot* get_active_snapshot(const UndoRedo::Stack& stack) {
    const std::vector<UndoRedo::Snapshot>& snapshots = stack.snapshots();
    const size_t active_snapshot_time = stack.active_snapshot_time();
    const auto it = std::lower_bound(snapshots.begin(), snapshots.end(), UndoRedo::Snapshot(active_snapshot_time));
    const int idx = it - snapshots.begin() - 1;
    const Slic3r::UndoRedo::Snapshot* ret = (0 <= idx && (size_t)idx < snapshots.size() - 1) ?
        &snapshots[idx] : nullptr;

    assert(ret != nullptr);

    return ret;
}

static const UndoRedo::Snapshot* get_last_valid_snapshot(EStackType type, const UndoRedo::Stack& stack) {
    auto skip_main = [](const UndoRedo::Snapshot& snapshot) {
        return boost::starts_with(snapshot.name, _utf8("Selection")) ||
            boost::starts_with(snapshot.name, _utf8("Entering")) ||
            boost::starts_with(snapshot.name, _utf8("Leaving"));
    };

    const UndoRedo::Snapshot* snapshot = get_active_snapshot(stack);

    const UndoRedo::Snapshot* curr = snapshot;
    const std::vector<UndoRedo::Snapshot>& snapshots = stack.snapshots();
    while (type == EStackType::Main && skip_main(*curr)) {
        curr = &(*std::lower_bound(snapshots.begin(), snapshots.end(), UndoRedo::Snapshot(curr->timestamp - 1)));
    }

    return curr;
}

void ProjectDirtyStateManager::update_from_undo_redo_stack(const Slic3r::UndoRedo::Stack& main_stack, const Slic3r::UndoRedo::Stack& active_stack)
{
    if (!wxGetApp().initialized())
        return;

    if (&main_stack == &active_stack)
        update_from_undo_redo_main_stack(main_stack);
    else
        update_from_undo_redo_gizmo_stack(active_stack);

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

    const Plater* plater = wxGetApp().plater();
    const UndoRedo::Stack& main_stack = plater->undo_redo_stack_main();
    const UndoRedo::Stack& active_stack = plater->undo_redo_stack_active();

    if (&main_stack == &active_stack) {
        const UndoRedo::Snapshot* active_snapshot = get_active_snapshot(main_stack);
        const UndoRedo::Snapshot* valid_snapshot = get_last_valid_snapshot(EStackType::Main, main_stack);

        m_last_save.main = valid_snapshot->timestamp;
    }
    else {
        const UndoRedo::Snapshot* active_snapshot = get_active_snapshot(active_stack);
        const UndoRedo::Snapshot* valid_snapshot = get_last_valid_snapshot(EStackType::Main, main_stack);

    }

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
    ImGuiWrapper& imgui = *wxGetApp().imgui();

    auto color = [](bool value) {
        return value ? ImVec4(1.0f, 0.49f, 0.216f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    };
    auto text = [](bool value) {
        return value ? "true" : "false";
    };
    auto append_item = [color, text, &imgui](const std::string& name, bool value) {
        imgui.text_colored(color(value), name);
        ImGui::SameLine();
        imgui.text_colored(color(value), text(value));
    };

    imgui.begin(std::string( "Project dirty state statistics"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    append_item("State:", is_dirty());
    ImGui::Separator();
    append_item("Plater:", m_state.plater);
    append_item("Presets:", m_state.presets);
    append_item("Current gizmo:", m_state.current_gizmo);

    imgui.end();
}
#endif // ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW

void ProjectDirtyStateManager::update_from_undo_redo_main_stack(const Slic3r::UndoRedo::Stack& stack)
{
    m_state.plater = false;

    const UndoRedo::Snapshot* active_snapshot = get_active_snapshot(stack);
    if (active_snapshot->name == _utf8("New Project") ||
        active_snapshot->name == _utf8("Reset Project") ||
        boost::starts_with(active_snapshot->name, _utf8("Load Project:")))
        return;

    const UndoRedo::Snapshot* valid_snapshot = get_last_valid_snapshot(EStackType::Main, stack);
    m_state.plater = valid_snapshot->timestamp != m_last_save.main;
}

void ProjectDirtyStateManager::update_from_undo_redo_gizmo_stack(const Slic3r::UndoRedo::Stack& stack)
{
    m_state.current_gizmo = false;

    const UndoRedo::Snapshot* active_snapshot = get_active_snapshot(stack);
    if (active_snapshot->name == "Gizmos-Initial")
        return;

    m_state.current_gizmo = true;
}

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_PROJECT_DIRTY_STATE

