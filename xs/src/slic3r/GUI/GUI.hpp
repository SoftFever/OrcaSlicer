#ifndef slic3r_GUI_hpp_
#define slic3r_GUI_hpp_

#include <string>
#include <vector>
#include "Config.hpp"

class wxApp;
class wxWindow;
class wxFrame;
class wxWindow;
class wxMenuBar;
class wxNotebook;
class wxComboCtrl;
class wxString;
class wxArrayString;
class wxArrayLong;
class wxColour;
class wxBoxSizer;
class wxFlexGridSizer;
class wxButton;
class wxFileDialog;

namespace Slic3r { 

class PresetBundle;
class PresetCollection;
class AppConfig;
class DynamicPrintConfig;
class TabIface;

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

class Tab;
class ConfigOptionsGroup;
// Map from an file_type name to full file wildcard name.
typedef std::map<std::string, std::string> t_file_wild_card;
inline t_file_wild_card& get_file_wild_card() {
	static t_file_wild_card FILE_WILDCARDS;
	if (FILE_WILDCARDS.empty()){
		FILE_WILDCARDS["known"] = "Known files (*.stl, *.obj, *.amf, *.xml, *.prusa)|*.stl;*.STL;*.obj;*.OBJ;*.amf;*.AMF;*.xml;*.XML;*.prusa;*.PRUSA";
		FILE_WILDCARDS["stl"] = "STL files (*.stl)|*.stl;*.STL";
		FILE_WILDCARDS["obj"] = "OBJ files (*.obj)|*.obj;*.OBJ";
        FILE_WILDCARDS["amf"] = "AMF files (*.amf)|*.zip.amf;*.amf;*.AMF;*.xml;*.XML";
        FILE_WILDCARDS["3mf"] = "3MF files (*.3mf)|*.3mf;*.3MF;";
        FILE_WILDCARDS["prusa"] = "Prusa Control files (*.prusa)|*.prusa;*.PRUSA";
		FILE_WILDCARDS["ini"] = "INI files *.ini|*.ini;*.INI";
		FILE_WILDCARDS["gcode"] = "G-code files (*.gcode, *.gco, *.g, *.ngc)|*.gcode;*.GCODE;*.gco;*.GCO;*.g;*.G;*.ngc;*.NGC";
		FILE_WILDCARDS["svg"] = "SVG files *.svg|*.svg;*.SVG";
	}
	return FILE_WILDCARDS;
}

void disable_screensaver();
void enable_screensaver();
std::vector<std::string> scan_serial_ports();
bool debugged();
void break_to_debugger();

// Passing the wxWidgets GUI classes instantiated by the Perl part to C++.
void set_wxapp(wxApp *app);
void set_main_frame(wxFrame *main_frame);
void set_tab_panel(wxNotebook *tab_panel);
void set_app_config(AppConfig *app_config);
void set_preset_bundle(PresetBundle *preset_bundle);

AppConfig*	get_app_config();
wxApp*		get_app();

const wxColour& get_modified_label_clr();
const wxColour& get_sys_label_clr();
unsigned get_colour_approx_luma(const wxColour &colour);

void add_debug_menu(wxMenuBar *menu, int event_language_change);

// Create "Preferences" dialog after selecting menu "Preferences" in Perl part
void open_preferences_dialog(int event_preferences);

// Create a new preset tab (print, filament and printer),
void create_preset_tabs(bool no_controller, int event_value_change, int event_presets_changed);
TabIface* get_preset_tab_iface(char *name);

// add it at the end of the tab panel.
void add_created_tab(Tab* panel);
// Change option value in config
void change_opt_value(DynamicPrintConfig& config, const t_config_option_key& opt_key, const boost::any& value, int opt_index = 0);

void show_error(wxWindow* parent, const wxString& message);
void show_info(wxWindow* parent, const wxString& message, const wxString& title);
void warning_catcher(wxWindow* parent, const wxString& message);

// load language saved at application config 
bool load_language();
// save language at application config 
void save_language();
// get list of installed languages 
void get_installed_languages(wxArrayString & names, wxArrayLong & identifiers);
// select language from the list of installed languages
bool select_language(wxArrayString & names, wxArrayLong & identifiers);

std::vector<Tab *>& get_tabs_list();
bool checked_tab(Tab* tab);
void delete_tab_from_list(Tab* tab);

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


void add_frequently_changed_parameters(wxWindow* parent, wxBoxSizer* sizer, wxFlexGridSizer* preset_sizer);

ConfigOptionsGroup* get_optgroup();
wxButton*			get_wiping_dialog_button();

void add_export_option(wxFileDialog* dlg, const std::string& format);
int get_export_option(wxFileDialog* dlg);
}
}

#endif
