// Headless replacement for the GUI permission front end. The probe installs an observation
// observer, so the audit hook records and returns before reaching this seam. If that observer is
// ever absent, permission decisions fail closed instead of consulting GUI or install state.

#include "slic3r/plugin/PluginAuditManagerInternal.hpp"

#include <boost/log/trivial.hpp>

#include <string>
#include <vector>

namespace Slic3r::plugin_audit {

bool request_filesystem_read_permissions(const std::string&              plugin_key,
                                         const std::vector<std::string>& paths)
{
    BOOST_LOG_TRIVIAL(warning) << "[PROBE] Refusing filesystem read permission request for " << plugin_key
                               << " (" << paths.size() << " path(s)): the headless probe does not evaluate grants";
    return false;
}

int decide_event(PluginAuditManager&             mgr,
                 const std::string&              event_name,
                 AuditEventCategory              category,
                 const std::vector<std::string>& targets)
{
    (void) category;
    (void) targets;
    return report_denied(mgr, event_name, {false, "permission decisions are unavailable in the headless probe"});
}

} // namespace Slic3r::plugin_audit
