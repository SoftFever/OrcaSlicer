#include "GUI.hpp"
#include "WipeTowerDialog.hpp"

#include <assert.h>
#include <cmath>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/format.hpp>

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
#pragma comment(lib, "user32.lib")
#endif

#include <wx/app.h>
#include <wx/button.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/frame.h>
#include <wx/menu.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/combo.h>
#include <wx/window.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/collpane.h>
#include <wx/wupdlock.h>

#include "wxExtensions.hpp"

#include "Tab.hpp"
#include "TabIface.hpp"
#include "AboutDialog.hpp"
#include "AppConfig.hpp"
#include "ConfigSnapshotDialog.hpp"
#include "Utils.hpp"
#include "MsgDialog.hpp"
#include "ConfigWizard.hpp"
#include "Preferences.hpp"
#include "PresetBundle.hpp"
#include "UpdateDialogs.hpp"

#include "../Utils/PresetUpdater.hpp"
#include "../Config/Snapshot.hpp"

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

std::vector<std::string> scan_serial_ports()
{
    std::vector<std::string> out;
#ifdef _WIN32
    // 1) Open the registry key SERIALCOM.
    HKEY hKey;
    LONG lRes = ::RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &hKey);
    assert(lRes == ERROR_SUCCESS);
    if (lRes == ERROR_SUCCESS) {
        // 2) Get number of values of SERIALCOM key.
        DWORD        cValues;                   // number of values for key 
        {
            TCHAR    achKey[255];               // buffer for subkey name
            DWORD    cbName;                    // size of name string 
            TCHAR    achClass[MAX_PATH] = TEXT("");  // buffer for class name 
            DWORD    cchClassName = MAX_PATH;   // size of class string 
            DWORD    cSubKeys=0;                // number of subkeys 
            DWORD    cbMaxSubKey;               // longest subkey size 
            DWORD    cchMaxClass;               // longest class string 
            DWORD    cchMaxValue;               // longest value name 
            DWORD    cbMaxValueData;            // longest value data 
            DWORD    cbSecurityDescriptor;      // size of security descriptor 
            FILETIME ftLastWriteTime;           // last write time 
            // Get the class name and the value count.
            lRes = RegQueryInfoKey(
                hKey,                    // key handle 
                achClass,                // buffer for class name 
                &cchClassName,           // size of class string 
                NULL,                    // reserved 
                &cSubKeys,               // number of subkeys 
                &cbMaxSubKey,            // longest subkey size 
                &cchMaxClass,            // longest class string 
                &cValues,                // number of values for this key 
                &cchMaxValue,            // longest value name 
                &cbMaxValueData,         // longest value data 
                &cbSecurityDescriptor,   // security descriptor 
                &ftLastWriteTime);       // last write time
            assert(lRes == ERROR_SUCCESS);
        }
        // 3) Read the SERIALCOM values.
        {
            DWORD dwIndex = 0;
            for (int i = 0; i < cValues; ++ i, ++ dwIndex) {
                wchar_t valueName[2048];
                DWORD	valNameLen = 2048;
                DWORD	dataType;
				wchar_t data[2048];
				DWORD	dataSize = 4096;
				lRes = ::RegEnumValueW(hKey, dwIndex, valueName, &valNameLen, nullptr, &dataType, (BYTE*)&data, &dataSize);
                if (lRes == ERROR_SUCCESS && dataType == REG_SZ && valueName[0] != 0)
					out.emplace_back(boost::nowide::narrow(data));
            }
        }
        ::RegCloseKey(hKey);
    }
#else
    // UNIX and OS X
    std::initializer_list<const char*> prefixes { "ttyUSB" , "ttyACM", "tty.", "cu.", "rfcomm" };
    for (auto &dir_entry : boost::filesystem::directory_iterator(boost::filesystem::path("/dev"))) {
        std::string name = dir_entry.path().filename().string();
        for (const char *prefix : prefixes) {
            if (boost::starts_with(name, prefix)) {
                out.emplace_back(dir_entry.path().string());
                break;
            }
        }
    }
#endif

    out.erase(std::remove_if(out.begin(), out.end(), 
        [](const std::string &key){ 
            return boost::starts_with(key, "Bluetooth") || boost::starts_with(key, "FireFly"); 
        }),
        out.end());
    return out;
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

// Passing the wxWidgets GUI classes instantiated by the Perl part to C++.
wxApp       *g_wxApp        = nullptr;
wxFrame     *g_wxMainFrame  = nullptr;
wxNotebook  *g_wxTabPanel   = nullptr;
AppConfig	*g_AppConfig	= nullptr;
PresetBundle *g_PresetBundle= nullptr;
PresetUpdater *g_PresetUpdater = nullptr;
wxColour    g_color_label_modified;
wxColour    g_color_label_sys;
wxColour    g_color_label_default;

