#include "PhysicalPrinterDialog.hpp"
#include "PresetComboBoxes.hpp"

#include <cstddef>
#include <vector>
#include <string>
#include <boost/algorithm/string.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/statbox.h>
#include <wx/wupdlock.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Tab.hpp"
#include "wxExtensions.hpp"
#include "PrintHostDialogs.hpp"
#include "../Utils/ASCIIFolding.hpp"
#include "../Utils/PrintHost.hpp"
#include "../Utils/FixModelByWin10.hpp"
#include "../Utils/UndoRedo.hpp"
#include "RemovableDriveManager.hpp"
#include "BitmapCache.hpp"
#include "BonjourDialog.hpp"

using Slic3r::GUI::format_wxstr;

//static const std::pair<unsigned int, unsigned int> THUMBNAIL_SIZE_3MF = { 256, 256 };

namespace Slic3r {
namespace GUI {

#define BORDER_W 10

//------------------------------------------
//          PresetForPrinter
//------------------------------------------

PresetForPrinter::PresetForPrinter(PhysicalPrinterDialog* parent, const std::string& preset_name) :
    m_parent(parent)
{
    m_sizer = new wxBoxSizer(wxVERTICAL);

    m_delete_preset_btn = new ScalableButton(parent, wxID_ANY, "cross", "", wxDefaultSize, wxDefaultPosition, /*wxBU_LEFT | */wxBU_EXACTFIT);
    m_delete_preset_btn->SetFont(wxGetApp().normal_font());
    m_delete_preset_btn->SetToolTip(_L("Delete this preset from this printer device"));
    m_delete_preset_btn->Bind(wxEVT_BUTTON, &PresetForPrinter::DeletePreset, this);

    m_presets_list = new PresetComboBox(parent, Preset::TYPE_PRINTER);
    m_presets_list->set_printer_technology(parent->get_printer_technology());

    m_presets_list->set_selection_changed_function([this](int selection) {
        std::string selected_string = Preset::remove_suffix_modified(m_presets_list->GetString(selection).ToUTF8().data());
        Preset* preset = wxGetApp().preset_bundle->printers.find_preset(selected_string);
        assert(preset);
        Preset& edited_preset = wxGetApp().preset_bundle->printers.get_edited_preset();
        if (preset->name == edited_preset.name)
            preset = &edited_preset;

        // if created physical printer doesn't have any settings, use the settings from the selected preset
        if (m_parent->get_printer()->has_empty_config()) {
            // update Print Host upload from the selected preset
            m_parent->get_printer()->update_from_preset(*preset);
            // update values in parent (PhysicalPrinterDialog)
            m_parent->update();
        }

        // update PrinterTechnology if it was changed
        if (m_presets_list->set_printer_technology(preset->printer_technology()))
            m_parent->set_printer_technology(preset->printer_technology());

        update_full_printer_name();
    });
    m_presets_list->update(preset_name);

    m_info_line = new wxStaticText(parent, wxID_ANY, _L("This printer will be shown in the presets list as") + ":");

    m_full_printer_name = new wxStaticText(parent, wxID_ANY, "");
    m_full_printer_name->SetFont(wxGetApp().bold_font());

    wxBoxSizer* preset_sizer = new wxBoxSizer(wxHORIZONTAL);
    preset_sizer->Add(m_presets_list        , 1, wxEXPAND);
    preset_sizer->Add(m_delete_preset_btn   , 0, wxEXPAND | wxLEFT, BORDER_W);

    wxBoxSizer* name_sizer = new wxBoxSizer(wxHORIZONTAL);
    name_sizer->Add(m_info_line, 0, wxEXPAND);
    name_sizer->Add(m_full_printer_name, 0, wxEXPAND | wxLEFT, BORDER_W);

    m_sizer->Add(preset_sizer   , 0, wxEXPAND);
    m_sizer->Add(name_sizer, 0, wxEXPAND);
}

PresetForPrinter::~PresetForPrinter()
{
    m_presets_list->Destroy();
    m_delete_preset_btn->Destroy();
    m_info_line->Destroy();
    m_full_printer_name->Destroy();
}

void PresetForPrinter::DeletePreset(wxEvent& event)
{
    m_parent->DeletePreset(this);
}

void PresetForPrinter::update_full_printer_name()
{
    wxString printer_name   = m_parent->get_printer_name();
    wxString preset_name    = m_presets_list->GetString(m_presets_list->GetSelection());

    m_full_printer_name->SetLabelText(printer_name + " * " + preset_name);
}

std::string PresetForPrinter::get_preset_name()
{
    return into_u8(m_presets_list->GetString(m_presets_list->GetSelection()));
}

void PresetForPrinter::SuppressDelete()
{
    m_delete_preset_btn->Enable(false);
    
    // this case means that now we have only one related preset for the printer
    // So, allow any selection
    m_presets_list->set_printer_technology(ptAny);
    m_presets_list->update();
}

void PresetForPrinter::AllowDelete()
{
    if (!m_delete_preset_btn->IsEnabled())
        m_delete_preset_btn->Enable();

    m_presets_list->set_printer_technology(m_parent->get_printer_technology());
    m_presets_list->update();
}

void PresetForPrinter::msw_rescale()
{
    m_presets_list->msw_rescale();
    m_delete_preset_btn->msw_rescale();
}


//------------------------------------------
//          PhysicalPrinterDialog
//------------------------------------------

PhysicalPrinterDialog::PhysicalPrinterDialog(wxString printer_name)
    : DPIDialog(NULL, wxID_ANY, _L("Physical Printer"), wxDefaultPosition, wxSize(45 * wxGetApp().em_unit(), -1), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetFont(wxGetApp().normal_font());
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    m_default_name = _L("Type here the name of your printer device");
    bool new_printer = true;

    if (printer_name.IsEmpty())
        printer_name = m_default_name;
    else {
        std::string full_name = into_u8(printer_name);
        printer_name = from_u8(PhysicalPrinter::get_short_name(full_name));
        new_printer = false;
    }

    wxStaticText* label_top = new wxStaticText(this, wxID_ANY, _L("Descriptive name for the printer device") + ":");

    m_add_preset_btn = new ScalableButton(this, wxID_ANY, "add_copies", "", wxDefaultSize, wxDefaultPosition, /*wxBU_LEFT | */wxBU_EXACTFIT);
    m_add_preset_btn->SetFont(wxGetApp().normal_font());
    m_add_preset_btn->SetToolTip(_L("Add preset for this printer device")); 
    m_add_preset_btn->Bind(wxEVT_BUTTON, &PhysicalPrinterDialog::AddPreset, this);

    m_printer_name    = new wxTextCtrl(this, wxID_ANY, printer_name, wxDefaultPosition, wxDefaultSize);
    m_printer_name->Bind(wxEVT_TEXT, [this](wxEvent&) { this->update_full_printer_names(); });

    PhysicalPrinterCollection& printers = wxGetApp().preset_bundle->physical_printers;
    PhysicalPrinter* printer = printers.find_printer(into_u8(printer_name));
    if (!printer) {
        const Preset& preset = wxGetApp().preset_bundle->printers.get_edited_preset();
        printer = new PhysicalPrinter(into_u8(printer_name), preset);
        // if printer_name is empty it means that new printer is created, so enable all items in the preset list
        m_presets.emplace_back(new PresetForPrinter(this, preset.name));
    }
    else
    {
        const std::set<std::string>& preset_names = printer->get_preset_names();
        for (const std::string& preset_name : preset_names)
            m_presets.emplace_back(new PresetForPrinter(this, preset_name));
    }
    assert(printer);
    m_printer = *printer;

    if (m_presets.size() == 1)
        m_presets.front()->SuppressDelete();

    update_full_printer_names();

    m_config = &m_printer.config;

    m_optgroup = new ConfigOptionsGroup(this, _L("Print Host upload"), m_config);
    build_printhost_settings(m_optgroup);

    wxStdDialogButtonSizer* btns = this->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    wxButton* btnOK = static_cast<wxButton*>(this->FindWindowById(wxID_OK, this));
    btnOK->Bind(wxEVT_BUTTON, &PhysicalPrinterDialog::OnOK, this);

    wxBoxSizer* nameSizer = new wxBoxSizer(wxHORIZONTAL);
    nameSizer->Add(m_printer_name, 1, wxEXPAND);
    nameSizer->Add(m_add_preset_btn, 0, wxEXPAND | wxLEFT, BORDER_W);

    m_presets_sizer = new wxBoxSizer(wxVERTICAL);
    for (PresetForPrinter* preset : m_presets)
        m_presets_sizer->Add(preset->sizer(), 1, wxEXPAND | wxTOP, BORDER_W);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(label_top           , 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, BORDER_W);
    topSizer->Add(nameSizer           , 0, wxEXPAND | wxLEFT | wxRIGHT, BORDER_W);
    topSizer->Add(m_presets_sizer     , 0, wxEXPAND | wxLEFT | wxRIGHT, BORDER_W);
    topSizer->Add(m_optgroup->sizer   , 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, BORDER_W);
    topSizer->Add(btns                , 0, wxEXPAND | wxALL, BORDER_W); 

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);

