#include "Preferences.hpp"
#include "OptionsGroup.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "MsgDialog.hpp"
#include "I18N.hpp"
#include "libslic3r/AppConfig.hpp"
#include <wx/notebook.h>
#include "Notebook.hpp"
#include "ButtonsDescription.hpp"
#include "OG_CustomCtrl.hpp"
#include <initializer_list>

namespace Slic3r {

	static t_config_enum_names enum_names_from_keys_map(const t_config_enum_values& enum_keys_map)
	{
		t_config_enum_names names;
		int cnt = 0;
		for (const auto& kvp : enum_keys_map)
			cnt = std::max(cnt, kvp.second);
		cnt += 1;
		names.assign(cnt, "");
		for (const auto& kvp : enum_keys_map)
			names[kvp.second] = kvp.first;
		return names;
	}

#define CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(NAME) \
    static t_config_enum_names s_keys_names_##NAME = enum_names_from_keys_map(s_keys_map_##NAME); \
    template<> const t_config_enum_values& ConfigOptionEnum<NAME>::get_enum_values() { return s_keys_map_##NAME; } \
    template<> const t_config_enum_names& ConfigOptionEnum<NAME>::get_enum_names() { return s_keys_names_##NAME; }



	static const t_config_enum_values s_keys_map_NotifyReleaseMode = {
		{"all",         NotifyReleaseAll},
		{"release",     NotifyReleaseOnly},
		{"none",        NotifyReleaseNone},
	};

	CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(NotifyReleaseMode)

namespace GUI {

PreferencesDialog::PreferencesDialog(wxWindow* parent, int selected_tab, const std::string& highlight_opt_key) :
    DPIDialog(parent, wxID_ANY, _L("Preferences"), wxDefaultPosition, 
              wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
#ifdef __WXOSX__
    isOSX = true;
#endif
	build(selected_tab);
	if (!highlight_opt_key.empty())
		init_highlighter(highlight_opt_key);
}

static std::shared_ptr<ConfigOptionsGroup>create_options_tab(const wxString& title, wxBookCtrlBase* tabs)
{
	wxPanel* tab = new wxPanel(tabs, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
	tabs->AddPage(tab, title);
	tab->SetFont(wxGetApp().normal_font());

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->SetSizeHints(tab);
	tab->SetSizer(sizer);

	std::shared_ptr<ConfigOptionsGroup> optgroup = std::make_shared<ConfigOptionsGroup>(tab);
	optgroup->label_width = 40;
	return optgroup;
}

static void activate_options_tab(std::shared_ptr<ConfigOptionsGroup> optgroup)
{
	optgroup->activate([](){}, wxALIGN_RIGHT);
	optgroup->update_visibility(comSimple);
	wxBoxSizer* sizer = static_cast<wxBoxSizer*>(static_cast<wxPanel*>(optgroup->parent())->GetSizer());
	sizer->Add(optgroup->sizer, 0, wxEXPAND | wxALL, 10);
}

void PreferencesDialog::build(size_t selected_tab)
{
#ifdef _WIN32
	wxGetApp().UpdateDarkUI(this);
#else
	SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif
	const wxFont& font = wxGetApp().normal_font();
	SetFont(font);

	auto app_config = get_app_config();

#ifdef _MSW_DARK_MODE
	wxBookCtrlBase* tabs;
//	if (wxGetApp().dark_mode())
		tabs = new Notebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME | wxNB_DEFAULT);
/*	else {
		tabs = new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME | wxNB_DEFAULT);
		tabs->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
	}*/
#else
    wxNotebook* tabs = new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP | wxTAB_TRAVERSAL  |wxNB_NOPAGETHEME | wxNB_DEFAULT );
	tabs->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif

	// Add "General" tab
	m_optgroup_general = create_options_tab(_L("General"), tabs);
	m_optgroup_general->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
		if (opt_key == "default_action_on_close_application" || opt_key == "default_action_on_select_preset" || opt_key == "default_action_on_new_project")
			m_values[opt_key] = boost::any_cast<bool>(value) ? "none" : "discard";
		else
		    m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
	};

	bool is_editor = wxGetApp().is_editor();

	ConfigOptionDef def;
	Option option(def, "");
	if (is_editor) {
		def.label = L("Remember output directory");
		def.type = coBool;
		def.tooltip = L("If this is enabled, Slic3r will prompt the last output directory "
			"instead of the one containing the input files.");
		def.set_default_value(new ConfigOptionBool{ app_config->has("remember_output_path") ? app_config->get("remember_output_path") == "1" : true });
		option = Option(def, "remember_output_path");
		m_optgroup_general->append_single_option_line(option);

		def.label = L("Auto-center parts");
		def.type = coBool;
		def.tooltip = L("If this is enabled, Slic3r will auto-center objects "
			"around the print bed center.");
		def.set_default_value(new ConfigOptionBool{ app_config->get("autocenter") == "1" });
		option = Option(def, "autocenter");
		m_optgroup_general->append_single_option_line(option);

		def.label = L("Background processing");
		def.type = coBool;
		def.tooltip = L("If this is enabled, Slic3r will pre-process objects as soon "
			"as they\'re loaded in order to save time when exporting G-code.");
		def.set_default_value(new ConfigOptionBool{ app_config->get("background_processing") == "1" });
		option = Option(def, "background_processing");
		m_optgroup_general->append_single_option_line(option);

		m_optgroup_general->append_separator();

		// Please keep in sync with ConfigWizard
		def.label = L("Export sources full pathnames to 3mf and amf");
		def.type = coBool;
		def.tooltip = L("If enabled, allows the Reload from disk command to automatically find and load the files when invoked.");
		def.set_default_value(new ConfigOptionBool(app_config->get("export_sources_full_pathnames") == "1"));
		option = Option(def, "export_sources_full_pathnames");
		m_optgroup_general->append_single_option_line(option);

#ifdef _WIN32
		// Please keep in sync with ConfigWizard
		def.label = L("Associate .3mf files to PrusaSlicer");
		def.type = coBool;
		def.tooltip = L("If enabled, sets PrusaSlicer as default application to open .3mf files.");
		def.set_default_value(new ConfigOptionBool(app_config->get("associate_3mf") == "1"));
		option = Option(def, "associate_3mf");
		m_optgroup_general->append_single_option_line(option);

		def.label = L("Associate .stl files to PrusaSlicer");
		def.type = coBool;
		def.tooltip = L("If enabled, sets PrusaSlicer as default application to open .stl files.");
		def.set_default_value(new ConfigOptionBool(app_config->get("associate_stl") == "1"));
		option = Option(def, "associate_stl");
		m_optgroup_general->append_single_option_line(option);
#endif // _WIN32

		m_optgroup_general->append_separator();

		// Please keep in sync with ConfigWizard
		def.label = L("Update built-in Presets automatically");
		def.type = coBool;
		def.tooltip = L("If enabled, Slic3r downloads updates of built-in system presets in the background. These updates are downloaded into a separate temporary location. When a new preset version becomes available it is offered at application startup.");
		def.set_default_value(new ConfigOptionBool(app_config->get("preset_update") == "1"));
		option = Option(def, "preset_update");
		m_optgroup_general->append_single_option_line(option);

		def.label = L("Suppress \" - default - \" presets");
		def.type = coBool;
		def.tooltip = L("Suppress \" - default - \" presets in the Print / Filament / Printer "
			"selections once there are any other valid presets available.");
		def.set_default_value(new ConfigOptionBool{ app_config->get("no_defaults") == "1" });
		option = Option(def, "no_defaults");
		m_optgroup_general->append_single_option_line(option);

		def.label = L("Show incompatible print and filament presets");
		def.type = coBool;
		def.tooltip = L("When checked, the print and filament presets are shown in the preset editor "
			"even if they are marked as incompatible with the active printer");
		def.set_default_value(new ConfigOptionBool{ app_config->get("show_incompatible_presets") == "1" });
		option = Option(def, "show_incompatible_presets");
		m_optgroup_general->append_single_option_line(option);

		m_optgroup_general->append_separator();

		def.label = L("Show drop project dialog");
		def.type = coBool;
		def.tooltip = L("When checked, whenever dragging and dropping a project file on the application, shows a dialog asking to select the action to take on the file to load.");
		def.set_default_value(new ConfigOptionBool{ app_config->get("show_drop_project_dialog") == "1" });
		option = Option(def, "show_drop_project_dialog");
		m_optgroup_general->append_single_option_line(option);

#if __APPLE__
		def.label = L("Allow just a single PrusaSlicer instance");
		def.type = coBool;
		def.tooltip = L("On OSX there is always only one instance of app running by default. However it is allowed to run multiple instances of same app from the command line. In such case this settings will allow only one instance.");
#else
		def.label = L("Allow just a single PrusaSlicer instance");
		def.type = coBool;
		def.tooltip = L("If this is enabled, when starting PrusaSlicer and another instance of the same PrusaSlicer is already running, that instance will be reactivated instead.");
#endif
		def.set_default_value(new ConfigOptionBool{ app_config->has("single_instance") ? app_config->get("single_instance") == "1" : false });
		option = Option(def, "single_instance");
		m_optgroup_general->append_single_option_line(option);

		m_optgroup_general->append_separator();

		def.label = L("Ask to save unsaved changes when closing the application or when loading a new project");
		def.type = coBool;
		def.tooltip = L("Always ask for unsaved changes, when: \n"
						"- Closing PrusaSlicer while some presets are modified,\n"
						"- Loading a new project while some presets are modified");
		def.set_default_value(new ConfigOptionBool{ app_config->get("default_action_on_close_application") == "none" });
		option = Option(def, "default_action_on_close_application");
		m_optgroup_general->append_single_option_line(option);

		def.label = L("Ask for unsaved changes when selecting new preset");
		def.type = coBool;
		def.tooltip = L("Always ask for unsaved changes when selecting new preset or resetting a preset");
		def.set_default_value(new ConfigOptionBool{ app_config->get("default_action_on_select_preset") == "none" });
		option = Option(def, "default_action_on_select_preset");
		m_optgroup_general->append_single_option_line(option);

		def.label = L("Ask for unsaved changes when creating new project");
		def.type = coBool;
		def.tooltip = L("Always ask for unsaved changes when creating new project");
		def.set_default_value(new ConfigOptionBool{ app_config->get("default_action_on_new_project") == "none" });
		option = Option(def, "default_action_on_new_project");
		m_optgroup_general->append_single_option_line(option);
	}
#ifdef _WIN32
	else {
		def.label = L("Associate .gcode files to PrusaSlicer G-code Viewer");
		def.type = coBool;
		def.tooltip = L("If enabled, sets PrusaSlicer G-code Viewer as default application to open .gcode files.");
		def.set_default_value(new ConfigOptionBool(app_config->get("associate_gcode") == "1"));
		option = Option(def, "associate_gcode");
		m_optgroup_general->append_single_option_line(option);
	}
#endif // _WIN32

#if __APPLE__
	def.label = L("Use Retina resolution for the 3D scene");
	def.type = coBool;
	def.tooltip = L("If enabled, the 3D scene will be rendered in Retina resolution. "
	                "If you are experiencing 3D performance problems, disabling this option may help.");
	def.set_default_value(new ConfigOptionBool{ app_config->get("use_retina_opengl") == "1" });
	option = Option (def, "use_retina_opengl");
	m_optgroup_general->append_single_option_line(option);
#endif

	m_optgroup_general->append_separator();

    // Show/Hide splash screen
	def.label = L("Show splash screen");
	def.type = coBool;
	def.tooltip = L("Show splash screen");
	def.set_default_value(new ConfigOptionBool{ app_config->get("show_splash_screen") == "1" });
	option = Option(def, "show_splash_screen");
	m_optgroup_general->append_single_option_line(option);

    // Clear Undo / Redo stack on new project
	def.label = L("Clear Undo / Redo stack on new project");
	def.type = coBool;
	def.tooltip = L("Clear Undo / Redo stack on new project or when an existing project is loaded.");
	def.set_default_value(new ConfigOptionBool{ app_config->get("clear_undo_redo_stack_on_new_project") == "1" });
	option = Option(def, "clear_undo_redo_stack_on_new_project");
	m_optgroup_general->append_single_option_line(option);

#if defined(_WIN32) || defined(__APPLE__)
	def.label = L("Enable support for legacy 3DConnexion devices");
	def.type = coBool;
	def.tooltip = L("If enabled, the legacy 3DConnexion devices settings dialog is available by pressing CTRL+M");
	def.set_default_value(new ConfigOptionBool{ app_config->get("use_legacy_3DConnexion") == "1" });
	option = Option(def, "use_legacy_3DConnexion");
	m_optgroup_general->append_single_option_line(option);
#endif // _WIN32 || __APPLE__

	activate_options_tab(m_optgroup_general);

	// Add "Camera" tab
	m_optgroup_camera = create_options_tab(_L("Camera"), tabs);
	m_optgroup_camera->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
		m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
	};

	def.label = L("Use perspective camera");
	def.type = coBool;
	def.tooltip = L("If enabled, use perspective camera. If not enabled, use orthographic camera.");
	def.set_default_value(new ConfigOptionBool{ app_config->get("use_perspective_camera") == "1" });
	option = Option(def, "use_perspective_camera");
	m_optgroup_camera->append_single_option_line(option);

	def.label = L("Use free camera");
	def.type = coBool;
	def.tooltip = L("If enabled, use free camera. If not enabled, use constrained camera.");
	def.set_default_value(new ConfigOptionBool(app_config->get("use_free_camera") == "1"));
	option = Option(def, "use_free_camera");
	m_optgroup_camera->append_single_option_line(option);

	def.label = L("Reverse direction of zoom with mouse wheel");
	def.type = coBool;
	def.tooltip = L("If enabled, reverses the direction of zoom with mouse wheel");
	def.set_default_value(new ConfigOptionBool(app_config->get("reverse_mouse_wheel_zoom") == "1"));
	option = Option(def, "reverse_mouse_wheel_zoom");
	m_optgroup_camera->append_single_option_line(option);

	activate_options_tab(m_optgroup_camera);

	// Add "GUI" tab
	m_optgroup_gui = create_options_tab(_L("GUI"), tabs);
	m_optgroup_gui->m_on_change = [this, tabs](t_config_option_key opt_key, boost::any value) {
        if (opt_key == "suppress_hyperlinks")
            m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "";
		else if (opt_key == "notify_release") {
			int val_int = boost::any_cast<int>(value);
			for (const auto& item : s_keys_map_NotifyReleaseMode) {
				if (item.second == val_int) {
					m_values[opt_key] = item.first;
					break;
				}
			}
		} else
            m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";

		if (opt_key == "use_custom_toolbar_size") {
			m_icon_size_sizer->ShowItems(boost::any_cast<bool>(value));
			m_optgroup_gui->parent()->Layout();
			tabs->Layout();
			this->layout();
		}
	};

	def.label = L("Sequential slider applied only to top layer");
	def.type = coBool;
	def.tooltip = L("If enabled, changes made using the sequential slider, in preview, apply only to gcode top layer."
					"If disabled, changes made using the sequential slider, in preview, apply to the whole gcode.");
	def.set_default_value(new ConfigOptionBool{ app_config->get("seq_top_layer_only") == "1" });
	option = Option(def, "seq_top_layer_only");
	m_optgroup_gui->append_single_option_line(option);

	if (is_editor) {
		def.label = L("Show sidebar collapse/expand button");
		def.type = coBool;
		def.tooltip = L("If enabled, the button for the collapse sidebar will be appeared in top right corner of the 3D Scene");
		def.set_default_value(new ConfigOptionBool{ app_config->get("show_collapse_button") == "1" });
		option = Option(def, "show_collapse_button");
		m_optgroup_gui->append_single_option_line(option);

		def.label = L("Suppress to open hyperlink in browser");
		def.type = coBool;
		def.tooltip = L("If enabled, the descriptions of configuration parameters in settings tabs wouldn't work as hyperlinks. "
			"If disabled, the descriptions of configuration parameters in settings tabs will work as hyperlinks.");
		def.set_default_value(new ConfigOptionBool{ app_config->get("suppress_hyperlinks") == "1" });
		option = Option(def, "suppress_hyperlinks");
		m_optgroup_gui->append_single_option_line(option);

		def.label = L("Use colors for axes values in Manipulation panel");
		def.type = coBool;
		def.tooltip = L("If enabled, the axes names and axes values will be colorized according to the axes colors. "
						"If disabled, old UI will be used.");
		def.set_default_value(new ConfigOptionBool{ app_config->get("color_mapinulation_panel") == "1" });
		option = Option(def, "color_mapinulation_panel");
		m_optgroup_gui->append_single_option_line(option);

		def.label = L("Order object volumes by types");
		def.type = coBool;
		def.tooltip = L("If enabled, volumes will be always ordered inside the object. Correct order is Model Part, Negative Volume, Modifier, Support Blocker and Support Enforcer. "
						"If disabled, you can reorder Model Parts, Negative Volumes and Modifiers. But one of the model parts have to be on the first place.");
		def.set_default_value(new ConfigOptionBool{ app_config->get("order_volumes") == "1" });
		option = Option(def, "order_volumes");
		m_optgroup_gui->append_single_option_line(option);

#ifdef _MSW_DARK_MODE
		def.label = L("Set settings tabs as menu items (experimental)");
		def.type = coBool;
		def.tooltip = L("If enabled, Settings Tabs will be placed as menu items. "
			            "If disabled, old UI will be used.");
		def.set_default_value(new ConfigOptionBool{ app_config->get("tabs_as_menu") == "1" });
		option = Option(def, "tabs_as_menu");
		m_optgroup_gui->append_single_option_line(option);
#endif

		m_optgroup_gui->append_separator();

		def.label = L("Show \"Tip of the day\" notification after start");
		def.type = coBool;
		def.tooltip = L("If enabled, useful hints are displayed at startup.");
		def.set_default_value(new ConfigOptionBool{ app_config->get("show_hints") == "1" });
		option = Option(def, "show_hints");
		m_optgroup_gui->append_single_option_line(option);

		ConfigOptionDef def_enum;
		def_enum.label = L("Notify about new releases");
		def_enum.type = coEnum;
		def_enum.tooltip = L("You will be notified about new release after startup acordingly: All = Regular release and alpha / beta releases. Release only = regular release.");
		def_enum.enum_keys_map = &ConfigOptionEnum<NotifyReleaseMode>::get_enum_values();
		def_enum.enum_values.push_back("all");
		def_enum.enum_values.push_back("release");
		def_enum.enum_values.push_back("none");
		def_enum.enum_labels.push_back(L("All"));
		def_enum.enum_labels.push_back(L("Release only"));
		def_enum.enum_labels.push_back(L("None"));
		def_enum.mode = comSimple;
		def_enum.set_default_value(new ConfigOptionEnum<NotifyReleaseMode>(static_cast<NotifyReleaseMode>(s_keys_map_NotifyReleaseMode.at(app_config->get("notify_release")))));
		option = Option(def_enum, "notify_release");
		m_optgroup_gui->append_single_option_line(option);

		m_optgroup_gui->append_separator();

		def.label = L("Use custom size for toolbar icons");
		def.type = coBool;
		def.tooltip = L("If enabled, you can change size of toolbar icons manually.");
		def.set_default_value(new ConfigOptionBool{ app_config->get("use_custom_toolbar_size") == "1" });
		option = Option(def, "use_custom_toolbar_size");
		m_optgroup_gui->append_single_option_line(option);	

	}

	activate_options_tab(m_optgroup_gui);
	// set Field for notify_release to its value to activate the object
	if (is_editor) {
		boost::any val = s_keys_map_NotifyReleaseMode.at(app_config->get("notify_release"));
		m_optgroup_gui->get_field("notify_release")->set_value(val, false);
	}

	if (is_editor) {
		create_icon_size_slider();
		m_icon_size_sizer->ShowItems(app_config->get("use_custom_toolbar_size") == "1");

		create_settings_mode_widget();
		create_settings_text_color_widget();
	}

#if ENABLE_ENVIRONMENT_MAP
	if (is_editor) {
		// Add "Render" tab
		m_optgroup_render = create_options_tab(_L("Render"), tabs);
		m_optgroup_render->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
			m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
		};

		def.label = L("Use environment map");
		def.type = coBool;
		def.tooltip = L("If enabled, renders object using the environment map.");
		def.set_default_value(new ConfigOptionBool{ app_config->get("use_environment_map") == "1" });
		option = Option(def, "use_environment_map");
		m_optgroup_render->append_single_option_line(option);

		activate_options_tab(m_optgroup_render);
	}
#endif // ENABLE_ENVIRONMENT_MAP

#ifdef _WIN32
	// Add "Dark Mode" tab
	if (is_editor) {
		// Add "Dark Mode" tab
		m_optgroup_dark_mode = create_options_tab(_L("Dark mode IU (experimental)"), tabs);
		m_optgroup_dark_mode->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
			m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
		};

