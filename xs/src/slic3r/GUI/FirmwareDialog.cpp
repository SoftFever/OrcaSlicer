#include "FirmwareDialog.hpp"

#include <ostream>
#include <numeric>
#include <boost/format.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>


#include <wx/app.h>
#include <wx/event.h>
#include <wx/sizer.h>
#include <wx/settings.h>
#include <wx/panel.h>
#include <wx/button.h>
#include <wx/filepicker.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <wx/combobox.h>
#include <wx/gauge.h>
#include <wx/collpane.h>

#include "libslic3r/Utils.hpp"
#include "avrdude/avrdude-slic3r.hpp"
#include "GUI.hpp"

namespace fs = boost::filesystem;


namespace Slic3r {


// Private

struct FirmwareDialog::priv
{
	std::string avrdude_config;

	wxComboBox *port_picker;
	wxFilePickerCtrl *hex_picker;
	wxStaticText *txt_status;
	wxStaticText *txt_progress;
	wxGauge *progressbar;
	wxTextCtrl *txt_stdout;
	wxButton *btn_close;
	wxButton *btn_flash;

	bool flashing;
	unsigned progress_tasks_done;

	priv() :
		avrdude_config((fs::path(::Slic3r::resources_dir()) / "avrdude" / "avrdude.conf").string()),
		flashing(false),
		progress_tasks_done(0)
	{}