std::vector<Tab *> g_tabs_list;

wxLocale*	g_wxLocale;

std::shared_ptr<ConfigOptionsGroup>	m_optgroup;
double m_brim_width = 0.0;
wxButton*	g_wiping_dialog_button = nullptr;

//showed/hided controls according to the view mode
wxWindow	*g_plater = nullptr;
wxBoxSizer	*g_frequently_changed_parameters_sizer = nullptr;
wxBoxSizer	*g_expert_mode_part_sizer = nullptr;
wxBoxSizer	*g_scrolled_window_sizer = nullptr;
wxButton	*g_btn_export_stl = nullptr;
wxButton	*g_btn_reslice = nullptr;
wxButton	*g_btn_print = nullptr;
wxButton	*g_btn_send_gcode = nullptr;
bool		g_show_print_info = false;

static void init_label_colours()
{
	auto luma = get_colour_approx_luma(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
	if (luma >= 128) {
		g_color_label_modified = wxColour(252, 77, 1);
		g_color_label_sys = wxColour(26, 132, 57);
	} else {
		g_color_label_modified = wxColour(253, 111, 40);
		g_color_label_sys = wxColour(115, 220, 103);
	}
	g_color_label_default = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
}

void update_label_colours_from_appconfig()
{
	if (g_AppConfig->has("label_clr_sys")){
		auto str = g_AppConfig->get("label_clr_sys");
		if (str != "")
			g_color_label_sys = wxColour(str);
	}
	
	if (g_AppConfig->has("label_clr_modified")){
		auto str = g_AppConfig->get("label_clr_modified");
		if (str != "")
			g_color_label_modified = wxColour(str);
	}
}

void set_wxapp(wxApp *app)
{
    g_wxApp = app;
    init_label_colours();
}

void set_main_frame(wxFrame *main_frame)
{
    g_wxMainFrame = main_frame;
}

void set_tab_panel(wxNotebook *tab_panel)
{
    g_wxTabPanel = tab_panel;
}

void set_app_config(AppConfig *app_config)
{
	g_AppConfig = app_config;
}

void set_preset_bundle(PresetBundle *preset_bundle)
{
	g_PresetBundle = preset_bundle;
}

void set_preset_updater(PresetUpdater *updater)
{
	g_PresetUpdater = updater;
}

void set_objects_from_perl(	wxWindow* parent, wxBoxSizer *frequently_changed_parameters_sizer,
							wxBoxSizer *expert_mode_part_sizer, wxBoxSizer *scrolled_window_sizer,
							wxButton *btn_export_stl, wxButton *btn_reslice, 
							wxButton *btn_print, wxButton *btn_send_gcode)
{
	g_plater = parent;
	g_frequently_changed_parameters_sizer = frequently_changed_parameters_sizer;
	g_expert_mode_part_sizer = expert_mode_part_sizer;
	g_scrolled_window_sizer = scrolled_window_sizer;
	g_btn_export_stl = btn_export_stl;
	g_btn_reslice = btn_reslice;
	g_btn_print = btn_print;
	g_btn_send_gcode = btn_send_gcode;
}

void set_show_print_info(bool show)
{
	g_show_print_info = show;
}

std::vector<Tab *>& get_tabs_list()
{
	return g_tabs_list;
}

bool checked_tab(Tab* tab)
{
	bool ret = true;
	if (find(g_tabs_list.begin(), g_tabs_list.end(), tab) == g_tabs_list.end())
		ret = false;
	return ret;
}

void delete_tab_from_list(Tab* tab)
{
	std::vector<Tab *>::iterator itr = find(g_tabs_list.begin(), g_tabs_list.end(), tab);
	if (itr != g_tabs_list.end())
		g_tabs_list.erase(itr);
}

bool select_language(wxArrayString & names,
	wxArrayLong & identifiers)
{
	wxCHECK_MSG(names.Count() == identifiers.Count(), false,
		_(L("Array of language names and identifiers should have the same size.")));
	int init_selection = 0;
	long current_language = g_wxLocale ? g_wxLocale->GetLanguage() : wxLANGUAGE_UNKNOWN;
	for (auto lang : identifiers){
		if (lang == current_language)
			break;
		else
			++init_selection;
	}
	if (init_selection == identifiers.size())
		init_selection = 0;
	long index = wxGetSingleChoiceIndex(_(L("Select the language")), _(L("Language")), 
										names, init_selection);
	if (index != -1)
	{
		g_wxLocale = new wxLocale;
		g_wxLocale->Init(identifiers[index]);
		g_wxLocale->AddCatalogLookupPathPrefix(wxPathOnly(localization_dir()));
		g_wxLocale->AddCatalog(g_wxApp->GetAppName());
		wxSetlocale(LC_NUMERIC, "C");
		return true;
	}
	return false;
}

bool load_language()
{
	wxString language = wxEmptyString;
	if (g_AppConfig->has("translation_language"))
		language = g_AppConfig->get("translation_language");

	if (language.IsEmpty()) 
		return false;
	wxArrayString	names;
	wxArrayLong		identifiers;
	get_installed_languages(names, identifiers);
	for (size_t i = 0; i < identifiers.Count(); i++)
	{
		if (wxLocale::GetLanguageCanonicalName(identifiers[i]) == language)
		{
			g_wxLocale = new wxLocale;
			g_wxLocale->Init(identifiers[i]);
			g_wxLocale->AddCatalogLookupPathPrefix(wxPathOnly(localization_dir()));
			g_wxLocale->AddCatalog(g_wxApp->GetAppName());
			wxSetlocale(LC_NUMERIC, "C");
			return true;
		}
	}
	return false;
}

void save_language()
{
	wxString language = wxEmptyString; 
	if (g_wxLocale)	
		language = g_wxLocale->GetCanonicalName();

	g_AppConfig->set("translation_language", language.ToStdString());
	g_AppConfig->save();
}

void get_installed_languages(wxArrayString & names,
	wxArrayLong & identifiers)
{
	names.Clear();
	identifiers.Clear();

	wxDir dir(wxPathOnly(localization_dir()));
	wxString filename;
	const wxLanguageInfo * langinfo;
	wxString name = wxLocale::GetLanguageName(wxLANGUAGE_DEFAULT);
	if (!name.IsEmpty())
	{
		names.Add(_(L("Default")));
		identifiers.Add(wxLANGUAGE_DEFAULT);
	}
	for (bool cont = dir.GetFirst(&filename, wxEmptyString, wxDIR_DIRS);
		cont; cont = dir.GetNext(&filename))
	{
		langinfo = wxLocale::FindLanguageInfo(filename);
		if (langinfo != NULL)
		{
			auto full_file_name = dir.GetName() + wxFileName::GetPathSeparator() +
				filename + wxFileName::GetPathSeparator() +
				g_wxApp->GetAppName() + wxT(".mo");
			if (wxFileExists(full_file_name))
			{
				names.Add(langinfo->Description);
				identifiers.Add(langinfo->Language);
			}
		}
	}
}

enum ConfigMenuIDs {
	ConfigMenuWizard,
	ConfigMenuSnapshots,
	ConfigMenuTakeSnapshot,
	ConfigMenuUpdate,
	ConfigMenuPreferences,
	ConfigMenuModeSimple,
	ConfigMenuModeRegular,
	ConfigMenuModeExpert,
	ConfigMenuLanguage,
	ConfigMenuCnt,
};
	
ConfigMenuIDs get_view_mode()
{
	if (!g_AppConfig->has("view_mode"))
		return ConfigMenuModeSimple;

	const auto mode = g_AppConfig->get("view_mode");
	return	mode == "expert" ?
			ConfigMenuModeExpert :
				mode == "regular" ?
				ConfigMenuModeRegular :
				ConfigMenuModeSimple;
}

void add_config_menu(wxMenuBar *menu, int event_preferences_changed, int event_language_change)
{
    auto local_menu = new wxMenu();
    wxWindowID config_id_base = wxWindow::NewControlId((int)ConfigMenuCnt);

    const auto config_wizard_tooltip = wxString::Format(_(L("Run %s")), ConfigWizard::name());
    // Cmd+, is standard on OS X - what about other operating systems?
   	local_menu->Append(config_id_base + ConfigMenuWizard, 		ConfigWizard::name() + "\u2026", 			config_wizard_tooltip);
   	local_menu->Append(config_id_base + ConfigMenuSnapshots, 	_(L("Configuration Snapshots"))+"\u2026",	_(L("Inspect / activate configuration snapshots")));
   	local_menu->Append(config_id_base + ConfigMenuTakeSnapshot, _(L("Take Configuration Snapshot")), 		_(L("Capture a configuration snapshot")));
   	local_menu->Append(config_id_base + ConfigMenuUpdate, 		_(L("Check for updates")), 					_(L("Check for configuration updates")));
   	local_menu->AppendSeparator();
   	local_menu->Append(config_id_base + ConfigMenuPreferences, 	_(L("Preferences"))+"\u2026\tCtrl+,", 		_(L("Application preferences")));
   	local_menu->AppendSeparator();
	auto mode_menu = new wxMenu();
	mode_menu->AppendRadioItem(config_id_base + ConfigMenuModeSimple,	_(L("&Simple")),					_(L("Simple View Mode")));
	mode_menu->AppendRadioItem(config_id_base + ConfigMenuModeRegular,	_(L("&Regular")),					_(L("Regular View Mode")));
	mode_menu->AppendRadioItem(config_id_base + ConfigMenuModeExpert,	_(L("&Expert")),					_(L("Expert View Mode")));
	mode_menu->Check(config_id_base + get_view_mode(), true);
	local_menu->AppendSubMenu(mode_menu,						_(L("&Mode")), 								_(L("Slic3r View Mode")));
   	local_menu->AppendSeparator();
	local_menu->Append(config_id_base + ConfigMenuLanguage,		_(L("Change Application Language")));
	local_menu->Bind(wxEVT_MENU, [config_id_base, event_language_change, event_preferences_changed](wxEvent &event){
		switch (event.GetId() - config_id_base) {
		case ConfigMenuWizard:
            config_wizard(ConfigWizard::RR_USER);
            break;
		case ConfigMenuTakeSnapshot:
			// Take a configuration snapshot.
			if (check_unsaved_changes()) {
				wxTextEntryDialog dlg(nullptr, _(L("Taking configuration snapshot")), _(L("Snapshot name")));
				if (dlg.ShowModal() == wxID_OK)
					g_AppConfig->set("on_snapshot", 
						Slic3r::GUI::Config::SnapshotDB::singleton().take_snapshot(
							*g_AppConfig, Slic3r::GUI::Config::Snapshot::SNAPSHOT_USER, dlg.GetValue().ToUTF8().data()).id);
			}
			break;
		case ConfigMenuSnapshots:
			if (check_unsaved_changes()) {
				std::string on_snapshot;
		    	if (Config::SnapshotDB::singleton().is_on_snapshot(*g_AppConfig))
		    		on_snapshot = g_AppConfig->get("on_snapshot");
		    	ConfigSnapshotDialog dlg(Slic3r::GUI::Config::SnapshotDB::singleton(), on_snapshot);
		    	dlg.ShowModal();
		    	if (! dlg.snapshot_to_activate().empty()) {
		    		if (! Config::SnapshotDB::singleton().is_on_snapshot(*g_AppConfig))
		    			Config::SnapshotDB::singleton().take_snapshot(*g_AppConfig, Config::Snapshot::SNAPSHOT_BEFORE_ROLLBACK);
		    		g_AppConfig->set("on_snapshot", 
		    			Config::SnapshotDB::singleton().restore_snapshot(dlg.snapshot_to_activate(), *g_AppConfig).id);
		    		g_PresetBundle->load_presets(*g_AppConfig);
		    		// Load the currently selected preset into the GUI, update the preset selection box.
					for (Tab *tab : g_tabs_list)
						tab->load_current_preset();
		    	}
		    }
		    break;
		case ConfigMenuPreferences:
		{
			PreferencesDialog dlg(g_wxMainFrame, event_preferences_changed);
			dlg.ShowModal();
			break;
		}
		case ConfigMenuLanguage:
		{
			wxArrayString names;
			wxArrayLong identifiers;
			get_installed_languages(names, identifiers);
			if (select_language(names, identifiers)) {
				save_language();
				show_info(g_wxTabPanel, _(L("Application will be restarted")), _(L("Attention!")));
				if (event_language_change > 0) {
					wxCommandEvent event(event_language_change);
					g_wxApp->ProcessEvent(event);
				}
			}
			break;
		}
		}
	});
	mode_menu->Bind(wxEVT_MENU, [config_id_base](wxEvent& event) {
		std::string mode = "";
		switch (event.GetId() - config_id_base){
		case ConfigMenuModeExpert:
			mode = "expert";
			break;
		case ConfigMenuModeRegular:
			mode = "regular";
			break;
		case ConfigMenuModeSimple:
			mode = "simple";
			break;
		}
		g_AppConfig->set("view_mode", mode);
		g_AppConfig->save();
		update_mode();
	});
	menu->Append(local_menu, _(L("&Configuration")));
}

// This is called when closing the application, when loading a config file or when starting the config wizard
// to notify the user whether he is aware that some preset changes will be lost.
bool check_unsaved_changes()
{
	std::string dirty;
	for (Tab *tab : g_tabs_list)
		if (tab->current_preset_is_dirty())
			if (dirty.empty())
				dirty = tab->name();
			else
				dirty += std::string(", ") + tab->name();
	if (dirty.empty())
		// No changes, the application may close or reload presets.
		return true;
	// Ask the user.
	auto dialog = new wxMessageDialog(g_wxMainFrame,
		_(L("You have unsaved changes ")) + dirty + _(L(". Discard changes and continue anyway?")), 
		_(L("Unsaved Presets")),
		wxICON_QUESTION | wxYES_NO | wxNO_DEFAULT);
	return dialog->ShowModal() == wxID_YES;
}

bool config_wizard_startup(bool app_config_exists)
{
	if (! app_config_exists || g_PresetBundle->has_defauls_only()) {
		config_wizard(ConfigWizard::RR_DATA_EMPTY);
		return true;
	} else if (g_AppConfig->legacy_datadir()) {
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
    if (! check_unsaved_changes())
    	return;

	ConfigWizard wizard(nullptr, static_cast<ConfigWizard::RunReason>(reason));
	wizard.run(g_PresetBundle, g_PresetUpdater);

    // Load the currently selected preset into the GUI, update the preset selection box.
	for (Tab *tab : g_tabs_list)
		tab->load_current_preset();
}

void open_preferences_dialog(int event_preferences)
{
	auto dlg = new PreferencesDialog(g_wxMainFrame, event_preferences);
	dlg->ShowModal();
}

void create_preset_tabs(bool no_controller, int event_value_change, int event_presets_changed)
{	
	update_label_colours_from_appconfig();
	add_created_tab(new TabPrint	(g_wxTabPanel, no_controller));
	add_created_tab(new TabFilament	(g_wxTabPanel, no_controller));
	add_created_tab(new TabPrinter	(g_wxTabPanel, no_controller));
	for (size_t i = 0; i < g_wxTabPanel->GetPageCount(); ++ i) {
		Tab *tab = dynamic_cast<Tab*>(g_wxTabPanel->GetPage(i));
		if (! tab)
			continue;
		tab->set_event_value_change(wxEventType(event_value_change));
		tab->set_event_presets_changed(wxEventType(event_presets_changed));
	}
	update_mode();// TODO change place of call this function
}

TabIface* get_preset_tab_iface(char *name)
{
	for (size_t i = 0; i < g_wxTabPanel->GetPageCount(); ++ i) {
		Tab *tab = dynamic_cast<Tab*>(g_wxTabPanel->GetPage(i));
		if (! tab)
			continue;
		if (tab->name() == name) {
			return new TabIface(tab);
		}
	}
	return new TabIface(nullptr);
}

// opt_index = 0, by the reason of zero-index in ConfigOptionVector by default (in case only one element)
void change_opt_value(DynamicPrintConfig& config, const t_config_option_key& opt_key, const boost::any& value, int opt_index /*= 0*/)
{
	try{
		switch (config.def()->get(opt_key)->type){
		case coFloatOrPercent:{
			std::string str = boost::any_cast<std::string>(value);
			bool percent = false;
			if (str.back() == '%'){
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
			else if (config.def()->get(opt_key)->gui_flags.compare("serialized") == 0){
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
			}
			break;
		case coPoints:{
			if (opt_key.compare("bed_shape") == 0){
				config.option<ConfigOptionPoints>(opt_key)->values = boost::any_cast<std::vector<Pointf>>(value);
				break;
			}
			ConfigOptionPoints* vec_new = new ConfigOptionPoints{ boost::any_cast<Pointf>(value) };
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

void add_created_tab(Tab* panel)
{
	panel->create_preset_tab(g_PresetBundle);

	// Load the currently selected preset into the GUI, update the preset selection box.
	panel->load_current_preset();
	g_wxTabPanel->AddPage(panel, panel->title());
}

void show_error(wxWindow* parent, const wxString& message) {
	ErrorDialog msg(parent, message);
	msg.ShowModal();
}

void show_error_id(int id, const std::string& message) {
	auto *parent = id != 0 ? wxWindow::FindWindowById(id) : nullptr;
	show_error(parent, message);
}

void show_info(wxWindow* parent, const wxString& message, const wxString& title){
	wxMessageDialog msg_wingow(parent, message, title.empty() ? _(L("Notice")) : title, wxOK | wxICON_INFORMATION);
	msg_wingow.ShowModal();
}

void warning_catcher(wxWindow* parent, const wxString& message){
	if (message == "GLUquadricObjPtr | " + _(L("Attempt to free unreferenced scalar")) )
		return;
	wxMessageDialog msg(parent, message, _(L("Warning")), wxOK | wxICON_WARNING);
	msg.ShowModal();
}

wxApp* get_app(){
	return g_wxApp;
}

PresetBundle* get_preset_bundle()
{
	return g_PresetBundle;
}

const wxColour& get_label_clr_modified() {
	return g_color_label_modified;
}

const wxColour& get_label_clr_sys() {
	return g_color_label_sys;
}

void set_label_clr_modified(const wxColour& clr) {
	g_color_label_modified = clr;
	auto clr_str = wxString::Format(wxT("#%02X%02X%02X"), clr.Red(), clr.Green(), clr.Blue());
	std::string str = clr_str.ToStdString();
	g_AppConfig->set("label_clr_modified", str);
	g_AppConfig->save();
}

void set_label_clr_sys(const wxColour& clr) {
	g_color_label_sys = clr;
	auto clr_str = wxString::Format(wxT("#%02X%02X%02X"), clr.Red(), clr.Green(), clr.Blue());
	std::string str = clr_str.ToStdString();
	g_AppConfig->set("label_clr_sys", str);
	g_AppConfig->save();
}

const wxColour& get_label_clr_default() {
	return g_color_label_default;
}

unsigned get_colour_approx_luma(const wxColour &colour)
{
	double r = colour.Red();
	double g = colour.Green();
	double b = colour.Blue();

	return std::round(std::sqrt(
		r * r * .241 +
		g * g * .691 +
		b * b * .068
	));
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
	return g_AppConfig;
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


void add_expert_mode_part(wxWindow* parent, wxBoxSizer* sizer)
{
 	sizer->SetMinSize(-1, 150);
	auto main_sizer = new wxBoxSizer(wxVERTICAL);
	auto main_page = new wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
	main_page->SetSizer(main_sizer);
	main_page->SetScrollbars(1, 1, 1, 1);
	sizer->Add(main_page, 1, wxEXPAND | wxALL, 1);

	// Experiments with new UI
// 	wxSizer *paneSz = new wxBoxSizer(wxVERTICAL);
// 	paneSz->Add(m_optgroup->sizer, 1, wxGROW | wxEXPAND | wxLEFT | wxRIGHT, 5);
// 	win->SetSizer(paneSz);
// 	paneSz->SetSizeHints(win);

	// *** Objects List ***
	auto *collpane_objects = new PrusaCollapsiblePane(main_page, wxID_ANY, "Objects List:");
	// add the pane with a zero proportion value to the sizer which contains it
	main_sizer->Add(collpane_objects, 0, wxGROW | wxALL, 0);

	wxWindow *win_objects = collpane_objects->GetPane();

	// **********************************************************************************************
	auto objects_ctrl = new wxDataViewCtrl(win_objects, wxID_ANY, wxDefaultPosition, wxDefaultSize);
	wxSizer *objects_sz = new wxBoxSizer(wxVERTICAL);
	objects_ctrl->SetBestFittingSize(wxSize(-1, 200)); 
	// TODO - Set correct height according to the opened/closed objects
//	objects_ctrl->SetMinSize(wxSize(-1, 200));
	objects_sz->Add(objects_ctrl, 1, wxGROW | wxALL, 5);

	auto objects_model = new MyObjectTreeModel;
	objects_ctrl->AssociateModel(objects_model);
#if wxUSE_DRAG_AND_DROP && wxUSE_UNICODE
	objects_ctrl->EnableDragSource(wxDF_UNICODETEXT);
	objects_ctrl->EnableDropTarget(wxDF_UNICODETEXT);
#endif // wxUSE_DRAG_AND_DROP && wxUSE_UNICODE

	// column 0 of the view control:

	wxDataViewTextRenderer *tr = new wxDataViewTextRenderer("string", wxDATAVIEW_CELL_INERT);
	wxDataViewColumn *column00 = new wxDataViewColumn("Name", tr, 0, 140, wxALIGN_LEFT,
		wxDATAVIEW_COL_SORTABLE | wxDATAVIEW_COL_RESIZABLE);
	objects_ctrl->AppendColumn(column00);

	// column 1 of the view control:

	tr = new wxDataViewTextRenderer("string", wxDATAVIEW_CELL_INERT);
	wxDataViewColumn *column01 = new wxDataViewColumn("Copy", tr, 1, 75, wxALIGN_CENTER_HORIZONTAL,
		wxDATAVIEW_COL_SORTABLE | wxDATAVIEW_COL_RESIZABLE);
	objects_ctrl->AppendColumn(column01);

	// column 2 of the view control:

	tr = new wxDataViewTextRenderer("string", wxDATAVIEW_CELL_INERT);
	wxDataViewColumn *column02 = new wxDataViewColumn("Scale", tr, 2, 80, wxALIGN_CENTER_HORIZONTAL,
		wxDATAVIEW_COL_SORTABLE | wxDATAVIEW_COL_RESIZABLE);
	objects_ctrl->AppendColumn(column02);

// 	common_sizer->Add(objects_sz, 0, wxEXPAND | wxALL, 1);

	wxSizer *paneSz_objects = new wxBoxSizer(wxVERTICAL);
	paneSz_objects->Add(objects_sz, 1, wxGROW | wxEXPAND | wxBOTTOM, 2);
	win_objects->SetSizer(paneSz_objects);
	paneSz_objects->SetSizeHints(win_objects);



// 	auto common_sizer = new wxBoxSizer(wxVERTICAL);
// 	common_sizer->Add(m_optgroup->sizer);

// 	auto listctrl = new wxDataViewListCtrl(win, wxID_ANY, wxDefaultPosition, wxSize(-1, 100));
// 	listctrl->AppendToggleColumn("Toggle");
// 	listctrl->AppendTextColumn("Text");
// 	wxVector<wxVariant> data;
// 	data.push_back(wxVariant(true));
// 	data.push_back(wxVariant("row 1"));
// 	listctrl->AppendItem(data);
// 	data.clear();
// 	data.push_back(wxVariant(false));
// 	data.push_back(wxVariant("row 3"));
// 	listctrl->AppendItem(data);
// 	data.clear();
// 	data.push_back(wxVariant(false));
// 	data.push_back(wxVariant("row 2"));
// 	listctrl->AppendItem(data);
// 	common_sizer->Add(listctrl, 0, wxEXPAND | wxALL, 1);


}

void add_frequently_changed_parameters(wxWindow* parent, wxBoxSizer* sizer, wxFlexGridSizer* preset_sizer)
{
	DynamicPrintConfig*	config = &g_PresetBundle->prints.get_edited_preset().config;
	m_optgroup = std::make_shared<ConfigOptionsGroup>(parent, "", config);
	//	const wxArrayInt& ar = preset_sizer->GetColWidths();
	// 	m_optgroup->label_width = ar.IsEmpty() ? 100 : ar.front(); // doesn't work
	m_optgroup->m_on_change = [config](t_config_option_key opt_key, boost::any value){
		TabPrint* tab_print = nullptr;
		for (size_t i = 0; i < g_wxTabPanel->GetPageCount(); ++i) {
			Tab *tab = dynamic_cast<Tab*>(g_wxTabPanel->GetPage(i));
			if (!tab)
				continue;
			if (tab->name() == "print"){
				tab_print = static_cast<TabPrint*>(tab);
				break;
			}
		}
		if (tab_print == nullptr)
			return;

		if (opt_key == "fill_density"){
			value = m_optgroup->get_config_value(*config, opt_key);
			tab_print->set_value(opt_key, value);
			tab_print->update();
		}
		else{
			DynamicPrintConfig new_conf = *config;
			if (opt_key == "brim"){
				double new_val;
				double brim_width = config->opt_float("brim_width");
				if (boost::any_cast<bool>(value) == true)
				{
					new_val = m_brim_width == 0.0 ? 10 :
						m_brim_width < 0.0 ? m_brim_width * (-1) :
						m_brim_width;
				}
				else{
					m_brim_width = brim_width * (-1);
					new_val = 0;
				}
				new_conf.set_key_value("brim_width", new ConfigOptionFloat(new_val));
			}
			else{ //(opt_key == "support")
				const wxString& selection = boost::any_cast<wxString>(value);

				auto support_material = selection == _("None") ? false : true;
				new_conf.set_key_value("support_material", new ConfigOptionBool(support_material));

				if (selection == _("Everywhere"))
					new_conf.set_key_value("support_material_buildplate_only", new ConfigOptionBool(false));
				else if (selection == _("Support on build plate only"))
					new_conf.set_key_value("support_material_buildplate_only", new ConfigOptionBool(true));
			}
			tab_print->load_config(new_conf);
		}

		tab_print->update_dirty();
	};

	Option option = m_optgroup->get_option("fill_density");
	option.opt.sidetext = "";
	option.opt.full_width = true;
	m_optgroup->append_single_option_line(option);

	ConfigOptionDef def;

	def.label = L("Support");
	def.type = coStrings;
	def.gui_type = "select_open";
	def.tooltip = L("Select what kind of support do you need");
	def.enum_labels.push_back(L("None"));
	def.enum_labels.push_back(L("Support on build plate only"));
	def.enum_labels.push_back(L("Everywhere"));
	std::string selection = !config->opt_bool("support_material") ?
		"None" :
		config->opt_bool("support_material_buildplate_only") ?
		"Support on build plate only" :
		"Everywhere";
	def.default_value = new ConfigOptionStrings { selection };
	option = Option(def, "support");
	option.opt.full_width = true;
	m_optgroup->append_single_option_line(option);

	m_brim_width = config->opt_float("brim_width");
	def.label = L("Brim");
	def.type = coBool;
	def.tooltip = L("This flag enables the brim that will be printed around each object on the first layer.");
	def.gui_type = "";
	def.default_value = new ConfigOptionBool{ m_brim_width > 0.0 ? true : false };
	option = Option(def, "brim");
	m_optgroup->append_single_option_line(option);


    Line line = { "", "" };
        line.widget = [config](wxWindow* parent){
			g_wiping_dialog_button = new wxButton(parent, wxID_ANY, _(L("Purging volumes")) + "\u2026", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
			auto sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(g_wiping_dialog_button);
			g_wiping_dialog_button->Bind(wxEVT_BUTTON, ([parent](wxCommandEvent& e)
			{
				auto &config = g_PresetBundle->project_config;
                std::vector<double> init_matrix = (config.option<ConfigOptionFloats>("wiping_volumes_matrix"))->values;
                std::vector<double> init_extruders = (config.option<ConfigOptionFloats>("wiping_volumes_extruders"))->values;

                WipingDialog dlg(parent,std::vector<float>(init_matrix.begin(),init_matrix.end()),std::vector<float>(init_extruders.begin(),init_extruders.end()));

				if (dlg.ShowModal() == wxID_OK) {
                    std::vector<float> matrix = dlg.get_matrix();
                    std::vector<float> extruders = dlg.get_extruders();
                    (config.option<ConfigOptionFloats>("wiping_volumes_matrix"))->values = std::vector<double>(matrix.begin(),matrix.end());
                    (config.option<ConfigOptionFloats>("wiping_volumes_extruders"))->values = std::vector<double>(extruders.begin(),extruders.end());
                }
			}));
			return sizer;
		};
		m_optgroup->append_line(line);

	sizer->Add(m_optgroup->sizer, 1, wxEXPAND | wxBOTTOM, 2);
}

void show_frequently_changed_parameters(bool show)
{
	g_frequently_changed_parameters_sizer->Show(show);
	if (!show) return;

	for (size_t i = 0; i < g_wxTabPanel->GetPageCount(); ++i) {
		Tab *tab = dynamic_cast<Tab*>(g_wxTabPanel->GetPage(i));
		if (!tab)
			continue;
		tab->update_wiping_button_visibility();
		break;
	}
}

void show_buttons(bool show)
{
	g_btn_export_stl->Show(show);
	g_btn_reslice->Show(show);
	for (size_t i = 0; i < g_wxTabPanel->GetPageCount(); ++i) {
		TabPrinter *tab = dynamic_cast<TabPrinter*>(g_wxTabPanel->GetPage(i));
		if (!tab)
			continue;
		g_btn_print->Show(show && !tab->m_config->opt_string("serial_port").empty());
		g_btn_send_gcode->Show(show && !tab->m_config->opt_string("octoprint_host").empty());
		break;
	}
}

void show_scrolled_window_sizer(bool show)
{
	g_scrolled_window_sizer->Show(static_cast<size_t>(0), show);
	g_scrolled_window_sizer->Show(1, show);
	g_scrolled_window_sizer->Show(2, show && g_show_print_info);
}

void update_mode()
{
	wxWindowUpdateLocker noUpdates(g_plater);
	ConfigMenuIDs mode = get_view_mode();

	show_frequently_changed_parameters(mode >= ConfigMenuModeRegular);
	g_expert_mode_part_sizer->Show(mode == ConfigMenuModeExpert);
	show_scrolled_window_sizer(mode >= ConfigMenuModeRegular);
	show_buttons(mode >= ConfigMenuModeRegular);
	g_plater->Layout();
}

ConfigOptionsGroup* get_optgroup()
{
	return m_optgroup.get();
}


wxButton* get_wiping_dialog_button()
{
	return g_wiping_dialog_button;
}

wxWindow* export_option_creator(wxWindow* parent)
{
    wxPanel* panel = new wxPanel(parent, -1);
    wxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    wxCheckBox* cbox = new wxCheckBox(panel, wxID_HIGHEST + 1, L("Export print config"));
    cbox->SetValue(true);
    sizer->AddSpacer(5);
    sizer->Add(cbox, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);
    panel->SetSizer(sizer);
    sizer->SetSizeHints(panel);
    return panel;
}

void add_export_option(wxFileDialog* dlg, const std::string& format)
{
    if ((dlg != nullptr) && (format == "AMF") || (format == "3MF"))
    {
        if (dlg->SupportsExtraControl())
            dlg->SetExtraControlCreator(export_option_creator);
    }
}

int get_export_option(wxFileDialog* dlg)
{
    if (dlg != nullptr)
    {
        wxWindow* wnd = dlg->GetExtraControl();
        if (wnd != nullptr)
        {
            wxPanel* panel = dynamic_cast<wxPanel*>(wnd);
            if (panel != nullptr)
            {
                wxWindow* child = panel->FindWindow(wxID_HIGHEST + 1);
                if (child != nullptr)
                {
                    wxCheckBox* cbox = dynamic_cast<wxCheckBox*>(child);
                    if (cbox != nullptr)
                        return cbox->IsChecked() ? 1 : 0;
                }
            }
        }
    }

    return 0;

}

void about()
{
    AboutDialog dlg;
    dlg.ShowModal();
    dlg.Destroy();
}

} }
