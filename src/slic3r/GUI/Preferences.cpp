#include "Preferences.hpp"
#include "AppConfig.hpp"
#include "OptionsGroup.hpp"
#include "I18N.hpp"

namespace Slic3r {
namespace GUI {

PreferencesDialog::PreferencesDialog(wxWindow* parent) : 
    DPIDialog(parent, wxID_ANY, _(L("Preferences")), wxDefaultPosition, 
              wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
#ifdef __WXOSX__
    isOSX = true;
#endif
	build();
}

void PreferencesDialog::build()
{
	auto app_config = get_app_config();
	m_optgroup_general = std::make_shared<ConfigOptionsGroup>(this, _(L("General")));
	m_optgroup_general->label_width = 40;
	m_optgroup_general->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
		m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
	};

	// TODO
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
    def.set_default_value(new ConfigOptionBool{ app_config->has("remember_output_path") ? app_config->get("remember_output_path") == "1" : true });
    Option option(def, "remember_output_path");
	m_optgroup_general->append_single_option_line(option);

	def.label = L("Auto-center parts");
	def.type = coBool;
	def.tooltip = L("If this is enabled, Slic3r will auto-center objects "
					  "around the print bed center.");
	def.set_default_value(new ConfigOptionBool{ app_config->get("autocenter") == "1" });
	option = Option (def,"autocenter");
	m_optgroup_general->append_single_option_line(option);

	def.label = L("Background processing");
	def.type = coBool;
	def.tooltip = L("If this is enabled, Slic3r will pre-process objects as soon "
					  "as they\'re loaded in order to save time when exporting G-code.");
	def.set_default_value(new ConfigOptionBool{ app_config->get("background_processing") == "1" });
	option = Option (def,"background_processing");
	m_optgroup_general->append_single_option_line(option);

	// Please keep in sync with ConfigWizard
	def.label = L("Check for application updates");
	def.type = coBool;
	def.tooltip = L("If enabled, PrusaSlicer will check for the new versions of itself online. When a new version becomes available a notification is displayed at the next application startup (never during program usage). This is only a notification mechanisms, no automatic installation is done.");
	def.set_default_value(new ConfigOptionBool(app_config->get("version_check") == "1"));
	option = Option (def, "version_check");
	m_optgroup_general->append_single_option_line(option);

	// Please keep in sync with ConfigWizard
	def.label = L("Export sources full pathnames to 3mf and amf");
	def.type = coBool;
	def.tooltip = L("If enabled, allows the Reload from disk command to automatically find and load the files when invoked.");
	def.set_default_value(new ConfigOptionBool(app_config->get("export_sources_full_pathnames") == "1"));
	option = Option(def, "export_sources_full_pathnames");
	m_optgroup_general->append_single_option_line(option);

	// Please keep in sync with ConfigWizard
	def.label = L("Update built-in Presets automatically");
	def.type = coBool;
	def.tooltip = L("If enabled, Slic3r downloads updates of built-in system presets in the background. These updates are downloaded into a separate temporary location. When a new preset version becomes available it is offered at application startup.");
	def.set_default_value(new ConfigOptionBool(app_config->get("preset_update") == "1"));
	option = Option (def, "preset_update");
	m_optgroup_general->append_single_option_line(option);

	def.label = L("Suppress \" - default - \" presets");
	def.type = coBool;
	def.tooltip = L("Suppress \" - default - \" presets in the Print / Filament / Printer "
					  "selections once there are any other valid presets available.");
	def.set_default_value(new ConfigOptionBool{ app_config->get("no_defaults") == "1" });
	option = Option (def,"no_defaults");
	m_optgroup_general->append_single_option_line(option);

	def.label = L("Show incompatible print and filament presets");
	def.type = coBool;
	def.tooltip = L("When checked, the print and filament presets are shown in the preset editor "
					  "even if they are marked as incompatible with the active printer");
	def.set_default_value(new ConfigOptionBool{ app_config->get("show_incompatible_presets") == "1" });
	option = Option (def,"show_incompatible_presets");
	m_optgroup_general->append_single_option_line(option);

