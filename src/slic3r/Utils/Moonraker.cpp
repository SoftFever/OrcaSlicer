#include "Moonraker.hpp"
#include "libslic3r/AFCMapping.hpp"

#include <exception>
#include <functional>
#include <algorithm>
#include <sstream>
#include <string>
#include <map>
#include <nlohmann/json.hpp>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <wx/string.h>
#include <utility>
#include <wx/arrstr.h>

#include "PrintHost.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/format.hpp"
#include "Http.hpp"

namespace pt = boost::property_tree;

namespace Slic3r {

bool fetch_afc_status(const std::string& base_url, const std::string& api_key,
                      AFCMapping& mapping, bool& detected, std::string& error,
                      const std::string& ca_file, bool ssl_revoke_best_effort)
{
    mapping = {};
    detected = false;
    error.clear();
    const auto get_result = [&](const std::string& endpoint, nlohmann::json& response) {
        bool success = false;
        const auto url = base_url + (base_url.back() == '/' ? "" : "/") + endpoint;
        auto http = Http::get(url);
        if (!api_key.empty()) http.header("X-Api-Key", api_key);
        if (!ca_file.empty()) http.ca_file(ca_file);
        http.timeout_connect(2).timeout_max(3)
            .on_complete([&](std::string body, unsigned) {
                auto json = nlohmann::json::parse(body, nullptr, false);
                if (json.is_object() && json.contains("result") && json["result"].is_object()) {
                    response = std::move(json["result"]);
                    success = true;
                } else {
                    error = _u8L("Invalid Moonraker status response");
                }
            })
            .on_error([&](std::string, std::string message, unsigned code) {
                error = message + " (HTTP " + std::to_string(code) + ")";
            })
#ifdef WIN32
            .ssl_revoke_best_effort(ssl_revoke_best_effort)
#endif
            .perform_sync();
        return success;
    };
    if (base_url.empty()) return false;
#ifndef WIN32
    (void) ssl_revoke_best_effort;
#endif
    nlohmann::json result;
    if (!get_result("printer/objects/list", result)) {
        // Discovery is optional. In particular, a powered-off Klipper may
        // return 503 while Moonraker can still accept and queue normal uploads.
        BOOST_LOG_TRIVIAL(debug) << "Moonraker: AFC detection unavailable: " << error;
        error.clear();
        return true;
    }
    if (!result.contains("objects") || !result["objects"].is_array()) {
        error = _u8L("Invalid Moonraker object list");
        return false;
    }
    const auto& objects = result["objects"];
    if (std::find(objects.begin(), objects.end(), nlohmann::json("AFC")) == objects.end()) return true;
    detected = true;
    std::string query = "printer/objects/query?AFC&print_stats";
    for (const auto& object : objects) {
        if (object.is_string()) {
            const auto name = object.get<std::string>();
            if (name.rfind("AFC_lane ", 0) == 0)
                query += "&" + Http::url_encode(name);
        }
    }
    // Fetch all lane maps and metadata together, so a concurrent external remap
    // cannot associate a selected color with a different physical lane.
    if (!get_result(query, result) || !result.contains("status") || !AFCMapping::parse(result["status"], mapping)) {
        if (error.empty()) error = _u8L("Invalid AFC lane status");
        return false;
    }
    return true;
}

bool Moonraker::fetch_afc_status(AFCMapping& mapping, bool& detected, wxString& error) const
{
    std::string detail;
    const bool success = Slic3r::fetch_afc_status(make_url(""), m_apikey, mapping, detected, detail,
                                                m_cafile, m_ssl_revoke_best_effort);
    if (!success)
        error = GUI::format_wxstr(_L("Unable to read AFC filament information: %s"), detail);
    return success;
}

Moonraker::Moonraker(DynamicPrintConfig *config)
    : m_host(config->opt_string("print_host"))
    , m_apikey(config->opt_string("printhost_apikey"))
    , m_cafile(config->opt_string("printhost_cafile"))
    , m_ssl_revoke_best_effort(config->opt_bool("printhost_ssl_ignore_revoke"))
{}

const char* Moonraker::get_name() const { return "Moonraker"; }

wxString Moonraker::get_test_ok_msg() const
{
    return _(L("Connection to Moonraker is working correctly."));
}

wxString Moonraker::get_test_failed_msg(wxString &msg) const
{
    return GUI::format_wxstr("%s: %s", _L("Could not connect to Moonraker"), msg);
}

std::string Moonraker::make_url(const std::string &path) const
{
    if (m_host.find("http://") == 0 || m_host.find("https://") == 0) {
        if (m_host.back() == '/')
            return (boost::format("%1%%2%") % m_host % path).str();
        return (boost::format("%1%/%2%") % m_host % path).str();
    }
    return (boost::format("http://%1%/%2%") % m_host % path).str();
}

void Moonraker::set_auth(Http &http) const
{
    if (!m_apikey.empty())
        http.header("X-Api-Key", m_apikey);
    if (!m_cafile.empty())
        http.ca_file(m_cafile);
}

bool Moonraker::test(wxString &msg) const
{
    //ORCA: Moonraker's /server/info returns
    //          { "result": { "klippy_state": "ready|startup|shutdown|error|disconnected", ... } }
    //      We treat the connection as healthy as long as the envelope is valid and `klippy_state`
    //      is present — matching the OctoPrint/PrusaLink convention of "can I reach this host?".
    //      Klipper state (idle, error, etc.) is surfaced to the log but does not gate the test:
    //      buddy-fork firmwares legitimately report non-`ready` states at idle, and any real upload
    //      problem will surface a contextual error at upload() time anyway.
    const char *name = get_name();
    bool res = true;
    auto url = make_url("server/info");

    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Get server info at: %2%") % name % url;

    auto http = Http::get(std::move(url));
    set_auth(http);
    http.on_error([&](std::string body, std::string error, unsigned status) {
        BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error getting server info: %2%, HTTP %3%, body: `%4%`")
            % name % error % status % body;
        res = false;
        msg = format_error(body, error, status);
    })
    .on_complete([&](std::string body, unsigned) {
        BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: /server/info body: %2%") % name % body;
        try {
            std::stringstream ss(body);
            pt::ptree ptree;
            pt::read_json(ss, ptree);

            const auto klippy_state = ptree.get_optional<std::string>("result.klippy_state");
            if (!klippy_state) {
                //ORCA: response wasn't shaped like a Moonraker /server/info reply — likely an OctoPrint
                //      or PrusaLink host the user mis-selected as Moonraker, or a totally different
                //      service. Treat as a connection failure with a clear hint.
                res = false;
                msg = _L("The host responded but it doesn't look like Moonraker (missing result.klippy_state).");
                return;
            }
            BOOST_LOG_TRIVIAL(info) << boost::format("%1%: klippy_state = %2%") % name % (*klippy_state);
        } catch (const std::exception &ex) {
            res = false;
            msg = GUI::format_wxstr(_L("Could not parse Moonraker server response: %s"), ex.what());
        }
    })
#ifdef WIN32
    .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
    .perform_sync();

    return res;
}

bool Moonraker::get_storage(wxArrayString &storage_path, wxArrayString &storage_name) const
{
    //ORCA: GET /server/files/roots enumerates Moonraker's storage roots (default "gcodes" plus any
    //      configured extras like "config", "logs", "timelapse"). Only roots with permissions
    //      including "rw" or "rwd" can receive uploads; we filter to those so the UI dropdown only
    //      offers usable destinations. The base class returns false (no per-host storage); returning
    //      true here populates the storage picker in PrintHostDialogs's send-to-print dialog.
    //      Failures (404 — older Moonraker, or a buddy-fork that doesn't implement the endpoint)
    //      gracefully degrade to false so upload() falls back to the hardcoded "gcodes" default.
    const char *name = get_name();
    bool got_any = false;
    auto url = make_url("server/files/roots");

    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Enumerating storage roots at: %2%") % name % url;

    auto http = Http::get(std::move(url));
    set_auth(http);
    http.on_error([&](std::string body, std::string error, unsigned status) {
        //ORCA: /server/files/roots is optional in the Moonraker spec and absent on older versions
        //      and slimmer shims (e.g. Prusa-Firmware-Buddy 0.8.x prusalink-shim returns 501). A
        //      missing endpoint here is benign — upload() silently falls back to the hardcoded
        //      "gcodes" root — so don't pollute the log at warning level for it. Other HTTP
        //      errors still warn.
        if (status == 404 || status == 501) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: /server/files/roots not implemented (HTTP %2%); upload() will fall back to the \"gcodes\" root.")
                % name % status;
        } else {
            BOOST_LOG_TRIVIAL(warning) << boost::format("%1%: Could not enumerate roots: %2%, HTTP %3%, body: `%4%`")
                % name % error % status % body;
        }
    })
    .on_complete([&](std::string body, unsigned) {
        BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: /server/files/roots body: %2%") % name % body;
        try {
            std::stringstream ss(body);
            pt::ptree ptree;
            pt::read_json(ss, ptree);
            const auto result_node = ptree.get_child_optional("result");
            if (!result_node)
                return;
            for (const auto &child : *result_node) {
                const std::string &root = child.second.get<std::string>("name", "");
                const std::string &perms = child.second.get<std::string>("permissions", "");
                if (root.empty() || perms.find('w') == std::string::npos)
                    continue;
                storage_path.Add(wxString::FromUTF8(root));
                storage_name.Add(wxString::FromUTF8(root));
                got_any = true;
            }
        } catch (const std::exception &ex) {
            BOOST_LOG_TRIVIAL(warning) << boost::format("%1%: Could not parse roots: %2%") % name % ex.what();
        }
    })
