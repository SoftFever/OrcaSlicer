#include "MoonrakerAPI.hpp"

#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "Http.hpp"
#include "slic3r/GUI/I18N.hpp"

namespace Slic3r {

MoonrakerAPI::MoonrakerAPI(const std::string& base_url, const std::string& api_key,
                         const std::string& ca_file, bool ssl_revoke_best_effort,
                         long connect_timeout, long request_timeout)
    : m_base_url(base_url), m_api_key(api_key), m_ca_file(ca_file),
      m_ssl_revoke_best_effort(ssl_revoke_best_effort),
      m_connect_timeout(connect_timeout), m_request_timeout(request_timeout)
{}

bool MoonrakerAPI::get_result(const std::string& endpoint, nlohmann::json& result, std::string& error) const
{
#ifndef WIN32
    (void) m_ssl_revoke_best_effort;
#endif
    result = nullptr;
    error.clear();
    if (m_base_url.empty()) {
        error = _u8L("Moonraker URL is empty");
        return false;
    }
    bool success = false;
    const auto url = m_base_url + (m_base_url.back() == '/' ? "" : "/") + endpoint;
    auto http = Http::get(url);
    if (!m_api_key.empty()) http.header("X-Api-Key", m_api_key);
    if (!m_ca_file.empty()) http.ca_file(m_ca_file);
    http.timeout_connect(m_connect_timeout).timeout_max(m_request_timeout)
        .on_complete([&](std::string body, unsigned) {
            auto json = nlohmann::json::parse(body, nullptr, false, true);
            if (!json.is_object() || json.contains("error")) {
                error = _u8L("Invalid Moonraker status response");
                return;
            }
            // Some compatible servers return the result without an envelope.
            if (json.contains("result"))
                result = std::move(json["result"]);
            else
                result = std::move(json);
            success = result.is_object();
            if (!success) error = _u8L("Invalid Moonraker status response");
        })
        .on_error([&](std::string, std::string message, unsigned code) {
            error = std::move(message);
            if (code != 0) error += " (HTTP " + std::to_string(code) + ")";
        })
#ifdef WIN32
        .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
        .perform_sync();
    return success;
}

bool MoonrakerAPI::list_objects(std::vector<std::string>& objects, std::string& error) const
{
    objects.clear();
    nlohmann::json result;
    if (!get_result("printer/objects/list", result, error))
        return false;
    if (!result.contains("objects") || !result["objects"].is_array()) {
        error = _u8L("Invalid Moonraker object list");
        return false;
    }
    for (const auto& object : result["objects"]) {
        if (object.is_string()) objects.push_back(object.get<std::string>());
    }
    return true;
}

bool MoonrakerAPI::query_objects(const std::vector<std::string>& objects, nlohmann::json& status, std::string& error) const
{
    status = nullptr;
    std::string query = "printer/objects/query";
    char separator = '?';
    for (const auto& object : objects) {
        query += separator + Http::url_encode(object);
        separator = '&';
    }
    nlohmann::json result;
    if (!get_result(query, result, error))
        return false;
    if (!result.contains("status") || !result["status"].is_object()) {
        error = _u8L("Invalid Moonraker status response");
        return false;
    }
    status = std::move(result["status"]);
    return true;
}

} // namespace Slic3r