		def.label = L("Enable dark mode");
		def.type = coBool;
		def.tooltip = L("If enabled, UI will use Dark mode colors. "
			"If disabled, old UI will be used.");
		def.set_default_value(new ConfigOptionBool{ app_config->get("dark_color_mode") == "1" });
		option = Option(def, "dark_color_mode");
		m_optgroup_dark_mode->append_single_option_line(option);

		if (wxPlatformInfo::Get().GetOSMajorVersion() >= 10) // Use system menu just for Window newer then Windows 10
															 // Use menu with ownerdrawn items by default on systems older then Windows 10
		{
			def.label = L("Use system menu for application");
			def.type = coBool;
			def.tooltip = L("If enabled, application will use the standart Windows system menu,\n"
				"but on some combination od display scales it can look ugly. "
				"If disabled, old UI will be used.");
			def.set_default_value(new ConfigOptionBool{ app_config->get("sys_menu_enabled") == "1" });
			option = Option(def, "sys_menu_enabled");
			m_optgroup_dark_mode->append_single_option_line(option);
		}

		activate_options_tab(m_optgroup_dark_mode);
	}
#endif //_WIN32

	// update alignment of the controls for all tabs
	update_ctrls_alignment();

	if (selected_tab < tabs->GetPageCount())
		tabs->SetSelection(selected_tab);

	auto sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(tabs, 1, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 5);

	auto buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
	this->Bind(wxEVT_BUTTON, &PreferencesDialog::accept, this, wxID_OK);

	for (int id : {wxID_OK, wxID_CANCEL})
		wxGetApp().UpdateDarkUI(static_cast<wxButton*>(FindWindowById(id, this)));

	sizer->Add(buttons, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM | wxTOP, 10);

	SetSizer(sizer);
	sizer->SetSizeHints(this);
	this->CenterOnParent();
}

