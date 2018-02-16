#include "GUI.hpp"

#include <assert.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

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
#include <wx/config.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/frame.h>
#include <wx/menu.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/combo.h>
#include <wx/window.h>

#include "wxExtensions.hpp"

#include "Tab.hpp"
#include "TabIface.hpp"
#include "AppConfig.hpp"
#include "Utils.hpp"

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

std::vector<Tab *> g_tabs_list;

wxLocale*	g_wxLocale;

void set_wxapp(wxApp *app)
{
    g_wxApp = app;
}

void set_main_frame(wxFrame *main_frame)
{
    g_wxMainFrame = main_frame;
}

void set_tab_panel(wxNotebook *tab_panel)
{
    g_wxTabPanel = tab_panel;
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
		_L("Array of language names and identifiers should have the same size."));
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
	long index = wxGetSingleChoiceIndex(_L("Select the language"), _L("Language"), 
										names, init_selection);
	if (index != -1)
	{
		g_wxLocale = new wxLocale;
		g_wxLocale->Init(identifiers[index]);
		g_wxLocale->AddCatalogLookupPathPrefix(wxPathOnly(localization_dir()));
		g_wxLocale->AddCatalog(g_wxApp->GetAppName());
		return true;
	}
	return false;
}

bool load_language()
{
	wxConfig config(g_wxApp->GetAppName());
	long language;
	if (!config.Read(wxT("wxTranslation_Language"),
		&language, wxLANGUAGE_UNKNOWN))
	{
		language = wxLANGUAGE_UNKNOWN;
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
			return true;
		}
	}
	return false;
}

void save_language()
{
	wxConfig config(g_wxApp->GetAppName());
	long language = wxLANGUAGE_UNKNOWN;
	if (g_wxLocale)	{
		language = g_wxLocale->GetLanguage();
	}
	config.Write(wxT("wxTranslation_Language"), language);
	config.Flush();
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
		names.Add(_L("Default"));
		identifiers.Add(wxLANGUAGE_DEFAULT);
	}
	for (bool cont = dir.GetFirst(&filename, wxEmptyString, wxDIR_DIRS);
		cont; cont = dir.GetNext(&filename))
	{
		wxLogTrace(wxTraceMask(),
			"L10n: Directory found = \"%s\"",
			filename.GetData());
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
	local_menu->Append(wxWindow::NewControlId(1), _L("Change Application Language"));
	local_menu->Bind(wxEVT_MENU, [event_language_change](wxEvent&){
		wxArrayString names;
		wxArrayLong identifiers;
		get_installed_languages(names, identifiers);
		if (select_language(names, identifiers)){
			save_language();
			show_info(g_wxTabPanel, "Application will be restarted", "Attention!");
			if (event_language_change > 0) {
				wxCommandEvent event(event_language_change);
				g_wxApp->ProcessEvent(event);
			}
		}
	});
	menu->Append(local_menu, _T("&Localization"));
//#endif
}

