#include "GUI.hpp"
#include "GUI_App.hpp"
#include "../AppController.hpp"
#include "WipeTowerDialog.hpp"

#include <assert.h>

#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

#if __APPLE__
#import <IOKit/pwr_mgt/IOPMLib.h>
#elif _WIN32
#include <Windows.h>
// Undefine min/max macros incompatible with the standard library
// For example, std::numeric_limits<std::streamsize>::max()
// produces some weird errors
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include "boost/nowide/convert.hpp"
#endif

#include <wx/display.h>

#include "wxExtensions.hpp"
#include "GUI_Preview.hpp"
#include "GUI_PreviewIface.hpp"
#include "AboutDialog.hpp"
#include "AppConfig.hpp"
#include "ConfigWizard.hpp"
#include "PresetBundle.hpp"
#include "UpdateDialogs.hpp"

#include "../../libslic3r/Utils.hpp"
#include "../../libslic3r/Print.hpp"

namespace Slic3r { namespace GUI {

#if __APPLE__
IOPMAssertionID assertionID;
#endif

void disable_screensaver()
{
    #if __APPLE__
    CFStringRef reasonForActivity = CFSTR("Slic3r");
    IOReturn success = IOPMAssertionCreateWithName(kIOPMAssertionTypeNoDisplaySleep, 
        kIOPMAssertionLevelOn, reasonForActivity, &assertionID); 
    // ignore result: success == kIOReturnSuccess
    #elif _WIN32
    SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_CONTINUOUS);
    #endif
}

void enable_screensaver()
{
    #if __APPLE__
    IOReturn success = IOPMAssertionRelease(assertionID);
    #elif _WIN32
    SetThreadExecutionState(ES_CONTINUOUS);
    #endif
}

bool debugged()
{
    #ifdef _WIN32
    return IsDebuggerPresent();
	#else
	return false;
    #endif /* _WIN32 */
}

void break_to_debugger()
{
    #ifdef _WIN32
    if (IsDebuggerPresent())
        DebugBreak();
    #endif /* _WIN32 */
}

PreviewIface* g_preview = nullptr; 

bool config_wizard_startup(bool app_config_exists)
{
    if (!app_config_exists || wxGetApp().preset_bundle->printers.size() <= 1) {
		config_wizard(ConfigWizard::RR_DATA_EMPTY);
		return true;
	} else if (get_app_config()->legacy_datadir()) {
		// Looks like user has legacy pre-vendorbundle data directory,
		// explain what this is and run the wizard

		MsgDataLegacy dlg;
		dlg.ShowModal();

		config_wizard(ConfigWizard::RR_DATA_LEGACY);
		return true;
	}
	return false;
}

void config_wizard(int reason)
{
    // Exit wizard if there are unsaved changes and the user cancels the action.
    if (! wxGetApp().check_unsaved_changes())
    	return;

	try {
		ConfigWizard wizard(nullptr, static_cast<ConfigWizard::RunReason>(reason));
        wizard.run(wxGetApp().preset_bundle, wxGetApp().preset_updater);
	}
	catch (const std::exception &e) {
		show_error(nullptr, e.what());
	}

	// Load the currently selected preset into the GUI, update the preset selection box.
	wxGetApp().load_current_presets();
}

PreviewIface* create_preview_iface(wxNotebook* parent, DynamicPrintConfig* config, Print* print, GCodePreviewData* gcode_preview_data)
{
    if (g_preview == nullptr)
    {
        Preview* panel = new Preview(parent, config, print, gcode_preview_data);
        g_preview = new PreviewIface(panel);
    }

    return g_preview;
}

// opt_index = 0, by the reason of zero-index in ConfigOptionVector by default (in case only one element)
void change_opt_value(DynamicPrintConfig& config, const t_config_option_key& opt_key, const boost::any& value, int opt_index /*= 0*/)
{
	try{
		switch (config.def()->get(opt_key)->type) {
		case coFloatOrPercent:{
			std::string str = boost::any_cast<std::string>(value);
			bool percent = false;
			if (str.back() == '%') {
				str.pop_back();
				percent = true;
			}
			double val = stod(str);
			config.set_key_value(opt_key, new ConfigOptionFloatOrPercent(val, percent));
			break;}
		case coPercent:
			config.set_key_value(opt_key, new ConfigOptionPercent(boost::any_cast<double>(value)));
			break;
		case coFloat:{
			double& val = config.opt_float(opt_key);
			val = boost::any_cast<double>(value);
			break;
		}
		case coPercents:{
			ConfigOptionPercents* vec_new = new ConfigOptionPercents{ boost::any_cast<double>(value) };
			config.option<ConfigOptionPercents>(opt_key)->set_at(vec_new, opt_index, opt_index);
			break;
		}
		case coFloats:{
			ConfigOptionFloats* vec_new = new ConfigOptionFloats{ boost::any_cast<double>(value) };
			config.option<ConfigOptionFloats>(opt_key)->set_at(vec_new, opt_index, opt_index);
 			break;
		}			
		case coString:
			config.set_key_value(opt_key, new ConfigOptionString(boost::any_cast<std::string>(value)));
			break;
		case coStrings:{
			if (opt_key.compare("compatible_printers") == 0) {
				config.option<ConfigOptionStrings>(opt_key)->values = 
					boost::any_cast<std::vector<std::string>>(value);
			}
			else if (config.def()->get(opt_key)->gui_flags.compare("serialized") == 0) {
				std::string str = boost::any_cast<std::string>(value);
				if (str.back() == ';') str.pop_back();
				// Split a string to multiple strings by a semi - colon.This is the old way of storing multi - string values.
				// Currently used for the post_process config value only.
				std::vector<std::string> values;
				boost::split(values, str, boost::is_any_of(";"));
				if (values.size() == 1 && values[0] == "") 
					break;
				config.option<ConfigOptionStrings>(opt_key)->values = values;
			}
			else{
				ConfigOptionStrings* vec_new = new ConfigOptionStrings{ boost::any_cast<std::string>(value) };
				config.option<ConfigOptionStrings>(opt_key)->set_at(vec_new, opt_index, 0);
			}
			}
			break;
		case coBool:
			config.set_key_value(opt_key, new ConfigOptionBool(boost::any_cast<bool>(value)));
			break;
		case coBools:{
			ConfigOptionBools* vec_new = new ConfigOptionBools{ (bool)boost::any_cast<unsigned char>(value) };
			config.option<ConfigOptionBools>(opt_key)->set_at(vec_new, opt_index, 0);
			break;}
		case coInt:
			config.set_key_value(opt_key, new ConfigOptionInt(boost::any_cast<int>(value)));
			break;
		case coInts:{
			ConfigOptionInts* vec_new = new ConfigOptionInts{ boost::any_cast<int>(value) };
			config.option<ConfigOptionInts>(opt_key)->set_at(vec_new, opt_index, 0);
			}
			break;
		case coEnum:{
			if (opt_key.compare("external_fill_pattern") == 0 ||
				opt_key.compare("fill_pattern") == 0)
				config.set_key_value(opt_key, new ConfigOptionEnum<InfillPattern>(boost::any_cast<InfillPattern>(value))); 
			else if (opt_key.compare("gcode_flavor") == 0)
				config.set_key_value(opt_key, new ConfigOptionEnum<GCodeFlavor>(boost::any_cast<GCodeFlavor>(value))); 
			else if (opt_key.compare("support_material_pattern") == 0)
				config.set_key_value(opt_key, new ConfigOptionEnum<SupportMaterialPattern>(boost::any_cast<SupportMaterialPattern>(value)));
			else if (opt_key.compare("seam_position") == 0)
				config.set_key_value(opt_key, new ConfigOptionEnum<SeamPosition>(boost::any_cast<SeamPosition>(value)));
			else if (opt_key.compare("host_type") == 0)
				config.set_key_value(opt_key, new ConfigOptionEnum<PrintHostType>(boost::any_cast<PrintHostType>(value)));
			}
			break;
		case coPoints:{
			if (opt_key.compare("bed_shape") == 0) {
				config.option<ConfigOptionPoints>(opt_key)->values = boost::any_cast<std::vector<Vec2d>>(value);
				break;
			}
			ConfigOptionPoints* vec_new = new ConfigOptionPoints{ boost::any_cast<Vec2d>(value) };
			config.option<ConfigOptionPoints>(opt_key)->set_at(vec_new, opt_index, 0);
			}
			break;
		case coNone:
			break;
		default:
			break;
		}
	}
	catch (const std::exception &e)
	{
		int i = 0;//no reason, just experiment
	}
}

void show_error(wxWindow* parent, const wxString& message)
{
	ErrorDialog msg(parent, message);
	msg.ShowModal();
}

void show_error_id(int id, const std::string& message)
{
	auto *parent = id != 0 ? wxWindow::FindWindowById(id) : nullptr;
	show_error(parent, wxString::FromUTF8(message.data()));
}

void show_info(wxWindow* parent, const wxString& message, const wxString& title)
{
	wxMessageDialog msg_wingow(parent, message, title.empty() ? _(L("Notice")) : title, wxOK | wxICON_INFORMATION);
	msg_wingow.ShowModal();
}

void warning_catcher(wxWindow* parent, const wxString& message)
{
	if (message == "GLUquadricObjPtr | " + _(L("Attempt to free unreferenced scalar")) )
		return;
	wxMessageDialog msg(parent, message, _(L("Warning")), wxOK | wxICON_WARNING);
	msg.ShowModal();
}

void create_combochecklist(wxComboCtrl* comboCtrl, std::string text, std::string items, bool initial_value)
{
    if (comboCtrl == nullptr)
        return;

    wxCheckListBoxComboPopup* popup = new wxCheckListBoxComboPopup;
    if (popup != nullptr)
    {
        // FIXME If the following line is removed, the combo box popup list will not react to mouse clicks.
        //  On the other side, with this line the combo box popup cannot be closed by clicking on the combo button on Windows 10.
        comboCtrl->UseAltPopupWindow();

        comboCtrl->EnablePopupAnimation(false);
        comboCtrl->SetPopupControl(popup);
        popup->SetStringValue(from_u8(text));
        popup->Bind(wxEVT_CHECKLISTBOX, [popup](wxCommandEvent& evt) { popup->OnCheckListBox(evt); });
        popup->Bind(wxEVT_LISTBOX, [popup](wxCommandEvent& evt) { popup->OnListBoxSelection(evt); });
        popup->Bind(wxEVT_KEY_DOWN, [popup](wxKeyEvent& evt) { popup->OnKeyEvent(evt); });
        popup->Bind(wxEVT_KEY_UP, [popup](wxKeyEvent& evt) { popup->OnKeyEvent(evt); });

        std::vector<std::string> items_str;
        boost::split(items_str, items, boost::is_any_of("|"), boost::token_compress_off);

        for (const std::string& item : items_str)
        {
            popup->Append(from_u8(item));
        }

        for (unsigned int i = 0; i < popup->GetCount(); ++i)
        {
            popup->Check(i, initial_value);
        }
    }
}

int combochecklist_get_flags(wxComboCtrl* comboCtrl)
{
    int flags = 0;

    wxCheckListBoxComboPopup* popup = wxDynamicCast(comboCtrl->GetPopupControl(), wxCheckListBoxComboPopup);
    if (popup != nullptr)
    {
        for (unsigned int i = 0; i < popup->GetCount(); ++i)
        {
            if (popup->IsChecked(i))
                flags |= 1 << i;
        }
    }

    return flags;
}

AppConfig* get_app_config()
{
    return wxGetApp().app_config;
}

wxString L_str(const std::string &str)
{
	//! Explicitly specify that the source string is already in UTF-8 encoding
	return wxGetTranslation(wxString(str.c_str(), wxConvUTF8));
}

wxString from_u8(const std::string &str)
{
	return wxString::FromUTF8(str.c_str());
}

std::string into_u8(const wxString &str)
{
	auto buffer_utf8 = str.utf8_str();
	return std::string(buffer_utf8.data());
}

bool get_current_screen_size(wxWindow *window, unsigned &width, unsigned &height)
{
	const auto idx = wxDisplay::GetFromWindow(window);
	if (idx == wxNOT_FOUND) {
		return false;
	}

	wxDisplay display(idx);
	const auto disp_size = display.GetClientArea();
	width = disp_size.GetWidth();
	height = disp_size.GetHeight();

	return true;
}

void save_window_size(wxTopLevelWindow *window, const std::string &name)
{
	const wxSize size = window->GetSize();
	const wxPoint pos = window->GetPosition();
	const auto maximized = window->IsMaximized() ? "1" : "0";

	get_app_config()->set((boost::format("window_%1%_size") % name).str(), (boost::format("%1%;%2%") % size.GetWidth() % size.GetHeight()).str());
	get_app_config()->set((boost::format("window_%1%_maximized") % name).str(), maximized);
}

void restore_window_size(wxTopLevelWindow *window, const std::string &name)
{
	// XXX: This still doesn't behave nicely in some situations (mostly on Linux).
	// The problem is that it's hard to obtain window position with respect to screen geometry reliably
	// from wxWidgets. Sometimes wxWidgets claim a window is located on a different screen than on which
	// it's actually visible. I suspect this has something to do with window initialization (maybe we
	// restore window geometry too early), but haven't yet found a workaround.

	const auto display_idx = wxDisplay::GetFromWindow(window);
	if (display_idx == wxNOT_FOUND) { return; }

	const auto display = wxDisplay(display_idx).GetClientArea();
	std::vector<std::string> pair;

	try {
		const auto key_size = (boost::format("window_%1%_size") % name).str();
		if (get_app_config()->has(key_size)) {
			if (unescape_strings_cstyle(get_app_config()->get(key_size), pair) && pair.size() == 2) {
				auto width = boost::lexical_cast<int>(pair[0]);
				auto height = boost::lexical_cast<int>(pair[1]);

				window->SetSize(width, height);
			}
		}
	} catch(const boost::bad_lexical_cast &) {}

	// Maximizing should be the last thing to do.
	// This ensure the size and position are sane when the user un-maximizes the window.
	const auto key_maximized = (boost::format("window_%1%_maximized") % name).str();
	if (get_app_config()->get(key_maximized) == "1") {
		window->Maximize(true);
	}
}

void about()
{
    AboutDialog dlg;
    dlg.ShowModal();
    dlg.Destroy();
}

void desktop_open_datadir_folder()
{
	// Execute command to open a file explorer, platform dependent.
	// FIXME: The const_casts aren't needed in wxWidgets 3.1, remove them when we upgrade.

	const auto path = data_dir();
#ifdef _WIN32
		const auto widepath = wxString::FromUTF8(path.data());
		const wchar_t *argv[] = { L"explorer", widepath.GetData(), nullptr };
		::wxExecute(const_cast<wchar_t**>(argv), wxEXEC_ASYNC, nullptr);
#elif __APPLE__
		const char *argv[] = { "open", path.data(), nullptr };
		::wxExecute(const_cast<char**>(argv), wxEXEC_ASYNC, nullptr);
#else
		const char *argv[] = { "xdg-open", path.data(), nullptr };

		// Check if we're running in an AppImage container, if so, we need to remove AppImage's env vars,
		// because they may mess up the environment expected by the file manager.
		// Mostly this is about LD_LIBRARY_PATH, but we remove a few more too for good measure.
		if (wxGetEnv("APPIMAGE", nullptr)) {
			// We're running from AppImage
			wxEnvVariableHashMap env_vars;
			wxGetEnvMap(&env_vars);

			env_vars.erase("APPIMAGE");
			env_vars.erase("APPDIR");
			env_vars.erase("LD_LIBRARY_PATH");
			env_vars.erase("LD_PRELOAD");
			env_vars.erase("UNION_PRELOAD");

			wxExecuteEnv exec_env;
			exec_env.env = std::move(env_vars);

			wxString owd;
			if (wxGetEnv("OWD", &owd)) {
				// This is the original work directory from which the AppImage image was run,
				// set it as CWD for the child process:
				exec_env.cwd = std::move(owd);
			}

			::wxExecute(const_cast<char**>(argv), wxEXEC_ASYNC, nullptr, &exec_env);
		} else {
			// Looks like we're NOT running from AppImage, we'll make no changes to the environment.
			::wxExecute(const_cast<char**>(argv), wxEXEC_ASYNC, nullptr, nullptr);
		}
#endif
}

namespace {
AppControllerPtr g_appctl;
}

AppControllerPtr get_appctl()
{
    return g_appctl;
}

void set_cli_appctl()
{
    g_appctl = std::make_shared<AppControllerCli>();
}

void set_gui_appctl()
{
    g_appctl = std::make_shared<AppControllerGui>();
}

} }