	void find_serial_ports();
	void set_flashing(bool flashing, int res = 0);
	void perform_upload();
};

void FirmwareDialog::priv::find_serial_ports()
{
	auto ports = GUI::scan_serial_ports();

	port_picker->Clear();
	for (const auto &port : ports) { port_picker->Append(port); }

	if (ports.size() > 0 && port_picker->GetValue().IsEmpty()) {
		port_picker->SetSelection(0);
	}
}

void FirmwareDialog::priv::set_flashing(bool value, int res)
{
	flashing = value;

	if (value) {
		txt_stdout->SetValue(wxEmptyString);
		txt_status->SetLabel(_(L("Flashing in progress. Please do not disconnect the printer!")));
		txt_status->SetForegroundColour(GUI::get_label_clr_modified());
		btn_close->Disable();
		btn_flash->Disable();
		progressbar->SetRange(200);   // See progress callback below
		progressbar->SetValue(0);
		progress_tasks_done = 0;
	} else {
		auto text_color = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
		btn_close->Enable();
		btn_flash->Enable();
		txt_status->SetForegroundColour(text_color);
		txt_status->SetLabel(
			res == 0 ? _(L("Flashing succeeded!")) : _(L("Flashing failed. Please see the avrdude log below."))
		);
		progressbar->SetValue(200);
	}
}

void FirmwareDialog::priv::perform_upload()
{
	auto filename = hex_picker->GetPath();
	auto port = port_picker->GetValue();
	if (filename.IsEmpty() || port.IsEmpty()) { return; }

	set_flashing(true);

	// Note: we're not using wxTextCtrl's ability to act as a std::ostream
	// because its imeplementation doesn't handle conversion from local charset
	// which mangles error messages from perror().

	auto message_fn = [this](const char *msg, unsigned /* size */) {
		// TODO: also log into boost? (Problematic with progress bars.)
		this->txt_stdout->AppendText(wxString(msg));
		wxTheApp->Yield();
	};

	auto progress_fn = [this](const char *, unsigned progress) {
		// We try to track overall progress here.
		// When uploading the firmware, avrdude first reas a littlebit of status data,
		// then performs write, then reading (verification).
		// We Pulse() during the first read and combine progress of the latter two tasks.

		if (this->progress_tasks_done == 0) {
			this->progressbar->Pulse();
		} else {
			this->progressbar->SetValue(this->progress_tasks_done - 100 + progress);
		}

		if (progress == 100) {
			this->progress_tasks_done += 100;
		}

		wxTheApp->Yield();
	};

	std::vector<std::string> args {{
		"-v",
		"-p", "atmega2560",
		"-c", "wiring",
		"-P", port.ToStdString(),
		"-b", "115200",   // XXX: is this ok to hardcode?
		"-D",
		"-U", (boost::format("flash:w:%1%:i") % filename.ToStdString()).str()
	}};

	BOOST_LOG_TRIVIAL(info) << "Invoking avrdude, arguments: "
		<< std::accumulate(std::next(args.begin()), args.end(), args[0], [](std::string a, const std::string &b) {
			return a + ' ' + b;
		});

	auto res = AvrDude()
		.sys_config(avrdude_config)
		.on_message(std::move(message_fn))
		.on_progress(std::move(progress_fn))
		.run(std::move(args));

	BOOST_LOG_TRIVIAL(info) << "avrdude exit code: " << res;

	set_flashing(false, res);
}


// Public

FirmwareDialog::FirmwareDialog(wxWindow *parent) :
	wxDialog(parent, wxID_ANY, _(L("Firmware flasher"))),
	p(new priv())
{
	enum {
		DIALOG_MARGIN = 15,
		SPACING = 10,
		MIN_WIDTH = 600,
		MIN_HEIGHT = 100,
		LOG_MIN_HEIGHT = 200,
	};

	wxFont status_font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
	status_font.MakeBold();
	wxFont mono_font(wxFontInfo().Family(wxFONTFAMILY_TELETYPE));
	mono_font.MakeSmaller();

	auto *panel = new wxPanel(this);
	wxBoxSizer *vsizer = new wxBoxSizer(wxVERTICAL);
	panel->SetSizer(vsizer);

	auto *label_port_picker = new wxStaticText(panel, wxID_ANY, _(L("Serial port:")));
	p->port_picker = new wxComboBox(panel, wxID_ANY);
	auto *btn_rescan = new wxButton(panel, wxID_ANY, _(L("Rescan")));
	auto *port_sizer = new wxBoxSizer(wxHORIZONTAL);
	port_sizer->Add(p->port_picker, 1, wxEXPAND | wxRIGHT, SPACING);
	port_sizer->Add(btn_rescan, 0);

	auto *label_hex_picker = new wxStaticText(panel, wxID_ANY, _(L("Firmware image:")));
	p->hex_picker = new wxFilePickerCtrl(panel, wxID_ANY);

	auto *label_status = new wxStaticText(panel, wxID_ANY, _(L("Status:")));
	p->txt_status = new wxStaticText(panel, wxID_ANY, _(L("Ready")));
	p->txt_status->SetFont(status_font);

	auto *label_progress = new wxStaticText(panel, wxID_ANY, _(L("Progress:")));
	p->progressbar = new wxGauge(panel, wxID_ANY, 1, wxDefaultPosition, wxDefaultSize, wxGA_HORIZONTAL | wxGA_SMOOTH);

	auto *grid = new wxFlexGridSizer(2, SPACING, SPACING);
	grid->AddGrowableCol(1);
	grid->Add(label_port_picker, 0, wxALIGN_CENTER_VERTICAL);
	grid->Add(port_sizer, 0, wxEXPAND);

	grid->Add(label_hex_picker, 0, wxALIGN_CENTER_VERTICAL);
	grid->Add(p->hex_picker, 0, wxEXPAND);

	grid->Add(label_status, 0, wxALIGN_CENTER_VERTICAL);
	grid->Add(p->txt_status, 0, wxEXPAND);

	grid->Add(label_progress, 0, wxALIGN_CENTER_VERTICAL);
	grid->Add(p->progressbar, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

	vsizer->Add(grid, 0, wxEXPAND | wxTOP | wxBOTTOM, SPACING);

	// Unfortunatelly wxCollapsiblePane seems to resize parent in weird ways.
	// Sometimes it disrespects min size.
	// The only combo that seems to work well is setting size in its c-tor and a min size on the window.
	auto *spoiler = new wxCollapsiblePane(panel, wxID_ANY, _(L("Advanced: avrdude output log")), wxDefaultPosition, wxSize(MIN_WIDTH, MIN_HEIGHT));
	auto *spoiler_pane = spoiler->GetPane();
	auto *spoiler_sizer = new wxBoxSizer(wxVERTICAL);
	p->txt_stdout = new wxTextCtrl(spoiler_pane, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(0, LOG_MIN_HEIGHT), wxTE_MULTILINE | wxTE_READONLY);
	p->txt_stdout->SetFont(mono_font);
	spoiler_sizer->Add(p->txt_stdout, 1, wxEXPAND);
	spoiler_pane->SetSizer(spoiler_sizer);
	vsizer->Add(spoiler, 1, wxEXPAND | wxBOTTOM, SPACING);

	p->btn_close = new wxButton(panel, wxID_CLOSE);
	p->btn_flash = new wxButton(panel, wxID_ANY, _(L("Flash!")));
	auto *bsizer = new wxBoxSizer(wxHORIZONTAL);
	bsizer->Add(p->btn_close);
	bsizer->AddStretchSpacer();
	bsizer->Add(p->btn_flash);
	vsizer->Add(bsizer, 0, wxEXPAND);

	auto *topsizer = new wxBoxSizer(wxVERTICAL);
	topsizer->Add(panel, 1, wxEXPAND | wxALL, DIALOG_MARGIN);
	SetSizerAndFit(topsizer);
	SetMinSize(wxSize(MIN_WIDTH, MIN_HEIGHT));

	p->find_serial_ports();

	p->btn_close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { this->Close(); });
	p->btn_flash->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { this->p->perform_upload(); });
	btn_rescan->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { this->p->find_serial_ports(); });

	Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &evt) {
		if (evt.CanVeto() && this->p->flashing) {
			evt.Veto();
		} else {
			evt.Skip();
		}
	});
}

FirmwareDialog::~FirmwareDialog()
{
	// Needed bacuse of forward defs
}

void FirmwareDialog::run(wxWindow *parent)
{
	FirmwareDialog dialog(parent);
	dialog.ShowModal();
}


}
