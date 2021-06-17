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

// returns the current active snapshot (the topmost snapshot in the undo part of the stack) in the given stack
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

// returns the last saveable snapshot (the topmost snapshot in the undo part of the stack that can be saved) in the given stack
static const UndoRedo::Snapshot* get_last_saveable_snapshot(EStackType type, const UndoRedo::Stack& stack,
    const ProjectDirtyStateManager::DirtyState::Gizmos& gizmos, size_t last_save_main) {

    // returns true if the given snapshot is not saveable
    auto skip_main = [&gizmos, last_save_main, &stack](const UndoRedo::Snapshot& snapshot) {
        auto is_gizmo_with_modifications = [&gizmos, &stack](const UndoRedo::Snapshot& snapshot) {
            if (boost::starts_with(snapshot.name, _utf8("Entering"))) {
                if (gizmos.current)
                    return true;

                std::string topmost_redo;
                wxGetApp().plater()->undo_redo_topmost_string_getter(false, topmost_redo);
                if (boost::starts_with(topmost_redo, _utf8("Leaving"))) {
                    const std::vector<UndoRedo::Snapshot>& snapshots = stack.snapshots();
                    const UndoRedo::Snapshot* leaving_snapshot = &(*std::lower_bound(snapshots.begin(), snapshots.end(), UndoRedo::Snapshot(snapshot.timestamp + 1)));
                    if (gizmos.is_used_and_modified(*leaving_snapshot))
                        return true;
                }
            }
            return false;
        };

        if (snapshot.name == _utf8("New Project"))
            return true;
        else if (snapshot.name == _utf8("Reset Project"))
            return true;
        else if (boost::starts_with(snapshot.name, _utf8("Load Project")))
            return true;
        else if (boost::starts_with(snapshot.name, _utf8("Selection")))
            return true;
        else if (boost::starts_with(snapshot.name, _utf8("Entering"))) {
            if (last_save_main != snapshot.timestamp + 1 && !is_gizmo_with_modifications(snapshot))
                return true;
        }
        else if (boost::starts_with(snapshot.name, _utf8("Leaving"))) {
            if (last_save_main != snapshot.timestamp && !gizmos.is_used_and_modified(snapshot))
                return true;
        }
        
        return false;
    };

    // returns true if the given snapshot is not saveable
    auto skip_gizmo = [](const UndoRedo::Snapshot& snapshot) {
        // put here any needed condition to skip the snapshot
        return false;
    };

    const UndoRedo::Snapshot* curr = get_active_snapshot(stack);
    const std::vector<UndoRedo::Snapshot>& snapshots = stack.snapshots();
    size_t shift = 1;
    while (curr->timestamp > 0 && ((type == EStackType::Main && skip_main(*curr)) || (type == EStackType::Gizmo && skip_gizmo(*curr)))) {
        const UndoRedo::Snapshot* temp = curr;
        curr = &(*std::lower_bound(snapshots.begin(), snapshots.end(), UndoRedo::Snapshot(curr->timestamp - shift)));
        shift = (curr == temp) ? shift + 1 : 1;
    }
    if (type == EStackType::Main) {
        if (boost::starts_with(curr->name, _utf8("Entering")) && last_save_main == curr->timestamp + 1) {
            std::string topmost_redo;
            wxGetApp().plater()->undo_redo_topmost_string_getter(false, topmost_redo);
            if (boost::starts_with(topmost_redo, _utf8("Leaving")))
                curr = &(*std::lower_bound(snapshots.begin(), snapshots.end(), UndoRedo::Snapshot(curr->timestamp + 1)));
        }
    }
    return curr->timestamp > 0 ? curr : nullptr;
}

// returns the name of the gizmo contained in the given string
static std::string extract_gizmo_name(const std::string& s) {
    static const std::array<std::string, 2> prefixes = { _utf8("Entering"), _utf8("Leaving") };

    std::string ret;
    for (const std::string& prefix : prefixes) {
        if (boost::starts_with(s, prefix))
            ret = s.substr(prefix.length() + 1);

        if (!ret.empty())
            break;
    }
    return ret;
}

