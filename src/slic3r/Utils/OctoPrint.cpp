#include "OctoPrint.hpp"

#include <algorithm>
#include <sstream>
#include <exception>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <curl/curl.h>

#include <wx/progdlg.h>

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "Http.hpp"
#include "libslic3r/AppConfig.hpp"


namespace fs = boost::filesystem;
namespace pt = boost::property_tree;


namespace Slic3r {

#ifdef WIN32
// Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
namespace {
std::string substitute_host(const std::string& orig_addr, std::string sub_addr)
{
    // put ipv6 into [] brackets 
    if (sub_addr.find(':') != std::string::npos && sub_addr.at(0) != '[')
        sub_addr = "[" + sub_addr + "]";

#if 0
    //URI = scheme ":"["//"[userinfo "@"] host [":" port]] path["?" query]["#" fragment]
    std::string final_addr = orig_addr;
    //  http
    size_t double_dash = orig_addr.find("//");
    size_t host_start = (double_dash == std::string::npos ? 0 : double_dash + 2);
    // userinfo
    size_t at = orig_addr.find("@");
    host_start = (at != std::string::npos && at > host_start ? at + 1 : host_start);
    // end of host, could be port(:), subpath(/) (could be query(?) or fragment(#)?)
    // or it will be ']' if address is ipv6 )
    size_t potencial_host_end = orig_addr.find_first_of(":/", host_start); 
    // if there are more ':' it must be ipv6
    if (potencial_host_end != std::string::npos && orig_addr[potencial_host_end] == ':' && orig_addr.rfind(':') != potencial_host_end) {
        size_t ipv6_end = orig_addr.find(']', host_start);
        // DK: Uncomment and replace orig_addr.length() if we want to allow subpath after ipv6 without [] parentheses.
        potencial_host_end = (ipv6_end != std::string::npos ? ipv6_end + 1 : orig_addr.length()); //orig_addr.find('/', host_start));
    }
    size_t host_end = (potencial_host_end != std::string::npos ? potencial_host_end : orig_addr.length());
    // now host_start and host_end should mark where to put resolved addr
    // check host_start. if its nonsense, lets just use original addr (or  resolved addr?)
    if (host_start >= orig_addr.length()) {
        return final_addr;
    }
    final_addr.replace(host_start, host_end - host_start, sub_addr);
    return final_addr;
#else
    // Using the new CURL API for handling URL. https://everything.curl.dev/libcurl/url
    // If anything fails, return the input unchanged.
    std::string out = orig_addr;
    CURLU *hurl = curl_url();
    if (hurl) {
        // Parse the input URL.
        CURLUcode rc = curl_url_set(hurl, CURLUPART_URL, orig_addr.c_str(), 0);
        if (rc == CURLUE_OK) {
            // Replace the address.
            rc = curl_url_set(hurl, CURLUPART_HOST, sub_addr.c_str(), 0);
            if (rc == CURLUE_OK) {
                // Extract a string fromt the CURL URL handle.
                char *url;
                rc = curl_url_get(hurl, CURLUPART_URL, &url, 0);
                if (rc == CURLUE_OK) {
                    out = url;
                    curl_free(url);
                } else
                    BOOST_LOG_TRIVIAL(error) << "OctoPrint substitute_host: failed to extract the URL after substitution";
            } else
                BOOST_LOG_TRIVIAL(error) << "OctoPrint substitute_host: failed to substitute host " << sub_addr << " in URL " << orig_addr;
        } else
            BOOST_LOG_TRIVIAL(error) << "OctoPrint substitute_host: failed to parse URL " << orig_addr;
        curl_url_cleanup(hurl);
    } else
        BOOST_LOG_TRIVIAL(error) << "OctoPrint substitute_host: failed to allocate curl_url";
    return out;
#endif
}
} //namespace
#endif // WIN32

OctoPrint::OctoPrint(DynamicPrintConfig *config) :
    m_host(config->opt_string("print_host")),
    m_apikey(config->opt_string("printhost_apikey")),
    m_cafile(config->opt_string("printhost_cafile")),
    m_ssl_revoke_best_effort(config->opt_bool("printhost_ssl_ignore_revoke"))
{}

const char* OctoPrint::get_name() const { return "OctoPrint"; }

bool OctoPrint::test(wxString &msg) const
{
    // Since the request is performed synchronously here,
    // it is ok to refer to `msg` from within the closure

    const char *name = get_name();

    bool res = true;
    auto url = make_url("api/version");

    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Get version at: %2%") % name % url;

    auto http = Http::get(std::move(url));
    set_auth(http);
    http.on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error getting version: %2%, HTTP %3%, body: `%4%`") % name % error % status % body;
            res = false;
            msg = format_error(body, error, status);
        })
        .on_complete([&, this](std::string body, unsigned) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: Got version: %2%") % name % body;

            try {
                std::stringstream ss(body);
                pt::ptree ptree;
                pt::read_json(ss, ptree);

                if (! ptree.get_optional<std::string>("api")) {
                    res = false;
                    return;
                }

                const auto text = ptree.get_optional<std::string>("text");
                res = validate_version_text(text);
                if (! res) {
                    msg = GUI::from_u8((boost::format(_utf8(L("Mismatched type of print host: %s"))) % (text ? *text : "OctoPrint")).str());
                }
            }
            catch (const std::exception &) {
                res = false;
                msg = "Could not parse server response";
            }
        })
