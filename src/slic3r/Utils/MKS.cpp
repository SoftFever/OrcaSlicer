#include "MKS.hpp"

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

MKS::MKS(DynamicPrintConfig *config) :
	host(config->opt_string("print_host")), console_port(8080)
{}

const char* MKS::get_name() const { return "MKS"; }

bool MKS::test(wxString &msg) const
{
	return run_simple_gcode("M105", msg);
}

wxString MKS::get_test_ok_msg () const
{
	return _(L("Connection to MKS works correctly."));
}

wxString MKS::get_test_failed_msg (wxString &msg) const
{
    return GUI::from_u8((boost::format("%s: %s")
                    % _utf8(L("Could not connect to MKS"))
                    % std::string(msg.ToUTF8())).str());
}

bool MKS::upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn) const
{
	bool res = true;

	auto upload_cmd = get_upload_url(upload_data.upload_path.string());
	BOOST_LOG_TRIVIAL(info) << boost::format("MKS: Uploading file %1%, filepath: %2%, print: %3%, command: %4%")
		% upload_data.source_path
		% upload_data.upload_path
		% upload_data.start_print
		% upload_cmd;

	auto http = Http::post(std::move(upload_cmd));
	http.set_post_body(upload_data.source_path);

	http.on_complete([&](std::string body, unsigned status) {
			BOOST_LOG_TRIVIAL(debug) << boost::format("MKS: File uploaded: HTTP %1%: %2%") % status % body;

			int err_code = get_err_code_from_body(body);
			if (err_code != 0) {
				BOOST_LOG_TRIVIAL(error) << boost::format("MKS: Request completed but error code was received: %1%") % err_code;
				error_fn(format_error(body, L("Unknown error occured"), 0));
				res = false;
			} else if (upload_data.start_print) {
				wxString errormsg;
				res = start_print(errormsg, upload_data.upload_path.string());
				if (! res) {
					error_fn(std::move(errormsg));
				}
			}
		})
		.on_error([&](std::string body, std::string error, unsigned status) {
			BOOST_LOG_TRIVIAL(error) << boost::format("MKS: Error uploading file: %1%, HTTP %2%, body: `%3%`") % error % status % body;
			error_fn(format_error(body, error, status));
			res = false;
		})
		.on_progress([&](Http::Progress progress, bool &cancel) {
			prorgess_fn(std::move(progress), cancel);
			if (cancel) {
				// Upload was canceled
				BOOST_LOG_TRIVIAL(info) << "MKS: Upload canceled";
				res = false;
			}
		})
		.perform_sync();

	if (res && upload_data.start_print) {
		// For some reason printer firmware does not want to respond on gcode commands immediately after file upload.
		// So we just introduce artificial delay to workaround it.
		std::this_thread::sleep_for(std::chrono::milliseconds(1500));

		wxString msg;
		res &= run_simple_gcode(std::string("M23 ") + upload_data.upload_path.string(), msg);
		if (res) {
			res &= run_simple_gcode(std::string("M24"), msg);
		}
	}

	return res;
}

std::string MKS::get_upload_url(const std::string &filename) const
{
	return (boost::format("http://%1%/upload?X-Filename=%2%")
		% host
		% Http::url_encode(filename)).str();
}

bool MKS::start_print(wxString &msg, const std::string &filename) const
{
	BOOST_LOG_TRIVIAL(warning) << boost::format("MKS: start_print is not implemented yet, called stub");
		return true;
}

int MKS::get_err_code_from_body(const std::string& body) const
{
	pt::ptree root;
	std::istringstream iss(body); // wrap returned json to istringstream
	pt::read_json(iss, root);

	return root.get<int>("err", 0);
}

bool MKS::run_simple_gcode(const std::string &cmd, wxString &msg) const
{
	using boost::asio::ip::tcp;

	try
	{
		boost::asio::io_context io_context;
		tcp::socket s(io_context);

		tcp::resolver resolver(io_context);
		boost::asio::connect(s, resolver.resolve(host, std::to_string(console_port)));
		boost::asio::write(s, boost::asio::buffer(cmd + "\r\n"));

		msg = "request:" + cmd + "\r\n";
		
		boost::asio::streambuf input_buffer;
		size_t reply_length = boost::asio::read_until(s, input_buffer, '\n');

		std::string response((std::istreambuf_iterator<char>(&input_buffer)), std::istreambuf_iterator<char>());
		if (response.length() == 0) {
			msg += "Empty response";
			return false;
		}

		msg += "response:" + response;
		return true;
	}
	catch (std::exception& e)
	{
		msg = std::string("exception:") + e.what();
		return false;
	}
}

}
