#include "OctoPrint.hpp"

#include <algorithm>
#include <boost/format.hpp>

#include <wx/frame.h>
#include <wx/event.h>
#include <wx/progdlg.h>

#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "Http.hpp"


namespace Slic3r {


OctoPrint::OctoPrint(DynamicPrintConfig *config) :
	host(config->opt_string("octoprint_host")),
	apikey(config->opt_string("octoprint_apikey")),
	cafile(config->opt_string("octoprint_cafile"))
{}

bool OctoPrint::test(wxString &msg) const
{
	// Since the request is performed synchronously here,
	// it is ok to refer to `msg` from within the closure

	bool res = true;

	auto url = std::move(make_url("api/version"));
	auto http = Http::get(std::move(url));
	set_auth(http);
	http.on_error([&](std::string, std::string error, unsigned status) {
			res = false;
			msg = format_error(error, status);
		})
		.perform_sync();

	return res;
}

bool OctoPrint::send_gcode(const std::string &filename, bool print) const
{
	enum { PROGRESS_RANGE = 1000 };

	const auto errortitle = _(L("Error while uploading to the OctoPrint server"));

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

	auto http = Http::post(std::move(make_url("api/files/local")));
	set_auth(http);
	http.form_add("print", print ? "true" : "false")
		.form_add_file("file", filename)
		.on_complete([&](std::string body, unsigned status) {
			progress_dialog.Update(PROGRESS_RANGE);
		})
		.on_error([&](std::string body, std::string error, unsigned status) {
			auto errormsg = wxString::Format("%s: %s", errortitle, format_error(error, status));
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
			return std::move((boost::format("%1%%2%") % host % path).str());
		} else {
			return std::move((boost::format("%1%/%2%") % host % path).str());
		}
	} else {
		return std::move((boost::format("http://%1%/%2%") % host % path).str());
	}
}

wxString OctoPrint::format_error(std::string error, unsigned status)
{
	const wxString wxerror = error;

	if (status != 0) {
		return wxString::Format("HTTP %u: %s", status,
			(status == 401 ? _(L("Invalid API key")) : wxerror));
	} else {
		return std::move(wxerror);
	}
}


}
