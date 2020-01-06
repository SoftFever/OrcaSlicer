#include "Duet.hpp"

#include <algorithm>
#include <ctime>
#include <boost/filesystem/path.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

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

Duet::Duet(DynamicPrintConfig *config) :
	host(config->opt_string("print_host")),
	password(config->opt_string("printhost_apikey"))
{}

const char* Duet::get_name() const { return "Duet"; }

bool Duet::test(wxString &msg) const
{
	bool connected = connect(msg);
	if (connected) {
		disconnect();
	}

	return connected;
}

wxString Duet::get_test_ok_msg () const
{
	return _(L("Connection to Duet works correctly."));
}

wxString Duet::get_test_failed_msg (wxString &msg) const
{
	return wxString::Format("%s: %s", _(L("Could not connect to Duet")), msg);
}

bool Duet::upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn) const
{
	wxString connect_msg;
	if (!connect(connect_msg)) {
		error_fn(std::move(connect_msg));
		return false;
	}

	bool res = true;

	auto upload_cmd = get_upload_url(upload_data.upload_path.string());
	BOOST_LOG_TRIVIAL(info) << boost::format("Duet: Uploading file %1%, filepath: %2%, print: %3%, command: %4%")
		% upload_data.source_path
		% upload_data.upload_path
		% upload_data.start_print
		% upload_cmd;

	auto http = Http::post(std::move(upload_cmd));
	http.set_post_body(upload_data.source_path)
		.on_complete([&](std::string body, unsigned status) {
			BOOST_LOG_TRIVIAL(debug) << boost::format("Duet: File uploaded: HTTP %1%: %2%") % status % body;

			int err_code = get_err_code_from_body(body);
			if (err_code != 0) {
				BOOST_LOG_TRIVIAL(error) << boost::format("Duet: Request completed but error code was received: %1%") % err_code;
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
			BOOST_LOG_TRIVIAL(error) << boost::format("Duet: Error uploading file: %1%, HTTP %2%, body: `%3%`") % error % status % body;
			error_fn(format_error(body, error, status));
			res = false;
		})
		.on_progress([&](Http::Progress progress, bool &cancel) {
			prorgess_fn(std::move(progress), cancel);
			if (cancel) {
				// Upload was canceled
				BOOST_LOG_TRIVIAL(info) << "Duet: Upload canceled";
				res = false;
			}
		})
		.perform_sync();

	disconnect();

	return res;
}

bool Duet::connect(wxString &msg) const
{
	bool res = false;
	auto url = get_connect_url();

	auto http = Http::get(std::move(url));
	http.on_error([&](std::string body, std::string error, unsigned status) {
			BOOST_LOG_TRIVIAL(error) << boost::format("Duet: Error connecting: %1%, HTTP %2%, body: `%3%`") % error % status % body;
			msg = format_error(body, error, status);
		})
		.on_complete([&](std::string body, unsigned) {
			BOOST_LOG_TRIVIAL(debug) << boost::format("Duet: Got: %1%") % body;

			int err_code = get_err_code_from_body(body);
			switch (err_code) {
				case 0:
					res = true;
					break;
				case 1:
					msg = format_error(body, L("Wrong password"), 0);
					break;
				case 2:
					msg = format_error(body, L("Could not get resources to create a new connection"), 0);
					break;
				default:
					msg = format_error(body, L("Unknown error occured"), 0);
					break;
			}

		})
		.perform_sync();

	return res;
}

void Duet::disconnect() const
{
	auto url =  (boost::format("%1%rr_disconnect")
			% get_base_url()).str();

	auto http = Http::get(std::move(url));
	http.on_error([&](std::string body, std::string error, unsigned status) {
		// we don't care about it, if disconnect is not working Duet will disconnect automatically after some time
		BOOST_LOG_TRIVIAL(error) << boost::format("Duet: Error disconnecting: %1%, HTTP %2%, body: `%3%`") % error % status % body;
	})
	.perform_sync();
}

std::string Duet::get_upload_url(const std::string &filename) const
{
	return (boost::format("%1%rr_upload?name=0:/gcodes/%2%&%3%")
			% get_base_url()
			% Http::url_encode(filename) 
			% timestamp_str()).str();
}

std::string Duet::get_connect_url() const
{
	return (boost::format("%1%rr_connect?password=%2%&%3%")
			% get_base_url()
			% (password.empty() ? "reprap" : password)
			% timestamp_str()).str();
}

std::string Duet::get_base_url() const
{
	if (host.find("http://") == 0 || host.find("https://") == 0) {
		if (host.back() == '/') {
			return host;
		} else {
			return (boost::format("%1%/") % host).str();
		}
	} else {
		return (boost::format("http://%1%/") % host).str();
	}
}

std::string Duet::timestamp_str() const
{
	enum { BUFFER_SIZE = 32 };

	auto t = std::time(nullptr);
	auto tm = *std::localtime(&t);

	char buffer[BUFFER_SIZE];
	std::strftime(buffer, BUFFER_SIZE, "time=%Y-%m-%dT%H:%M:%S", &tm);

	return std::string(buffer);
}

bool Duet::start_print(wxString &msg, const std::string &filename) const 
{
	bool res = false;

	auto url = (boost::format("%1%rr_gcode?gcode=M32%%20\"%2%\"")
			% get_base_url()
			% Http::url_encode(filename)).str();

	auto http = Http::get(std::move(url));
	http.on_error([&](std::string body, std::string error, unsigned status) {
			BOOST_LOG_TRIVIAL(error) << boost::format("Duet: Error starting print: %1%, HTTP %2%, body: `%3%`") % error % status % body;
			msg = format_error(body, error, status);
		})
		.on_complete([&](std::string body, unsigned) {
			BOOST_LOG_TRIVIAL(debug) << boost::format("Duet: Got: %1%") % body;
			res = true;
		})
		.perform_sync();

	return res;
}

int Duet::get_err_code_from_body(const std::string &body) const
{
	pt::ptree root;
	std::istringstream iss (body); // wrap returned json to istringstream
	pt::read_json(iss, root);

	return root.get<int>("err", 0);
}

}