void create_preset_tabs(PresetBundle *preset_bundle, AppConfig *app_config,
						bool no_controller, bool is_disabled_button_browse, bool is_user_agent,
						int event_value_change, int event_presets_changed,
						int event_button_browse, int event_button_test)
{	
	add_created_tab(new TabPrint	(g_wxTabPanel, no_controller), preset_bundle, app_config);
	add_created_tab(new TabFilament	(g_wxTabPanel, no_controller), preset_bundle, app_config);
	add_created_tab(new TabPrinter	(g_wxTabPanel, no_controller, is_disabled_button_browse, is_user_agent), 
					preset_bundle, app_config);
	for (size_t i = 0; i < g_wxTabPanel->GetPageCount(); ++ i) {
		Tab *tab = dynamic_cast<Tab*>(g_wxTabPanel->GetPage(i));
		if (! tab)
			continue;
		tab->set_event_value_change(wxEventType(event_value_change));
		tab->set_event_presets_changed(wxEventType(event_presets_changed));
		if (tab->name() == "printer"){
			TabPrinter* tab_printer = static_cast<TabPrinter*>(tab);
			tab_printer->set_event_button_browse(wxEventType(event_button_browse));
			tab_printer->set_event_button_test(wxEventType(event_button_test));
		}
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
void change_opt_value(DynamicPrintConfig& config, t_config_option_key opt_key, boost::any value, int opt_index /*= 0*/)
{
	try{
		switch (config.def()->get(opt_key)->type){
		case coFloatOrPercent:{
			const auto &val = *config.option<ConfigOptionFloatOrPercent>(opt_key);
			config.set_key_value(opt_key, new ConfigOptionFloatOrPercent(boost::any_cast<double>(value), val.percent));
			break;}
		case coPercent:
			config.set_key_value(opt_key, new ConfigOptionPercent(boost::any_cast<double>(value)));
			break;
		case coFloat:{
			double& val = config.opt_float(opt_key);
			val = boost::any_cast<double>(value);
			break;
		}
		case coPercents:
		case coFloats:{
			double& val = config.opt_float(opt_key, 0);
			val = boost::any_cast<double>(value);
			break;
		}			
		case coString:
			config.set_key_value(opt_key, new ConfigOptionString(boost::any_cast<std::string>(value)));
			break;
		case coStrings:{
			if (opt_key.compare("compatible_printers") == 0){
				config.option<ConfigOptionStrings>(opt_key)->values.resize(0);
				for (auto el : boost::any_cast<std::vector<std::string>>(value))
					config.option<ConfigOptionStrings>(opt_key)->values.push_back(el);
			}
			else{
				ConfigOptionStrings* vec_new = new ConfigOptionStrings{ boost::any_cast<std::string>(value) };
				config.option<ConfigOptionStrings>(opt_key)->set_at(vec_new, opt_index, opt_index);
			}
			}
			break;
		case coBool:
			config.set_key_value(opt_key, new ConfigOptionBool(boost::any_cast<bool>(value)));
			break;
		case coBools:{
			ConfigOptionBools* vec_new = new ConfigOptionBools{ boost::any_cast<bool>(value) };
			config.option<ConfigOptionBools>(opt_key)->set_at(vec_new, opt_index, opt_index);
			break;}
		case coInt:
			config.set_key_value(opt_key, new ConfigOptionInt(boost::any_cast<int>(value)));
			break;
		case coInts:{
			ConfigOptionInts* vec_new = new ConfigOptionInts{ boost::any_cast<int>(value) };
			config.option<ConfigOptionInts>(opt_key)->set_at(vec_new, opt_index, opt_index);
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
			ConfigOptionPoints points;
			points.values = boost::any_cast<std::vector<Pointf>>(value);
			config.set_key_value(opt_key, new ConfigOptionPoints(points));
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

void add_created_tab(Tab* panel, PresetBundle *preset_bundle, AppConfig *app_config)
{
	panel->m_show_btn_incompatible_presets = app_config->get("show_incompatible_presets").empty();
	panel->create_preset_tab(preset_bundle);

	// Load the currently selected preset into the GUI, update the preset selection box.
	panel->load_current_preset();
	g_wxTabPanel->AddPage(panel, panel->title());
}

void show_error(wxWindow* parent, wxString message){
	auto msg_wingow = new wxMessageDialog(parent, message, _L("Error"), wxOK | wxICON_ERROR);
	msg_wingow->ShowModal();
}

void show_info(wxWindow* parent, wxString message, wxString title){
	auto msg_wingow = new wxMessageDialog(parent, message, title.empty() ? _L("Notice") : title, wxOK | wxICON_INFORMATION);
	msg_wingow->ShowModal();
}

wxApp* get_app(){
	return g_wxApp;
}

void create_combochecklist(wxComboCtrl* comboCtrl, std::string text, std::string items, bool initial_value)
{
    if (comboCtrl == nullptr)
        return;

    wxCheckListBoxComboPopup* popup = new wxCheckListBoxComboPopup;
    if (popup != nullptr)
    {
        comboCtrl->SetPopupControl(popup);
        popup->SetStringValue(text);
        popup->Connect(wxID_ANY, wxEVT_CHECKLISTBOX, wxCommandEventHandler(wxCheckListBoxComboPopup::OnCheckListBox), nullptr, popup);
        popup->Connect(wxID_ANY, wxEVT_LISTBOX, wxCommandEventHandler(wxCheckListBoxComboPopup::OnListBoxSelection), nullptr, popup);

        std::vector<std::string> items_str;
        boost::split(items_str, items, boost::is_any_of("|"), boost::token_compress_off);

        for (const std::string& item : items_str)
        {
            popup->Append(item);
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

} }