#ifdef WIN32
    .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
    .perform_sync();

    return got_any;
}

bool Moonraker::start_print(wxString &error_msg, const std::string &filename) const
{
    //ORCA: POST /printer/print/start with JSON body { "filename": "<name>.gcode" }.
    //      `filename` is the item.path /server/files/upload returned (the storage-relative
    //      path inside `root`, no leading slash, with extension), or the original filename
    //      when the reply had none. Build the body via property_tree so that special characters
    //      in the filename (quotes, backslashes) are properly escaped.
    const char *name = get_name();
    bool res = true;
    auto url = make_url("printer/print/start");
    pt::ptree body_tree;
    body_tree.put("filename", filename);
    std::ostringstream body_ss;
    pt::write_json(body_ss, body_tree, /*pretty=*/false);
    std::string body = body_ss.str();

    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Starting print of %2% at %3%") % name % filename % url;

    auto http = Http::post(std::move(url));
    set_auth(http);
    http.header("Content-Type", "application/json")
        .set_post_body(body)
        .on_complete([&](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: print/start HTTP %2%: %3%") % name % status % body;
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error starting print at %2%: %3%, HTTP %4%, body: `%5%`")
                % name % url % error % status % body;
            res = false;
            error_msg = format_error(body, error, status);
        })
#ifdef WIN32
        .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
        .perform_sync();

    return res;
}

