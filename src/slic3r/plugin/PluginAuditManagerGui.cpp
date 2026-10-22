#include "PluginAuditManagerInternal.hpp"

#include "PluginDescriptor.hpp"
#include "PluginFsUtils.hpp"
#include "PluginManager.hpp"

#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cstddef>
#include <future>
#include <memory>
#include <slic3r/plugin/PluginAuditManager.hpp>
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

// A sandbox plugin may not have a sidecar yet. Seed the small amount of install metadata the
// writer needs so clicking Yes still creates the JSON file.
void seed_install_metadata(const PluginDescriptor& descriptor, PluginInstallState& state)
{
    if (!state.plugin_name.empty())
        return;

    state.installed_from    = descriptor.is_cloud_plugin() ? "cloud" : "local";
    state.installed_version = !descriptor.installed_version.empty() ? descriptor.installed_version : descriptor.version;
    state.plugin_name       = descriptor.name;
    state.cloud_uuid        = descriptor.cloud_uuid();
    state.enabled           = true;
}

bool persist_permission(const std::string&        plugin_key,
                        PluginInstallState&       state,
                        std::vector<std::string>& permission_list,
                        const std::string&        target)
{
    PluginDescriptor descriptor;
    if (!PluginManager::instance().try_get_plugin_descriptor(plugin_key, descriptor) || descriptor.plugin_root.empty())
        return false;

    seed_install_metadata(descriptor, state);

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

// Shows a prompt whose body may cover several permission buckets. Must run on the GUI thread.
bool prompt_for_permission_message(const wxString& message)
{
    wxMessageDialog dialog(nullptr, message, _L("Plugin permissions"), wxYES_NO | wxICON_WARNING);
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
// request_permissions runs on -- would therefore deadlock the application (the
// invariant PluginHostUi.cpp documents for slicing-hook UI calls). Off the main thread the prompt
// is therefore posted asynchronously and the current event denied (fail closed, like an unanswered
// prompt); the grant is persisted once the user accepts, so a later attempt succeeds without
// re-prompting.
bool request_permission(AuditEventCategory category, const std::string& plugin_key, const std::string& plugin_name,
                        const std::string& event_name, bool by_event, const std::vector<std::string>& keys)
{
    const std::vector<std::string> shown = by_event ? std::vector<std::string>{} : keys;
    std::string targets_text = by_event ? "<none>" : "";
    for (const auto& target : shown) {
        if (!targets_text.empty())
            targets_text += ", ";
        targets_text += target;
    }

    BOOST_LOG_TRIVIAL(info) << "[AUDIT PROMPT] plugin=" << plugin_key << " event=" << event_name
                            << " category=" << plugin_audit::category_name(category) << " targets=[" << targets_text << "]"
                            << (by_event && !keys.empty() ? " grant=" + keys.front() : std::string{});

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

bool plugin_audit::request_permissions(const std::string& plugin_key, const PluginPermissions& requested)
{
    if (plugin_key.empty())
        return true;

    PluginDescriptor descriptor;
    if (!PluginManager::instance().try_get_plugin_descriptor(plugin_key, descriptor) || descriptor.plugin_root.empty())
        return false;

    PluginInstallState state;
    read_install_state(boost::filesystem::path(descriptor.plugin_root), state);

    // Per bucket, keep only the targets that no earlier grant already covers. Dropping these
    // before prompting keeps a repeated declaration from asking again for what is on disk.
    struct MissingBucket {
        AuditEventCategory       category;
        std::vector<std::string> targets;
    };
    std::vector<MissingBucket> missing_buckets;

    const auto collect_missing = [&missing_buckets](AuditEventCategory category,
                                                    const std::vector<std::string>& requested_targets,
                                                    const std::vector<std::string>& granted_targets) {
        MissingBucket bucket{category, {}};
        for (const std::string& target : requested_targets) {
            if (target.empty())
                continue;
            if (std::find(granted_targets.begin(), granted_targets.end(), target) == granted_targets.end() &&
                std::find(bucket.targets.begin(), bucket.targets.end(), target) == bucket.targets.end())
                bucket.targets.push_back(target);
        }
        if (!bucket.targets.empty())
            missing_buckets.push_back(std::move(bucket));
    };

    collect_missing(AuditEventCategory::FsRead, requested.fs_read, state.permissions.fs_read);
    collect_missing(AuditEventCategory::FsReadWrite, requested.fs_readwrite, state.permissions.fs_readwrite);
    collect_missing(AuditEventCategory::Http, requested.network_http, state.permissions.network_http);
    collect_missing(AuditEventCategory::Socket, requested.network_socket, state.permissions.network_socket);
    collect_missing(AuditEventCategory::ProcessCreate, requested.process, state.permissions.process);
    collect_missing(AuditEventCategory::Threading, requested.threading, state.permissions.threading);

    if (missing_buckets.empty())
        return true;

    if (wxTheApp == nullptr || GUI::wxGetApp().is_closing())
        return false;

    const auto build_message = [](const std::string& plugin_name, const std::vector<MissingBucket>& buckets) {
        wxString message;
        for (const MissingBucket& bucket : buckets) {
            wxString targets;
            for (const std::string& target : bucket.targets)
                targets += wxString::FromUTF8(target.c_str()) + "\n";

            if (!message.IsEmpty())
                message += "\n\n";
            message += audit_message(bucket.category, wxString::FromUTF8(plugin_name.c_str()), wxEmptyString, targets,
                                     bucket.targets.size());
        }
        return message;
    };

    bool granted = false;
    if (wxIsMainThread()) {
        granted = prompt_for_permission_message(build_message(descriptor.name, missing_buckets));
    } else {
        auto result = std::make_shared<std::promise<bool>>();
        auto future = result->get_future();
        GUI::wxGetApp().CallAfter([result, plugin_name = descriptor.name, missing_buckets, build_message]() {
            result->set_value(prompt_for_permission_message(build_message(plugin_name, missing_buckets)));
        });
        granted = future.get();
    }

    if (!granted)
        return false;

    // The dialog can stay open while another grant is persisted; re-read before saving so this
    // request adds to the current state instead of replacing it.
    PluginInstallState latest_state;
    if (read_install_state(boost::filesystem::path(descriptor.plugin_root), latest_state))
        state = std::move(latest_state);

    seed_install_metadata(descriptor, state);

    for (const MissingBucket& bucket : missing_buckets) {
        std::vector<std::string>* permission_list =
            permission_list_for(bucket.category, /*by_event=*/false, state.permissions);
        if (!permission_list)
            continue;
        for (const std::string& target : bucket.targets)
            if (std::find(permission_list->begin(), permission_list->end(), target) == permission_list->end())
                permission_list->push_back(target);
    }

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