void PreferencesDialog::update_ctrls_alignment()
{
	int max_ctrl_width{ 0 };
	std::initializer_list<ConfigOptionsGroup*> og_list = { m_optgroup_general.get(), m_optgroup_camera.get(), m_optgroup_gui.get() 
#ifdef _WIN32
		, m_optgroup_dark_mode.get()
#endif // _WIN32
	};
	for (auto og : og_list) {
		if (int max = og->custom_ctrl->get_max_win_width();
			max_ctrl_width < max)
			max_ctrl_width = max;
	}
	if (max_ctrl_width)
		for (auto og : og_list)
			og->custom_ctrl->set_max_win_width(max_ctrl_width);
}

void PreferencesDialog::accept(wxEvent&)
{
//	if (m_values.find("no_defaults") != m_values.end()
//		warning_catcher(this, wxString::Format(_L("You need to restart %s to make the changes effective."), SLIC3R_APP_NAME));

	std::vector<std::string> options_to_recreate_GUI = { "no_defaults", "tabs_as_menu", "sys_menu_enabled" };

	for (const std::string& option : options_to_recreate_GUI) {
		if (m_values.find(option) != m_values.end()) {
			wxString title = wxGetApp().is_editor() ? wxString(SLIC3R_APP_NAME) : wxString(GCODEVIEWER_APP_NAME);
			title += " - " + _L("Changes for the critical options");
			MessageDialog dialog(nullptr,
				_L("Changing some options will trigger application restart.\n"
				   "You will lose the content of the plater.") + "\n\n" +
				_L("Do you want to proceed?"),
				title,
				wxICON_QUESTION | wxYES | wxNO);
			if (dialog.ShowModal() == wxID_YES) {
				m_recreate_GUI = true;
			}
			else {
				for (const std::string& option : options_to_recreate_GUI)
					m_values.erase(option);
			}
			break;
		}
	}

    auto app_config = get_app_config();

	m_seq_top_layer_only_changed = false;
	if (auto it = m_values.find("seq_top_layer_only"); it != m_values.end())
		m_seq_top_layer_only_changed = app_config->get("seq_top_layer_only") != it->second;

	m_settings_layout_changed = false;
	for (const std::string& key : { "old_settings_layout_mode",
								    "new_settings_layout_mode",
								    "dlg_settings_layout_mode" })
	{
	    auto it = m_values.find(key);
	    if (it != m_values.end() && app_config->get(key) != it->second) {
			m_settings_layout_changed = true;
			break;
	    }
	}

	for (const std::string& key : {"default_action_on_close_application", "default_action_on_select_preset"}) {
	    auto it = m_values.find(key);
		if (it != m_values.end() && it->second != "none" && app_config->get(key) != "none")
			m_values.erase(it); // we shouldn't change value, if some of those parameters was selected, and then deselected
	}

#if 0 //#ifdef _WIN32 // #ysDarkMSW - Allow it when we deside to support the sustem colors for application
	if (m_values.find("always_dark_color_mode") != m_values.end())
		wxGetApp().force_sys_colors_update();
#endif

	for (std::map<std::string, std::string>::iterator it = m_values.begin(); it != m_values.end(); ++it)
		app_config->set(it->first, it->second);

	app_config->save();
	if (wxGetApp().is_editor()) {
		wxGetApp().set_label_clr_sys(m_sys_colour->GetColour());
		wxGetApp().set_label_clr_modified(m_mod_colour->GetColour());
	}

	EndModal(wxID_OK);

#ifdef _WIN32
	if (m_values.find("dark_color_mode") != m_values.end())
		wxGetApp().force_colors_update();
#ifdef _MSW_DARK_MODE
	if (m_values.find("sys_menu_enabled") != m_values.end())
		wxGetApp().force_menu_update();
#endif //_MSW_DARK_MODE
#endif // _WIN32
	if (m_settings_layout_changed)
		;// application will be recreated after Preference dialog will be destroyed
	else
	    // Nothify the UI to update itself from the ini file.
        wxGetApp().update_ui_from_settings();
}

