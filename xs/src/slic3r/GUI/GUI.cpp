#include "GUI.hpp"
#include "WipeTowerDialog.hpp"

#include <assert.h>
#include <cmath>

#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
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
#include <wx/display.h>
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
#include "FirmwareDialog.hpp"
#include "GUI_ObjectParts.hpp"

#include "../Utils/PresetUpdater.hpp"
#include "../Config/Snapshot.hpp"
#include "Model.hpp"


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

std::vector <std::shared_ptr<ConfigOptionsGroup>> m_optgroups;
double		m_brim_width = 0.0;
size_t		m_label_width = 100;
wxButton*	g_wiping_dialog_button = nullptr;

//showed/hided controls according to the view mode
wxWindow	*g_right_panel = nullptr;
wxBoxSizer	*g_frequently_changed_parameters_sizer = nullptr;
wxBoxSizer	*g_expert_mode_part_sizer = nullptr;
wxBoxSizer	*g_scrolled_window_sizer = nullptr;
wxButton	*g_btn_export_gcode = nullptr;
wxButton	*g_btn_export_stl = nullptr;
wxButton	*g_btn_reslice = nullptr;
wxButton	*g_btn_print = nullptr;
wxButton	*g_btn_send_gcode = nullptr;
wxStaticBitmap	*g_manifold_warning_icon = nullptr;
bool		g_show_print_info = false;
bool		g_show_manifold_warning_icon = false;
wxSizer		*m_sizer_object_buttons = nullptr;
wxSizer		*m_sizer_part_buttons = nullptr;
wxSizer		*m_sizer_object_movers = nullptr;
wxDataViewCtrl			*m_objects_ctrl = nullptr;
MyObjectTreeModel		*m_objects_model = nullptr;
wxCollapsiblePane		*m_collpane_settings = nullptr;
int			m_event_object_selection_changed = 0;
int			m_event_object_settings_changed = 0;
bool		g_prevent_list_events = false;		// We use this flag to avoid circular event handling Select() 
												// happens to fire a wxEVT_LIST_ITEM_SELECTED on OSX, whose event handler 
												// calls this method again and again and again
ModelObjectPtrs			m_objects;

wxFont		g_small_font;
wxFont		g_bold_font;

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

static void init_fonts()
{
	g_small_font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
	g_bold_font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Bold();
#ifdef __WXMAC__
	g_small_font.SetPointSize(11);
	g_bold_font.SetPointSize(11);
#endif /*__WXMAC__*/
}

void set_wxapp(wxApp *app)
{
    g_wxApp = app;
    init_label_colours();
	init_fonts();
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
							wxButton *btn_export_gcode,
							wxButton *btn_export_stl, wxButton *btn_reslice, 
							wxButton *btn_print, wxButton *btn_send_gcode,
							wxStaticBitmap *manifold_warning_icon)
{
	g_right_panel = parent;
	g_frequently_changed_parameters_sizer = frequently_changed_parameters_sizer;
	g_expert_mode_part_sizer = expert_mode_part_sizer;
	g_scrolled_window_sizer = scrolled_window_sizer;
	g_btn_export_gcode = btn_export_gcode;
	g_btn_export_stl = btn_export_stl;
	g_btn_reslice = btn_reslice;
	g_btn_print = btn_print;
	g_btn_send_gcode = btn_send_gcode;
	g_manifold_warning_icon = manifold_warning_icon;
}

void set_show_print_info(bool show)
{
	g_show_print_info = show;
}

void set_show_manifold_warning_icon(bool show)
{
	g_show_manifold_warning_icon = show;
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
	ConfigMenuModeExpert,
	ConfigMenuLanguage,
	ConfigMenuFlashFirmware,
	ConfigMenuCnt,
};
	
ConfigMenuIDs get_view_mode()
{
	if (!g_AppConfig->has("view_mode"))
		return ConfigMenuModeSimple;

	const auto mode = g_AppConfig->get("view_mode");
	return mode == "expert" ? ConfigMenuModeExpert : ConfigMenuModeSimple;
}

