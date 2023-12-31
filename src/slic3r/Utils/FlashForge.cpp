#include "FlashForge.hpp"

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

namespace fs = boost::filesystem;
namespace pt = boost::property_tree;

namespace Slic3r {

FlashForge::FlashForge(DynamicPrintConfig* config) :
	m_host(config->opt_string("print_host")), m_console_port("8899")
{}

const char* FlashForge::get_name() const { return "FlashPrint"; }

bool FlashForge::test(wxString& msg) const
{
	Utils::TCPConsole console(m_host, m_console_port);

	console.enqueue_cmd("~M601 S1");
	bool ret = console.run_queue();

	if (!ret)
		msg = wxString::FromUTF8(console.error_message().c_str());

	return ret;
}

wxString FlashForge::get_test_ok_msg() const
{
	return _(L("Connection to FlashForge works correctly."));
}

wxString FlashForge::get_test_failed_msg(wxString& msg) const
{
	return GUI::from_u8((boost::format("%s: %s")
		% _utf8(L("Could not connect to MKS"))
		% std::string(msg.ToUTF8())).str());
}

bool FlashForge::upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn, InfoFn info_fn) const
{
	Utils::TCPConsole console(m_host, m_console_port);
	bool res = true;

	BOOST_LOG_TRIVIAL(info) << boost::format("FlashPrint: Uploading file %1%, filepath: %2%, print: %3%, command: %4%")
		% upload_data.source_path
		% upload_data.upload_path
		% (upload_data.post_action == PrintHostPostUploadAction::StartPrint)
		% "~M28";


	return res;
}

bool FlashForge::start_print(wxString& msg, const std::string& filename) const
{
	// For some reason printer firmware does not want to respond on gcode commands immediately after file upload.
	// So we just introduce artificial delay to workaround it.
	// TODO: Inspect reasons
	std::this_thread::sleep_for(std::chrono::milliseconds(1500));

	Utils::TCPConsole console(m_host, m_console_port);

	console.enqueue_cmd(std::string("M23 ") + filename);
	console.enqueue_cmd("M24");

	bool ret = console.run_queue();

	if (!ret)
		msg = wxString::FromUTF8(console.error_message().c_str());

	return ret;
}

int FlashForge::get_err_code_from_body(const std::string& body) const
{
	pt::ptree root;
	std::istringstream iss(body); // wrap returned json to istringstream
	pt::read_json(iss, root);

	return root.get<int>("err", 0);
}

} // Slic3r