void PreferencesDialog::on_dpi_changed(const wxRect &suggested_rect)
{
	m_optgroup_general->msw_rescale();
	m_optgroup_camera->msw_rescale();
	m_optgroup_gui->msw_rescale();

    msw_buttons_rescale(this, em_unit(), { wxID_OK, wxID_CANCEL });

    layout();
}

void PreferencesDialog::layout()
{
    const int em = em_unit();

    SetMinSize(wxSize(47 * em, 28 * em));
    Fit();

    Refresh();
}

void PreferencesDialog::create_icon_size_slider()
{
    const auto app_config = get_app_config();

    const int em = em_unit();

    m_icon_size_sizer = new wxBoxSizer(wxHORIZONTAL);

	wxWindow* parent = m_optgroup_gui->parent();
	wxGetApp().UpdateDarkUI(parent);

    if (isOSX)
        // For correct rendering of the slider and value label under OSX
        // we should use system default background
        parent->SetBackgroundStyle(wxBG_STYLE_ERASE);

    auto label = new wxStaticText(parent, wxID_ANY, _L("Icon size in a respect to the default size") + " (%) :");

    m_icon_size_sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL| wxRIGHT | (isOSX ? 0 : wxLEFT), em);

    const int def_val = atoi(app_config->get("custom_toolbar_size").c_str());

    long style = wxSL_HORIZONTAL;
    if (!isOSX)
        style |= wxSL_LABELS | wxSL_AUTOTICKS;

    auto slider = new wxSlider(parent, wxID_ANY, def_val, 30, 100, 
                               wxDefaultPosition, wxDefaultSize, style);

    slider->SetTickFreq(10);
    slider->SetPageSize(10);
    slider->SetToolTip(_L("Select toolbar icon size in respect to the default one."));

    m_icon_size_sizer->Add(slider, 1, wxEXPAND);

    wxStaticText* val_label{ nullptr };
    if (isOSX) {
        val_label = new wxStaticText(parent, wxID_ANY, wxString::Format("%d", def_val));
        m_icon_size_sizer->Add(val_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em);
    }

    slider->Bind(wxEVT_SLIDER, ([this, slider, val_label](wxCommandEvent e) {
        auto val = slider->GetValue();
        m_values["custom_toolbar_size"] = (boost::format("%d") % val).str();

        if (val_label)
            val_label->SetLabelText(wxString::Format("%d", val));
    }), slider->GetId());

    for (wxWindow* win : std::vector<wxWindow*>{ slider, label, val_label }) {
        if (!win) continue;         
        win->SetFont(wxGetApp().normal_font());

        if (isOSX) continue; // under OSX we use wxBG_STYLE_ERASE
        win->SetBackgroundStyle(wxBG_STYLE_PAINT);
    }

	m_optgroup_gui->sizer->Add(m_icon_size_sizer, 0, wxEXPAND | wxALL, em);
}

