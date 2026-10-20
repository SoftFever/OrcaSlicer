#include "PluginAuditManagerInternal.hpp"

#include "PluginDescriptor.hpp"
#include "PluginFsUtils.hpp"
#include "PluginManager.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <slic3r/GUI/GUI_App.hpp>
#include <slic3r/GUI/I18N.hpp>

#include <wx/app.h>
#include <wx/msgdlg.h>
#include <wx/string.h>
#include <wx/thread.h>

namespace Slic3r {
namespace {

// The saved list that grants an event: its category's target list, or the event-grant list for
// an event with no target. Kept apart, so that no grant doubles as a target of the same name (an
// os.fork grant is not an os.exec of a command named "os.fork").
std::vector<std::string>* permission_list_for(AuditEventCategory category, bool by_event, PluginPermissions& permissions)
{
    if (by_event)
        return &permissions.events;

    switch (category) {
    case AuditEventCategory::FsRead:        return &permissions.fs_read;
    case AuditEventCategory::FsReadWrite:   return &permissions.fs_readwrite;
    case AuditEventCategory::Http:          return &permissions.network_http;
    case AuditEventCategory::Socket:        return &permissions.network_socket;
    case AuditEventCategory::ProcessCreate: return &permissions.process;
    case AuditEventCategory::Threading:     return &permissions.threading;
    case AuditEventCategory::ProcessReplace: return &permissions.process;
    default:                                return nullptr;
    }
}

// An event with no target is granted by its effective category and name, e.g. "FsRead:open"
// for a read of a file descriptor, which does not grant a write of one ("FsReadWrite:open").
std::string event_grant_key(AuditEventCategory category, const std::string& event_name)
{
    return std::string(plugin_audit::category_name(category)) + ":" + event_name;
}

bool has_permission(const std::vector<std::string>& granted, const std::string& target)
{
    return std::find(granted.begin(), granted.end(), target) != granted.end();
}

bool persist_permission(const std::string&        plugin_key,
                        PluginInstallState&       state,
                        std::vector<std::string>& permission_list,
                        const std::string&        target)
{
    PluginDescriptor descriptor;
    if (!PluginManager::instance().try_get_plugin_descriptor(plugin_key, descriptor) || descriptor.plugin_root.empty())
        return false;

    // A sandbox plugin may not have a sidecar yet.  Seed the small amount of install metadata
    // needed by the writer so clicking Yes still creates the JSON file.
    if (state.plugin_name.empty()) {
        state.installed_from    = descriptor.is_cloud_plugin() ? "cloud" : "local";
        state.installed_version = !descriptor.installed_version.empty() ? descriptor.installed_version : descriptor.version;
        state.plugin_name       = descriptor.name;
        state.cloud_uuid        = descriptor.cloud_uuid();
        state.enabled            = true;
    }

    if (!has_permission(permission_list, target))
        permission_list.push_back(target);
    return write_install_state(boost::filesystem::path(descriptor.plugin_root), state);
}

wxString audit_message(AuditEventCategory category, const wxString& plugin_name, const wxString& event_name,
                       const wxString& target_list, size_t target_count)
{
    if (target_list.IsEmpty())
        return wxString::Format(
            _L("Plugin \"%s\" is requesting permission for the Python audit event \"%s\".\n\n"
                "This operation does not expose a target the audit hook can display."),
            plugin_name, event_name);

    switch (category) {
    case AuditEventCategory::FsRead:
        return wxString::Format(_L_PLURAL("Plugin \"%s\" is requesting to read the following file:\n%s",
                                          "Plugin \"%s\" is requesting to read the following files:\n%s", target_count),
                                plugin_name, target_list);
    case AuditEventCategory::FsReadWrite:
        return wxString::Format(_L_PLURAL("Plugin \"%s\" is requesting to read/write the following file:\n%s",
                                          "Plugin \"%s\" is requesting to read/write the following files:\n%s", target_count),
                                plugin_name, target_list);
    case AuditEventCategory::FsCreate:
        return wxString::Format(_L_PLURAL("Plugin \"%s\" is requesting to create the following file:\n%s",
                                          "Plugin \"%s\" is requesting to create the following files:\n%s", target_count),
                                plugin_name, target_list);
    case AuditEventCategory::FsDelete:
        return wxString::Format(_L_PLURAL("Plugin \"%s\" is requesting to delete the following file:\n%s",
                                          "Plugin \"%s\" is requesting to delete the following files:\n%s", target_count),
                                plugin_name, target_list);
    case AuditEventCategory::Http:
        return wxString::Format(_L("Plugin \"%s\" is requesting to make an HTTP request to:\n%s"), plugin_name, target_list);
    case AuditEventCategory::Socket:
        return wxString::Format(_L("Plugin \"%s\" is requesting to open a network connection to:\n%s"), plugin_name, target_list);
    case AuditEventCategory::ProcessCreate:
        return wxString::Format(_L_PLURAL("Plugin \"%s\" is requesting to run the following command:\n%s",
                                          "Plugin \"%s\" is requesting to run the following commands:\n%s", target_count),
                                plugin_name, target_list);
    case AuditEventCategory::Threading:
        return wxString::Format(_L("Plugin \"%s\" is requesting permission to create a thread."), plugin_name);
    case AuditEventCategory::ProcessReplace:
        return wxString::Format(_L("Plugin \"%s\" is requesting to replace the running application with:\n%s"), plugin_name, target_list);
    default:
        return wxString::Format(_L("Plugin \"%s\" is requesting permission for the Python audit event \"%s\"."), plugin_name, event_name);
    }
}

// Builds and shows the modal permission prompt. Must run on the GUI thread.
bool prompt_for_targets(AuditEventCategory category, const std::string& plugin_name, const std::string& event_name,
                        const std::vector<std::string>& unresolved)
{
    wxString target_list;
    for (const auto& target : unresolved)
        target_list += wxString::FromUTF8(target.c_str()) + "\n";

    wxMessageDialog dialog(nullptr,
                           audit_message(category, wxString::FromUTF8(plugin_name.c_str()),
                                        wxString::FromUTF8(event_name.c_str()), target_list, unresolved.size()),
                           _L("Plugin permission request"), wxYES_NO | wxICON_WARNING);
    return dialog.ShowModal() == wxID_YES;
}

// Records a grant in the plugin's sidecar so it is not asked again. Reads the install state
// freshly because the async prompt outlives the caller's stack copy of it.
void persist_grant(const std::string& plugin_key, AuditEventCategory category, bool by_event, const std::vector<std::string>& keys)
{
    PluginInstallState state;
    if (!PluginManager::instance().get_install_state(plugin_key, state))
        return;

    std::vector<std::string>* permission_list = permission_list_for(category, by_event, state.permissions);
    if (!permission_list)
        return;

    for (const auto& key : keys)
        persist_permission(plugin_key, state, *permission_list, key);
}

PluginAuditManager::PermissionPrompt s_permission_prompt_for_testing;

// Asks the user to grant an audited event, returning true when they approve an inline prompt.
// `keys` are what a Yes saves: the unresolved targets, or the event-grant key when the event has
// no target (by_event), which the prompt does not show as a target.
//
// An audited event can fire on a thread the UI thread may itself be blocked waiting on: the
// SlicingPipeline hook runs on the slicing worker thread (see PluginHooks.cpp), and
// BackgroundSlicingProcess::stop()/stop_internal() park the UI thread until that worker stops.
// Blocking the worker on a marshaled modal -- which is safe for the plugin-load worker that
// request_filesystem_read_permissions runs on -- would therefore deadlock the application (the
// invariant PluginHostUi.cpp documents for slicing-hook UI calls). Off the main thread the prompt
// is therefore posted asynchronously and the current event denied (fail closed, like an unanswered
// prompt); the grant is persisted once the user accepts, so a later attempt succeeds without
// re-prompting.
bool request_permission(AuditEventCategory category, const std::string& plugin_key, const std::string& plugin_name,
                        const std::string& event_name, bool by_event, const std::vector<std::string>& keys)
{
    const std::vector<std::string> shown = by_event ? std::vector<std::string>{} : keys;

    if (s_permission_prompt_for_testing)
        return s_permission_prompt_for_testing(event_name, shown);

    if (wxTheApp == nullptr || GUI::wxGetApp().is_closing())
        return false;

    if (wxIsMainThread())
        return prompt_for_targets(category, plugin_name, event_name, shown);

    GUI::wxGetApp().CallAfter([category, plugin_key, plugin_name, event_name, by_event, keys, shown]() {
        if (wxTheApp == nullptr || GUI::wxGetApp().is_closing())
            return;
        if (prompt_for_targets(category, plugin_name, event_name, shown))
            persist_grant(plugin_key, category, by_event, keys);
    });
    return false;
}

// Decides an event that no allowed root or running operation covers: a saved grant allows it,
// otherwise the user is asked and a Yes is saved.
int decide_audited_event(PluginAuditManager&             mgr,
                         PluginInstallState&              state,
                         const std::string&                plugin_key,
                         const std::string&                plugin_name,
                         const std::string&                event_name,
                         AuditEventCategory                category,
                         const std::vector<std::string>&   targets)
{
    const bool                     by_event = targets.empty();
    const std::vector<std::string> keys     = by_event ? std::vector<std::string>{event_grant_key(category, event_name)} : targets;
    std::vector<std::string>*      permission_list = permission_list_for(category, by_event, state.permissions);

    std::vector<std::string> unresolved = keys;
    if (permission_list) {
        unresolved.clear();
        for (const auto& key : keys)
            if (!has_permission(*permission_list, key))
                unresolved.push_back(key);
        if (unresolved.empty())
            return 0;
    }

    // Letting a root frame go runs finalizers inside a scope destructor or another event's hook,
    // where no dialog may open: what they do must already be granted.
    if (mgr.releasing_frames())
        return plugin_audit::report_denied(mgr, event_name, {false, "audit permission required"});

    if (!request_permission(category, plugin_key, plugin_name, event_name, by_event, unresolved))
        return plugin_audit::report_denied(mgr, event_name, {false, "audit permission required"});

    if (permission_list)
        for (const auto& key : unresolved)
            persist_permission(plugin_key, state, *permission_list, key);
    return 0;
}

} // namespace

bool plugin_audit::request_filesystem_read_permissions(const std::string&              plugin_key,
                                                       const std::vector<std::string>& paths)
{
    PluginDescriptor descriptor;
    if (!PluginManager::instance().try_get_plugin_descriptor(plugin_key, descriptor) || descriptor.plugin_root.empty())
        return false;

    PluginInstallState state;
    read_install_state(boost::filesystem::path(descriptor.plugin_root), state);

    std::vector<std::string> missing;
    for (const std::string& path : paths) {
        if (std::find(state.permissions.fs_read.begin(), state.permissions.fs_read.end(), path) == state.permissions.fs_read.end())
            missing.push_back(path);
    }
    if (missing.empty())
        return true;

    if (wxTheApp == nullptr || GUI::wxGetApp().is_closing())
        return false;

    auto show_dialog = [&descriptor, &missing]() {
        wxString requested_paths;
        for (const std::string& path : missing)
            requested_paths += wxString::FromUTF8(path.c_str()) + "\n";

        wxMessageDialog dialog(
            nullptr,
            wxString::Format(_L("Plugin \"%s\" requests filesystem read access to:\n%s"),
                             wxString::FromUTF8(descriptor.name.c_str()), requested_paths),
            _L("Plugin permissions"),
            wxYES_NO | wxICON_WARNING);
        return dialog.ShowModal() == wxID_YES;
    };

    bool granted = false;
    if (wxIsMainThread()) {
        granted = show_dialog();
    } else {
        auto result = std::make_shared<std::promise<bool>>();
        auto future = result->get_future();
        GUI::wxGetApp().CallAfter([result, descriptor_name = descriptor.name, missing]() {
            wxString requested_paths;
            for (const std::string& path : missing)
                requested_paths += wxString::FromUTF8(path.c_str()) + "\n";

            wxMessageDialog dialog(
                nullptr,
                wxString::Format(_L("Plugin \"%s\" requests filesystem read access to:\n%s"),
                                 wxString::FromUTF8(descriptor_name.c_str()), requested_paths),
                _L("Plugin permissions"),
                wxYES_NO | wxICON_WARNING);
            result->set_value(dialog.ShowModal() == wxID_YES);
        });
        granted = future.get();
    }

    if (!granted)
        return false;

    if (state.plugin_name.empty()) {
        state.installed_from    = descriptor.is_cloud_plugin() ? "cloud" : "local";
        state.installed_version = !descriptor.installed_version.empty() ? descriptor.installed_version : descriptor.version;
        state.plugin_name       = descriptor.name;
        state.cloud_uuid        = descriptor.cloud_uuid();
        state.enabled            = true;
    }

    state.permissions.fs_read.insert(state.permissions.fs_read.end(), missing.begin(), missing.end());
    return write_install_state(boost::filesystem::path(descriptor.plugin_root), state);
}

int plugin_audit::decide_event(PluginAuditManager&             mgr,
                               const std::string&              event_name,
                               AuditEventCategory              category,
                               const std::vector<std::string>& targets)
{
    PluginInstallState state;
    const bool have_install_state = PluginManager::instance().get_install_state(mgr.current_plugin(), state);
    if (!have_install_state) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " Failed to get install state for " << mgr.current_plugin();
    }

    const std::string plugin_name = state.plugin_name.empty() ? mgr.current_plugin() : state.plugin_name;

    return decide_audited_event(mgr, state, mgr.current_plugin(), plugin_name, event_name, category, targets);
}

void PluginAuditManager::set_permission_prompt_for_testing(PermissionPrompt prompt)
{
    s_permission_prompt_for_testing = std::move(prompt);
}

} // namespace Slic3r
