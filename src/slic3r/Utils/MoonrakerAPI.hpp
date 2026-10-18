#pragma once

#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace Slic3r {

// Shared synchronous Moonraker reads for PrintHost and the printer agent.
class MoonrakerAPI
{
public:
    MoonrakerAPI(const std::string& base_url, const std::string& api_key,
                 const std::string& ca_file = {}, bool ssl_revoke_best_effort = false,
                 long connect_timeout = 5, long request_timeout = 10);

    bool get_result(const std::string& endpoint, nlohmann::json& result, std::string& error) const;
    bool list_objects(std::vector<std::string>& objects, std::string& error) const;
    bool query_objects(const std::vector<std::string>& objects, nlohmann::json& status, std::string& error) const;

private:
    std::string m_base_url;
    std::string m_api_key;
    std::string m_ca_file;
    bool m_ssl_revoke_best_effort;
    long m_connect_timeout;
    long m_request_timeout;
};

} // namespace Slic3r