void PreferencesDialog::create_settings_mode_widget()
{
#ifdef _MSW_DARK_MODE
	bool disable_new_layout = wxGetApp().tabs_as_menu();
#endif
	std::vector<wxString> choices = {  _L("Old regular layout with the tab bar"),
                                       _L("New layout, access via settings button in the top menu"),
                                       _L("Settings in non-modal window") };

	auto app_config = get_app_config();
    int selection = app_config->get("old_settings_layout_mode") == "1" ? 0 :
                    app_config->get("new_settings_layout_mode") == "1" ? 1 :
                    app_config->get("dlg_settings_layout_mode") == "1" ? 2 : 0;

#ifdef _MSW_DARK_MODE
	if (disable_new_layout) {
		choices = { _L("Old regular layout with the tab bar"),
					_L("Settings in non-modal window") };
		selection = app_config->get("dlg_settings_layout_mode") == "1" ? 1 : 0;
	}
#endif

	wxWindow* parent = m_optgroup_gui->parent();
	wxGetApp().UpdateDarkUI(parent);

    wxStaticBox* stb = new wxStaticBox(parent, wxID_ANY, _L("Layout Options"));
	wxGetApp().UpdateDarkUI(stb);
	if (!wxOSX) stb->SetBackgroundStyle(wxBG_STYLE_PAINT);
	stb->SetFont(wxGetApp().normal_font());

	wxSizer* stb_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

	int id = 0;
	for (const wxString& label : choices) {
		wxRadioButton* btn = new wxRadioButton(parent, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, id==0 ? wxRB_GROUP : 0);
		stb_sizer->Add(btn);
		btn->SetValue(id == selection);

        int dlg_id = 2;
#ifdef _MSW_DARK_MODE
		if (disable_new_layout)
			dlg_id = 1;
#endif

        btn->Bind(wxEVT_RADIOBUTTON, [this, id, dlg_id
#ifdef _MSW_DARK_MODE
			, disable_new_layout
#endif
		](wxCommandEvent& ) {
            m_values["old_settings_layout_mode"] = (id == 0) ? "1" : "0";
#ifdef _MSW_DARK_MODE
			if (!disable_new_layout)
#endif
            m_values["new_settings_layout_mode"] = (id == 1) ? "1" : "0";
            m_values["dlg_settings_layout_mode"] = (id == dlg_id) ? "1" : "0";
		});
		id++;
	}

	auto sizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(stb_sizer, 1, wxALIGN_CENTER_VERTICAL);
	m_optgroup_gui->sizer->Add(sizer, 0, wxEXPAND | wxTOP, em_unit());
}

