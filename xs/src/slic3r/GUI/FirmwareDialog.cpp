#include <numeric>
#include <algorithm>
#include <thread>
#include <condition_variable>
#include <stdexcept>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/log/trivial.hpp>
#include <boost/optional.hpp>

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
#include <wx/filefn.h>


namespace fs = boost::filesystem;
namespace asio = boost::asio;
using boost::system::error_code;
using boost::optional;


namespace Slic3r {

using Utils::HexFile;
using Utils::SerialPortInfo;
using Utils::Serial;


// USB IDs used to perform device lookup
enum {
	USB_VID_PRUSA    = 0x2c99,
	USB_PID_MK2      = 1,
	USB_PID_MK3      = 2,
	USB_PID_MMU_BOOT = 3,
	USB_PID_MMU_APP  = 4,
};

// This enum discriminates the kind of information in EVT_AVRDUDE,
// it's stored in the ExtraLong field of wxCommandEvent.
enum AvrdudeEvent
{
	AE_MESSAGE,
	AE_PROGRESS,
	AE_STATUS,
	AE_EXIT,
};

wxDECLARE_EVENT(EVT_AVRDUDE, wxCommandEvent);
wxDEFINE_EVENT(EVT_AVRDUDE, wxCommandEvent);

wxDECLARE_EVENT(EVT_ASYNC_DIALOG, wxCommandEvent);
wxDEFINE_EVENT(EVT_ASYNC_DIALOG, wxCommandEvent);


// Private

struct FirmwareDialog::priv
{
	enum AvrDudeComplete
	{
		AC_NONE,
		AC_SUCCESS,
		AC_FAILURE,
		AC_USER_CANCELLED,
	};

	FirmwareDialog *q;      // PIMPL back pointer ("Q-Pointer")

	// GUI elements
	wxComboBox *port_picker;
	wxStaticText *port_autodetect;
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
	wxString label_status_flashing;

	wxTimer timer_pulse;

	// Async modal dialog during flashing
	std::mutex mutex;
	int modal_response;
	std::condition_variable response_cv;

	// Data
	std::vector<SerialPortInfo> ports;
	optional<SerialPortInfo> port;
	HexFile hex_file;

	// This is a shared pointer holding the background AvrDude task
	// also serves as a status indication (it is set _iff_ the background task is running, otherwise it is reset).
	AvrDude::Ptr avrdude;
	std::string avrdude_config;
	unsigned progress_tasks_done;
	unsigned progress_tasks_bar;
	bool user_cancelled;
	const bool extra_verbose;   // For debugging

	priv(FirmwareDialog *q) :
		q(q),
		btn_flash_label_ready(_(L("Flash!"))),
		btn_flash_label_flashing(_(L("Cancel"))),
		label_status_flashing(_(L("Flashing in progress. Please do not disconnect the printer!"))),
		timer_pulse(q),
		avrdude_config((fs::path(::Slic3r::resources_dir()) / "avrdude" / "avrdude.conf").string()),
		progress_tasks_done(0),
		progress_tasks_bar(0),
		user_cancelled(false),
		extra_verbose(false)
	{}

	void find_serial_ports();
	void fit_no_shrink();
	void set_txt_status(const wxString &label);
	void flashing_start(unsigned tasks);
	void flashing_done(AvrDudeComplete complete);
	void enable_port_picker(bool enable);
	void load_hex_file(const wxString &path);
	void queue_status(wxString message);
	void queue_error(const wxString &message);

	bool ask_model_id_mismatch(const std::string &printer_model);
	bool check_model_id();
	void wait_for_mmu_bootloader(unsigned retries);
	void mmu_reboot(const SerialPortInfo &port);
	void lookup_port_mmu();
	void prepare_common();
	void prepare_mk2();
	void prepare_mk3();
	void prepare_mm_control();
	void perform_upload();

