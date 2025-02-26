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

Flashforge::Flashforge(DynamicPrintConfig* config) : m_host(config->opt_string("print_host")), m_console_port("8899")
{
}

const char* Flashforge::get_name() const { return "Flashforge"; }

bool Flashforge::test(wxString& msg) const
{
    BOOST_LOG_TRIVIAL(debug) << boost::format("[Flashforge] testing connection");
    // Utils::TCPConsole console(m_host, m_console_port);
    Utils::TCPConsole client(m_host, m_console_port);
    client.enqueue_cmd(controlCommand);
    bool res = client.run_queue();
    if (!res) {
        msg = wxString::FromUTF8(client.error_message().c_str());
        BOOST_LOG_TRIVIAL(info) << boost::format("[Flashforge] testing connection failed");
    } else {
        BOOST_LOG_TRIVIAL(info) << boost::format("[Flashforge] testing connection success");
    }
    return res;
}

wxString Flashforge::get_test_ok_msg() const { return _(L("Connection to Flashforge works correctly.")); }

wxString Flashforge::get_test_failed_msg(wxString& msg) const
{
    return GUI::from_u8((boost::format("%s: %s") % _utf8(L("Could not connect to Flashforge")) % std::string(msg.ToUTF8())).str());
}

bool Flashforge::upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn info_fn) const
{
    bool res = true;

    Utils::TCPConsole client(m_host, m_console_port);
    //sometimes FF AD5M is very slow in data upload, so timeout is increased to 10 minutes
    client.set_write_timeout(std::chrono::minutes(10));
    client.set_read_timeout(std::chrono::minutes(10));
    client.enqueue_cmd(controlCommand);
   
    client.enqueue_cmd(connect5MCommand);
   
    client.enqueue_cmd(statusCommand);
    wxString errormsg;
    try {
        std::ifstream newfile;
        newfile.open(upload_data.source_path.c_str(), std::ios::binary); // open a file to perform read operation using file object
        if (newfile.is_open()) {                                         // checking whether the file is open
            BOOST_LOG_TRIVIAL(info) << boost::format("[Flashforge] Reading file...");
            newfile.seekg(0, std::ios::end);
            std::ifstream::pos_type pos = newfile.tellg();

            std::vector<char> result(pos);

            newfile.seekg(0, std::ios::beg);
            newfile.read(&result[0], pos);
            BOOST_LOG_TRIVIAL(info) << boost::format("[Flashforge] Reading file...done size is %1%") % result.size();

            Slic3r::Utils::SerialMessage fileuploadCommand =
                {(boost::format("~M28 %1% 0:/user/%2%") % result.size() % upload_data.upload_path.generic_string()).str(),
                 Slic3r::Utils::Command};
            client.enqueue_cmd(fileuploadCommand);
            Slic3r::Utils::SerialMessage dataCommand = {std::string(result.begin(), result.end()), Slic3r::Utils::Data};
            client.enqueue_cmd(dataCommand);
            newfile.close(); // close the file object.
            BOOST_LOG_TRIVIAL(info) << boost::format("[Flashforge] Sent %1% ") % result.size();
        }
        BOOST_LOG_TRIVIAL(info) << boost::format("[Flashforge] Sending file save command ");
        client.enqueue_cmd(saveFileCommand);
        if (upload_data.post_action == PrintHostPostUploadAction::StartPrint) {
            BOOST_LOG_TRIVIAL(info) << boost::format("[Flashforge] Starting print %1%") % upload_data.upload_path.string();
            Slic3r::Utils::SerialMessage startPrintCommand = {(boost::format("~M23 0:/user/%1%") % upload_data.upload_path.string()).str(),
                                                              Slic3r::Utils::Command};
            client.enqueue_cmd(startPrintCommand);
        }

        res = client.run_queue();

        if (!res) {
            BOOST_LOG_TRIVIAL(info) << boost::format("[Flashforge] error %1%") % client.error_message().c_str();
            errormsg = wxString::FromUTF8(client.error_message().c_str());
        }
        if (!res) {
            error_fn(std::move(errormsg));
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(info) << boost::format("[Flashforge] error %1%") % e.what();
        errormsg = wxString::FromUTF8(e.what());
        error_fn(std::move(errormsg));
    }

    return res;
}

int Flashforge::get_err_code_from_body(const std::string& body) const
{
    pt::ptree          root;
    std::istringstream iss(body); // wrap returned json to istringstream
    pt::read_json(iss, root);

    return root.get<int>("err", 0);
}

} // namespace Slic3r