#ifdef WIN32
        .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
        .on_ip_resolve([&](std::string address) {
            // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
            // Remember resolved address to be reused at successive REST API call.
            msg = GUI::from_u8(address);
        })
#endif // WIN32
        .perform_sync();

    return res;
}

wxString OctoPrint::get_test_ok_msg () const
{
    return _(L("Connection to OctoPrint works correctly."));
}

wxString OctoPrint::get_test_failed_msg (wxString &msg) const
{
    return GUI::from_u8((boost::format("%s: %s\n\n%s")
        % _utf8(L("Could not connect to OctoPrint"))
        % std::string(msg.ToUTF8())
        % _utf8(L("Note: OctoPrint version at least 1.1.0 is required."))).str());
}

bool OctoPrint::upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn) const
{
    const char *name = get_name();

    const auto upload_filename = upload_data.upload_path.filename();
    const auto upload_parent_path = upload_data.upload_path.parent_path();

    // If test fails, test_msg_or_host_ip contains the error message.
    // Otherwise on Windows it contains the resolved IP address of the host.
    wxString test_msg_or_host_ip;
    if (! test(test_msg_or_host_ip)) {
        error_fn(std::move(test_msg_or_host_ip));
        return false;
    }

    std::string url;
    bool res = true;

#ifdef WIN32
    // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
    if (m_host.find("https://") == 0 || test_msg_or_host_ip.empty() || GUI::get_app_config()->get("allow_ip_resolve") != "1")
#endif // _WIN32
    {
        // If https is entered we assume signed ceritificate is being used
        // IP resolving will not happen - it could resolve into address not being specified in cert
        url = make_url("api/files/local");
    }
#ifdef WIN32
    else {
        // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
        // Curl uses easy_getinfo to get ip address of last successful transaction.
        // If it got the address use it instead of the stored in "host" variable.
        // This new address returns in "test_msg_or_host_ip" variable.
        // Solves troubles of uploades failing with name address.
        // in original address (m_host) replace host for resolved ip 
        url = substitute_host(make_url("api/files/local"), GUI::into_u8(test_msg_or_host_ip));
        BOOST_LOG_TRIVIAL(info) << "Upload address after ip resolve: " << url;
    }
#endif // _WIN32

    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Uploading file %2% at %3%, filename: %4%, path: %5%, print: %6%")
        % name
        % upload_data.source_path
        % url
        % upload_filename.string()
        % upload_parent_path.string()
        % (upload_data.post_action == PrintHostPostUploadAction::StartPrint ? "true" : "false");

    auto http = Http::post(std::move(url));
    set_auth(http);
    http.form_add("print", upload_data.post_action == PrintHostPostUploadAction::StartPrint ? "true" : "false")
        .form_add("path", upload_parent_path.string())      // XXX: slashes on windows ???
        .form_add_file("file", upload_data.source_path.string(), upload_filename.string())
        .on_complete([&](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: File uploaded: HTTP %2%: %3%") % name % status % body;
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error uploading file: %2%, HTTP %3%, body: `%4%`") % name % error % status % body;
            error_fn(format_error(body, error, status));
            res = false;
        })
        .on_progress([&](Http::Progress progress, bool &cancel) {
            prorgess_fn(std::move(progress), cancel);
            if (cancel) {
                // Upload was canceled
                BOOST_LOG_TRIVIAL(info) << "Octoprint: Upload canceled";
                res = false;
            }
        })
#ifdef WIN32
        .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
        .perform_sync();

    return res;
}