	void user_cancel();
	void on_avrdude(const wxCommandEvent &evt);
	void on_async_dialog(const wxCommandEvent &evt);
	void ensure_joined();
};

void FirmwareDialog::priv::find_serial_ports()
{
	auto new_ports = Utils::scan_serial_ports_extended();
	if (new_ports != this->ports) {
		this->ports = new_ports;
		port_picker->Clear();
		for (const auto &port : this->ports)
			port_picker->Append(wxString::FromUTF8(port.friendly_name.data()));
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

void FirmwareDialog::priv::fit_no_shrink()
{
	// Ensure content fits into window and window is not shrinked
	const auto old_size = q->GetSize();
	q->Layout();
	q->Fit();
	const auto new_size = q->GetSize();
	const auto new_width = std::max(old_size.GetWidth(), new_size.GetWidth());
	const auto new_height = std::max(old_size.GetHeight(), new_size.GetHeight());
	q->SetSize(new_width, new_height);
}

void FirmwareDialog::priv::set_txt_status(const wxString &label)
{
	const auto width = txt_status->GetSize().GetWidth();
	txt_status->SetLabel(label);
	txt_status->Wrap(width);

	fit_no_shrink();
}

void FirmwareDialog::priv::flashing_start(unsigned tasks)
{
	modal_response = wxID_NONE;
	txt_stdout->Clear();
	set_txt_status(label_status_flashing);
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
	user_cancelled = false;
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
	case AC_SUCCESS: set_txt_status(_(L("Flashing succeeded!"))); break;
	case AC_FAILURE: set_txt_status(_(L("Flashing failed. Please see the avrdude log below."))); break;
	case AC_USER_CANCELLED: set_txt_status(_(L("Flashing cancelled."))); break;
	default: break;
	}
}

void FirmwareDialog::priv::enable_port_picker(bool enable)
{
	port_picker->Show(enable);
	btn_rescan->Show(enable);
	port_autodetect->Show(! enable);
	q->Layout();
	fit_no_shrink();
}

void FirmwareDialog::priv::load_hex_file(const wxString &path)
{
	hex_file = HexFile(path.wx_str());
	enable_port_picker(hex_file.device != HexFile::DEV_MM_CONTROL);
}

void FirmwareDialog::priv::queue_status(wxString message)
{
	auto evt = new wxCommandEvent(EVT_AVRDUDE, this->q->GetId());
	evt->SetExtraLong(AE_STATUS);
	evt->SetString(std::move(message));
	wxQueueEvent(this->q, evt);
}

void FirmwareDialog::priv::queue_error(const wxString &message)
{
	auto evt = new wxCommandEvent(EVT_AVRDUDE, this->q->GetId());
	evt->SetExtraLong(AE_STATUS);
	evt->SetString(wxString::Format(_(L("Flashing failed: %s")), message));

	wxQueueEvent(this->q, evt);	avrdude->cancel();
}

bool FirmwareDialog::priv::ask_model_id_mismatch(const std::string &printer_model)
{
	// model_id in the hex file doesn't match what the printer repoted.
	// Ask the user if it should be flashed anyway.

	std::unique_lock<std::mutex> lock(mutex);

	auto evt = new wxCommandEvent(EVT_ASYNC_DIALOG, this->q->GetId());
	evt->SetString(wxString::Format(_(L(
		"This firmware hex file does not match the printer model.\n"
		"The hex file is intended for: %s\n"
		"Printer reported: %s\n\n"
		"Do you want to continue and flash this hex file anyway?\n"
		"Please only continue if you are sure this is the right thing to do.")),
		hex_file.model_id, printer_model
	));
	wxQueueEvent(this->q, evt);

	response_cv.wait(lock, [this]() { return this->modal_response != wxID_NONE; });

	if (modal_response == wxID_YES) { 
		return true;
	} else {
		user_cancel();
		return false;
	}
}

bool FirmwareDialog::priv::check_model_id()
{
	// XXX: The implementation in Serial doesn't currently work reliably enough to be used.
	// Therefore, regretably, so far the check cannot be used and we just return true here.
	// TODO: Rewrite Serial using more platform-native code.
	return true;
	
	// if (hex_file.model_id.empty()) {
	// 	// No data to check against, assume it's ok
	// 	return true;
	// }

	// asio::io_service io;
	// Serial serial(io, port->port, 115200);
	// serial.printer_setup();

	// enum {
	// 	TIMEOUT = 2000,
	// 	RETREIES = 5,
	// };

	// if (! serial.printer_ready_wait(RETREIES, TIMEOUT)) {
	// 	queue_error(wxString::Format(_(L("Could not connect to the printer at %s")), port->port));
	// 	return false;
	// }

	// std::string line;
	// error_code ec;
	// serial.printer_write_line("PRUSA Rev");
	// while (serial.read_line(TIMEOUT, line, ec)) {
	// 	if (ec) {
	// 		queue_error(wxString::Format(_(L("Could not connect to the printer at %s")), port->port));
	// 		return false;
	// 	}

	// 	if (line == "ok") { continue; }

	// 	if (line == hex_file.model_id) {
	// 		return true;
	// 	} else {
	// 		return ask_model_id_mismatch(line);
	// 	}

	// 	line.clear();
	// }

	// return false;
}

void FirmwareDialog::priv::wait_for_mmu_bootloader(unsigned retries)
{
	enum {
		SLEEP_MS = 500,
	};

	for (unsigned i = 0; i < retries && !user_cancelled; i++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_MS));

		auto ports = Utils::scan_serial_ports_extended();
		ports.erase(std::remove_if(ports.begin(), ports.end(), [=](const SerialPortInfo &port ) {
			return port.id_vendor != USB_VID_PRUSA && port.id_product != USB_PID_MMU_BOOT;
		}), ports.end());

		if (ports.size() == 1) {
			port = ports[0];
			return;
		} else if (ports.size() > 1) {
			BOOST_LOG_TRIVIAL(error) << "Several VID/PID 0x2c99/3 devices found";
			queue_error(_(L("Multiple Original Prusa i3 MMU 2.0 devices found. Please only connect one at a time for flashing.")));
			return;
		}
	}
}