    if (new_printer) {
        m_printer_name->SetFocus();
        m_printer_name->SelectAll();
    }
}

PhysicalPrinterDialog::~PhysicalPrinterDialog()
{
    for (PresetForPrinter* preset : m_presets) {
        delete preset;
        preset = nullptr;
    }
}

void PhysicalPrinterDialog::build_printhost_settings(ConfigOptionsGroup* m_optgroup)
{
    m_optgroup->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
        if (opt_key == "authorization_type")
            this->update();
    };

    m_optgroup->append_single_option_line("host_type");

    auto create_sizer_with_btn = [this](wxWindow* parent, ScalableButton** btn, const std::string& icon_name, const wxString& label) {
        *btn = new ScalableButton(parent, wxID_ANY, icon_name, label, wxDefaultSize, wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
        (*btn)->SetFont(wxGetApp().normal_font());

        auto sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(*btn);
        return sizer;
    };

    auto printhost_browse = [=](wxWindow* parent) 
    {
        auto sizer = create_sizer_with_btn(parent, &m_printhost_browse_btn, "browse", _L("Browse") + " " + dots);
        m_printhost_browse_btn->Bind(wxEVT_BUTTON, [=](wxCommandEvent& e) {
            BonjourDialog dialog(this, Preset::printer_technology(m_printer.config));
            if (dialog.show_and_lookup()) {
                m_optgroup->set_value("print_host", std::move(dialog.get_selected()), true);
                m_optgroup->get_field("print_host")->field_changed();
            }
        });

        return sizer;
    };

    auto print_host_test = [=](wxWindow* parent) {
        auto sizer = create_sizer_with_btn(parent, &m_printhost_test_btn, "test", _L("Test"));

        m_printhost_test_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
            std::unique_ptr<PrintHost> host(PrintHost::get_print_host(m_config));
            if (!host) {
                const wxString text = _L("Could not get a valid Printer Host reference");
                show_error(this, text);
                return;
            }
            wxString msg;
            if (host->test(msg)) {
                show_info(this, host->get_test_ok_msg(), _L("Success!"));
            }
            else {
                show_error(this, host->get_test_failed_msg(msg));
            }
            });