bool OctoPrint::validate_version_text(const boost::optional<std::string> &version_text) const
{
    return version_text ? boost::starts_with(*version_text, "OctoPrint") : true;
}

void OctoPrint::set_auth(Http &http) const
{
    http.header("X-Api-Key", m_apikey);

    if (!m_cafile.empty()) {
        http.ca_file(m_cafile);
    }
}

std::string OctoPrint::make_url(const std::string &path) const
{
    if (m_host.find("http://") == 0 || m_host.find("https://") == 0) {
        if (m_host.back() == '/') {
            return (boost::format("%1%%2%") % m_host % path).str();
        } else {
            return (boost::format("%1%/%2%") % m_host % path).str();
        }
    } else {
        return (boost::format("http://%1%/%2%") % m_host % path).str();
    }
}

SL1Host::SL1Host(DynamicPrintConfig *config) : 
    OctoPrint(config),
    m_authorization_type(dynamic_cast<const ConfigOptionEnum<AuthorizationType>*>(config->option("printhost_authorization_type"))->value),
    m_username(config->opt_string("printhost_user")),
    m_password(config->opt_string("printhost_password"))
{
}

// SL1Host
const char* SL1Host::get_name() const { return "SL1Host"; }

wxString SL1Host::get_test_ok_msg () const
{
    return _(L("Connection to Prusa SL1 / SL1S works correctly."));
}

wxString SL1Host::get_test_failed_msg (wxString &msg) const
{
    return GUI::from_u8((boost::format("%s: %s")
                    % _utf8(L("Could not connect to Prusa SLA"))
                    % std::string(msg.ToUTF8())).str());
}

bool SL1Host::validate_version_text(const boost::optional<std::string> &version_text) const
{
    return version_text ? boost::starts_with(*version_text, "Prusa SLA") : false;
}

void SL1Host::set_auth(Http &http) const
{
    switch (m_authorization_type) {
    case atKeyPassword:
        http.header("X-Api-Key", get_apikey());
        break;
    case atUserPassword:
        http.auth_digest(m_username, m_password);
        break;
    }

    if (! get_cafile().empty()) {
        http.ca_file(get_cafile());
    }
}

// PrusaLink
PrusaLink::PrusaLink(DynamicPrintConfig* config) :
    OctoPrint(config),
    m_authorization_type(dynamic_cast<const ConfigOptionEnum<AuthorizationType>*>(config->option("printhost_authorization_type"))->value),
    m_username(config->opt_string("printhost_user")),
    m_password(config->opt_string("printhost_password"))
{
}

const char* PrusaLink::get_name() const { return "PrusaLink"; }

wxString PrusaLink::get_test_ok_msg() const
{
    return _(L("Connection to PrusaLink works correctly."));
}

wxString PrusaLink::get_test_failed_msg(wxString& msg) const
{
    return GUI::from_u8((boost::format("%s: %s")
        % _utf8(L("Could not connect to PrusaLink"))
        % std::string(msg.ToUTF8())).str());
}

bool PrusaLink::validate_version_text(const boost::optional<std::string>& version_text) const
{
    return version_text ? (boost::starts_with(*version_text, "PrusaLink") || boost::starts_with(*version_text, "OctoPrint")) : false;
}

void PrusaLink::set_auth(Http& http) const
{
    switch (m_authorization_type) {
    case atKeyPassword:
        http.header("X-Api-Key", get_apikey());
        break;
    case atUserPassword:
        http.auth_digest(m_username, m_password);
        break;
    }

    if (!get_cafile().empty()) {
        http.ca_file(get_cafile());
    }
}

}