void ProjectDirtyStateManager::DirtyState::Gizmos::add_used(const UndoRedo::Snapshot& snapshot)
{
    const std::string name = extract_gizmo_name(snapshot.name);
    auto it = used.find(name);
    if (it == used.end())
        it = used.insert({ name, { {} } }).first;

    it->second.modified_timestamps.push_back(snapshot.timestamp);
}

void ProjectDirtyStateManager::DirtyState::Gizmos::remove_obsolete_used(const Slic3r::UndoRedo::Stack& main_stack)
{
    const std::vector<UndoRedo::Snapshot>& snapshots = main_stack.snapshots();
    for (auto& item : used) {
        auto it = item.second.modified_timestamps.begin();
        while (it != item.second.modified_timestamps.end()) {
            size_t timestamp = *it;
            auto snapshot_it = std::find_if(snapshots.begin(), snapshots.end(), [timestamp](const Slic3r::UndoRedo::Snapshot& snapshot) { return snapshot.timestamp == timestamp; });
            if (snapshot_it == snapshots.end())
                it = item.second.modified_timestamps.erase(it);
            else
                ++it;
        }
    }
}

#if ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW
bool ProjectDirtyStateManager::DirtyState::Gizmos::any_used_modified() const
{
    for (auto& [name, gizmo] : used) {
        if (!gizmo.modified_timestamps.empty())
            return true;
    }
    return false;
}
#endif // ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW

// returns true if the given snapshot is contained in any of the gizmos caches
bool ProjectDirtyStateManager::DirtyState::Gizmos::is_used_and_modified(const UndoRedo::Snapshot& snapshot) const
{
    for (const auto& item : used) {
        for (size_t i : item.second.modified_timestamps) {
            if (i == snapshot.timestamp)
                return true;
        }
    }
    return false;
}

void ProjectDirtyStateManager::DirtyState::Gizmos::reset()
{
    used.clear();
}