        return sizer;
    };

    // Set a wider width for a better alignment
    Option option = m_optgroup->get_option("print_host");
    option.opt.width = Field::def_width_wider();
    Line host_line = m_optgroup->create_single_option_line(option);
    host_line.append_widget(printhost_browse);
    host_line.append_widget(print_host_test);
    m_optgroup->append_line(host_line);

    m_optgroup->append_single_option_line("authorization_type");

    option = m_optgroup->get_option("printhost_apikey");
    option.opt.width = Field::def_width_wider();
    m_optgroup->append_single_option_line(option);

    const auto ca_file_hint = _u8L("HTTPS CA file is optional. It is only needed if you use HTTPS with a self-signed certificate.");

    if (Http::ca_file_supported()) {
        option = m_optgroup->get_option("printhost_cafile");
        option.opt.width = Field::def_width_wider();
        Line cafile_line = m_optgroup->create_single_option_line(option);

        auto printhost_cafile_browse = [=](wxWindow* parent) {
            auto sizer = create_sizer_with_btn(parent, &m_printhost_cafile_browse_btn, "browse", _L("Browse") + " " + dots);
            m_printhost_cafile_browse_btn->Bind(wxEVT_BUTTON, [this, m_optgroup](wxCommandEvent e) {
                static const auto filemasks = _L("Certificate files (*.crt, *.pem)|*.crt;*.pem|All files|*.*");
                wxFileDialog openFileDialog(this, _L("Open CA certificate file"), "", "", filemasks, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
                if (openFileDialog.ShowModal() != wxID_CANCEL) {
                    m_optgroup->set_value("printhost_cafile", std::move(openFileDialog.GetPath()), true);
                    m_optgroup->get_field("printhost_cafile")->field_changed();
                }
                });

            return sizer;
        };

        cafile_line.append_widget(printhost_cafile_browse);
        m_optgroup->append_line(cafile_line);

        Line cafile_hint{ "", "" };
        cafile_hint.full_width = 1;
        cafile_hint.widget = [this, ca_file_hint](wxWindow* parent) {
            auto txt = new wxStaticText(parent, wxID_ANY, ca_file_hint);
            auto sizer = new wxBoxSizer(wxHORIZONTAL);
            sizer->Add(txt);
            return sizer;
        };
        m_optgroup->append_line(cafile_hint);
    }
    else {
        Line line{ "", "" };
        line.full_width = 1;

        line.widget = [ca_file_hint](wxWindow* parent) {
            std::string info = _u8L("HTTPS CA File") + ":\n\t" +
                (boost::format(_u8L("On this system, %s uses HTTPS certificates from the system Certificate Store or Keychain.")) % SLIC3R_APP_NAME).str() +
                "\n\t" + _u8L("To use a custom CA file, please import your CA file into Certificate Store / Keychain.");

            //auto txt = new wxStaticText(parent, wxID_ANY, from_u8((boost::format("%1%\n\n\t%2%") % info % ca_file_hint).str()));
            auto txt = new wxStaticText(parent, wxID_ANY, from_u8((boost::format("%1%\n\t%2%") % info % ca_file_hint).str()));
            txt->SetFont(wxGetApp().normal_font());
            auto sizer = new wxBoxSizer(wxHORIZONTAL);
            sizer->Add(txt, 1, wxEXPAND);
            return sizer;
        };

        m_optgroup->append_line(line);
    }

    for (const std::string& opt_key : std::vector<std::string>{ "login", "password" }) {        
        option = m_optgroup->get_option(opt_key);
        option.opt.width = Field::def_width_wider();
        m_optgroup->append_single_option_line(option);
    }

    update();
}

