#include "OctoPrint.hpp"

#include <algorithm>
#include <boost/filesystem/path.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

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


namespace Slic3r {


struct SendDialog : public GUI::MsgDialog
{
	wxTextCtrl *txt_filename;
	wxCheckBox *box_print;

	SendDialog(const fs::path &path) :
		MsgDialog(nullptr, _(L("Send G-Code to printer")), _(L("Upload to OctoPrint with the following filename:")), wxID_NONE),
		txt_filename(new wxTextCtrl(this, wxID_ANY, path.filename().wstring())),
		box_print(new wxCheckBox(this, wxID_ANY, _(L("Start printing after upload"))))
	{
		auto *label_dir_hint = new wxStaticText(this, wxID_ANY, _(L("Use forward slashes ( / ) as a directory separator if needed.")));
		label_dir_hint->Wrap(CONTENT_WIDTH);

		content_sizer->Add(txt_filename, 0, wxEXPAND);
		content_sizer->Add(label_dir_hint);
		content_sizer->AddSpacer(VERT_SPACING);
		content_sizer->Add(box_print, 0, wxBOTTOM, 2*VERT_SPACING);

		btn_sizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL));

		txt_filename->SetFocus();
		wxString stem(path.stem().wstring());
		txt_filename->SetSelection(0, stem.Length());

		Fit();
	}

	fs::path filename() const {
		return fs::path(txt_filename->GetValue().wx_str());
	}

	bool print() const { return box_print->GetValue(); }
};



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

bool OctoPrint::send_gcode(const std::string &filename) const
{
	enum { PROGRESS_RANGE = 1000 };

	const auto errortitle = _(L("Error while uploading to the OctoPrint server"));
	fs::path filepath(filename);

	SendDialog send_dialog(filepath.filename());
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
