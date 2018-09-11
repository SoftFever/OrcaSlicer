#include "Duet.hpp"
#include "PrintHostSendDialog.hpp"

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
#include "slic3r/GUI/MsgDialog.hpp"
#include "Http.hpp"

namespace fs = boost::filesystem;
namespace pt = boost::property_tree;

namespace Slic3r {

Duet::Duet(DynamicPrintConfig *config) :
	host(config->opt_string("print_host")),
	password(config->opt_string("printhost_apikey"))
{}

Duet::~Duet() {}

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
	return wxString::Format("%s", _(L("Connection to Duet works correctly.")));
}

wxString Duet::get_test_failed_msg (wxString &msg) const
{
	return wxString::Format("%s: %s", _(L("Could not connect to Duet")), msg);
}

bool Duet::send_gcode(const std::string &filename) const
{
	enum { PROGRESS_RANGE = 1000 };

	const auto errortitle = _(L("Error while uploading to the Duet"));
	fs::path filepath(filename);

	PrintHostSendDialog send_dialog(filepath.filename(), true);
	if (send_dialog.ShowModal() != wxID_OK) { return false; }

	const bool print = send_dialog.print(); 
	const auto upload_filepath = send_dialog.filename();
	const auto upload_filename = upload_filepath.filename();
	const auto upload_parent_path = upload_filepath.parent_path();

	wxProgressDialog progress_dialog(
	 	_(L("Duet upload")),
	 	_(L("Sending G-code file to Duet...")),
		PROGRESS_RANGE, nullptr, wxPD_AUTO_HIDE | wxPD_APP_MODAL | wxPD_CAN_ABORT);
	progress_dialog.Pulse();

	wxString connect_msg;
	if (!connect(connect_msg)) {
		auto errormsg = wxString::Format("%s: %s", errortitle, connect_msg);
		GUI::show_error(&progress_dialog, std::move(errormsg));
		return false;
	}

	bool res = true;

	auto upload_cmd = get_upload_url(upload_filepath.string());
	BOOST_LOG_TRIVIAL(info) << boost::format("Duet: Uploading file %1%, filename: %2%, path: %3%, print: %4%, command: %5%")
		% filepath.string()
		% upload_filename.string()
		% upload_parent_path.string()
		% print
		% upload_cmd;

	auto http = Http::post(std::move(upload_cmd));
	http.set_post_body(filename)
		.on_complete([&](std::string body, unsigned status) {
			BOOST_LOG_TRIVIAL(debug) << boost::format("Duet: File uploaded: HTTP %1%: %2%") % status % body;
			progress_dialog.Update(PROGRESS_RANGE);

			int err_code = get_err_code_from_body(body);
			if (err_code != 0) {
				auto msg = format_error(body, L("Unknown error occured"), 0);
				GUI::show_error(&progress_dialog, std::move(msg));
				res = false;
			} else if (print) {
				wxString errormsg;
				res = start_print(errormsg, upload_filepath.string());
				if (!res) {
					GUI::show_error(&progress_dialog, std::move(errormsg));
				}
			}
		})
		.on_error([&](std::string body, std::string error, unsigned status) {
			BOOST_LOG_TRIVIAL(error) << boost::format("Duet: Error uploading file: %1%, HTTP %2%, body: `%3%`") % error % status % body;
			auto errormsg = wxString::Format("%s: %s", errortitle, format_error(body, error, status));
			GUI::show_error(&progress_dialog, std::move(errormsg));
			res = false;
		})
		.on_progress([&](Http::Progress progress, bool &cancel) {
			if (cancel) {
				// Upload was canceled
				res = false;
			} else if (progress.ultotal > 0) {
				int value = PROGRESS_RANGE * progress.ulnow / progress.ultotal;
				cancel = !progress_dialog.Update(std::min(value, PROGRESS_RANGE - 1));    // Cap the value to prevent premature dialog closing
			} else {
				cancel = !progress_dialog.Pulse();
			}
		})
		.perform_sync();

	disconnect();

	return res;
}

bool Duet::has_auto_discovery() const
{
	return false;
}

bool Duet::can_test() const
{
	return true;
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

wxString Duet::format_error(const std::string &body, const std::string &error, unsigned status)
{
	if (status != 0) {
		auto wxbody = wxString::FromUTF8(body.data());
		return wxString::Format("HTTP %u: %s", status, wxbody);
	} else {
		return wxString::FromUTF8(error.data());
	}
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
