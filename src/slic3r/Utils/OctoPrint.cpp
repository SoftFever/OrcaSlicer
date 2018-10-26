#include "OctoPrint.hpp"
#include "PrintHostSendDialog.hpp"

#include <algorithm>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "Http.hpp"

namespace fs = boost::filesystem;


namespace Slic3r {

OctoPrint::OctoPrint(DynamicPrintConfig *config) :
	host(config->opt_string("print_host")),
	apikey(config->opt_string("printhost_apikey")),
	cafile(config->opt_string("printhost_cafile"))
{}

OctoPrint::~OctoPrint() {}

bool OctoPrint::test(wxString &msg) const
{
	// Since the request is performed synchronously here,
	// it is ok to refer to `msg` from within the closure

	bool res = true;
	auto url = make_url("api/version");

	BOOST_LOG_TRIVIAL(info) << boost::format("Octoprint: Get version at: %1%") % url;

	auto http = Http::get(std::move(url));
	set_auth(http);
	http.on_error([&](std::string body, std::string error, unsigned status) {
			BOOST_LOG_TRIVIAL(error) << boost::format("Octoprint: Error getting version: %1%, HTTP %2%, body: `%3%`") % error % status % body;
			res = false;
			msg = format_error(body, error, status);
		})
		.on_complete([&](std::string body, unsigned) {
			BOOST_LOG_TRIVIAL(debug) << boost::format("Octoprint: Got version: %1%") % body;
		})
		.perform_sync();

	return res;
}

wxString OctoPrint::get_test_ok_msg () const
{
	return wxString::Format("%s", _(L("Connection to OctoPrint works correctly.")));
}

wxString OctoPrint::get_test_failed_msg (wxString &msg) const
{
	return wxString::Format("%s: %s\n\n%s",
						_(L("Could not connect to OctoPrint")), msg, _(L("Note: OctoPrint version at least 1.1.0 is required.")));
}

bool OctoPrint::send_gcode(const std::string &filename) const
{
	enum { PROGRESS_RANGE = 1000 };

	const auto errortitle = _(L("Error while uploading to the OctoPrint server"));
	fs::path filepath(filename);

	PrintHostSendDialog send_dialog(filepath.filename(), true);
	if (send_dialog.ShowModal() != wxID_OK) { return false; }

	const bool print = send_dialog.print();
	const auto upload_filepath = send_dialog.filename();
	const auto upload_filename = upload_filepath.filename();
	const auto upload_parent_path = upload_filepath.parent_path();

	wxProgressDialog progress_dialog(
		_(L("OctoPrint upload")),
		_(L("Sending G-code file to the OctoPrint server...")),
		PROGRESS_RANGE, nullptr, wxPD_AUTO_HIDE | wxPD_APP_MODAL | wxPD_CAN_ABORT);
	progress_dialog.Pulse();

	wxString test_msg;
	if (!test(test_msg)) {
		auto errormsg = wxString::Format("%s: %s", errortitle, test_msg);
		GUI::show_error(&progress_dialog, std::move(errormsg));
		return false;
	}

	bool res = true;

	auto url = make_url("api/files/local");

	BOOST_LOG_TRIVIAL(info) << boost::format("Octoprint: Uploading file %1% at %2%, filename: %3%, path: %4%, print: %5%")
		% filepath.string()
		% url
		% upload_filename.string()
		% upload_parent_path.string()
		% print;

	auto http = Http::post(std::move(url));
	set_auth(http);
	http.form_add("print", print ? "true" : "false")
		.form_add("path", upload_parent_path.string())
		.form_add_file("file", filename, upload_filename.string())
		.on_complete([&](std::string body, unsigned status) {
			BOOST_LOG_TRIVIAL(debug) << boost::format("Octoprint: File uploaded: HTTP %1%: %2%") % status % body;
			progress_dialog.Update(PROGRESS_RANGE);
		})
		.on_error([&](std::string body, std::string error, unsigned status) {
			BOOST_LOG_TRIVIAL(error) << boost::format("Octoprint: Error uploading file: %1%, HTTP %2%, body: `%3%`") % error % status % body;
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

	return res;
}

bool OctoPrint::has_auto_discovery() const
{
	return true;
}

bool OctoPrint::can_test() const
{
	return true;
}

void OctoPrint::set_auth(Http &http) const
{
	http.header("X-Api-Key", apikey);

	if (! cafile.empty()) {
		http.ca_file(cafile);
	}
}

std::string OctoPrint::make_url(const std::string &path) const
{
	if (host.find("http://") == 0 || host.find("https://") == 0) {
		if (host.back() == '/') {
			return (boost::format("%1%%2%") % host % path).str();
		} else {
			return (boost::format("%1%/%2%") % host % path).str();
		}
	} else {
		return (boost::format("http://%1%/%2%") % host % path).str();
	}
}

wxString OctoPrint::format_error(const std::string &body, const std::string &error, unsigned status)
{
	if (status != 0) {
		auto wxbody = wxString::FromUTF8(body.data());
		return wxString::Format("HTTP %u: %s", status, wxbody);
	} else {
		return wxString::FromUTF8(error.data());
	}
}


}