static wxString dots("…", wxConvUTF8);

void add_config_menu(wxMenuBar *menu, int event_preferences_changed, int event_language_change)
{
    auto local_menu = new wxMenu();
    wxWindowID config_id_base = wxWindow::NewControlId((int)ConfigMenuCnt);

    const auto config_wizard_tooltip = wxString::Format(_(L("Run %s")), ConfigWizard::name());
    // Cmd+, is standard on OS X - what about other operating systems?
   	local_menu->Append(config_id_base + ConfigMenuWizard, 		ConfigWizard::name() + dots, 			config_wizard_tooltip);
   	local_menu->Append(config_id_base + ConfigMenuSnapshots, 	_(L("Configuration Snapshots"))+dots,	_(L("Inspect / activate configuration snapshots")));
   	local_menu->Append(config_id_base + ConfigMenuTakeSnapshot, _(L("Take Configuration Snapshot")), 		_(L("Capture a configuration snapshot")));
// 	local_menu->Append(config_id_base + ConfigMenuUpdate, 		_(L("Check for updates")), 					_(L("Check for configuration updates")));
   	local_menu->AppendSeparator();
   	local_menu->Append(config_id_base + ConfigMenuPreferences, 	_(L("Preferences"))+dots+"\tCtrl+,", 		_(L("Application preferences")));
	local_menu->AppendSeparator();
	auto mode_menu = new wxMenu();
	mode_menu->AppendRadioItem(config_id_base + ConfigMenuModeSimple,	_(L("&Simple")),					_(L("Simple View Mode")));
	mode_menu->AppendRadioItem(config_id_base + ConfigMenuModeExpert,	_(L("&Expert")),					_(L("Expert View Mode")));
	mode_menu->Check(config_id_base + get_view_mode(), true);
	local_menu->AppendSubMenu(mode_menu,						_(L("&Mode")), 								_(L("Slic3r View Mode")));
   	local_menu->AppendSeparator();
	local_menu->Append(config_id_base + ConfigMenuLanguage,		_(L("Change Application Language")));
	local_menu->AppendSeparator();
	local_menu->Append(config_id_base + ConfigMenuFlashFirmware, _(L("Flash printer firmware")), _(L("Upload a firmware image into an Arduino based printer")));
	// TODO: for when we're able to flash dictionaries
	// local_menu->Append(config_id_base + FirmwareMenuDict,  _(L("Flash language file")),    _(L("Upload a language dictionary file into a Prusa printer")));

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
					load_current_presets();
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
		case ConfigMenuFlashFirmware:
			FirmwareDialog::run(g_wxMainFrame);
			break;
		default:
			break;
		}
	});
	mode_menu->Bind(wxEVT_MENU, [config_id_base](wxEvent& event) {
		std::string mode =	event.GetId() - config_id_base == ConfigMenuModeExpert ?
							"expert" : "simple";
		g_AppConfig->set("view_mode", mode);
		g_AppConfig->save();
		update_mode();
	});
	menu->Append(local_menu, _(L("&Configuration")));
}

void add_menus(wxMenuBar *menu, int event_preferences_changed, int event_language_change)
{
	add_config_menu(menu, event_preferences_changed, event_language_change);
}

