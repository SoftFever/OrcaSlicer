#include <numeric>
#include <algorithm>
#include <thread>
#include <stdexcept>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/log/trivial.hpp>

#include "libslic3r/Utils.hpp"
#include "avrdude/avrdude-slic3r.hpp"
#include "GUI.hpp"
#include "MsgDialog.hpp"
#include "../Utils/HexFile.hpp"
#include "../Utils/Serial.hpp"

// wx includes need to come after asio because of the WinSock.h problem
#include "FirmwareDialog.hpp"

#include <wx/app.h>
#include <wx/event.h>
#include <wx/sizer.h>
#include <wx/settings.h>
#include <wx/timer.h>
#include <wx/panel.h>
#include <wx/button.h>
#include <wx/filepicker.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <wx/combobox.h>
#include <wx/gauge.h>
#include <wx/collpane.h>
#include <wx/msgdlg.h>


namespace fs = boost::filesystem;
namespace asio = boost::asio;
using boost::system::error_code;


namespace Slic3r {

using Utils::HexFile;
using Utils::SerialPortInfo;
using Utils::Serial;


// This enum discriminates the kind of information in EVT_AVRDUDE,
// it's stored in the ExtraLong field of wxCommandEvent.
enum AvrdudeEvent
{
	AE_MESSAGE,
	AE_PROGRESS,
	AE_EXIT,
	AE_ERROR,
};

wxDECLARE_EVENT(EVT_AVRDUDE, wxCommandEvent);
wxDEFINE_EVENT(EVT_AVRDUDE, wxCommandEvent);


// Private

struct FirmwareDialog::priv
{
	enum AvrDudeComplete
	{
		AC_SUCCESS,
		AC_FAILURE,
		AC_CANCEL,
	};

	FirmwareDialog *q;      // PIMPL back pointer ("Q-Pointer")

	wxComboBox *port_picker;
	std::vector<SerialPortInfo> ports;
	wxFilePickerCtrl *hex_picker;
	wxStaticText *txt_status;
	wxGauge *progressbar;
	wxCollapsiblePane *spoiler;
	wxTextCtrl *txt_stdout;
	wxButton *btn_rescan;
	wxButton *btn_close;
	wxButton *btn_flash;
	wxString btn_flash_label_ready;
	wxString btn_flash_label_flashing;

	wxTimer timer_pulse;

	// This is a shared pointer holding the background AvrDude task
	// also serves as a status indication (it is set _iff_ the background task is running, otherwise it is reset).
	AvrDude::Ptr avrdude;
	std::string avrdude_config;
	unsigned progress_tasks_done;
	unsigned progress_tasks_bar;
	bool cancelled;
	const bool extra_verbose;   // For debugging

	priv(FirmwareDialog *q) :
		q(q),
		btn_flash_label_ready(_(L("Flash!"))),
		btn_flash_label_flashing(_(L("Cancel"))),
		timer_pulse(q),
		avrdude_config((fs::path(::Slic3r::resources_dir()) / "avrdude" / "avrdude.conf").string()),
		progress_tasks_done(0),
		progress_tasks_bar(0),
		cancelled(false),
		extra_verbose(false)
	{}

	void find_serial_ports();
	void flashing_start(unsigned tasks);
	void flashing_done(AvrDudeComplete complete);
	void check_model_id(const HexFile &metadata, const SerialPortInfo &port);

	void prepare_common(AvrDude &, const SerialPortInfo &port);
	void prepare_mk2(AvrDude &, const SerialPortInfo &port);
	void prepare_mk3(AvrDude &, const SerialPortInfo &port);
	void prepare_mm_control(AvrDude &, const SerialPortInfo &port);
	void perform_upload();