bool Moonraker::send_gcode_script(const std::string& script, wxString& error) const
{
    bool success = false;
    auto request = Http::post(make_url("printer/gcode/script"));
    set_auth(request);
    request.header("Content-Type", "application/json")
        .set_post_body(nlohmann::json{{"script", script}}.dump())
        .timeout_connect(5).timeout_max(10)
        .on_complete([&](std::string body, unsigned) {
            const auto response = nlohmann::json::parse(body, nullptr, false);
            success = response.is_object() && response.contains("result") && !response.contains("error");
        })
        .on_error([&](std::string body, std::string message, unsigned status) {
            error = format_error(body, message, status);
        })
#ifdef WIN32
        .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
        .perform_sync();
    return success;
}

bool Moonraker::apply_afc_mapping(const AFCSelections& selection, bool& applied, wxString& error,
                                 const std::function<bool()>& cancelled) const
{
    applied = false;
    const auto is_cancelled = [&] { return cancelled && cancelled(); };
    if (is_cancelled()) return false;
    AFCMapping current;
    bool detected = false;
    if (!fetch_afc_status(current, detected, error)) return false;
    std::map<int, std::string> desired;
    if (detected && current.tools.empty()) {
        error = _L("AFC reports no filament-to-tool mappings, so the filament mapping cannot be applied.");
        return false;
    }
    if (!detected || !current.plan(selection, desired)) {
        error = _L("AFC filament mapping is no longer valid. Check that the printer is idle, that all selected lanes are loaded, and that no selected lane is already serving another tool this print does not use, then reopen the send dialog.");
        return false;
    }
    // The upload and status query may take time; honor cancellation immediately
    // before changing the printer's persistent mapping.
    if (is_cancelled()) return false;
    const auto expected = current.expected_tools(desired);
    const auto script = current.mapping_script(desired);
    if (!script.empty()) {
        applied = true;
        if (!send_gcode_script(script, error)) {
            error = _L("Failed to apply AFC filament mapping. The print was not started; check the printer's current lane assignments.");
            return false;
        }
    }
    AFCMapping verified;
    if (!fetch_afc_status(verified, detected, error) || !detected
        || !verified.plan(selection, desired) || !verified.matches(expected)) {
        error = _L("AFC filament mapping could not be verified. The print was not started; check that the printer is idle, that the selected lanes are loaded, and that the lane assignments are correct.");
        return false;
    }
    if (is_cancelled()) {
        if (applied) error = _L("The print was canceled after AFC mapping was applied. The lane assignments remain saved on the printer.");
        return false;
    }
    return true;
}

