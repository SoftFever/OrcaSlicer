#include "Preferences.hpp"
#include "AppConfig.hpp"
#include "OptionsGroup.hpp"

namespace Slic3r {
namespace GUI {

void PreferencesDialog::build()
{
	auto app_config = get_app_config();
	m_optgroup = std::make_shared<ConfigOptionsGroup>(this, _(L("General")));
	m_optgroup->label_width = 200;
	m_optgroup->m_on_change = [this](t_config_option_key opt_key, boost::any value){
		m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
	};

//    $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
//        opt_id = > 'version_check',
//        type = > 'bool',
//        label = > 'Check for updates',
//        tooltip = > 'If this is enabled, Slic3r will check for updates daily and display a reminder if a newer version is available.',
//        default = > $app_config->get("version_check") // 1,
//        readonly = > !wxTheApp->have_version_check,
//    ));

	ConfigOptionDef def;
	def.label = L("Remember output directory");
	def.type = coBool;
	def.tooltip = L("If this is enabled, Slic3r will prompt the last output directory "
					  "instead of the one containing the input files.");
	def.default_value = new ConfigOptionBool{ app_config->get("remember_output_path")[0] == '1' }; // 1;
	Option option(def, "remember_output_path");
	m_optgroup->append_single_option_line(option);

	def.label = L("Auto-center parts");
	def.type = coBool;
	def.tooltip = L("If this is enabled, Slic3r will auto-center objects "
					  "around the print bed center.");
	def.default_value = new ConfigOptionBool{ app_config->get("autocenter")[0] == '1' }; // 1;
	option = Option (def,"autocenter");
	m_optgroup->append_single_option_line(option);

	def.label = L("Background processing");
	def.type = coBool;
	def.tooltip = L("If this is enabled, Slic3r will pre-process objects as soon "
					  "as they\'re loaded in order to save time when exporting G-code.");
	def.default_value = new ConfigOptionBool{ app_config->get("background_processing")[0] == '1' }; // 1;
	option = Option (def,"background_processing");
	m_optgroup->append_single_option_line(option);

	def.label = L("Disable USB/serial connection");
	def.type = coBool;
	def.tooltip = L("Disable communication with the printer over a serial / USB cable. "
					  "This simplifies the user interface in case the printer is never attached to the computer.");
	def.default_value = new ConfigOptionBool{ app_config->get("no_controller")[0] == '1' }; // 1;
	option = Option (def,"no_controller");
	m_optgroup->append_single_option_line(option);

	def.label = L("Suppress \" - default - \" presets");
	def.type = coBool;
	def.tooltip = L("Suppress \" - default - \" presets in the Print / Filament / Printer "
					  "selections once there are any other valid presets available.");
	def.default_value = new ConfigOptionBool{ app_config->get("no_defaults")[0] == '1' }; // 1;
	option = Option (def,"no_defaults");
	m_optgroup->append_single_option_line(option);

	def.label = L("Show incompatible print and filament presets");
	def.type = coBool;
	def.tooltip = L("When checked, the print and filament presets are shown in the preset editor "
					  "even if they are marked as incompatible with the active printer");
	def.default_value = new ConfigOptionBool{ app_config->get("show_incompatible_presets")[0] == '1' }; // 1;
	option = Option (def,"show_incompatible_presets");
	m_optgroup->append_single_option_line(option);

	def.label = L("Use legacy OpenGL 1.1 rendering");
	def.type = coBool;
	def.tooltip = L("If you have rendering issues caused by a buggy OpenGL 2.0 driver, "
					  "you may try to check this checkbox. This will disable the layer height "
					  "editing and anti aliasing, so it is likely better to upgrade your graphics driver.");
	def.default_value = new ConfigOptionBool{ app_config->get("use_legacy_opengl")[0] == '1' }; // 1;
	option = Option (def,"use_legacy_opengl");
	m_optgroup->append_single_option_line(option);

	auto sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_optgroup->sizer, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);

	auto buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
	wxButton* btn = static_cast<wxButton*>(FindWindowById(wxID_OK, this));
	btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { accept(); });
	sizer->Add(buttons, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);

	SetSizer(sizer);
	sizer->SetSizeHints(this);
}

void PreferencesDialog::accept()
{
	if (m_values.find("no_controller")    != m_values.end()||
		m_values.find("no_defaults")      != m_values.end()||
		m_values.find("use_legacy_opengl")!= m_values.end()) {
		warning_catcher(this, _(L("You need to restart Slic3r to make the changes effective.")));
	}

	auto app_config = get_app_config();
	for (std::map<std::string, std::string>::iterator it = m_values.begin(); it != m_values.end(); ++it) {
		app_config->set(it->first, it->second);
	}

	EndModal(wxID_OK);
	Close();  // needed on Linux

	// Nothify the UI to update itself from the ini file.
	if (m_event_preferences > 0) {
		wxCommandEvent event(m_event_preferences);
		get_app()->ProcessEvent(event);
	}
}

} // GUI
} // Slic3r