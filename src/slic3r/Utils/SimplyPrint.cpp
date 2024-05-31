#include "SimplyPrint.hpp"

#include <openssl/sha.h>
#include <boost/beast/core/detail/base64.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/filesystem.hpp>

#include "nlohmann/json.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/format.hpp"


namespace Slic3r {

static constexpr boost::asio::ip::port_type CALLBACK_PORT = 21328;
static const std::string CALLBACK_URL = "http://localhost:21328/callback";
static const std::string RESPONSE_TYPE  = "code";
static const std::string CLIENT_ID = "simplyprintorcaslicer";
static const std::string CLIENT_SCOPES = "user.read files.temp_upload";
static const std::string OAUTH_CREDENTIAL_PATH = "simplyprint_oauth.json";
static const std::string TOKEN_URL = "https://simplyprint.io/api/oauth2/Token";

static std::string generate_verification_code(int code_length = 32)
{
    std::stringstream ss;
    for (auto i = 0; i < code_length; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (rand() % 0x100);
    }

    return ss.str();
}

static std::string sha256b64(const std::string& inputStr)
{
    unsigned char        hash[SHA256_DIGEST_LENGTH];
    const unsigned char* data = (const unsigned char*) inputStr.c_str();
    SHA256(data, inputStr.size(), hash);

    std::string b64;
    b64.resize(boost::beast::detail::base64::encoded_size(sizeof(hash)));
    b64.resize(boost::beast::detail::base64::encode(&b64[0], hash, sizeof(hash)));

    // uses '-' instead of '+' and '_' instead of '/' for url-safe
    std::replace(b64.begin(), b64.end(), '+', '-');
    std::replace(b64.begin(), b64.end(), '/', '_');

    // Stripping "=" is for RFC 7636 compliance
    b64.erase(std::remove(b64.begin(), b64.end(), '='), b64.end());

    return b64;
}

static std::string url_encode(const std::vector<std::pair<std::string, std::string>> query)
{
    std::vector<std::string> q;
    q.reserve(query.size());

    std::transform(query.begin(), query.end(), std::back_inserter(q), [](const auto& kv) {
        return Http::url_encode(kv.first) + "=" + Http::url_encode(kv.second);
    });

    return boost::algorithm::join(q, "&");
}

static void set_auth(Http& http, const std::string& access_token) { http.header("Authorization", "Bearer " + access_token); }

SimplyPrint::SimplyPrint(DynamicPrintConfig* config)
{
    cred_file = (boost::filesystem::path(data_dir()) / OAUTH_CREDENTIAL_PATH).make_preferred().string();
    load_oauth_credential();
}

GUI::OAuthParams SimplyPrint::get_oauth_params() const
{
    const auto verification_code = generate_verification_code();
    // SimplyPrint uses S256 for PKCE
    const auto code_challenge = sha256b64(verification_code);
    const auto state          = generate_verification_code();

    const std::vector<std::pair<std::string, std::string>> query_parameters{
        {"client_id", CLIENT_ID},
        {"redirect_uri", CALLBACK_URL},
        {"scope", CLIENT_SCOPES},
        {"response_type", RESPONSE_TYPE},
        {"state", state},
        {"code_challenge", code_challenge},
        {"code_challenge_method", "S256"},
    };
    const auto login_url = (boost::format("https://simplyprint.io/panel/oauth2/authorize?%s") % url_encode(query_parameters)).str();

    return GUI::OAuthParams{
        login_url,
        CLIENT_ID,
        CALLBACK_PORT,
        CALLBACK_URL,
        CLIENT_SCOPES,
        RESPONSE_TYPE,
        "https://simplyprint.io/login-success",
        "https://simplyprint.io/login-success",
        TOKEN_URL,
        verification_code,
        state,
    };
}

void SimplyPrint::load_oauth_credential()
{
    cred.clear();
    if (boost::filesystem::exists(cred_file)) {
        nlohmann::json j;
        try {
            boost::nowide::ifstream ifs(cred_file);
            ifs >> j;
            ifs.close();

            cred["access_token"] = j["access_token"];
            cred["refresh_token"] = j["refresh_token"];
        } catch (std::exception& err) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse " << cred_file << " failed, reason = " << err.what();
            cred.clear();
        }
    }
}

void SimplyPrint::save_oauth_credential(const GUI::OAuthResult& cred) const
{
    nlohmann::json j;
    j["access_token"]  = cred.access_token;
    j["refresh_token"] = cred.refresh_token;
    
    boost::nowide::ofstream c;
    c.open(cred_file, std::ios::out | std::ios::trunc);
    c << std::setw(4) << j << std::endl;
    c.close();
}

wxString SimplyPrint::get_test_ok_msg() const { return _(L("Connected to SimplyPrint successfully!")); }

wxString SimplyPrint::get_test_failed_msg(wxString& msg) const
{
    return GUI::format_wxstr("%s: %s", _L("Could not connect to SimplyPrint"), msg.Truncate(256));
}

void SimplyPrint::log_out() const
{
    boost::nowide::remove(cred_file.c_str());
}