void PreferencesDialog::create_settings_text_color_widget()
{
	wxWindow* parent = m_optgroup_gui->parent();

	wxStaticBox* stb = new wxStaticBox(parent, wxID_ANY, _L("Text colors"));
	wxGetApp().UpdateDarkUI(stb);
	if (!wxOSX) stb->SetBackgroundStyle(wxBG_STYLE_PAINT);

	wxSizer* sizer = new wxStaticBoxSizer(stb, wxVERTICAL);
	ButtonsDescription::FillSizerWithTextColorDescriptions(sizer, parent, &m_sys_colour, &m_mod_colour);

	m_optgroup_gui->sizer->Add(sizer, 0, wxEXPAND | wxTOP, em_unit());
}

void PreferencesDialog::init_highlighter(const t_config_option_key& opt_key)
{
	m_highlighter.set_timer_owner(this, 0);
	this->Bind(wxEVT_TIMER, [this](wxTimerEvent&)
		{
			m_highlighter.blink();
		});

	std::pair<OG_CustomCtrl*, bool*> ctrl = { nullptr, nullptr };
	for (auto opt_group : { m_optgroup_general, m_optgroup_camera, m_optgroup_gui }) {
		ctrl = opt_group->get_custom_ctrl_with_blinking_ptr(opt_key, -1);
		if (ctrl.first && ctrl.second) {
			m_highlighter.init(ctrl);
			break;
		}
	}
}

