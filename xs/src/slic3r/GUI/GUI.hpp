#ifndef slic3r_GUI_hpp_
#define slic3r_GUI_hpp_

#include <string>
#include <vector>
#include "Config.hpp"

class wxApp;
class wxFrame;
class wxWindow;
class wxMenuBar;
class wxNotebook;
class wxString;
class wxArrayString;
class wxArrayLong;

namespace Slic3r { 

class PresetBundle;
class PresetCollection;
class AppConfig;
class DynamicPrintConfig;
class TabIface;

//! macro used to localization, return wxString
#define _L(s) wxGetTranslation(s)
//! macro used to localization, return const CharType *
#define _LU8(s) wxGetTranslation(s).ToUTF8().data()

namespace GUI {

class Tab;
// Map from an file_type name to full file wildcard name.
typedef std::map<std::string, std::string> t_file_wild_card;
inline t_file_wild_card& get_file_wild_card() {
	static t_file_wild_card FILE_WILDCARDS;
	if (FILE_WILDCARDS.empty()){
		FILE_WILDCARDS["known"] = "Known files (*.stl, *.obj, *.amf, *.xml, *.prusa)|*.stl;*.STL;*.obj;*.OBJ;*.amf;*.AMF;*.xml;*.XML;*.prusa;*.PRUSA";
		FILE_WILDCARDS["stl"] = "STL files (*.stl)|*.stl;*.STL";
		FILE_WILDCARDS["obj"] = "OBJ files (*.obj)|*.obj;*.OBJ";
		FILE_WILDCARDS["amf"] = "AMF files (*.amf)|*.amf;*.AMF;*.xml;*.XML";
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

void add_debug_menu(wxMenuBar *menu, int event_language_change);
// Create a new preset tab (print, filament and printer),
void create_preset_tabs(PresetBundle *preset_bundle, AppConfig *app_config, 
						bool no_controller, bool is_disabled_button_browse,	bool is_user_agent,
						int event_value_change, int event_presets_changed,
						int event_button_browse, int event_button_test);
TabIface* get_preset_tab_iface(char *name);

// add it at the end of the tab panel.
void add_created_tab(Tab* panel, PresetBundle *preset_bundle, AppConfig *app_config);
// Change option value in config
void change_opt_value(DynamicPrintConfig& config, t_config_option_key opt_key, boost::any value);

void show_error(wxWindow* parent, wxString message);
void show_info(wxWindow* parent, wxString message, wxString title);

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

} }

#endif
