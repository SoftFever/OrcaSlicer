#ifndef slic3r_PluginAuditManagerInternal_hpp_
#define slic3r_PluginAuditManagerInternal_hpp_

// Internal seam between the GUI-free audit core and the permission front end. The GUI library
// supplies the dialogs, prompts and .install_state.json persistence; the headless probe supplies
// a fail-closed implementation that never links wx. Production behavior lives in the GUI
// implementation, which is the code that used to be part of PluginAuditManager.cpp.

#include "PluginAuditManager.hpp"

#include <string>
#include <vector>

namespace Slic3r::plugin_audit {

const char* category_name(AuditEventCategory category);

int report_denied(PluginAuditManager&       mgr,
                  const std::string&        event_name,
                  const AuditDecision&      decision,
                  const std::string&        target = {});

// Used only when the manager is not in observation mode. The GUI implementation consults the
// install state, prompts and persists; the probe implementation fails closed.
bool request_filesystem_read_permissions(const std::string&              plugin_key,
                                         const std::vector<std::string>& paths);

// Decides an event that no allowed root or running operation covered. The GUI implementation
// reads the install state and prompts; the probe implementation fails closed. In observation
// mode the audit hook records and returns before reaching this seam.
int decide_event(PluginAuditManager&             mgr,
                 const std::string&              event_name,
                 AuditEventCategory              category,
                 const std::vector<std::string>& targets);

} // namespace Slic3r::plugin_audit

#endif // slic3r_PluginAuditManagerInternal_hpp_
