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
#include <wx/settings.h>

#include "wxExtensions.hpp"

#include "Tab.hpp"
#include "TabIface.hpp"
#include "AppConfig.hpp"
#include "Utils.hpp"
#include "Preferences.hpp"
#include "PresetBundle.hpp"

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
wxColour    g_color_label_modified;
wxColour    g_color_label_sys;

std::vector<Tab *> g_tabs_list;

wxLocale*	g_wxLocale;

std::shared_ptr<ConfigOptionsGroup>	m_optgroup;
double m_brim_width = 0.0;
wxButton*	g_wiping_dialog_button = nullptr;

static void init_label_colours()
{
	auto luma = get_colour_approx_luma(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
	if (luma >= 128) {
		g_color_label_modified = wxColour(253, 88, 0);
		g_color_label_sys = wxColour(26, 132, 57);
	} else {
		g_color_label_modified = wxColour(253, 111, 40);
		g_color_label_sys = wxColour(115, 220, 103);
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
	long language;
	if (!g_AppConfig->has("translation_language"))
		language = wxLANGUAGE_UNKNOWN;
	else {
		auto str_language = g_AppConfig->get("translation_language");
		language = str_language != "" ? stol(str_language) : wxLANGUAGE_UNKNOWN;
	}

	if (language == wxLANGUAGE_UNKNOWN) 
		return false;
	wxArrayString	names;
	wxArrayLong		identifiers;
	get_installed_languages(names, identifiers);
	for (size_t i = 0; i < identifiers.Count(); i++)
	{
		if (identifiers[i] == language)
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
	long language = wxLANGUAGE_UNKNOWN;
	if (g_wxLocale)	{
		language = g_wxLocale->GetLanguage();
	}
	std::string str_language = std::to_string(language);
	g_AppConfig->set("translation_language", str_language);
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

void add_debug_menu(wxMenuBar *menu, int event_language_change)
{
//#if 0
    auto local_menu = new wxMenu();
	local_menu->Append(wxWindow::NewControlId(1), _(L("Change Application Language")));
	local_menu->Bind(wxEVT_MENU, [event_language_change](wxEvent&){
		wxArrayString names;
		wxArrayLong identifiers;
		get_installed_languages(names, identifiers);
		if (select_language(names, identifiers)){
			save_language();
			show_info(g_wxTabPanel, _(L("Application will be restarted")), _(L("Attention!")));
			if (event_language_change > 0) {
				wxCommandEvent event(event_language_change);
				g_wxApp->ProcessEvent(event);
			}
		}
	});
	menu->Append(local_menu, _(L("&Localization")));
//#endif
}

void open_preferences_dialog(int event_preferences)
{
	auto dlg = new PreferencesDialog(g_wxMainFrame, event_preferences);
	dlg->ShowModal();
}

void create_preset_tabs(bool no_controller, int event_value_change, int event_presets_changed)
{	
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

void show_error(wxWindow* parent, const wxString& message){
	auto msg_wingow = new wxMessageDialog(parent, message, _(L("Error")), wxOK | wxICON_ERROR);
	msg_wingow->ShowModal();
}

void show_info(wxWindow* parent, const wxString& message, const wxString& title){
	auto msg_wingow = new wxMessageDialog(parent, message, title.empty() ? _(L("Notice")) : title, wxOK | wxICON_INFORMATION);
	msg_wingow->ShowModal();
}

void warning_catcher(wxWindow* parent, const wxString& message){
	if (message == _(L("GLUquadricObjPtr | Attempt to free unreferenced scalar")) )
		return;
	auto msg = new wxMessageDialog(parent, message, _(L("Warning")), wxOK | wxICON_WARNING);
	msg->ShowModal();	
}

wxApp* get_app(){
	return g_wxApp;
}

const wxColour& get_modified_label_clr() {
	return g_color_label_modified;
}

const wxColour& get_sys_label_clr() {
	return g_color_label_sys;
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


void add_frequently_changed_parameters(wxWindow* parent, wxBoxSizer* sizer, wxFlexGridSizer* preset_sizer)
{
	DynamicPrintConfig*	config = &g_PresetBundle->prints.get_edited_preset().config;
	m_optgroup = std::make_shared<ConfigOptionsGroup>(parent, "", config);
	const wxArrayInt& ar = preset_sizer->GetColWidths();
	m_optgroup->label_width = ar.IsEmpty() ? 100 : ar.front();
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

	const int width = 250;
	Option option = m_optgroup->get_option("fill_density");
	option.opt.sidetext = "";
	option.opt.width = width;
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
	option.opt.width = width;
	m_optgroup->append_single_option_line(option);

	m_brim_width = config->opt_float("brim_width");
	def.label = L("Brim");
	def.type = coBool;
	def.tooltip = L("This flag enables the brim that will be printed around each object on the first layer.");
	def.gui_type = "";
	def.default_value = new ConfigOptionBool{ m_brim_width > 0.0 ? true : false };
	option = Option(def, "brim");
	m_optgroup->append_single_option_line(option);


    Line line = { _(L("")), "" };
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



	sizer->Add(m_optgroup->sizer, 0, wxEXPAND | wxBOTTOM | wxBottom, 1);
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


} // namespace GUI
} // namespace Slic3r
