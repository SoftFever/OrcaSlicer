#include "OctoPrint.hpp"

#include <iostream>
#include <boost/format.hpp>

#include <wx/frame.h>
#include <wx/event.h>

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

void OctoPrint::send_gcode(int windowId, int completeEvt, int errorEvt, const std::string &filename, bool print) const
{
	auto http = Http::post(std::move(make_url("api/files/local")));
	set_auth(http);
	http.form_add("print", print ? "true" : "false")
		.form_add_file("file", filename)
		.on_complete([=](std::string body, unsigned status) {
			wxWindow *window = wxWindow::FindWindowById(windowId);
			if (window == nullptr) { return; }

			wxCommandEvent* evt = new wxCommandEvent(completeEvt);
			evt->SetString(_(L("G-code file successfully uploaded to the OctoPrint server")));
			evt->SetInt(100);
			wxQueueEvent(window, evt);
		})
		.on_error([=](std::string body, std::string error, unsigned status) {
			wxWindow *window = wxWindow::FindWindowById(windowId);
			if (window == nullptr) { return; }

			wxCommandEvent* evt_complete = new wxCommandEvent(completeEvt);
			evt_complete->SetInt(100);
			wxQueueEvent(window, evt_complete);

			wxCommandEvent* evt_error = new wxCommandEvent(errorEvt);
			evt_error->SetString(wxString::Format("%s: %s",
				_(L("Error while uploading to the OctoPrint server")),
				format_error(error, status)));
			wxQueueEvent(window, evt_error);
		})
		.perform();
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
