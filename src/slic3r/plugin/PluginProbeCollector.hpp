#ifndef slic3r_PluginProbeCollector_hpp_
#define slic3r_PluginProbeCollector_hpp_

// Record-only collection for the headless plugin permission probe. This is deliberately a plain
// in-memory store: it has no dialog, grant or persistence path, and it never decides whether an
// audited operation may proceed. Keeping it independent of wx/Python also makes its grouping and
// phase attribution testable without starting the interpreter.

#include "PluginAuditManager.hpp"

#include <nlohmann/json.hpp>

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {

// What the probe attempted and what came back. `status` is one of "invoked", "failed" or
// "skipped"; a skipped callback is reported so the absence of observations is not mistaken for a
// clean result.
struct ProbeCallbackRecord
{
    std::string capability;
    std::string phase;
    std::string callback;
    std::string status;
    std::string detail;
};

class PluginProbeCollector
{
public:
    void record(const AuditObservation& observation)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_observations.push_back(observation);
    }

    void record_callback(ProbeCallbackRecord record)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_callbacks.push_back(std::move(record));
    }

    std::vector<AuditObservation> observations() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_observations;
    }

    std::vector<ProbeCallbackRecord> callbacks() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_callbacks;
    }

    // Groups the run by the permission categories the audit hook already assigned, and lists the
    // distinct targets and event names seen in each. This is a view of this run only; it says
    // nothing about code paths the plugin did not exercise.
    nlohmann::json summary() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        std::map<std::string, std::set<std::string>> events;
        std::map<std::string, std::set<std::string>> targets;
        for (const AuditObservation& observation : m_observations) {
            events[observation.category].insert(observation.event_name);
            targets[observation.category].insert(observation.targets.begin(), observation.targets.end());
        }

        nlohmann::json categories = nlohmann::json::object();
        for (const auto& [category, category_events] : events) {
            categories[category] = {
                {"events", category_events},
                {"targets", targets[category]},
            };
        }

        return {
            {"observed_event_count", m_observations.size()},
            {"categories", categories},
        };
    }

private:
    mutable std::mutex m_mutex;
    std::vector<AuditObservation> m_observations;
    std::vector<ProbeCallbackRecord> m_callbacks;
};

} // namespace Slic3r

#endif // slic3r_PluginProbeCollector_hpp_