void FirmwareDialog::priv::mmu_reboot(const SerialPortInfo &port)
{
	asio::io_service io;
	Serial serial(io, port.port, 1200);
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

void FirmwareDialog::priv::lookup_port_mmu()
{
	BOOST_LOG_TRIVIAL(info) << "Flashing MMU 2.0, looking for VID/PID 0x2c99/3 or 0x2c99/4 ...";

	auto ports = Utils::scan_serial_ports_extended();
	ports.erase(std::remove_if(ports.begin(), ports.end(), [=](const SerialPortInfo &port ) {
		return port.id_vendor != USB_VID_PRUSA &&
			port.id_product != USB_PID_MMU_BOOT &&
			port.id_product != USB_PID_MMU_APP;
	}), ports.end());

	if (ports.size() == 0) {
		BOOST_LOG_TRIVIAL(info) << "MMU 2.0 device not found, asking the user to press Reset and waiting for the device to show up ...";

		queue_status(_(L(
			"The Multi Material Control device was not found.\n"
			"If the device is connected, please press the Reset button next to the USB connector ..."
		)));

		wait_for_mmu_bootloader(30);
	} else if (ports.size() > 1) {
		BOOST_LOG_TRIVIAL(error) << "Several VID/PID 0x2c99/3 devices found";
		queue_error(_(L("Multiple Original Prusa i3 MMU 2.0 devices found. Please only connect one at a time for flashing.")));
	} else {
		if (ports[0].id_product == USB_PID_MMU_APP) {
			// The device needs to be rebooted into the bootloader mode
			BOOST_LOG_TRIVIAL(info) << boost::format("Found VID/PID 0x2c99/4 at `%1%`, rebooting the device ...") % ports[0].port;
			mmu_reboot(ports[0]);
			wait_for_mmu_bootloader(10);
		} else {
			port = ports[0];
		}
	}
}

void FirmwareDialog::priv::prepare_common()
{
	std::vector<std::string> args {{
		extra_verbose ? "-vvvvv" : "-v",
		"-p", "atmega2560",
		// Using the "Wiring" mode to program Rambo or Einsy, using the STK500v2 protocol (not the STK500).
		// The Prusa's avrdude is patched to never send semicolons inside the data packets, as the USB to serial chip
		// is flashed with a buggy firmware.
		"-c", "wiring",
		"-P", port->port,
		"-b", "115200",   // TODO: Allow other rates? Ditto elsewhere.
		"-D",
		"-U", (boost::format("flash:w:0:%1%:i") % hex_file.path.string()).str(),
	}};

	BOOST_LOG_TRIVIAL(info) << "Invoking avrdude, arguments: "
		<< std::accumulate(std::next(args.begin()), args.end(), args[0], [](std::string a, const std::string &b) {
			return a + ' ' + b;
		});

	avrdude->push_args(std::move(args));
}

void FirmwareDialog::priv::prepare_mk2()
{
	if (! port) { return; }

	if (! check_model_id()) {
		avrdude->cancel();
		return;
	}

	prepare_common();
}

void FirmwareDialog::priv::prepare_mk3()
{
	if (! port) { return; }

	if (! check_model_id()) {
		avrdude->cancel();
		return;
	}

	prepare_common();

	// The hex file also contains another section with l10n data to be flashed into the external flash on MK3 (Einsy)
	// This is done via another avrdude invocation, here we build arg list for that:
	std::vector<std::string> args {{
		extra_verbose ? "-vvvvv" : "-v",
		"-p", "atmega2560",
		// Using the "Arduino" mode to program Einsy's external flash with languages, using the STK500 protocol (not the STK500v2).
		// The Prusa's avrdude is patched again to never send semicolons inside the data packets.
		"-c", "arduino",
		"-P", port->port,
		"-b", "115200",
		"-D",
		"-u", // disable safe mode
		"-U", (boost::format("flash:w:1:%1%:i") % hex_file.path.string()).str(),
	}};

	BOOST_LOG_TRIVIAL(info) << "Invoking avrdude for external flash flashing, arguments: "
		<< std::accumulate(std::next(args.begin()), args.end(), args[0], [](std::string a, const std::string &b) {
			return a + ' ' + b;
		});

	avrdude->push_args(std::move(args));
}

void FirmwareDialog::priv::prepare_mm_control()
{
	port = boost::none;
	lookup_port_mmu();
	if (! port) {
		queue_error(_(L("The device could not have been found")));
		return;
	}

	BOOST_LOG_TRIVIAL(info) << boost::format("Found VID/PID 0x2c99/3 at `%1%`, flashing ...") % port->port;
	queue_status(label_status_flashing);

	std::vector<std::string> args {{
		extra_verbose ? "-vvvvv" : "-v",
		"-p", "atmega32u4",
		"-c", "avr109",
		"-P", port->port,
		"-b", "57600",
		"-D",
		"-U", (boost::format("flash:w:0:%1%:i") % hex_file.path.string()).str(),
	}};

	BOOST_LOG_TRIVIAL(info) << "Invoking avrdude, arguments: "
		<< std::accumulate(std::next(args.begin()), args.end(), args[0], [](std::string a, const std::string &b) {
			return a + ' ' + b;
		});

	avrdude->push_args(std::move(args));
}


void FirmwareDialog::priv::perform_upload()
{
	auto filename = hex_picker->GetPath();
	if (filename.IsEmpty()) { return; }

	load_hex_file(filename);  // Might already be loaded, but we want to make sure it's fresh

	int selection = port_picker->GetSelection();
	if (selection != wxNOT_FOUND) {
		port = this->ports[selection];

		// Verify whether the combo box list selection equals to the combo box edit value.
		if (wxString::FromUTF8(port->friendly_name.data()) != port_picker->GetValue()) {
			return;
		}
	}

	const bool extra_verbose = false;   // For debugging

	flashing_start(hex_file.device == HexFile::DEV_MK3 ? 2 : 1);

	// Init the avrdude object
	AvrDude avrdude(avrdude_config);

	// It is ok here to use the q-pointer to the FirmwareDialog
	// because the dialog ensures it doesn't exit before the background thread is done.
	auto q = this->q;

	this->avrdude = avrdude
		.on_run([this]() {
			try {
				switch (this->hex_file.device) {
				case HexFile::DEV_MK3:
					this->prepare_mk3();
					break;

				case HexFile::DEV_MM_CONTROL:
					this->prepare_mm_control();
					break;

				default:
					this->prepare_mk2();
					break;
				}
			} catch (const std::exception &ex) {
				queue_error(wxString::Format(_(L("Error accessing port at %s: %s")), port->port, ex.what()));
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
		.on_complete(std::move([this]() {
			auto evt = new wxCommandEvent(EVT_AVRDUDE, this->q->GetId());
			evt->SetExtraLong(AE_EXIT);
			evt->SetInt(this->avrdude->exit_code());
			wxQueueEvent(this->q, evt);
		}))
		.run();
}

void FirmwareDialog::priv::user_cancel()
{
	if (avrdude) {
		user_cancelled = true;
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

		// Figure out the exit state
		if (user_cancelled) { complete_kind = AC_USER_CANCELLED; }
		else if (avrdude->cancelled()) { complete_kind = AC_NONE; } // Ie. cancelled programatically
		else { complete_kind = evt.GetInt() == 0 ? AC_SUCCESS : AC_FAILURE; }

		flashing_done(complete_kind);
		ensure_joined();
		break;

	case AE_STATUS:
		set_txt_status(evt.GetString());
		break;

	default:
		break;
	}
}

void FirmwareDialog::priv::on_async_dialog(const wxCommandEvent &evt)
{
	wxMessageDialog dlg(this->q, evt.GetString(), wxMessageBoxCaptionStr, wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
	{
		std::lock_guard<std::mutex> lock(mutex);
		modal_response = dlg.ShowModal();
	}
	response_cv.notify_all();
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

	// Create GUI components and layout

	auto *panel = new wxPanel(this);
	wxBoxSizer *vsizer = new wxBoxSizer(wxVERTICAL);
	panel->SetSizer(vsizer);

	auto *label_hex_picker = new wxStaticText(panel, wxID_ANY, _(L("Firmware image:")));
	p->hex_picker = new wxFilePickerCtrl(panel, wxID_ANY);

	auto *label_port_picker = new wxStaticText(panel, wxID_ANY, _(L("Serial port:")));
	p->port_picker = new wxComboBox(panel, wxID_ANY);
	p->port_autodetect = new wxStaticText(panel, wxID_ANY, _(L("Autodetected")));
	p->btn_rescan = new wxButton(panel, wxID_ANY, _(L("Rescan")));
	auto *port_sizer = new wxBoxSizer(wxHORIZONTAL);
	port_sizer->Add(p->port_picker, 1, wxEXPAND | wxRIGHT, SPACING);
	port_sizer->Add(p->btn_rescan, 0);
	port_sizer->Add(p->port_autodetect, 1, wxEXPAND);
	p->enable_port_picker(true);

	auto *label_progress = new wxStaticText(panel, wxID_ANY, _(L("Progress:")));
	p->progressbar = new wxGauge(panel, wxID_ANY, 1, wxDefaultPosition, wxDefaultSize, wxGA_HORIZONTAL | wxGA_SMOOTH);

	auto *label_status = new wxStaticText(panel, wxID_ANY, _(L("Status:")));
	p->txt_status = new wxStaticText(panel, wxID_ANY, _(L("Ready")));
	p->txt_status->SetFont(status_font);

	auto *grid = new wxFlexGridSizer(2, SPACING, SPACING);
	grid->AddGrowableCol(1);

	grid->Add(label_hex_picker, 0, wxALIGN_CENTER_VERTICAL);
	grid->Add(p->hex_picker, 0, wxEXPAND);

	grid->Add(label_port_picker, 0, wxALIGN_CENTER_VERTICAL);
	grid->Add(port_sizer, 0, wxEXPAND);

	grid->Add(label_progress, 0, wxALIGN_CENTER_VERTICAL);
	grid->Add(p->progressbar, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

	grid->Add(label_status, 0, wxALIGN_CENTER_VERTICAL);
	grid->Add(p->txt_status, 0, wxEXPAND);

	vsizer->Add(grid, 0, wxEXPAND | wxTOP | wxBOTTOM, SPACING);

	p->spoiler = new wxCollapsiblePane(panel, wxID_ANY, _(L("Advanced: avrdude output log")), wxDefaultPosition, wxDefaultSize, wxCP_DEFAULT_STYLE | wxCP_NO_TLW_RESIZE);
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
	p->btn_flash->Disable();
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

	// Bind events

	p->hex_picker->Bind(wxEVT_FILEPICKER_CHANGED, [this](wxFileDirPickerEvent& evt) {
		if (wxFileExists(evt.GetPath())) {
			this->p->load_hex_file(evt.GetPath());
			this->p->btn_flash->Enable();
		}
	});

	p->spoiler->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED, [this](wxCollapsiblePaneEvent &evt) {
		if (evt.GetCollapsed()) {
			this->SetMinSize(wxSize(MIN_WIDTH, MIN_HEIGHT));
			const auto new_height = this->GetSize().GetHeight() - this->p->txt_stdout->GetSize().GetHeight();
			this->SetSize(this->GetSize().GetWidth(), new_height);
		} else {
			this->SetMinSize(wxSize(MIN_WIDTH, MIN_HEIGHT_EXPANDED));
		}

		this->Layout();
		this->p->fit_no_shrink();
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
				this->p->set_txt_status(_(L("Cancelling...")));
				this->p->user_cancel();
			}
		} else {
			// Start a flashing task
			this->p->perform_upload();
		}
	});

	Bind(wxEVT_TIMER, [this](wxTimerEvent &evt) { this->p->progressbar->Pulse(); });

	Bind(EVT_AVRDUDE, [this](wxCommandEvent &evt) { this->p->on_avrdude(evt); });
	Bind(EVT_ASYNC_DIALOG, [this](wxCommandEvent &evt) { this->p->on_async_dialog(evt); });

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
