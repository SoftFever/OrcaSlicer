#include "ESP3D.hpp"

#include <algorithm>
#include <ctime>
#include <chrono>
#include <thread>
#include <boost/filesystem/path.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

#include <wx/frame.h>
#include <wx/event.h>
#include <wx/progdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>

#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "Http.hpp"
#include "SerialMessage.hpp"
#include "SerialMessageType.hpp"

namespace fs = boost::filesystem;
namespace pt = boost::property_tree;

namespace Slic3r {

ESP3D::ESP3D(DynamicPrintConfig* config) : m_host(config->opt_string("print_host")), m_console_port("8888") {}

const char* ESP3D::get_name() const { return "ESP3D"; }

bool ESP3D::test(wxString& msg) const
{
    bool ret = false;
    auto http = Http::get((boost::format("http://%1%/command?plain=%2%") % m_host % "M105").str());
    http.on_complete([&](std::string body, unsigned status) {
            ret = true;
            msg = get_test_ok_msg();
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            ret = false;
            msg = get_test_failed_msg( wxString::FromUTF8( error));
        }).perform_sync();

    return ret;
}

wxString ESP3D::get_test_ok_msg() const { return _(L("Connection to ESP3D works correctly.")); }

wxString ESP3D::get_test_failed_msg(wxString& msg) const
{
    return GUI::from_u8((boost::format("%s: %s") % _utf8(L("Could not connect to ESP3D")) % std::string(msg.ToUTF8())).str());
}

bool ESP3D::upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn, InfoFn info_fn) const
{
    bool res = true;

    auto upload_cmd = get_upload_url(upload_data.upload_path.string());
    BOOST_LOG_TRIVIAL(info) << boost::format("ESP3D: Uploading file %1%, filepath: %2%, print: %3%, command: %4%") % upload_data.source_path %
                                   upload_data.upload_path % (upload_data.post_action == PrintHostPostUploadAction::StartPrint) %
                                   upload_cmd;

    auto http = Http::post((boost::format("http://%1%/upload_serial") % m_host).str());
    http.form_add_file("file", upload_data.source_path, "test.gco");
  

    http.on_complete([&](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("ESP3D: File uploaded: HTTP %1%: %2%") % status % body;

            int err_code = get_err_code_from_body(body);
            if (err_code != 0) {
                BOOST_LOG_TRIVIAL(error) << boost::format("ESP3D: Request completed but error code was received: %1%") % err_code;
                error_fn(format_error(body, L("Unknown error occured"), 0));
                res = false;
            } else if (upload_data.post_action == PrintHostPostUploadAction::StartPrint) {
                wxString errormsg;
                res = start_print(errormsg, upload_data.upload_path.string());
                if (!res) {
                    error_fn(std::move(errormsg));
                }
            }
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("ESP3D: Error uploading file: %1%, HTTP %2%, body: `%3%`") % error % status % body;
            error_fn(format_error(body, error, status));
            res = false;
        })
        .on_progress([&](Http::Progress progress, bool& cancel) {
            prorgess_fn(std::move(progress), cancel);
            if (cancel) {
                // Upload was canceled
                BOOST_LOG_TRIVIAL(info) << "ESP3D: Upload canceled";
                res = false;
            }
        })
        .perform_sync();

    return res;
}

std::string ESP3D::get_upload_url(const std::string& filename) const
{
    return (boost::format("http://%1%/upload_serial") % m_host).str();
}

bool ESP3D::start_print(wxString& msg, const std::string& filename) const
{
    return false;
    // ESP3D only accepts 8.3 filenames else it crashes marlin
    std::string             legacyname = "";
    boost::filesystem::path p(filename);
    std::string             stem      = p.stem().string();      // Get the filename without extension
    std::string             extension = p.extension().string(); // Get the extension including the dot
    if (!extension.empty() && extension[0] == '.') {
        extension = extension.substr(1);
    }
    stem      = stem.substr(0, 8);
    extension = extension.substr(0, 3);
    if (!extension.empty()) {
        legacyname = stem + "." + extension;
    } else {
        legacyname = stem;
    }
    // For some reason printer firmware does not want to respond on gcode commands immediately after file upload.
    // So we just introduce artificial delay to workaround it.
    // TODO: Inspect reasons
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    Utils::TCPConsole            console(m_host, m_console_port);
    Slic3r::Utils::SerialMessage s(std::string("M23 ") + filename, Slic3r::Utils::Command);
    console.enqueue_cmd(s);
    s.message = "M24";
    console.enqueue_cmd(s);

    bool ret = console.run_queue();

    if (!ret)
        msg = wxString::FromUTF8(console.error_message().c_str());

    return ret;
}

int ESP3D::get_err_code_from_body(const std::string& body) const
{
    pt::ptree          root;
    std::istringstream iss(body); // wrap returned json to istringstream
    pt::read_json(iss, root);

    return root.get<int>("err", 0);
}

} // namespace Slic3r