void PreferencesDialog::PreferencesHighlighter::set_timer_owner(wxEvtHandler* owner, int timerid/* = wxID_ANY*/)
{
	m_timer.SetOwner(owner, timerid);
}

void PreferencesDialog::PreferencesHighlighter::init(std::pair<OG_CustomCtrl*, bool*> params)
{
	if (m_timer.IsRunning())
		invalidate();
	if (!params.first || !params.second)
		return;

	m_timer.Start(300, false);

	m_custom_ctrl = params.first;
	m_show_blink_ptr = params.second;

	*m_show_blink_ptr = true;
	m_custom_ctrl->Refresh();
}

void PreferencesDialog::PreferencesHighlighter::invalidate()
{
	m_timer.Stop();

	if (m_custom_ctrl && m_show_blink_ptr) {
		*m_show_blink_ptr = false;
		m_custom_ctrl->Refresh();
		m_show_blink_ptr = nullptr;
		m_custom_ctrl = nullptr;
	}

	m_blink_counter = 0;
}

void PreferencesDialog::PreferencesHighlighter::blink()
{
	if (m_custom_ctrl && m_show_blink_ptr) {
		*m_show_blink_ptr = !*m_show_blink_ptr;
		m_custom_ctrl->Refresh();
	}
	else
		return;

	if ((++m_blink_counter) == 11)
		invalidate();
}

} // GUI
} // Slic3r