	void cancel();
	void on_avrdude(const wxCommandEvent &evt);
	void ensure_joined();
};

void FirmwareDialog::priv::find_serial_ports()
{
	auto new_ports = Utils::scan_serial_ports_extended();
	if (new_ports != this->ports) {
		this->ports = new_ports;
		port_picker->Clear();
		for (const auto &port : this->ports)
			port_picker->Append(port.friendly_name);
		if (ports.size() > 0) {
			int idx = port_picker->GetValue().IsEmpty() ? 0 : -1;
			for (int i = 0; i < (int)this->ports.size(); ++ i)
				if (this->ports[i].is_printer) {
					idx = i;
					break;
				}
			if (idx != -1)
				port_picker->SetSelection(idx);
		}
	}
}

void FirmwareDialog::priv::flashing_start(unsigned tasks)
{
	txt_stdout->Clear();
	txt_status->SetLabel(_(L("Flashing in progress. Please do not disconnect the printer!")));
	txt_status->SetForegroundColour(GUI::get_label_clr_modified());
	port_picker->Disable();
	btn_rescan->Disable();
	hex_picker->Disable();
	btn_close->Disable();
	btn_flash->SetLabel(btn_flash_label_flashing);
	progressbar->SetRange(200 * tasks);   // See progress callback below
	progressbar->SetValue(0);
	progress_tasks_done = 0;
	progress_tasks_bar = 0;
	cancelled = false;
	timer_pulse.Start(50);
}

void FirmwareDialog::priv::flashing_done(AvrDudeComplete complete)
{
	auto text_color = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
	port_picker->Enable();
	btn_rescan->Enable();
	hex_picker->Enable();
	btn_close->Enable();
	btn_flash->SetLabel(btn_flash_label_ready);
	txt_status->SetForegroundColour(text_color);
	timer_pulse.Stop();
	progressbar->SetValue(progressbar->GetRange());

	switch (complete) {
	case AC_SUCCESS: txt_status->SetLabel(_(L("Flashing succeeded!"))); break;
	case AC_FAILURE: txt_status->SetLabel(_(L("Flashing failed. Please see the avrdude log below."))); break;
	case AC_CANCEL: txt_status->SetLabel(_(L("Flashing cancelled."))); break;
	}
}

void FirmwareDialog::priv::check_model_id(const HexFile &metadata, const SerialPortInfo &port)
{
	if (metadata.model_id.empty()) {
		// No data to check against
		return;
	}

	asio::io_service io;
	Serial serial(io, port.port, 115200);
	serial.printer_setup();

	enum {
		TIMEOUT = 1000,
		RETREIES = 3,
	};

	if (! serial.printer_ready_wait(RETREIES, TIMEOUT)) {
		throw wxString::Format(_(L("Could not connect to the printer at %s")), port.port);
	}

	std::string line;
	error_code ec;
	serial.printer_write_line("PRUSA Rev");
	while (serial.read_line(TIMEOUT, line, ec)) {
		if (ec) { throw wxString::Format(_(L("Could not connect to the printer at %s")), port.port); }
		if (line == "ok") { continue; }

		if (line == metadata.model_id) {
			return;
		} else {
			throw wxString::Format(_(L(
				"The firmware hex file does not match the printer model.\n"
				"The hex file is intended for:\n  %s\n"
				"Printer reports:\n  %s"
			)), metadata.model_id, line);
		}

		line.clear();
	}
}

void FirmwareDialog::priv::prepare_common(AvrDude &avrdude, const SerialPortInfo &port)
{
	auto filename = hex_picker->GetPath();

	std::vector<std::string> args {{
		extra_verbose ? "-vvvvv" : "-v",
		"-p", "atmega2560",
		// Using the "Wiring" mode to program Rambo or Einsy, using the STK500v2 protocol (not the STK500).
		// The Prusa's avrdude is patched to never send semicolons inside the data packets, as the USB to serial chip
		// is flashed with a buggy firmware.
		"-c", "wiring",
		"-P", port.port,
		"-b", "115200",   // TODO: Allow other rates? Ditto below.
		"-D",
		"-U", (boost::format("flash:w:0:%1%:i") % filename.utf8_str().data()).str(),
	}};

	BOOST_LOG_TRIVIAL(info) << "Invoking avrdude, arguments: "
		<< std::accumulate(std::next(args.begin()), args.end(), args[0], [](std::string a, const std::string &b) {
			return a + ' ' + b;
		});

	avrdude.push_args(std::move(args));
}

void FirmwareDialog::priv::prepare_mk2(AvrDude &avrdude, const SerialPortInfo &port)
{
	flashing_start(1);
	prepare_common(avrdude, port);
}

void FirmwareDialog::priv::prepare_mk3(AvrDude &avrdude, const SerialPortInfo &port)
{
	flashing_start(2);
	prepare_common(avrdude, port);

	auto filename = hex_picker->GetPath();

	// The hex file also contains another section with l10n data to be flashed into the external flash on MK3 (Einsy)
	// This is done via another avrdude invocation, here we build arg list for that:
	std::vector<std::string> args_l10n {{
		extra_verbose ? "-vvvvv" : "-v",
		"-p", "atmega2560",
		// Using the "Arduino" mode to program Einsy's external flash with languages, using the STK500 protocol (not the STK500v2).
		// The Prusa's avrdude is patched again to never send semicolons inside the data packets.
		"-c", "arduino",
		"-P", port.port,
		"-b", "115200",
		"-D",
		"-u", // disable safe mode
		"-U", (boost::format("flash:w:1:%1%:i") % filename.utf8_str().data()).str(),
	}};

	BOOST_LOG_TRIVIAL(info) << "Invoking avrdude for external flash flashing, arguments: "
		<< std::accumulate(std::next(args_l10n.begin()), args_l10n.end(), args_l10n[0], [](std::string a, const std::string &b) {
			return a + ' ' + b;
		});

	avrdude.push_args(std::move(args_l10n));
}

void FirmwareDialog::priv::prepare_mm_control(AvrDude &avrdude, const SerialPortInfo &port_in)
{
	// Check if the port has the PID/VID of 0x2c99/3
	// If not, check if it is the MMU (0x2c99/4) and reboot the by opening @ 1200 bauds
	BOOST_LOG_TRIVIAL(info) << "Flashing MMU 2.0, looking for VID/PID 0x2c99/3 or 0x2c99/4 ...";
	SerialPortInfo port = port_in;
	if (! port.id_match(0x2c99, 3)) {
		if (! port.id_match(0x2c99, 4)) {
			// This is not a Prusa MMU 2.0 device
			BOOST_LOG_TRIVIAL(error) << boost::format("Not a Prusa MMU 2.0 device: `%1%`") % port.port;
			throw wxString::Format(_(L("The device at `%s` is not am Original Prusa i3 MMU 2.0 device")), port.port);
		}

		BOOST_LOG_TRIVIAL(info) << boost::format("Found VID/PID 0x2c99/4 at `%1%`, rebooting the device ...") % port.port;

		{
			asio::io_service io;
			Serial serial(io, port.port, 1200);
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}

		// Wait for the bootloader to show up
		std::this_thread::sleep_for(std::chrono::milliseconds(2000));

		// Look for the rebooted device
		BOOST_LOG_TRIVIAL(info) << "... looking for VID/PID 0x2c99/3 ...";
		auto new_ports = Utils::scan_serial_ports_extended();
		unsigned hits = 0;
		for (auto &&new_port : new_ports) {
			if (new_port.id_match(0x2c99, 3)) {
				hits++;
				port = std::move(new_port);
			}
		}

		if (hits == 0) {
			BOOST_LOG_TRIVIAL(error) << "No VID/PID 0x2c99/3 device found after rebooting the MMU 2.0";
			throw wxString::Format(_(L("Failed to reboot the device at `%s` for programming")), port.port);
		} else if (hits > 1) {
			// We found multiple 0x2c99/3 devices, this is bad, because there's no way to find out
			// which one is the one user wants to flash.
			BOOST_LOG_TRIVIAL(error) << "Several VID/PID 0x2c99/3 devices found after rebooting the MMU 2.0";
			throw wxString::Format(_(L("Multiple Original Prusa i3 MMU 2.0 devices found. Please only connect one at a time for flashing.")), port.port);
		}
	}

	BOOST_LOG_TRIVIAL(info) << boost::format("Found VID/PID 0x2c99/3 at `%1%`, flashing ...") % port.port;

	auto filename = hex_picker->GetPath();

	std::vector<std::string> args {{
		extra_verbose ? "-vvvvv" : "-v",
		"-p", "atmega32u4",
		"-c", "avr109",
		"-P", port.port,
		"-b", "57600",
		"-D",
		"-U", (boost::format("flash:w:0:%1%:i") % filename.utf8_str().data()).str(),
	}};

	BOOST_LOG_TRIVIAL(info) << "Invoking avrdude, arguments: "
		<< std::accumulate(std::next(args.begin()), args.end(), args[0], [](std::string a, const std::string &b) {
			return a + ' ' + b;
		});

	avrdude.push_args(std::move(args));
}


void FirmwareDialog::priv::perform_upload()
{
	auto filename = hex_picker->GetPath();
	if (filename.IsEmpty()) { return; }

	int selection = port_picker->GetSelection();
	if (selection == wxNOT_FOUND) { return; }

	std::string port_selected = port_picker->GetValue().ToStdString();
	const SerialPortInfo &port = this->ports[selection];
	// Verify whether the combo box list selection equals to the combo box edit value.
	if (this->ports[selection].friendly_name != port_selected) { return; }

	const bool extra_verbose = false;   // For debugging
	HexFile metadata(filename.wx_str());
	// const auto filename_utf8 = filename.utf8_str();

	flashing_start(metadata.device == HexFile::DEV_MK3 ? 2 : 1);

	// Init the avrdude object
	AvrDude avrdude(avrdude_config);

	// It is ok here to use the q-pointer to the FirmwareDialog
	// because the dialog ensures it doesn't exit before the background thread is done.
	auto q = this->q;

	this->avrdude = avrdude
		.on_run([this, metadata, port](AvrDude &avrdude) {
			auto queue_error = [&](wxString message) {
				avrdude.cancel();

				auto evt = new wxCommandEvent(EVT_AVRDUDE, this->q->GetId());
				evt->SetExtraLong(AE_ERROR);
				evt->SetString(std::move(message));
				wxQueueEvent(this->q, evt);
			};

			try {
				switch (metadata.device) {
				case HexFile::DEV_MK3:
					this->check_model_id(metadata, port);
					this->prepare_mk3(avrdude, port);
					break;

				case HexFile::DEV_MM_CONTROL:
					this->check_model_id(metadata, port);
					this->prepare_mm_control(avrdude, port);
					break;

				default:
					this->prepare_mk2(avrdude, port);
					break;
				}
			} catch (const wxString &message) {
				queue_error(message);
			} catch (const std::exception &ex) {
				queue_error(wxString::Format(_(L("Error accessing port at %s: %s")), port.port, ex.what()));
			}
		})
		.on_message(std::move([q, extra_verbose](const char *msg, unsigned /* size */) {
			if (extra_verbose) {
				BOOST_LOG_TRIVIAL(debug) << "avrdude: " << msg;
			}

			auto evt = new wxCommandEvent(EVT_AVRDUDE, q->GetId());
			auto wxmsg = wxString::FromUTF8(msg);
			evt->SetExtraLong(AE_MESSAGE);
			evt->SetString(std::move(wxmsg));
			wxQueueEvent(q, evt);
		}))
		.on_progress(std::move([q](const char * /* task */, unsigned progress) {
			auto evt = new wxCommandEvent(EVT_AVRDUDE, q->GetId());
			evt->SetExtraLong(AE_PROGRESS);
			evt->SetInt(progress);
			wxQueueEvent(q, evt);
		}))
		.on_complete(std::move([q](int status, size_t /* args_id */) {
			auto evt = new wxCommandEvent(EVT_AVRDUDE, q->GetId());
			evt->SetExtraLong(AE_EXIT);
			evt->SetInt(status);
			wxQueueEvent(q, evt);
		}))
		.run();
}

void FirmwareDialog::priv::cancel()
{
	if (avrdude) {
		cancelled = true;
		txt_status->SetLabel(_(L("Cancelling...")));
		avrdude->cancel();
	}
}

void FirmwareDialog::priv::on_avrdude(const wxCommandEvent &evt)
{
	AvrDudeComplete complete_kind;

	switch (evt.GetExtraLong()) {
	case AE_MESSAGE:
		txt_stdout->AppendText(evt.GetString());
		break;

	case AE_PROGRESS:
		// We try to track overall progress here.
		// Avrdude performs 3 tasks per one memory operation ("-U" arg),
		// first of which is reading of status data (very short).
		// We use the timer_pulse during the very first task to indicate intialization
		// and then display overall progress during the latter tasks.

		if (progress_tasks_done > 0) {
			progressbar->SetValue(progress_tasks_bar + evt.GetInt());
		}

		if (evt.GetInt() == 100) {
			timer_pulse.Stop();
			if (progress_tasks_done % 3 != 0) {
				progress_tasks_bar += 100;
			}
			progress_tasks_done++;
		}

		break;

	case AE_EXIT:
		BOOST_LOG_TRIVIAL(info) << "avrdude exit code: " << evt.GetInt();

		complete_kind = cancelled ? AC_CANCEL : (evt.GetInt() == 0 ? AC_SUCCESS : AC_FAILURE);
		flashing_done(complete_kind);
		ensure_joined();
		break;

	case AE_ERROR:
		txt_stdout->AppendText(evt.GetString());
		flashing_done(AC_FAILURE);
		ensure_joined();
		{
			GUI::ErrorDialog dlg(this->q, evt.GetString());
			dlg.ShowModal();
		}
		break;

	default:
		break;
	}
}

void FirmwareDialog::priv::ensure_joined()
{
	// Make sure the background thread is collected and the AvrDude object reset
	if (avrdude) { avrdude->join(); }
	avrdude.reset();
}


// Public

FirmwareDialog::FirmwareDialog(wxWindow *parent) :
	wxDialog(parent, wxID_ANY, _(L("Firmware flasher")), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	p(new priv(this))
{
	enum {
		DIALOG_MARGIN = 15,
		SPACING = 10,
		MIN_WIDTH = 600,
		MIN_HEIGHT = 200,
		MIN_HEIGHT_EXPANDED = 500,
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
	p->btn_rescan = new wxButton(panel, wxID_ANY, _(L("Rescan")));
	auto *port_sizer = new wxBoxSizer(wxHORIZONTAL);
	port_sizer->Add(p->port_picker, 1, wxEXPAND | wxRIGHT, SPACING);
	port_sizer->Add(p->btn_rescan, 0);

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

	p->spoiler = new wxCollapsiblePane(panel, wxID_ANY, _(L("Advanced: avrdude output log")));
	auto *spoiler_pane = p->spoiler->GetPane();
	auto *spoiler_sizer = new wxBoxSizer(wxVERTICAL);
	p->txt_stdout = new wxTextCtrl(spoiler_pane, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
	p->txt_stdout->SetFont(mono_font);
	spoiler_sizer->Add(p->txt_stdout, 1, wxEXPAND);
	spoiler_pane->SetSizer(spoiler_sizer);
	// The doc says proportion need to be 0 for wxCollapsiblePane.
	// Experience says it needs to be 1, otherwise things won't get sized properly.
	vsizer->Add(p->spoiler, 1, wxEXPAND | wxBOTTOM, SPACING);

	p->btn_close = new wxButton(panel, wxID_CLOSE);
	p->btn_flash = new wxButton(panel, wxID_ANY, p->btn_flash_label_ready);
	auto *bsizer = new wxBoxSizer(wxHORIZONTAL);
	bsizer->Add(p->btn_close);
	bsizer->AddStretchSpacer();
	bsizer->Add(p->btn_flash);
	vsizer->Add(bsizer, 0, wxEXPAND);

	auto *topsizer = new wxBoxSizer(wxVERTICAL);
	topsizer->Add(panel, 1, wxEXPAND | wxALL, DIALOG_MARGIN);
	SetMinSize(wxSize(MIN_WIDTH, MIN_HEIGHT));
	SetSizerAndFit(topsizer);
	const auto size = GetSize();
	SetSize(std::max(size.GetWidth(), static_cast<int>(MIN_WIDTH)), std::max(size.GetHeight(), static_cast<int>(MIN_HEIGHT)));
	Layout();

	p->spoiler->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED, [this](wxCollapsiblePaneEvent &evt) {
		// Dialog size gets screwed up by wxCollapsiblePane, we need to fix it here
		if (evt.GetCollapsed()) {
			this->SetMinSize(wxSize(MIN_WIDTH, MIN_HEIGHT));
		} else {
			this->SetMinSize(wxSize(MIN_WIDTH, MIN_HEIGHT_EXPANDED));
		}

		this->Fit();
		this->Layout();
	});

	p->btn_close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { this->Close(); });
	p->btn_rescan->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { this->p->find_serial_ports(); });

	p->btn_flash->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
		if (this->p->avrdude) {
			// Flashing is in progress, ask the user if they're really sure about canceling it
			wxMessageDialog dlg(this,
				_(L("Are you sure you want to cancel firmware flashing?\nThis could leave your printer in an unusable state!")),
				_(L("Confirmation")),
				wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
			if (dlg.ShowModal() == wxID_YES) {
				this->p->cancel();
			}
		} else {
			// Start a flashing task
			this->p->perform_upload();
		}
	});

	Bind(wxEVT_TIMER, [this](wxTimerEvent &evt) { this->p->progressbar->Pulse(); });

	Bind(EVT_AVRDUDE, [this](wxCommandEvent &evt) { this->p->on_avrdude(evt); });

	Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &evt) {
		if (this->p->avrdude) {
			evt.Veto();
		} else {
			evt.Skip();
		}
	});

	p->find_serial_ports();
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