void ProjectDirtyStateManager::update_from_undo_redo_stack(UpdateType type)
{
    if (!wxGetApp().initialized())
        return;

    const Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return;

    const UndoRedo::Stack& main_stack = plater->undo_redo_stack_main();
    const UndoRedo::Stack& active_stack = plater->undo_redo_stack_active();

    if (&main_stack == &active_stack)
        update_from_undo_redo_main_stack(type, main_stack);
    else
        update_from_undo_redo_gizmo_stack(type, active_stack);

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
    const Plater* plater = wxGetApp().plater();
    const UndoRedo::Stack& main_stack = plater->undo_redo_stack_main();
    const UndoRedo::Stack& active_stack = plater->undo_redo_stack_active();

    if (&main_stack == &active_stack) {
        const UndoRedo::Snapshot* saveable_snapshot = get_last_saveable_snapshot(EStackType::Main, main_stack, m_state.gizmos, m_last_save.main);
        m_last_save.main = (saveable_snapshot != nullptr) ? saveable_snapshot->timestamp : 0;
    }
    else {
        const UndoRedo::Snapshot* main_active_snapshot = get_active_snapshot(main_stack);
        if (boost::starts_with(main_active_snapshot->name, _utf8("Entering"))) {
            if (m_state.gizmos.current)
                m_last_save.main = main_active_snapshot->timestamp + 1;
        }
        const UndoRedo::Snapshot* saveable_snapshot = get_last_saveable_snapshot(EStackType::Gizmo, active_stack, m_state.gizmos, m_last_save.main);
        m_last_save.gizmo = (saveable_snapshot != nullptr) ? saveable_snapshot->timestamp : 0;
    }

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
    ImGuiWrapper& imgui = *wxGetApp().imgui();

    auto color = [](bool value) {
        return value ? ImVec4(1.0f, 0.49f, 0.216f, 1.0f) /* orange */: ImVec4(1.0f, 1.0f, 1.0f, 1.0f) /* white */;
    };
    auto bool_to_text = [](bool value) {
        return value ? "true" : "false";
    };
    auto append_bool_item = [color, bool_to_text, &imgui](const std::string& name, bool value) {
        imgui.text_colored(color(value), name);
        ImGui::SameLine();
        imgui.text_colored(color(value), bool_to_text(value));
    };
    auto append_int_item = [&imgui](const std::string& name, int value) {
        imgui.text(name);
        ImGui::SameLine();
        imgui.text(std::to_string(value));
    };
    auto append_snapshot_item = [&imgui](const std::string& label, const UndoRedo::Snapshot* snapshot) {
        imgui.text(label);
        ImGui::SameLine(100);
        if (snapshot != nullptr)
            imgui.text(snapshot->name + " (" + std::to_string(snapshot->timestamp) + ")");
        else
            imgui.text("-");
    };

    imgui.begin(std::string("Project dirty state statistics"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    if (ImGui::CollapsingHeader("Dirty state", ImGuiTreeNodeFlags_DefaultOpen)) {
        append_bool_item("Overall:", is_dirty());
        ImGui::Separator();
        append_bool_item("Plater:", m_state.plater);
        append_bool_item("Presets:", m_state.presets);
        append_bool_item("Current gizmo:", m_state.gizmos.current);
    }

    if (ImGui::CollapsingHeader("Last save timestamps", ImGuiTreeNodeFlags_DefaultOpen)) {
        append_int_item("Main:", m_last_save.main);
        append_int_item("Current gizmo:", m_last_save.gizmo);
    }

    const UndoRedo::Stack& main_stack = wxGetApp().plater()->undo_redo_stack_main();
    const UndoRedo::Snapshot* main_active_snapshot = get_active_snapshot(main_stack);
    const UndoRedo::Snapshot* main_last_saveable_snapshot = get_last_saveable_snapshot(EStackType::Main, main_stack, m_state.gizmos, m_last_save.main);
    const std::vector<UndoRedo::Snapshot>& main_snapshots = main_stack.snapshots();

    if (ImGui::CollapsingHeader("Main snapshots", ImGuiTreeNodeFlags_DefaultOpen)) {
        append_snapshot_item("Active:", main_active_snapshot);
        append_snapshot_item("Last saveable:", main_last_saveable_snapshot);
    }

    if (ImGui::CollapsingHeader("Main undo/redo stack", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const UndoRedo::Snapshot& snapshot : main_snapshots) {
            bool active = main_active_snapshot->timestamp == snapshot.timestamp;
            imgui.text_colored(color(active), snapshot.name);
            ImGui::SameLine(150);
            imgui.text_colored(color(active), " (" + std::to_string(snapshot.timestamp) + ")");
            if (&snapshot == main_last_saveable_snapshot) {
                ImGui::SameLine();
                imgui.text_colored(color(active), " (S)");
            }
            if (m_last_save.main > 0 && m_last_save.main == snapshot.timestamp) {
                ImGui::SameLine();
                imgui.text_colored(color(active), " (LS)");
            }
        }
    }

    const UndoRedo::Stack& active_stack = wxGetApp().plater()->undo_redo_stack_active();
    if (&active_stack != &main_stack) {
        if (ImGui::CollapsingHeader("Gizmo undo/redo stack", ImGuiTreeNodeFlags_DefaultOpen)) {
            const UndoRedo::Snapshot* active_active_snapshot = get_active_snapshot(active_stack);
            const std::vector<UndoRedo::Snapshot>& active_snapshots = active_stack.snapshots();
            for (const UndoRedo::Snapshot& snapshot : active_snapshots) {
                bool active = active_active_snapshot->timestamp == snapshot.timestamp;
                imgui.text_colored(color(active), snapshot.name);
                ImGui::SameLine(150);
                imgui.text_colored(color(active), " (" + std::to_string(snapshot.timestamp) + ")");
            }
        }
    }

    if (m_state.gizmos.any_used_modified()) {
        if (ImGui::CollapsingHeader("Gizmos", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(10.0f);
            for (const auto& [name, gizmo] : m_state.gizmos.used) {
                if (!gizmo.modified_timestamps.empty()) {
                    if (ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                        std::string modified_timestamps;
                        for (size_t i = 0; i < gizmo.modified_timestamps.size(); ++i) {
                            if (i > 0)
                                modified_timestamps += " | ";
                            modified_timestamps += std::to_string(gizmo.modified_timestamps[i]);
                        }
                        imgui.text(modified_timestamps);
                    }
                }
            }
            ImGui::Unindent(10.0f);
        }
    }

    imgui.end();
}
#endif // ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW

void ProjectDirtyStateManager::update_from_undo_redo_main_stack(UpdateType type, const Slic3r::UndoRedo::Stack& stack)
{
    m_state.plater = false;

    if (type == UpdateType::TakeSnapshot) {
        if (m_last_save.main != 0) {
            const std::vector<UndoRedo::Snapshot>& snapshots = stack.snapshots();
            auto snapshot_it = std::find_if(snapshots.begin(), snapshots.end(), [this](const Slic3r::UndoRedo::Snapshot& snapshot) { return snapshot.timestamp == m_last_save.main; });
            if (snapshot_it == snapshots.end())
                m_last_save.main = 0;
        }
        m_state.gizmos.remove_obsolete_used(stack);
    }

    const UndoRedo::Snapshot* active_snapshot = get_active_snapshot(stack);
    if (active_snapshot->name == _utf8("New Project") ||
        active_snapshot->name == _utf8("Reset Project") ||
        boost::starts_with(active_snapshot->name, _utf8("Load Project")))
        return;

    if (boost::starts_with(active_snapshot->name, _utf8("Entering"))) {
        if (type == UpdateType::UndoRedoTo) {
            std::string topmost_redo;
            wxGetApp().plater()->undo_redo_topmost_string_getter(false, topmost_redo);
            if (boost::starts_with(topmost_redo, _utf8("Leaving"))) {
                const std::vector<UndoRedo::Snapshot>& snapshots = stack.snapshots();
                const UndoRedo::Snapshot* leaving_snapshot = &(*std::lower_bound(snapshots.begin(), snapshots.end(), UndoRedo::Snapshot(active_snapshot->timestamp + 1)));
                if (m_state.gizmos.is_used_and_modified(*leaving_snapshot)) {
                    m_state.plater = (leaving_snapshot != nullptr && leaving_snapshot->timestamp != m_last_save.main);
                    return;
                }
            }
        }
        m_state.gizmos.current = false;
        m_last_save.gizmo = 0;
    }
    else if (boost::starts_with(active_snapshot->name, _utf8("Leaving"))) {
        if (m_state.gizmos.current)
            m_state.gizmos.add_used(*active_snapshot);
        m_state.gizmos.current = false;
        m_last_save.gizmo = 0;
    }

    const UndoRedo::Snapshot* last_saveable_snapshot = get_last_saveable_snapshot(EStackType::Main, stack, m_state.gizmos, m_last_save.main);
    m_state.plater = (last_saveable_snapshot != nullptr && last_saveable_snapshot->timestamp != m_last_save.main);
}

void ProjectDirtyStateManager::update_from_undo_redo_gizmo_stack(UpdateType type, const Slic3r::UndoRedo::Stack& stack)
{
    m_state.gizmos.current = false;

    const UndoRedo::Snapshot* active_snapshot = get_active_snapshot(stack);
    if (active_snapshot->name == "Gizmos-Initial")
        return;

    const UndoRedo::Snapshot* last_saveable_snapshot = get_last_saveable_snapshot(EStackType::Gizmo, stack, m_state.gizmos, m_last_save.main);
    m_state.gizmos.current = (last_saveable_snapshot != nullptr && last_saveable_snapshot->timestamp != m_last_save.gizmo);
}

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_PROJECT_DIRTY_STATE