void open_model(wxWindow *parent, wxArrayString& input_files){
	t_file_wild_card vec_FILE_WILDCARDS = get_file_wild_card();
	std::vector<std::string> file_types = { "known", "stl", "obj", "amf", "3mf", "prusa" };
	wxString MODEL_WILDCARD;
	for (auto file_type : file_types)
		MODEL_WILDCARD += vec_FILE_WILDCARDS.at(file_type) + "|";

	auto dlg_title = _(L("Choose one or more files (STL/OBJ/AMF/3MF/PRUSA):"));
	auto dialog = new wxFileDialog(parent /*? parent : GetTopWindow(g_wxMainFrame)*/, dlg_title, 
		g_AppConfig->get_last_dir(), "",
		MODEL_WILDCARD, wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
	if (dialog->ShowModal() != wxID_OK) {
		dialog->Destroy();
		return ;
	}
	
	dialog->GetPaths(input_files);
	dialog->Destroy();
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

	try {
		ConfigWizard wizard(nullptr, static_cast<ConfigWizard::RunReason>(reason));
		wizard.run(g_PresetBundle, g_PresetUpdater);
	}
	catch (const std::exception &e) {
		show_error(nullptr, e.what());
	}

	// Load the currently selected preset into the GUI, update the preset selection box.
	load_current_presets();
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

void load_current_presets()
{
	for (Tab *tab : g_tabs_list) {
		tab->load_current_preset();
	}
}

void show_error(wxWindow* parent, const wxString& message) {
	ErrorDialog msg(parent, message);
	msg.ShowModal();
}

void show_error_id(int id, const std::string& message) {
	auto *parent = id != 0 ? wxWindow::FindWindowById(id) : nullptr;
	show_error(parent, wxString::FromUTF8(message.data()));
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

const wxFont& small_font(){
	return g_small_font;
}

const wxFont& bold_font(){
	return g_bold_font;
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
wxDataViewCtrl*		get_objects_ctrl() {
	return m_objects_ctrl;
}
MyObjectTreeModel*	get_objects_model() {
	return m_objects_model;
}

ModelObjectPtrs& get_objects() {
	return m_objects;
}

const int& get_event_object_settings_changed() {
	return m_event_object_settings_changed;
}

wxFrame* get_main_frame() {
	return g_wxMainFrame;
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

// add Collapsible Pane to sizer
wxCollapsiblePane* add_collapsible_pane(wxWindow* parent, wxBoxSizer* sizer_parent, const wxString& name, std::function<wxSizer *(wxWindow *)> content_function)
{
#ifdef __WXMSW__
	auto *collpane = new PrusaCollapsiblePaneMSW(parent, wxID_ANY, name);
#else
	auto *collpane = new PrusaCollapsiblePane/*wxCollapsiblePane*/(parent, wxID_ANY, name);
#endif // __WXMSW__
	// add the pane with a zero proportion value to the sizer which contains it
	sizer_parent->Add(collpane, 0, wxGROW | wxALL, 0);

	wxWindow *win = collpane->GetPane();

	wxSizer *sizer = content_function(win);

	wxSizer *sizer_pane = new wxBoxSizer(wxVERTICAL);
	sizer_pane->Add(sizer, 1, wxGROW | wxEXPAND | wxBOTTOM, 2);
	win->SetSizer(sizer_pane);
// 	sizer_pane->SetSizeHints(win);
	return collpane;
}

wxBoxSizer* content_objects_list(wxWindow *win)
{
	m_objects_ctrl = new wxDataViewCtrl(win, wxID_ANY, wxDefaultPosition, wxDefaultSize);
	m_objects_ctrl->SetInitialSize(wxSize(-1, 150)); // TODO - Set correct height according to the opened/closed objects
	auto objects_sz = new wxBoxSizer(wxVERTICAL);
	objects_sz->Add(m_objects_ctrl, 1, wxGROW | wxLEFT, 20);

	m_objects_model = new MyObjectTreeModel;
	m_objects_ctrl->AssociateModel(m_objects_model);
#if wxUSE_DRAG_AND_DROP && wxUSE_UNICODE
	m_objects_ctrl->EnableDragSource(wxDF_UNICODETEXT);
	m_objects_ctrl->EnableDropTarget(wxDF_UNICODETEXT);
#endif // wxUSE_DRAG_AND_DROP && wxUSE_UNICODE

	// column 0 of the view control:

	wxDataViewTextRenderer *tr = new wxDataViewTextRenderer("string", wxDATAVIEW_CELL_INERT);
	wxDataViewColumn *column00 = new wxDataViewColumn("Name", tr, 0, 110, wxALIGN_LEFT,
		wxDATAVIEW_COL_SORTABLE | wxDATAVIEW_COL_RESIZABLE);
	m_objects_ctrl->AppendColumn(column00);

	// column 1 of the view control:

	tr = new wxDataViewTextRenderer("string", wxDATAVIEW_CELL_INERT);
	wxDataViewColumn *column01 = new wxDataViewColumn("Copy", tr, 1, 75, wxALIGN_CENTER_HORIZONTAL,
		wxDATAVIEW_COL_SORTABLE | wxDATAVIEW_COL_RESIZABLE);
	m_objects_ctrl->AppendColumn(column01);

	// column 2 of the view control:

	tr = new wxDataViewTextRenderer("string", wxDATAVIEW_CELL_INERT);
	wxDataViewColumn *column02 = new wxDataViewColumn("Scale", tr, 2, 80, wxALIGN_CENTER_HORIZONTAL,
		wxDATAVIEW_COL_SORTABLE | wxDATAVIEW_COL_RESIZABLE);
	m_objects_ctrl->AppendColumn(column02);

	m_objects_ctrl->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [](wxEvent& event)
	{
		if (g_prevent_list_events) return;

		wxWindowUpdateLocker noUpdates(g_right_panel);
		auto item = m_objects_ctrl->GetSelection();
		int obj_idx = -1;
		if (!item) 
			unselect_objects();
		else
		{
			if (m_objects_model->GetParent(item) == wxDataViewItem(0))
				obj_idx = m_objects_model->GetIdByItem(item);
			else {
				auto parent = m_objects_model->GetParent(item);
				obj_idx = m_objects_model->GetIdByItem(parent); // TODO Temporary decision for sub-objects selection
			}
		}

		if (m_event_object_selection_changed > 0) {
			wxCommandEvent event(m_event_object_selection_changed);
			event.SetInt(obj_idx);
			g_wxMainFrame->ProcessWindowEvent(event);
		}

		if (obj_idx < 0) return;

//		m_objects_ctrl->SetSize(m_objects_ctrl->GetBestSize()); // TODO override GetBestSize(), than use it

		auto show_obj_sizer = m_objects_model->GetParent(item) == wxDataViewItem(0);
		m_sizer_object_buttons->Show(show_obj_sizer);
		m_sizer_part_buttons->Show(!show_obj_sizer);
		m_collpane_settings->SetLabelText((show_obj_sizer ? _(L("Object Settings")) : _(L("Part Settings"))) + ":");
		m_collpane_settings->Show(true);
	});

	m_objects_ctrl->Bind(wxEVT_KEY_DOWN, [](wxKeyEvent& event)
	{
		if (event.GetKeyCode() == WXK_TAB)
			m_objects_ctrl->Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
		else
			event.Skip();
	});

	return objects_sz;
}

wxBoxSizer* content_edit_object_buttons(wxWindow* win)
{
	auto sizer = new wxBoxSizer(wxVERTICAL);

	auto btn_load_part = new wxButton(win, wxID_ANY, /*Load */"part"+dots, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER/*wxBU_LEFT*/);
	auto btn_load_modifier = new wxButton(win, wxID_ANY, /*Load */"modifier" + dots, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER/*wxBU_LEFT*/);
	auto btn_load_lambda_modifier = new wxButton(win, wxID_ANY, /*Load */"generic" + dots, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER/*wxBU_LEFT*/);
    auto btn_delete = new wxButton(win, wxID_ANY, "Delete"/*" part"*/, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER/*wxBU_LEFT*/);
    auto btn_split = new wxButton(win, wxID_ANY, "Split"/*" part"*/, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER/*wxBU_LEFT*/);
	auto btn_move_up = new wxButton(win, wxID_ANY, "", wxDefaultPosition, wxDefaultSize/*wxSize(30, -1)*/, wxBU_LEFT);
	auto btn_move_down = new wxButton(win, wxID_ANY, "", wxDefaultPosition, wxDefaultSize/*wxSize(30, -1)*/, wxBU_LEFT);

	//*** button's functions
	btn_load_part->Bind(wxEVT_BUTTON, [win](wxEvent&)
	{
		on_btn_load(win);
	});

	btn_load_modifier->Bind(wxEVT_BUTTON, [win](wxEvent&)
	{
		on_btn_load(win, true);
	});

	btn_delete->Bind(wxEVT_BUTTON, [](wxEvent&)
	{
		auto item = m_objects_ctrl->GetSelection();
		if (!item) return;
		m_objects_ctrl->Select(m_objects_model->Delete(item));
	});
	//***

	btn_move_up->SetMinSize(wxSize(20, -1));
	btn_move_down->SetMinSize(wxSize(20, -1));
	btn_load_part->SetBitmap(wxBitmap(from_u8(Slic3r::var("brick_add.png")), wxBITMAP_TYPE_PNG));
    btn_load_modifier->SetBitmap(wxBitmap(from_u8(Slic3r::var("brick_add.png")), wxBITMAP_TYPE_PNG));
    btn_load_lambda_modifier->SetBitmap(wxBitmap(from_u8(Slic3r::var("brick_add.png")), wxBITMAP_TYPE_PNG));
    btn_delete->SetBitmap(wxBitmap(from_u8(Slic3r::var("brick_delete.png")), wxBITMAP_TYPE_PNG));
    btn_split->SetBitmap(wxBitmap(from_u8(Slic3r::var("shape_ungroup.png")), wxBITMAP_TYPE_PNG));
    btn_move_up->SetBitmap(wxBitmap(from_u8(Slic3r::var("bullet_arrow_up.png")), wxBITMAP_TYPE_PNG));
    btn_move_down->SetBitmap(wxBitmap(from_u8(Slic3r::var("bullet_arrow_down.png")), wxBITMAP_TYPE_PNG));

	m_sizer_object_buttons = new wxGridSizer(1, 3, 0, 0);
	m_sizer_object_buttons->Add(btn_load_part, 0, wxEXPAND);
	m_sizer_object_buttons->Add(btn_load_modifier, 0, wxEXPAND);
	m_sizer_object_buttons->Add(btn_load_lambda_modifier, 0, wxEXPAND);
	m_sizer_object_buttons->Show(false);

	m_sizer_part_buttons = new wxGridSizer(1, 3, 0, 0);
	m_sizer_part_buttons->Add(btn_delete, 0, wxEXPAND);
	m_sizer_part_buttons->Add(btn_split, 0, wxEXPAND);
	{
		auto up_down_sizer = new wxGridSizer(1, 2, 0, 0);
		up_down_sizer->Add(btn_move_up, 1, wxEXPAND);
		up_down_sizer->Add(btn_move_down, 1, wxEXPAND);
		m_sizer_part_buttons->Add(up_down_sizer, 0, wxEXPAND);
	}
	m_sizer_part_buttons->Show(false);

	btn_load_part->SetFont(Slic3r::GUI::small_font());
	btn_load_modifier->SetFont(Slic3r::GUI::small_font());
	btn_load_lambda_modifier->SetFont(Slic3r::GUI::small_font());
	btn_delete->SetFont(Slic3r::GUI::small_font());
	btn_split->SetFont(Slic3r::GUI::small_font());
	btn_move_up->SetFont(Slic3r::GUI::small_font());
	btn_move_down->SetFont(Slic3r::GUI::small_font());

	sizer->Add(m_sizer_object_buttons, 0, wxEXPAND|wxLEFT, 20);
	sizer->Add(m_sizer_part_buttons, 0, wxEXPAND|wxLEFT, 20);
	return sizer;
}

wxSizer* object_movers(wxWindow *win)
{
	DynamicPrintConfig* config = &g_PresetBundle->/*full_config();//*/printers.get_edited_preset().config; // TODO get config from Model_volume
	std::shared_ptr<ConfigOptionsGroup> optgroup = std::make_shared<ConfigOptionsGroup>(win, "Move", config);
 	optgroup->label_width = 20;

	ConfigOptionDef def;
	def.label = L("X");
	def.type = coInt;
	def.gui_type = "slider";
	def.default_value = new ConfigOptionInt(0);
// 	def.min = -(model_object->bounding_box->size->x) * 4;
// 	def.max =  model_object->bounding_box->size->x * 4;

	Option option = Option(def, "x");
	option.opt.full_width = true;
	optgroup->append_single_option_line(option);

	def.label = L("Y");
// 	def.min = -(model_object->bounding_box->size->y) * 4;
// 	def.max =  model_object->bounding_box->size->y * 4;
	option = Option(def, "y");
	optgroup->append_single_option_line(option);
	
	def.label = L("Z");
// 	def.min = -(model_object->bounding_box->size->z) * 4;
// 	def.max =  model_object->bounding_box->size->z * 4;
	option = Option(def, "z");
	optgroup->append_single_option_line(option);

	m_optgroups.push_back(optgroup);  // ogObjectMovers
	m_sizer_object_movers = optgroup->sizer;
	m_sizer_object_movers->Show(false);
	return optgroup->sizer;
}

Line add_og_to_object_settings(const std::string& option_name, const std::string& sidetext, int def_value=0)
{
	Line line = { _(option_name), "" };
	ConfigOptionDef def;

	def.label = L("X");
	def.type = coInt;
	def.default_value = new ConfigOptionInt(def_value);
	def.sidetext = sidetext;
 	def.width = 70;

	const std::string lower_name = boost::algorithm::to_lower_copy(option_name);

	Option option = Option(def, lower_name + "_X");
	option.opt.full_width = true;
	line.append_option(option);

	def.label = L("Y");
	option = Option(def, lower_name + "_Y");
	line.append_option(option);

	def.label = L("Z");
	option = Option(def, lower_name + "_Z");
	line.append_option(option);

	if (option_name == "Scale")
	{
		def.label = L("Units");
		def.type = coStrings;
		def.gui_type = "select_open";
		def.enum_labels.push_back(L("%"));
		def.enum_labels.push_back(L("mm"));
		def.default_value = new ConfigOptionStrings{ "%" };
		def.sidetext = " ";

		option = Option(def, lower_name + "_unit");
		line.append_option(option);
	}
	return line;
}

wxBoxSizer* content_settings(wxWindow *win)
{
	DynamicPrintConfig* config = &g_PresetBundle->/*full_config();//*/printers.get_edited_preset().config; // TODO get config from Model_volume
	std::shared_ptr<ConfigOptionsGroup> optgroup = std::make_shared<ConfigOptionsGroup>(win, "Extruders", config);
	optgroup->label_width = m_label_width;

	Option option = optgroup->get_option("extruder");
	option.opt.default_value = new ConfigOptionInt(1);
	optgroup->append_single_option_line(option);

	m_optgroups.push_back(optgroup);  // ogObjectSettings

	auto sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(content_edit_object_buttons(win), 0, wxEXPAND, 0); // *** Edit Object Buttons***

	sizer->Add(optgroup->sizer, 1, wxEXPAND | wxLEFT, 20);

	auto add_btn = new wxButton(win, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER);
	if (wxMSW) add_btn->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
	add_btn->SetBitmap(wxBitmap(from_u8(Slic3r::var("add.png")), wxBITMAP_TYPE_PNG));
	sizer->Add(add_btn, 0, wxALIGN_LEFT | wxLEFT, 20);

	sizer->Add(object_movers(win), 0, wxEXPAND | wxLEFT, 20);

	return sizer;
}

void add_object_to_list(const std::string &name, ModelObject* model_object)
{
	wxString item = name;
	int scale = model_object->instances[0]->scaling_factor * 100;
	m_objects_ctrl->Select(m_objects_model->Add(item, model_object->instances.size(), scale));
	m_objects.push_back(model_object);
}

void delete_object_from_list()
{
	auto item = m_objects_ctrl->GetSelection();
	if (!item || m_objects_model->GetParent(item) != wxDataViewItem(0)) 
		return;
// 	m_objects_ctrl->Select(m_objects_model->Delete(item));
	m_objects_model->Delete(item);

	if (m_objects_model->IsEmpty())
		m_collpane_settings->Show(false);
}

void delete_all_objects_from_list()
{
	m_objects_model->DeleteAll();
	m_collpane_settings->Show(false);
}

void set_object_count(int idx, int count)
{
	m_objects_model->SetValue(wxString::Format("%d", count), idx, 1);
	m_objects_ctrl->Refresh();
}

void set_object_scale(int idx, int scale)
{
	m_objects_model->SetValue(wxString::Format("%d%%", scale), idx, 2);
	m_objects_ctrl->Refresh();
}

void unselect_objects()
{
	m_objects_ctrl->UnselectAll();
	if (m_sizer_object_buttons->IsShown(1)) 
		m_sizer_object_buttons->Show(false);
	if (m_sizer_part_buttons->IsShown(1)) 
		m_sizer_part_buttons->Show(false);
	if (m_sizer_object_movers->IsShown(1)) 
		m_sizer_object_movers->Show(false);
	if (m_collpane_settings->IsShown())
		m_collpane_settings->Show(false);
}

void select_current_object(int idx)
{
	g_prevent_list_events = true;
	m_objects_ctrl->UnselectAll();
	if (idx < 0) {
		g_prevent_list_events = false;
		return;
	}
	m_objects_ctrl->Select(m_objects_model->GetItemById(idx));
	g_prevent_list_events = false;

	if (get_view_mode() == ConfigMenuModeExpert){
		if (!m_sizer_object_buttons->IsShown(1))
			m_sizer_object_buttons->Show(true);
		if (!m_sizer_object_movers->IsShown(1))
			m_sizer_object_movers->Show(true);
		if (!m_collpane_settings->IsShown())
			m_collpane_settings->Show(true);
	}
}

void add_expert_mode_part(	wxWindow* parent, wxBoxSizer* sizer, 
							int event_object_selection_changed,
							int event_object_settings_changed)
{
	m_event_object_selection_changed = event_object_selection_changed;
	m_event_object_settings_changed = event_object_settings_changed;
	wxWindowUpdateLocker noUpdates(parent);

	// *** Objects List ***	
 	auto collpane = add_collapsible_pane(parent, sizer, "Objects List:", content_objects_list);
	collpane->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED, ([collpane](wxCommandEvent& e){
// 		wxWindowUpdateLocker noUpdates(g_right_panel);
		if (collpane->IsCollapsed()) {
			m_sizer_object_buttons->Show(false);
			m_sizer_part_buttons->Show(false);
			m_sizer_object_movers->Show(false);
			m_collpane_settings->Show(false);
		}
// 		else 
// 			m_objects_ctrl->UnselectAll();
		
// 		e.Skip();
//		g_right_panel->Layout();
	}));

	// *** Object/Part Settings ***
	m_collpane_settings = add_collapsible_pane(parent, sizer, "Object Settings", content_settings);

	// More experiments with UI
// 	auto listctrl = new wxDataViewListCtrl(main_page, wxID_ANY, wxDefaultPosition, wxSize(-1, 100));
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
// 	main_sizer->Add(listctrl, 0, wxEXPAND | wxALL, 1);
}

void add_frequently_changed_parameters(wxWindow* parent, wxBoxSizer* sizer, wxFlexGridSizer* preset_sizer)
{
	DynamicPrintConfig*	config = &g_PresetBundle->prints.get_edited_preset().config;
	std::shared_ptr<ConfigOptionsGroup> optgroup = std::make_shared<ConfigOptionsGroup>(parent, "", config);
	const wxArrayInt& ar = preset_sizer->GetColWidths();
	m_label_width = ar.IsEmpty() ? 100 : ar.front()-4;
	optgroup->label_width = m_label_width;
	optgroup->m_on_change = [config](t_config_option_key opt_key, boost::any value){
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
			value = m_optgroups[ogFrequentlyChangingParameters]->get_config_value(*config, opt_key);
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

	Option option = optgroup->get_option("fill_density");
	option.opt.sidetext = "";
	option.opt.full_width = true;
	optgroup->append_single_option_line(option);

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
	optgroup->append_single_option_line(option);

	m_brim_width = config->opt_float("brim_width");
	def.label = L("Brim");
	def.type = coBool;
	def.tooltip = L("This flag enables the brim that will be printed around each object on the first layer.");
	def.gui_type = "";
	def.default_value = new ConfigOptionBool{ m_brim_width > 0.0 ? true : false };
	option = Option(def, "brim");
	optgroup->append_single_option_line(option);


    Line line = { "", "" };
        line.widget = [config](wxWindow* parent){
			g_wiping_dialog_button = new wxButton(parent, wxID_ANY, _(L("Purging volumes")) + dots, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
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
		optgroup->append_line(line);

	sizer->Add(optgroup->sizer, 0, wxEXPAND | wxBOTTOM, 2);

	m_optgroups.push_back(optgroup);// ogFrequentlyChangingParameters

	// Frequently Object Settings
	optgroup = std::make_shared<ConfigOptionsGroup>(parent, _(L("Object Settings")), config);
 	optgroup->label_width = 100;
	optgroup->set_grid_vgap(5);

	def.label = L("Name");
	def.type = coString;
	def.tooltip = L("Object name");
	def.full_width = true;
	def.default_value = new ConfigOptionString{ "BlaBla_object.stl" };
	optgroup->append_single_option_line(Option(def, "object_name"));

	optgroup->set_flag(ogSIDE_OPTIONS_VERTICAL);
	optgroup->sidetext_width = 25;
	
	optgroup->append_line(add_og_to_object_settings(L("Position"), L("mm")));
	optgroup->append_line(add_og_to_object_settings(L("Rotation"), "°", 1));
	optgroup->append_line(add_og_to_object_settings(L("Scale"), "%", 2));

	optgroup->set_flag(ogDEFAULT);

	def.label = L("Place on bed");
	def.type = coBool;
	def.tooltip = L("Automatic placing of models on printing bed in Y axis");
	def.gui_type = "";
	def.sidetext = "";
	def.default_value = new ConfigOptionBool{ false };
	optgroup->append_single_option_line(Option(def, "place_on_bed"));

	sizer->Add(optgroup->sizer, 0, wxEXPAND | wxLEFT, 20);

	m_optgroups.push_back(optgroup);  // ogFrequentlyObjectSettings
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

void show_info_sizer(bool show)
{
	g_scrolled_window_sizer->Show(static_cast<size_t>(0), show); 
	g_scrolled_window_sizer->Show(1, show && g_show_print_info);
	g_manifold_warning_icon->Show(show && g_show_manifold_warning_icon);
}

void update_mode()
{
	wxWindowUpdateLocker noUpdates(g_right_panel);

	// TODO There is a not the best place of it!
	//*** Update style of the "Export G-code" button****
	if (g_btn_export_gcode->GetFont() != bold_font()){
		g_btn_export_gcode->SetBackgroundColour(wxColour(252, 77, 1));
		g_btn_export_gcode->SetFont(bold_font());
	}
	// ***********************************

	ConfigMenuIDs mode = get_view_mode();

// 	show_frequently_changed_parameters(mode >= ConfigMenuModeRegular);
	g_expert_mode_part_sizer->Show(mode == ConfigMenuModeExpert);
	show_info_sizer(mode == ConfigMenuModeExpert);
	show_buttons(mode == ConfigMenuModeExpert);

	// TODO There is a not the best place of it!
	// *** Update showing of the collpane_settings
	m_collpane_settings->Show(mode == ConfigMenuModeExpert && !m_objects_model->IsEmpty());
	// *************************
	g_right_panel->GetParent()->Layout();
	g_right_panel->Layout();
}

ConfigOptionsGroup* get_optgroup(size_t i)
{
	return m_optgroups[i].get();
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

void get_current_screen_size(unsigned &width, unsigned &height)
{
	wxDisplay display(wxDisplay::GetFromWindow(g_wxMainFrame));
	const auto disp_size = display.GetClientArea();
	width = disp_size.GetWidth();
	height = disp_size.GetHeight();
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

} }
