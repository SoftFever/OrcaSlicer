#ifndef slic3r_GUI_hpp_
#define slic3r_GUI_hpp_

#include "Config.hpp"
#include "callback.hpp"

#include <wx/intl.h>

class wxWindow;
class wxMenuBar;
class wxNotebook;
class wxComboCtrl;
class wxFileDialog;
class wxTopLevelWindow;

namespace Slic3r { 

class AppConfig;
class DynamicPrintConfig;
class Print;
class GCodePreviewData;
class AppControllerBase;

using AppControllerPtr = std::shared_ptr<AppControllerBase>;

#define _(s)    Slic3r::GUI::I18N::translate((s))

namespace GUI { namespace I18N {
	inline wxString translate(const char *s)    	 { return wxGetTranslation(wxString(s, wxConvUTF8)); }
	inline wxString translate(const wchar_t *s) 	 { return wxGetTranslation(s); }
	inline wxString translate(const std::string &s)  { return wxGetTranslation(wxString(s.c_str(), wxConvUTF8)); }
	inline wxString translate(const std::wstring &s) { return wxGetTranslation(s.c_str()); }
} }

// !!! If you needed to translate some wxString,
// !!! please use _(L(string))
// !!! _() - is a standard wxWidgets macro to translate
// !!! L() is used only for marking localizable string 
// !!! It will be used in "xgettext" to create a Locating Message Catalog.
#define L(s) s

//! macro used to localization, return wxScopedCharBuffer
//! With wxConvUTF8 explicitly specify that the source string is already in UTF-8 encoding
#define _CHB(s) wxGetTranslation(wxString(s, wxConvUTF8)).utf8_str()

// Minimal buffer length for translated string (char buf[MIN_BUF_LENGTH_FOR_L])
#define MIN_BUF_LENGTH_FOR_L	512

namespace GUI {

void disable_screensaver();
void enable_screensaver();
bool debugged();
void break_to_debugger();

AppConfig*		get_app_config();

AppControllerPtr get_appctl();
void             set_cli_appctl();
void             set_gui_appctl();

extern void add_menus(wxMenuBar *menu, int event_preferences_changed, int event_language_change);

// Checks if configuration wizard needs to run, calls config_wizard if so.
// Returns whether the Wizard ran.
extern bool config_wizard_startup(bool app_config_exists);

// Opens the configuration wizard, returns true if wizard is finished & accepted.
// The run_reason argument is actually ConfigWizard::RunReason, but int is used here because of Perl.
extern void config_wizard(int run_reason);

// Change option value in config
void change_opt_value(DynamicPrintConfig& config, const t_config_option_key& opt_key, const boost::any& value, int opt_index = 0);

void show_error(wxWindow* parent, const wxString& message);
void show_error_id(int id, const std::string& message);   // For Perl
void show_info(wxWindow* parent, const wxString& message, const wxString& title);
void warning_catcher(wxWindow* parent, const wxString& message);

// Creates a wxCheckListBoxComboPopup inside the given wxComboCtrl, filled with the given text and items.
// Items are all initialized to the given value.
// Items must be separated by '|', for example "Item1|Item2|Item3", and so on.
void create_combochecklist(wxComboCtrl* comboCtrl, std::string text, std::string items, bool initial_value);

// Returns the current state of the items listed in the wxCheckListBoxComboPopup contained in the given wxComboCtrl,
// encoded inside an int.
int combochecklist_get_flags(wxComboCtrl* comboCtrl);

// Return translated std::string as a wxString
wxString	L_str(const std::string &str);
// Return wxString from std::string in UTF8
wxString	from_u8(const std::string &str);
// Return std::string in UTF8 from wxString
std::string	into_u8(const wxString &str);

// Callback to trigger a configuration update timer on the Plater.
static PerlCallback g_on_request_update_callback;

// Returns the dimensions of the screen on which the main frame is displayed
bool get_current_screen_size(wxWindow *window, unsigned &width, unsigned &height);

// Save window size and maximized status into AppConfig
void save_window_size(wxTopLevelWindow *window, const std::string &name);
// Restore the above
void restore_window_size(wxTopLevelWindow *window, const std::string &name);

// Display an About dialog
extern void about();
// Ask the destop to open the datadir using the default file explorer.
extern void desktop_open_datadir_folder();

} // namespace GUI
} // namespace Slic3r

#endif
