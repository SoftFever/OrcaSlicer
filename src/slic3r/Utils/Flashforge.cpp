#include "Flashforge.hpp"
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
#include "TCPConsole.hpp"
#include "SerialMessage.hpp"
#include "SerialMessageType.hpp"

namespace fs = boost::filesystem;
namespace pt = boost::property_tree;

namespace Slic3r {

Flashforge::Flashforge(DynamicPrintConfig* config)
    : m_host(config->opt_string("print_host"))
    , m_port("8898")
    , m_serial(config->opt_string("machine_serial"))
    , m_activation_code(config->opt_string("activation_code"))
{
    BOOST_LOG_TRIVIAL(error) << boost::format("Flashforge: init  %1% %2% %3% %4%") % m_host % m_port % m_serial % m_activation_code;
}

const char* Flashforge::get_name() const { return "Flashforge"; }

std::string Flashforge::make_url(const std::string& path) const
{
    if (m_host.find("http://") == 0 || m_host.find("https://") == 0) {
        if (m_host.back() == '/') {
            return (boost::format("%1%:%2%%3%") % m_host % m_port % path).str();
        } else {
            return (boost::format("%1%:%2%/%3%") % m_host % m_port % path).str();
        }
    } else {
        return (boost::format("http://%1%:%2%/%3%") % m_host % m_port % path).str();
    }
}

bool Flashforge::test(wxString& msg) const
{
    const char* name = get_name();

    BOOST_LOG_TRIVIAL(error) << boost::format("%1%: init test %2% %3% %4% %5%") % name % m_host % m_port % m_serial % m_activation_code;

    bool res = true;
    auto url = make_url("product");

    BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Get printer at: %2%") % name % url;

    auto http = Http::post(std::move(url));
    http.header("Accept", "application/json");
    std::string body = (boost::format("{\"serialNumber\":\"%1%\",\"checkCode\":\"%2%\"}") % m_serial % m_activation_code).str();
    BOOST_LOG_TRIVIAL(error) << boost::format("%1%: %2% with body %3%") % name % url % body;

    http.set_post_body(std::move(body));

    http.on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error getting product info %2%, HTTP %3%, body: `%4%`") % name % error %
                                            status % body;
            res = false;
            msg = format_error(body, error, status);
        })
        .on_complete([&, this](std::string body, unsigned) {
            BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Got info: %2%") % name % body;

            try {
                std::stringstream ss(body);
                pt::ptree         ptree;
                pt::read_json(ss, ptree);

                const auto code    = ptree.get_optional<std::int32_t>("code");
                const auto message = ptree.get_optional<std::string>("message");

                BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Got info: %2% %3%") % name % code % message;

                if (code != 0) {
                    msg = GUI::format_wxstr(_L("%s"), (message));
                    res = false;
                }
            } catch (const std::exception&) {
                res = false;
                msg = "Could not parse server response";
            }
        })
        .perform_sync();

    return res;
}

wxString Flashforge::get_test_ok_msg() const { return _(L("Connection to Flashforge works correctly.")); }

wxString Flashforge::get_test_failed_msg(wxString& msg) const
{
    return GUI::from_u8((boost::format("%s: %s") % _utf8(L("Could not connect to Flashforge")) % std::string(msg.ToUTF8())).str());
}

bool Flashforge::upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn info_fn) const
{
    const char* name = get_name();
    const auto upload_filename = upload_data.upload_path.filename();
    const auto upload_parent_path = upload_data.upload_path.parent_path();
    std::string url = make_url("uploadGcode");
    bool result = true;

    BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Uploading file %2% at %3%, filename: %4%, path: %5%, print: %6%")
        % name
        % upload_data.source_path
        % url
        % upload_filename.string()
        % upload_parent_path.string()
        % (upload_data.post_action == PrintHostPostUploadAction::StartPrint ? "true" : "false");

    auto http = Http::post(std::move(url));
    http.header("serialNumber", m_serial);
    http.header("checkCode", m_activation_code);
    http.header("printNow", upload_data.post_action == PrintHostPostUploadAction::StartPrint ? "true" : "false");
    http.header("levelingBeforePrint", "false");
    std::uintmax_t filesize = boost::filesystem::file_size(upload_data.source_path.c_str());
    http.header("fileSize", std::to_string(filesize));

    http.form_add_file("gcodeFile", upload_data.source_path.string(), upload_filename.string())
  
        .on_complete([&](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("%1%: File uploaded: HTTP %2%: %3%") % name % status % body;
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error uploading file to %2%: %3%, HTTP %4%, body: `%5%`") % name % url % error % status % body;
            error_fn(format_error(body, error, status));
            result = false;
        })
        .on_progress([&](Http::Progress progress, bool& cancel) {
            progress_fn(std::move(progress), cancel);
            if (cancel) {
                // Upload was canceled
                BOOST_LOG_TRIVIAL(error) << name << ": Upload canceled";
                result = false;
            }
        })
        .perform_sync();

    return result;
}
} // namespace Slic3r