bool Moonraker::upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn info_fn) const
{
    // POST /server/files/upload with `print=true` so Moonraker starts the print
    // itself (issue #14945). It reports back in two fields: print_started when it
    // began the print immediately, print_queued when it could not start it (Klippy
    // not ready, or another print running) but put the job in its queue
    // ([file_manager] queue_gcode_uploads). That covers the power-on case: a
    // [power <device>] section with on_when_job_queued switches the printer on, and
    // with [job_queue] load_on_startup the queue runs the job once Klippy is READY.
    // Either flag means Moonraker owns the print and we must NOT issue our own
    // /printer/print/start. When both are false (no job queue, a non-G-code upload,
    // or a server that ignores `print`) we fall back to the explicit start below.
    wxString test_msg;
    if (!test(test_msg)) {
        error_fn(std::move(test_msg));
        return false;
    }

    const char *name = get_name();
    const auto upload_filename = upload_data.upload_path.filename();
    const auto upload_parent_path = upload_data.upload_path.parent_path();
    //ORCA: upload_data.storage is plumbed from the (future) per-printer storage dropdown. When unset,
    //      fall back to the Moonraker-standard "gcodes" root. Reading it through here means a UI
    //      addition later (storage picker) needs no change to this method.
    const std::string root = upload_data.storage.empty() ? std::string("gcodes") : upload_data.storage;

    const bool want_start = upload_data.post_action == PrintHostPostUploadAction::StartPrint;
    const std::string serialized_mapping = want_start ? upload_data.extended("afc_mapping") : std::string{};
    const bool map_afc = !serialized_mapping.empty();
    AFCSelections selection;
    if (map_afc && (!parse_afc_selection(serialized_mapping, selection) || root != "gcodes")) {
        error_fn(_L("AFC printing requires valid filament assignments and the gcodes storage root. Reopen the upload dialog to check the mapping."));
        return false;
    }

    std::string url = make_url("server/files/upload");
    bool result = true;
    std::string uploaded_path;
    bool moonraker_started_print = false;
    bool afc_mapping_applied = false;

    //ORCA: gcode inside a .gcode.3mf is index-coded (Metadata/plate_<N>.gcode), so the upload names the
    //      plate via a 1-based `plateindex` (set only in the .3mf path, see Plater::send_gcode_legacy);
    //      servers that don't use it ignore the unknown form field.
    const std::string plateindex = upload_data.extended("plateindex");

    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Uploading file %2% to %3% (root=%4%, filename=%5%, plateindex=%6%, start_print=%7%)")
        % name
        % upload_data.source_path
        % url
        % root
        % upload_filename.string()
        % (plateindex.empty() ? "-" : plateindex)
        % (want_start ? "true" : "false");

    auto http = Http::post(std::move(url));
    set_auth(http);
    http.form_add("root", root);
    if (!plateindex.empty())
        http.form_add("plateindex", plateindex);
    // AFC assignments belong to an immediate print, not a queued upload whose
    // lane mappings might change before it starts.
    http.form_add("print", want_start && !map_afc ? "true" : "false");
    http.form_add_file("file", upload_data.source_path.string(), upload_filename.string())
        .on_complete([&](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: upload HTTP %2%: %3%") % name % status % body;
            try {
                std::stringstream ss(body);
                pt::ptree ptree;
                pt::read_json(ss, ptree);

                //ORCA: unlike the other endpoints (compare result.klippy_state in test()), the upload
                //      answers with a bare object, not a {"result": ...} envelope: Moonraker's
                //      FileUploadHandler writes finalize_upload()'s dict straight to the response, so
                //      item, print_started and print_queued are top-level. Reading them under result.
                //      silently took the fallbacks below on every upload.
                //ORCA: Moonraker confirms the storage-relative path in item.path. We pass exactly
                //      that string to /printer/print/start so any server-side renaming (.ufp stored as
                //      .gcode, surrounding whitespace and leading slashes stripped) is respected.
                const auto stored_path = ptree.get_optional<std::string>("item.path");
                if (stored_path) {
                    uploaded_path = *stored_path;
                } else {
                    //ORCA: fallback if the server response omits item.path (older Moonraker, or
                    //      a buddy-fork that returns a slimmer envelope). Use the original filename.
                    uploaded_path = upload_filename.string();
                    BOOST_LOG_TRIVIAL(warning) << boost::format(
                        "%1%: upload response missing item.path, falling back to original filename `%2%`")
                        % name % uploaded_path;
                }
                moonraker_started_print = ptree.get<bool>("print_started", false) ||
                                          ptree.get<bool>("print_queued", false);
            } catch (const std::exception &ex) {
                BOOST_LOG_TRIVIAL(warning) << boost::format(
                    "%1%: could not parse upload response (%2%); falling back to original filename")
                    % name % ex.what();
                uploaded_path = upload_filename.string();
            }
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error uploading to %2%: %3%, HTTP %4%, body: `%5%`")
                % name % url % error % status % body;
            error_fn(format_error(body, error, status));
            result = false;
        })
        .on_progress([&](Http::Progress progress, bool &cancel) {
            progress_fn(std::move(progress), cancel);
            if (cancel) {
                BOOST_LOG_TRIVIAL(info) << name << ": Upload canceled";
                result = false;
            }
        })
#ifdef WIN32
        .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
        .perform_sync();

    if (!result)
        return false;

    if (map_afc) {
        const auto cancelled = [&] {
            const std::string empty;
            bool cancel = false;
            progress_fn(Http::Progress(0, 0, 1, 1, empty), cancel);
            // Mirror Http's final cancellation notification to the upload queue.
            if (cancel) progress_fn(Http::Progress(0, 0, 1, 1, empty), cancel);
            return cancel;
        };
        if (moonraker_started_print) {
            error_fn(_L("Moonraker unexpectedly started or queued the upload before AFC mapping could be applied."));
            return false;
        }
        wxString message;
        if (!apply_afc_mapping(selection, afc_mapping_applied, message, cancelled)) {
            if (!message.empty()) error_fn(std::move(message));
            return false;
        }
    }

    // Fallback only when Moonraker neither started nor queued the print. If the
    // printer cannot start it, the error from this request is what the user sees.
    if (want_start && !uploaded_path.empty() && !moonraker_started_print) {
        wxString start_msg;
        if (!start_print(start_msg, uploaded_path)) {
            if (afc_mapping_applied)
                start_msg += "\n" + _L("The AFC lane assignments remain saved on the printer. Check them before retrying.");
            error_fn(std::move(start_msg));
            return false;
        }
    }
    return true;
}

}