	def.label = L("Single Instance");
	def.type = coBool;
	def.tooltip = L("If this is enabled, when staring PrusaSlicer and another instance is running, that instance will be reactivated instead.");
	def.set_default_value(new ConfigOptionBool{ app_config->has("single_instance") ? app_config->get("single_instance") == "1" : false });
	option = Option(def, "single_instance");
	m_optgroup_general->append_single_option_line(option);

#if __APPLE__
	def.label = L("Use Retina resolution for the 3D scene");
	def.type = coBool;
	def.tooltip = L("If enabled, the 3D scene will be rendered in Retina resolution. "
	                "If you are experiencing 3D performance problems, disabling this option may help.");
	def.set_default_value(new ConfigOptionBool{ app_config->get("use_retina_opengl") == "1" });
	option = Option (def, "use_retina_opengl");
	m_optgroup_general->append_single_option_line(option);
#endif

	m_optgroup_camera = std::make_shared<ConfigOptionsGroup>(this, _(L("Camera")));
	m_optgroup_camera->label_width = 40;
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

	m_optgroup_gui = std::make_shared<ConfigOptionsGroup>(this, _(L("GUI")));
	m_optgroup_gui->label_width = 40;
	m_optgroup_gui->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
		m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
		if (opt_key == "use_custom_toolbar_size") {
			m_icon_size_sizer->ShowItems(boost::any_cast<bool>(value));
			this->layout();
		}
	};

	def.label = L("Use custom size for toolbar icons");
	def.type = coBool;
	def.tooltip = L("If enabled, you can change size of toolbar icons manually.");
	def.set_default_value(new ConfigOptionBool{ app_config->get("use_custom_toolbar_size") == "1" });
	option = Option(def, "use_custom_toolbar_size");
	m_optgroup_gui->append_single_option_line(option);

	create_icon_size_slider();
	m_icon_size_sizer->ShowItems(app_config->get("use_custom_toolbar_size") == "1");

	auto sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_optgroup_general->sizer, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
	sizer->Add(m_optgroup_camera->sizer, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
	sizer->Add(m_optgroup_gui->sizer, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);

    SetFont(wxGetApp().normal_font());

	auto buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
	wxButton* btn = static_cast<wxButton*>(FindWindowById(wxID_OK, this));
	btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { accept(); });
	sizer->Add(buttons, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);

	SetSizer(sizer);
	sizer->SetSizeHints(this);
}

void PreferencesDialog::accept()
{
    if (m_values.find("no_defaults") != m_values.end()) {
        warning_catcher(this, wxString::Format(_(L("You need to restart %s to make the changes effective.")), SLIC3R_APP_NAME));
	}

	auto app_config = get_app_config();
	for (std::map<std::string, std::string>::iterator it = m_values.begin(); it != m_values.end(); ++it) {
		app_config->set(it->first, it->second);
	}

	app_config->save();

	EndModal(wxID_OK);

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

	wxWindow* parent = m_optgroup_gui->ctrl_parent();

    if (isOSX)
        // For correct rendering of the slider and value label under OSX
        // we should use system default background
        parent->SetBackgroundStyle(wxBG_STYLE_ERASE);

    auto label = new wxStaticText(parent, wxID_ANY, _(L("Icon size in a respect to the default size")) + " (%) :");

    m_icon_size_sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL| wxRIGHT | (isOSX ? 0 : wxLEFT), em);

    const int def_val = atoi(app_config->get("custom_toolbar_size").c_str());

    long style = wxSL_HORIZONTAL;
    if (!isOSX)
        style |= wxSL_LABELS | wxSL_AUTOTICKS;

    auto slider = new wxSlider(parent, wxID_ANY, def_val, 30, 100, 
                               wxDefaultPosition, wxDefaultSize, style);

    slider->SetTickFreq(10);
    slider->SetPageSize(10);
    slider->SetToolTip(_(L("Select toolbar icon size in respect to the default one.")));

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


} // GUI
} // Slic3r