bool SimplyPrint::do_api_call(std::function<Http(bool)>                               build_request,
                              std::function<bool(std::string, unsigned)>              on_complete,
                              std::function<bool(std::string, std::string, unsigned)> on_error) const
{
    if (cred.find("access_token") == cred.end()) {
        return false;
    }

    bool res = true;

    const auto create_request = [this, &build_request, &res, &on_complete](const std::string& access_token, bool is_retry) {
        auto http = build_request(is_retry);
        set_auth(http, access_token);
        http.header("User-Agent", "SimplyPrint Orca Plugin")
            .on_complete([&](std::string body, unsigned http_status) {
                res = on_complete(body, http_status);
            });

        return http;
    };

    create_request(cred.at("access_token"), false)
        .on_error([&res, &on_error, this, &create_request](std::string body, std::string error, unsigned http_status) {
            if (http_status == 401) {
                // Refresh token
                BOOST_LOG_TRIVIAL(warning) << boost::format("SimplyPrint: Access token invalid: %1%, HTTP %2%, body: `%3%`") % error %
                                                  http_status % body;
                BOOST_LOG_TRIVIAL(info) << "SimplyPrint: Attempt to refresh access token";

                auto http = Http::post(TOKEN_URL);
                http.timeout_connect(5)
                    .timeout_max(5)
                    .form_add("grant_type", "refresh_token")
                    .form_add("client_id", CLIENT_ID)
                    .form_add("refresh_token", cred.at("refresh_token"))
                    .on_complete([this, &res, &on_error, &create_request](std::string body, unsigned http_status) {
                        GUI::OAuthResult r;
                        GUI::OAuthJob::parse_token_response(body, false, r);
                        if (r.success) {
                            BOOST_LOG_TRIVIAL(info) << "SimplyPrint: Successfully refreshed access token";
                            this->save_oauth_credential(r);

                            // Run the api call again
                            create_request(r.access_token, true)
                                .on_error([&res, &on_error](std::string body, std::string error, unsigned http_status) {
                                    res = on_error(body, error, http_status);
                                })
                                .perform_sync();
                        } else {
                            BOOST_LOG_TRIVIAL(error)
                                << boost::format("SimplyPrint: Failed to refresh access token: %1%, body: `%2%`") % r.error_message % body;
                            res = on_error(body, r.error_message, http_status);
                        }
                    })
                    .on_error([&res, &on_error](std::string body, std::string error, unsigned http_status) {
                        BOOST_LOG_TRIVIAL(error)
                            << boost::format("SimplyPrint: Failed to refresh access token: %1%, HTTP %2%, body: `%3%`") % error %
                                   http_status % body;
                        res = on_error(body, error, http_status);
                    })
                    .perform_sync();
            } else {
                res = on_error(body, error, http_status);
            }
        })
        .perform_sync();

    return res;
}


bool SimplyPrint::test(wxString& curl_msg) const
{
    if (cred.find("access_token") == cred.end()) {
        return false;
    }

    return do_api_call(
        [](bool is_retry) {
            auto http = Http::get("https://api.simplyprint.io/oauth2/TokenInfo");
            http.header("Accept", "application/json");
            return http;
        },
        [](std::string body, unsigned) {
            BOOST_LOG_TRIVIAL(info) << boost::format("SimplyPrint: Got token info: %1%") % body;
            return true;
        },
        [](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("SimplyPrint: Error getting token info: %1%, HTTP %2%, body: `%3%`") % error %
                                            status % body;
            return false;
        });
}

bool SimplyPrint::upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn, InfoFn info_fn) const
{
    if (cred.find("access_token") == cred.end()) {
        error_fn(_L("SimplyPrint account not linked. Go to Connect options to set it up."));
        return false;
    }

    // If file is over 100 MB, fail
    if (boost::filesystem::file_size(upload_data.source_path) > 104857600ull) {
        error_fn(_L("File size exceeds the 100MB upload limit. Please upload your file through the panel."));
        return false;
    }

    const auto filename = upload_data.upload_path.filename().string();

    return do_api_call(
        [&upload_data, &prorgess_fn, &filename](bool is_retry) {
            auto http = Http::post("https://simplyprint.io/api/files/TempUpload");
            http.form_add_file("file", upload_data.source_path.string(), filename)
                .on_progress([&prorgess_fn](Http::Progress progress, bool& cancel) { prorgess_fn(std::move(progress), cancel); });

            return http;
        },
        [&error_fn, &filename](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << boost::format("SimplyPrint: File uploaded: HTTP %1%: %2%") % status % body;

            // Get file UUID
            const auto j = nlohmann::json::parse(body, nullptr, false, true);
            if (j.is_discarded()) {
                BOOST_LOG_TRIVIAL(error) << "SimplyPrint: Invalid or no JSON data on token response: " << body;
                error_fn(_L("Unknown error"));
                return false;
            }

            if (j.find("uuid") == j.end()) {
                BOOST_LOG_TRIVIAL(error) << "SimplyPrint: Invalid or no JSON data on token response: " << body;
                error_fn(_L("Unknown error"));
                return false;
            }
            const std::string uuid = j["uuid"];

            // Launch external browser for file importing after uploading
            const auto url = "https://simplyprint.io/panel?" + url_encode({{"import", "tmp:" + uuid}, {"filename", filename}});
            wxLaunchDefaultBrowser(url);

            return true;
        },
        [this, &error_fn](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("SimplyPrint: Error uploading file : %1%, HTTP %2%, body: `%3%`") % error % status % body;
            error_fn(format_error(body, error, status));
            return false;
        });
}

} // namespace Slic3r
