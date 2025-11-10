#include "SpoolmanDialog.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "OptionsGroup.hpp"
#include "Plater.hpp"
#include "Spoolman.hpp"
#include "Widgets/DialogButtons.hpp"
#include "Widgets/LabeledStaticBox.hpp"
#include "Widgets/LoadingSpinner.hpp"
#include "wx/sizer.h"
#define EM wxGetApp().em_unit()

namespace Slic3r::GUI {

static BitmapCache cache;
const static std::regex spoolman_regex("^spoolman_");

SpoolInfoWidget::SpoolInfoWidget(wxWindow* parent, const Preset* preset) : wxPanel(parent, wxID_ANY), m_preset(preset)
{
    auto main_sizer = new wxStaticBoxSizer(new LabeledStaticBox(this), wxVERTICAL);

    auto bitmap = cache.load_svg("spool", EM * 10, EM * 10, false, false,
                                 {{"#009688", m_preset->config.opt_string("default_filament_colour", 0)}});

    m_spool_bitmap = new wxStaticBitmap(this, wxID_ANY, *bitmap);
    m_spool_bitmap->SetMinSize({EM * 10, EM * 10});
    main_sizer->Add(m_spool_bitmap, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, EM);

    m_preset_name_label = new Label(this, wxString::FromUTF8(preset->name));
    m_preset_name_label->SetFont(Label::Body_12);
    m_preset_name_label->SetBackgroundColour(*wxWHITE);
    wxGetApp().UpdateDarkUI(m_preset_name_label);
    main_sizer->Add(m_preset_name_label, 0, wxALIGN_CENTER_HORIZONTAL | wxDOWN | wxLEFT | wxRIGHT, EM);

    m_remaining_weight_label = new Label(this);
    if (preset->spoolman_enabled()) {
        auto spool = Spoolman::get_instance()->get_spoolman_spool_by_id(preset->config.opt_int("spoolman_spool_id", 0));
        m_remaining_weight_label->SetLabelText(
            format("%1% g / %2% g", double_to_string(spool->remaining_weight, 2), double_to_string(spool->m_filament_ptr->weight, 2)));
    } else {
        m_remaining_weight_label->SetLabelText(_L("Not Spoolman enabled"));
        m_remaining_weight_label->SetForegroundColour(*wxRED);
    }
    m_remaining_weight_label->SetFont(Label::Body_12);
    m_remaining_weight_label->SetBackgroundColour(*wxWHITE);
    wxGetApp().UpdateDarkUI(m_remaining_weight_label);
    main_sizer->Add(m_remaining_weight_label, 0, wxALIGN_CENTER_HORIZONTAL | wxDOWN | wxLEFT | wxRIGHT, EM);

    this->SetSizer(main_sizer);
}

void SpoolInfoWidget::rescale()
{
    auto bitmap = cache.load_svg("spool", EM * 10, EM * 10, false, false,
                                 {{"#009688", m_preset->config.opt_string("default_filament_colour", 0)}});
    m_spool_bitmap->SetBitmap(*bitmap);
    m_spool_bitmap->SetMinSize({EM * 10, EM * 10});
}

SpoolmanDialog::SpoolmanDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Spoolman"), wxDefaultPosition, {-1, 45 * EM}, wxDEFAULT_DIALOG_STYLE)
{
#ifdef _WIN32
    this->SetBackgroundColour(*wxWHITE);
    wxGetApp().UpdateDarkUI(this);
    wxGetApp().UpdateDlgDarkUI(this);
#else
    wxWindow::SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif

    auto main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->SetMinSize({-1, 45 * EM});

    m_optgroup = new OptionsGroup(this, _L("Spoolman Options"), wxEmptyString);
    build_options_group();
    m_optgroup->m_on_change = [&](const std::string& key, const boost::any& value) { m_dirty_settings = true; };
    main_sizer->Add(m_optgroup->sizer, 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, EM);

    m_spoolman_info_sizer = new wxBoxSizer(wxVERTICAL);

    m_spoolman_error_label = new Label(this);
    m_spoolman_error_label->SetFont(Label::Body_16);
    m_spoolman_error_label->SetForegroundColour(*wxRED);

    m_spoolman_info_sizer->AddStretchSpacer(1);
    m_spoolman_info_sizer->Add(m_spoolman_error_label, 0, wxALIGN_CENTER);
    m_spoolman_info_sizer->AddStretchSpacer(1);
    main_sizer->Add(m_spoolman_info_sizer, 1, wxALL | wxALIGN_CENTER | wxEXPAND, EM);

    m_info_widgets_sizer = new wxGridSizer(2, EM, EM);
    build_spool_info();
    main_sizer->Add(m_info_widgets_sizer, 1, wxALL | wxALIGN_CENTER, EM);

    m_buttons = new DialogButtons(this, {"Refresh", "OK"});
    m_buttons->UpdateButtons();
    m_buttons->GetButtonFromLabel(_L("Refresh"))->Bind(wxEVT_BUTTON, &SpoolmanDialog::OnRefresh, this);
    m_buttons->GetOK()->Bind(wxEVT_BUTTON, &SpoolmanDialog::OnOK, this);
    main_sizer->Add(m_buttons, 0, wxALIGN_BOTTOM | wxEXPAND | wxLEFT | wxRIGHT, EM);

    this->SetSizerAndFit(main_sizer);
    this->SetMinSize(wxDefaultSize);

    this->ShowModal();
}
void SpoolmanDialog::build_options_group() const
{
    ConfigOptionDef def;
    def.type    = coBool;
    def.label   = _u8L("Spoolman Enabled");
    def.tooltip = _u8L("Enables spool management features powered by a Spoolman server instance");
    def.set_default_value(new ConfigOptionBool());
    m_optgroup->append_single_option_line((Option(def, "spoolman_enabled")));

    def         = ConfigOptionDef();
    def.type    = coString;
    def.label   = _u8L("Spoolman Host");
    def.tooltip = _u8L("Points to where you Spoolman instance is hosted. Use the format of <host>:<port>. You may also just specify the "
                       "host and it will use the default Spoolman port of ") +
                  Spoolman::DEFAULT_PORT;
    def.set_default_value(new ConfigOptionString());
    m_optgroup->append_single_option_line(Option(def, "spoolman_host"));

    m_optgroup->activate();

    // Load config values from the app config
    const auto        app_config = wxGetApp().app_config;
    for (auto& line : m_optgroup->get_lines()) {
        for (auto& option : line.get_options()) {
            auto app_config_key = regex_replace(option.opt_id, spoolman_regex, "");
            if (option.opt.type == coBool)
                m_optgroup->set_value(option.opt_id, app_config->get_bool("spoolman", app_config_key));
            else if (option.opt.type == coString)
                m_optgroup->set_value(option.opt_id, wxString::FromUTF8(app_config->get("spoolman", app_config_key)));
            else
                BOOST_LOG_TRIVIAL(error) << "SpoolmanDialog load: Unknown option type " << option.opt.type;
        }
    }
}

void SpoolmanDialog::build_spool_info()
{
    wxBusyCursor cursor;
    m_info_widgets_sizer->Show(false);
    m_spoolman_info_sizer->Show(false);
    m_info_widgets_sizer->Clear();
    if (!Spoolman::is_enabled()) {
        m_spoolman_error_label->SetLabelText(_L("Spoolman is not enabled"));
        m_spoolman_info_sizer->Show(true);
        return;
    }
    if (!Spoolman::is_server_valid()) {
        m_spoolman_error_label->SetLabelText(_L("Spoolman server is invalid"));
        m_spoolman_info_sizer->Show(true);
        return;
    }
    m_info_widgets_sizer->Show(true);
    auto preset_bundle = wxGetApp().preset_bundle;
    for (auto& filament_preset_name : preset_bundle->filament_presets) {
        auto spool_info_widget = new SpoolInfoWidget(this, preset_bundle->filaments.find_preset(filament_preset_name));
        m_info_widgets_sizer->Add(spool_info_widget, 0, wxEXPAND);
    }
}

void SpoolmanDialog::save_spoolman_settings()
{
    // clear the Spoolman cache and reload if any of the Spoolman settings change
    if (!m_dirty_settings)
        return;

    // Save config values to the app config
    const auto        app_config = wxGetApp().app_config;
    for (auto& line : m_optgroup->get_lines()) {
        for (auto& option : line.get_options()) {
            auto app_config_key = regex_replace(option.opt_id, spoolman_regex, "");
            auto val            = m_optgroup->get_value(option.opt_id);
            if (option.opt.type == coBool)
                app_config->set("spoolman", app_config_key, any_cast<bool>(val));
            else if (option.opt.type == coString)
                app_config->set("spoolman", app_config_key, any_cast<std::string>(val));
            else
                BOOST_LOG_TRIVIAL(error) << "SpoolmanDialog save: Unknown option type " << option.opt.type;
        }
    }

    Spoolman::update_visible_spool_statistics(true);
    m_dirty_settings = false;
}

void SpoolmanDialog::OnRefresh(wxCommandEvent& e)
{
    Freeze();
    save_spoolman_settings();
    build_spool_info();
    Thaw();
    Fit();
    Layout();
    Refresh();
}

void SpoolmanDialog::OnOK(wxCommandEvent& e)
{
    save_spoolman_settings();
    this->EndModal(wxID_OK);
}

void SpoolmanDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    GetSizer()->SetMinSize(wxDefaultCoord, 45 * EM);
    m_optgroup->msw_rescale();
    for (auto item : m_info_widgets_sizer->GetChildren())
        if (auto info_widget = dynamic_cast<SpoolInfoWidget*>(item))
            info_widget->rescale();
    Fit();
    Refresh();
}

} // namespace Slic3r::GUI