void PhysicalPrinterDialog::update()
{
    m_optgroup->reload_config();

    const PrinterTechnology tech = Preset::printer_technology(*m_config);
    // Only offer the host type selection for FFF, for SLA it's always the SL1 printer (at the moment)
    if (tech == ptFFF) {
        m_optgroup->show_field("host_type");
        m_optgroup->hide_field("authorization_type");
        for (const std::string& opt_key : std::vector<std::string>{ "login", "password" })
            m_optgroup->hide_field(opt_key);
    }
    else {
        m_optgroup->set_value("host_type", int(PrintHostType::htOctoPrint), false);
        m_optgroup->hide_field("host_type");

        m_optgroup->show_field("authorization_type");

        AuthorizationType auth_type = m_config->option<ConfigOptionEnum<AuthorizationType>>("authorization_type")->value;
        m_optgroup->show_field("printhost_apikey", auth_type == AuthorizationType::atKeyPassword);

        for (const std::string& opt_key : std::vector<std::string>{ "login", "password" })
            m_optgroup->show_field(opt_key, auth_type == AuthorizationType::atUserPassword);
    }

    this->Layout();
}


wxString PhysicalPrinterDialog::get_printer_name()
{
    return m_printer_name->GetValue();
}

void PhysicalPrinterDialog::update_full_printer_names()
{
    for (PresetForPrinter* preset : m_presets)
        preset->update_full_printer_name();

    this->Layout();
}

void PhysicalPrinterDialog::set_printer_technology(PrinterTechnology pt)
{
    m_config->set_key_value("printer_technology", new ConfigOptionEnum<PrinterTechnology>(pt));
    update();
}

PrinterTechnology PhysicalPrinterDialog::get_printer_technology()
{
    return m_printer.printer_technology();
}

void PhysicalPrinterDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int& em = em_unit();

    m_printhost_browse_btn->msw_rescale();
    m_printhost_test_btn->msw_rescale();
    if (m_printhost_cafile_browse_btn)
        m_printhost_cafile_browse_btn->msw_rescale();

    m_optgroup->msw_rescale();

    msw_buttons_rescale(this, em, { wxID_OK, wxID_CANCEL });

    for (PresetForPrinter* preset : m_presets)
        preset->msw_rescale();

    const wxSize& size = wxSize(45 * em, 35 * em);
    SetMinSize(size);

    Fit();
    Refresh();
}

