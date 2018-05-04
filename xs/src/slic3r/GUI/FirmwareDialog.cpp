#include "FirmwareDialog.hpp"

#include <ostream>
#include <numeric>
#include <boost/format.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>


#include <wx/app.h>
#include <wx/sizer.h>
#include <wx/settings.h>
#include <wx/panel.h>
#include <wx/button.h>
#include <wx/filepicker.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <wx/combobox.h>

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
	wxStaticText *status;
	wxTextCtrl *txt_stdout;
	wxButton *btn_flash;

	priv() : avrdude_config((fs::path(::Slic3r::resources_dir()) / "avrdude" / "avrdude.conf").string()) {}

	void find_serial_ports();
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

void FirmwareDialog::priv::perform_upload()
{
	auto filename = hex_picker->GetPath();
	auto port = port_picker->GetValue();
	if (filename.IsEmpty() || port.IsEmpty()) { return; }

	// Note: we're not using wxTextCtrl's ability to act as a std::ostream
	// because its imeplementation doesn't handle conversion from local charset
	// which mangles error messages from perror().

	auto message_fn = [this](const char *msg, unsigned /* size */) {
		// TODO: also log into boost? (Problematic with progress bars.)
		this->txt_stdout->AppendText(wxString(msg));
		wxTheApp->Yield();
	};

	txt_stdout->SetValue(wxEmptyString);
	status->SetLabel(_(L("Flashing in progress. Please do not disconnect the printer!")));
	auto status_color_orig = status->GetForegroundColour();
	status->SetForegroundColour(GUI::get_label_clr_modified());
	btn_flash->Disable();

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

	auto res = AvrDude::main(std::move(args), avrdude_config, std::move(message_fn));

	BOOST_LOG_TRIVIAL(info) << "avrdude exit code: " << res;

	btn_flash->Enable();

	status->SetForegroundColour(status_color_orig);
	status->SetLabel(
		res == 0 ? _(L("Flashing succeeded!")) : _(L("Flashing failed. Please see the avrdude log below."))
	);
}


// Public

FirmwareDialog::FirmwareDialog(wxWindow *parent) :
	wxDialog(parent, wxID_ANY, _(L("Firmware flasher"))),
	// p(new priv(this))
	p(new priv())
{
	enum {
		DIALOG_MARGIN = 15,
		SPACING = 10,
	};

	wxFont bold_font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
	bold_font.MakeBold();
	wxFont mono_font(wxFontInfo().Family(wxFONTFAMILY_TELETYPE));
	mono_font.MakeSmaller();

	auto *panel = new wxPanel(this);
	wxBoxSizer *vsizer = new wxBoxSizer(wxVERTICAL);
	panel->SetSizer(vsizer);

	auto *txt_port_picker = new wxStaticText(panel, wxID_ANY, _(L("Serial port:")));
	p->port_picker = new wxComboBox(panel, wxID_ANY);
	auto *btn_rescan = new wxButton(panel, wxID_ANY, _(L("Rescan")));
	auto *port_sizer = new wxBoxSizer(wxHORIZONTAL);
	port_sizer->Add(p->port_picker, 1, wxEXPAND | wxRIGHT, SPACING);
	port_sizer->Add(btn_rescan, 0);

	auto *txt_hex_picker = new wxStaticText(panel, wxID_ANY, _(L("Firmware image:")));
	p->hex_picker = new wxFilePickerCtrl(panel, wxID_ANY);

	auto *txt_status = new wxStaticText(panel, wxID_ANY, _(L("Status:")));
	p->status = new wxStaticText(panel, wxID_ANY, _(L("Ready")));
	p->status->SetFont(bold_font);

	auto *sizer_pickers = new wxFlexGridSizer(2, SPACING, SPACING);
	sizer_pickers->AddGrowableCol(1);
	sizer_pickers->Add(txt_port_picker, 0, wxALIGN_CENTER_VERTICAL);
	sizer_pickers->Add(port_sizer, 0, wxEXPAND);
	sizer_pickers->Add(txt_hex_picker, 0, wxALIGN_CENTER_VERTICAL);
	sizer_pickers->Add(p->hex_picker, 0, wxEXPAND);
	sizer_pickers->Add(txt_status, 0, wxALIGN_CENTER_VERTICAL);
	sizer_pickers->Add(p->status, 0, wxEXPAND);
	vsizer->Add(sizer_pickers, 0, wxEXPAND | wxBOTTOM, SPACING);

	p->txt_stdout = new wxTextCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
	p->txt_stdout->SetFont(mono_font);
	vsizer->Add(p->txt_stdout, 1, wxEXPAND | wxBOTTOM, SPACING);

	p->btn_flash = new wxButton(panel, wxID_ANY, _(L("Flash!")));
	auto *bsizer = new wxBoxSizer(wxHORIZONTAL);
	bsizer->AddStretchSpacer();
	bsizer->Add(p->btn_flash);
	vsizer->Add(bsizer, 0, wxEXPAND);

	auto *topsizer = new wxBoxSizer(wxVERTICAL);
	topsizer->Add(panel, 1, wxEXPAND | wxALL, DIALOG_MARGIN);
	SetSizerAndFit(topsizer);
	SetMinSize(wxSize(400, 400));
	SetSize(wxSize(800, 800));

	p->find_serial_ports();

	p->btn_flash->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { this->p->perform_upload(); });
	btn_rescan->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { this->p->find_serial_ports(); });

	// Bind(EVT_BONJOUR_REPLY, &FirmwareDialog::on_reply, this);

	// Bind(EVT_BONJOUR_COMPLETE, [this](wxCommandEvent &) {
	// 	this->timer_state = 0;
	// });

	// Bind(wxEVT_TIMER, &FirmwareDialog::on_timer, this);
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
