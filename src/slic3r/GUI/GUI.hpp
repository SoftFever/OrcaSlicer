#ifndef slic3r_GUI_hpp_
#define slic3r_GUI_hpp_

namespace boost { class any; }
namespace boost::filesystem { class path; }

#include <wx/string.h>

#include "libslic3r/Config.hpp"

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

namespace GUI {

void disable_screensaver();
void enable_screensaver();
bool debugged();
void break_to_debugger();

// Platform specific Ctrl+/Alt+ (Windows, Linux) vs. ⌘/⌥ (OSX) prefixes 
extern const std::string& shortkey_ctrl_prefix();
extern const std::string& shortkey_alt_prefix();

extern AppConfig* get_app_config();

extern void add_menus(wxMenuBar *menu, int event_preferences_changed, int event_language_change);

// Change option value in config
void change_opt_value(DynamicPrintConfig& config, const t_config_option_key& opt_key, const boost::any& value, int opt_index = 0);

void show_error(wxWindow* parent, const wxString& message);
void show_error(wxWindow* parent, const char* message);
inline void show_error(wxWindow* parent, const std::string& message) { show_error(parent, message.c_str()); }
void show_error_id(int id, const std::string& message);   // For Perl
void show_info(wxWindow* parent, const wxString& message, const wxString& title = wxString());
void show_info(wxWindow* parent, const char* message, const char* title = nullptr);
inline void show_info(wxWindow* parent, const std::string& message,const std::string& title = std::string()) { show_info(parent, message.c_str(), title.c_str()); }
void warning_catcher(wxWindow* parent, const wxString& message);

// Creates a wxCheckListBoxComboPopup inside the given wxComboCtrl, filled with the given text and items.
// Items are all initialized to the given value.
// Items must be separated by '|', for example "Item1|Item2|Item3", and so on.
void create_combochecklist(wxComboCtrl* comboCtrl, std::string text, std::string items, bool initial_value);

// Returns the current state of the items listed in the wxCheckListBoxComboPopup contained in the given wxComboCtrl,
// encoded inside an int.
int combochecklist_get_flags(wxComboCtrl* comboCtrl);

// wxString conversions:

// wxString from std::string in UTF8
wxString	from_u8(const std::string &str);
// std::string in UTF8 from wxString
std::string	into_u8(const wxString &str);
// wxString from boost path
wxString	from_path(const boost::filesystem::path &path);
// boost path from wxString
boost::filesystem::path	into_path(const wxString &str);

// Display an About dialog
extern void about();
// Ask the destop to open the datadir using the default file explorer.
extern void desktop_open_datadir_folder();

} // namespace GUI
} // namespace Slic3r

#endif