void PhysicalPrinterDialog::OnOK(wxEvent& event)
{
    wxString printer_name = m_printer_name->GetValue();
    if (printer_name.IsEmpty()) {
        warning_catcher(this, _L("The supplied name is empty. It can't be saved."));
        return;
    }
    if (printer_name == m_default_name) {
        warning_catcher(this, _L("You should to change a name of your printer device. It can't be saved."));
        return;
    }

    PhysicalPrinterCollection& printers = wxGetApp().preset_bundle->physical_printers;
    const PhysicalPrinter* existing = printers.find_printer(into_u8(printer_name));
    if (existing && into_u8(printer_name) != printers.get_selected_printer_name())
    {
        wxString msg_text = from_u8((boost::format(_u8L("Printer with name \"%1%\" already exists.")) % printer_name).str());
        msg_text += "\n" + _L("Replace?");
        wxMessageDialog dialog(nullptr, msg_text, _L("Warning"), wxICON_WARNING | wxYES | wxNO);

        if (dialog.ShowModal() == wxID_NO)
            return;
    }

    std::set<std::string> repeat_presets;
    m_printer.reset_presets();
    for (PresetForPrinter* preset : m_presets) {
        if (!m_printer.add_preset(preset->get_preset_name()))
            repeat_presets.emplace(preset->get_preset_name());
    }

    if (!repeat_presets.empty())
    {
        wxString repeatable_presets = "\n";
        for (const std::string& preset_name : repeat_presets)
            repeatable_presets += "    " + from_u8(preset_name) + "\n";
        repeatable_presets += "\n";

        wxString msg_text = from_u8((boost::format(_u8L("Next printer preset(s) is(are) duplicated:%1%"
                                                        "Should I add it(they) just once for the printer \"%2%\" and close the Editing Dialog?")) % repeatable_presets % printer_name).str());
        wxMessageDialog dialog(nullptr, msg_text, _L("Warning"), wxICON_WARNING | wxYES | wxNO);
        if (dialog.ShowModal() == wxID_NO)
            return;
    }

    std::string renamed_from;
    // temporary save previous printer name if it was edited
    if (m_printer.name != into_u8(m_default_name) &&
        m_printer.name != into_u8(printer_name))
        renamed_from = m_printer.name;

    //update printer name, if it was changed
    m_printer.set_name(into_u8(printer_name));

    // save new physical printer
    printers.save_printer(m_printer, renamed_from);

    if (m_printer.preset_names.find(printers.get_selected_printer_preset_name()) == m_printer.preset_names.end()) {
        // select first preset for this printer
        printers.select_printer(m_printer);
        // refresh preset list on Printer Settings Tab
        wxGetApp().get_tab(Preset::TYPE_PRINTER)->select_preset(printers.get_selected_printer_preset_name());
    }
    else
        wxGetApp().get_tab(Preset::TYPE_PRINTER)->update_preset_choice();

    event.Skip();
}

void PhysicalPrinterDialog::AddPreset(wxEvent& event)
{
    m_presets.emplace_back(new PresetForPrinter(this));
    // enable DELETE button for the first preset, if was disabled
    m_presets.front()->AllowDelete();

    m_presets_sizer->Add(m_presets.back()->sizer(), 1, wxEXPAND | wxTOP, BORDER_W);
    update_full_printer_names();

    this->Fit();
}

void PhysicalPrinterDialog::DeletePreset(PresetForPrinter* preset_for_printer)
{
    if (m_presets.size() == 1) {
        wxString msg_text = _L("It's not possible to delete last related preset for the printer.");
        wxMessageDialog dialog(nullptr, msg_text, _L("Infornation"), wxICON_INFORMATION | wxOK);
        dialog.ShowModal();
        return;
    }

    assert(preset_for_printer);
    auto it = std::find(m_presets.begin(), m_presets.end(), preset_for_printer);
    if (it == m_presets.end())
        return;

    const int remove_id = it - m_presets.begin();
    m_presets_sizer->Remove(remove_id);
    delete preset_for_printer;
    m_presets.erase(it);

    if (m_presets.size() == 1)
        m_presets.front()->SuppressDelete();

    this->Layout();
    this->Fit();
}


}}    // namespace Slic3r::GUI
