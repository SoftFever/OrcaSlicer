#include "SavePresetDialog.hpp"

#include <cstddef>
#include <vector>
#include <string>
#include <boost/algorithm/string.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/wupdlock.h>

#include "libslic3r/PresetBundle.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "format.hpp"
#include "Tab.hpp"

using Slic3r::GUI::format_wxstr;

namespace Slic3r {
namespace GUI {

#define BORDER_W 10


//-----------------------------------------------
//          SavePresetDialog::Item
//-----------------------------------------------

SavePresetDialog::Item::Item(Preset::Type type, const std::string& suffix, wxBoxSizer* sizer, SavePresetDialog* parent):
    m_type(type),
    m_parent(parent)
{
    Tab* tab = wxGetApp().get_tab(m_type);
    assert(tab);
    m_presets = tab->get_presets();

    const Preset& sel_preset = m_presets->get_selected_preset();
    std::string preset_name =   sel_preset.is_default ? "Untitled" :
                                sel_preset.is_system ? (boost::format(("%1% - %2%")) % sel_preset.name % suffix).str() :
                                sel_preset.name;

    // if name contains extension
    if (boost::iends_with(preset_name, ".ini")) {
        size_t len = preset_name.length() - 4;
        preset_name.resize(len);
    }

    std::vector<std::string> values;
    for (const Preset& preset : *m_presets) {
        if (preset.is_default || preset.is_system || preset.is_external)
            continue;
        values.push_back(preset.name);
    }

    wxStaticText* label_top = new wxStaticText(m_parent, wxID_ANY, from_u8((boost::format(_utf8(L("Save %s as:"))) % into_u8(tab->title())).str()));

    m_valid_bmp = new wxStaticBitmap(m_parent, wxID_ANY, create_scaled_bitmap("tick_mark", m_parent));

    m_combo = new wxComboBox(m_parent, wxID_ANY, from_u8(preset_name), wxDefaultPosition, wxSize(35 * wxGetApp().em_unit(), -1));
    for (const std::string& value : values)
        m_combo->Append(from_u8(value));

    m_combo->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { update(); });
#ifdef __WXOSX__
    // Under OSX wxEVT_TEXT wasn't invoked after change selection in combobox,
    // So process wxEVT_COMBOBOX too
    m_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { update(); });
#endif //__WXOSX__

    m_valid_label = new wxStaticText(m_parent, wxID_ANY, "");
    m_valid_label->SetFont(wxGetApp().bold_font());

    wxBoxSizer* combo_sizer = new wxBoxSizer(wxHORIZONTAL);
    combo_sizer->Add(m_valid_bmp,   0, wxALIGN_CENTER_VERTICAL | wxRIGHT, BORDER_W);
    combo_sizer->Add(m_combo,       1, wxEXPAND, BORDER_W);

    sizer->Add(label_top,       0, wxEXPAND | wxTOP| wxBOTTOM, BORDER_W);
    sizer->Add(combo_sizer,     0, wxEXPAND | wxBOTTOM, BORDER_W);
    sizer->Add(m_valid_label,   0, wxEXPAND | wxLEFT,   3*BORDER_W);

    if (m_type == Preset::TYPE_PRINTER)
        m_parent->add_info_for_edit_ph_printer(sizer);

    update();
}

void SavePresetDialog::Item::update()
{
    m_preset_name = into_u8(m_combo->GetValue());

    m_valid_type = Valid;
    wxString info_line;

    const char* unusable_symbols = "<>[]:/\\|?*\"";

    const std::string unusable_suffix = PresetCollection::get_suffix_modified();//"(modified)";
    for (size_t i = 0; i < std::strlen(unusable_symbols); i++) {
        if (m_preset_name.find_first_of(unusable_symbols[i]) != std::string::npos) {
            info_line = _L("The supplied name is not valid;") + "\n" +
                        _L("the following characters are not allowed:") + " " + unusable_symbols;
            m_valid_type = NoValid;
            break;
        }
    }

    if (m_valid_type == Valid && m_preset_name.find(unusable_suffix) != std::string::npos) {
        info_line = _L("The supplied name is not valid;") + "\n" +
                    _L("the following suffix is not allowed:") + "\n\t" +
                    from_u8(PresetCollection::get_suffix_modified());
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && m_preset_name == "- default -") {
        info_line = _L("The supplied name is not available.");
        m_valid_type = NoValid;
    }

    const Preset* existing = m_presets->find_preset(m_preset_name, false);
    if (m_valid_type == Valid && existing && (existing->is_default || existing->is_system)) {
        info_line = _L("Cannot overwrite a system profile.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && existing && (existing->is_external)) {
        info_line = _L("Cannot overwrite an external profile.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && existing && m_preset_name != m_presets->get_selected_preset_name())
    {
        if (existing->is_compatible)
            info_line = from_u8((boost::format(_u8L("Preset with name \"%1%\" already exists.")) % m_preset_name).str());
        else
            info_line = from_u8((boost::format(_u8L("Preset with name \"%1%\" already exists and is incompatible with selected printer.")) % m_preset_name).str());
        info_line += "\n" + _L("Note: This preset will be replaced after saving");
        m_valid_type = Warning;
    }

    if (m_valid_type == Valid && m_preset_name.empty()) {
        info_line = _L("The name cannot be empty.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && m_preset_name.find_first_of(' ') == 0) {
        info_line = _L("The name cannot start with space character.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && m_preset_name.find_last_of(' ') == m_preset_name.length()-1) {
        info_line = _L("The name cannot end with space character.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && m_presets->get_preset_name_by_alias(m_preset_name) != m_preset_name) {
        info_line = _L("The name cannot be the same as a preset alias name.");
        m_valid_type = NoValid;
    }

    m_valid_label->SetLabel(info_line);
    m_valid_label->Show(!info_line.IsEmpty());

    update_valid_bmp();

    if (m_type == Preset::TYPE_PRINTER)
        m_parent->update_info_for_edit_ph_printer(m_preset_name);

    m_parent->layout();
}

void SavePresetDialog::Item::update_valid_bmp()
{
    std::string bmp_name =  m_valid_type == Warning ? "exclamation" :
                            m_valid_type == NoValid ? "cross"       : "tick_mark" ;
    m_valid_bmp->SetBitmap(create_scaled_bitmap(bmp_name, m_parent));
}

void SavePresetDialog::Item::accept()
{
    if (m_valid_type == Warning)
        m_presets->delete_preset(m_preset_name);
}


//-----------------------------------------------
//          SavePresetDialog
//-----------------------------------------------

SavePresetDialog::SavePresetDialog(wxWindow* parent, Preset::Type type, std::string suffix)
    : DPIDialog(parent, wxID_ANY, _L("Save preset"), wxDefaultPosition, wxSize(45 * wxGetApp().em_unit(), 5 * wxGetApp().em_unit()), wxDEFAULT_DIALOG_STYLE | wxICON_WARNING | wxRESIZE_BORDER)
{
    build(std::vector<Preset::Type>{type}, suffix);
}

SavePresetDialog::SavePresetDialog(wxWindow* parent, std::vector<Preset::Type> types, std::string suffix)
    : DPIDialog(parent, wxID_ANY, _L("Save preset"), wxDefaultPosition, wxSize(45 * wxGetApp().em_unit(), 5 * wxGetApp().em_unit()), wxDEFAULT_DIALOG_STYLE | wxICON_WARNING | wxRESIZE_BORDER)
{
    build(types, suffix);
}

SavePresetDialog::~SavePresetDialog()
{
    for (auto  item : m_items) {
        delete item;
    }
}

void SavePresetDialog::build(std::vector<Preset::Type> types, std::string suffix)
{
#if defined(__WXMSW__)
    // ys_FIXME! temporary workaround for correct font scaling
    // Because of from wxWidgets 3.1.3 auto rescaling is implemented for the Fonts,
    // From the very beginning set dialog font to the wxSYS_DEFAULT_GUI_FONT
    this->SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
#else
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif // __WXMSW__

    if (suffix.empty())
        suffix = _CTX_utf8(L_CONTEXT("Copy", "PresetName"), "PresetName");

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    m_presets_sizer = new wxBoxSizer(wxVERTICAL);

    // Add first item
    for (Preset::Type type : types)
        AddItem(type, suffix);

    // Add dialog's buttons
    wxStdDialogButtonSizer* btns = this->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    wxButton* btnOK = static_cast<wxButton*>(this->FindWindowById(wxID_OK, this));
    btnOK->Bind(wxEVT_BUTTON,    [this](wxCommandEvent&)        { accept(); });
    btnOK->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt)   { evt.Enable(enable_ok_btn()); });

    topSizer->Add(m_presets_sizer,  0, wxEXPAND | wxALL, BORDER_W);
    topSizer->Add(btns,             0, wxEXPAND | wxALL, BORDER_W);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);

    this->CenterOnScreen();

#ifdef _WIN32
    wxGetApp().UpdateDlgDarkUI(this);
#endif
}

void SavePresetDialog::AddItem(Preset::Type type, const std::string& suffix)
{
    m_items.emplace_back(new Item{type, suffix, m_presets_sizer, this});
}

std::string SavePresetDialog::get_name()
{
    return m_items.front()->preset_name();
}

std::string SavePresetDialog::get_name(Preset::Type type)
{
    for (const Item* item : m_items)
        if (item->type() == type)
            return item->preset_name();
    return "";
}

bool SavePresetDialog::enable_ok_btn() const
{
    for (const Item* item : m_items)
        if (!item->is_valid())
            return false;

    return true;
}

void SavePresetDialog::add_info_for_edit_ph_printer(wxBoxSizer* sizer)
{
    PhysicalPrinterCollection& printers = wxGetApp().preset_bundle->physical_printers;
    m_ph_printer_name = printers.get_selected_printer_name();
    m_old_preset_name = printers.get_selected_printer_preset_name();

    wxString msg_text = from_u8((boost::format(_u8L("You have selected physical printer \"%1%\" \n"
                                                    "with related printer preset \"%2%\"")) %
                                                    m_ph_printer_name % m_old_preset_name).str());
    m_label = new wxStaticText(this, wxID_ANY, msg_text);
    m_label->SetFont(wxGetApp().bold_font());

    wxString choices[] = {"","",""};

    m_action_radio_box = new wxRadioBox(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                        WXSIZEOF(choices), choices, 3, wxRA_SPECIFY_ROWS);
    m_action_radio_box->SetSelection(0);
    m_action_radio_box->Bind(wxEVT_RADIOBOX, [this](wxCommandEvent& e) {
        m_action = (ActionType)e.GetSelection(); });
    m_action = ChangePreset;

    m_radio_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_radio_sizer->Add(m_action_radio_box, 1, wxEXPAND | wxTOP, 2*BORDER_W);

    sizer->Add(m_label,         0, wxEXPAND | wxLEFT | wxTOP,   3*BORDER_W);
    sizer->Add(m_radio_sizer,   1, wxEXPAND | wxLEFT,           3*BORDER_W);
}

void SavePresetDialog::update_info_for_edit_ph_printer(const std::string& preset_name)
{
    bool show = wxGetApp().preset_bundle->physical_printers.has_selection() && m_old_preset_name != preset_name;

    m_label->Show(show);
    m_radio_sizer->ShowItems(show);
    if (!show) {
        this->SetMinSize(wxSize(100,50));
        return;
    }

    wxString msg_text = from_u8((boost::format(_u8L("What would you like to do with \"%1%\" preset after saving?")) % preset_name).str());
    m_action_radio_box->SetLabel(msg_text);

    wxString choices[] = { from_u8((boost::format(_u8L("Change \"%1%\" to \"%2%\" for this physical printer \"%3%\"")) % m_old_preset_name % preset_name % m_ph_printer_name).str()),
                           from_u8((boost::format(_u8L("Add \"%1%\" as a next preset for the the physical printer \"%2%\"")) % preset_name % m_ph_printer_name).str()),
                           from_u8((boost::format(_u8L("Just switch to \"%1%\" preset")) % preset_name).str()) };

    int n = 0;
    for(const wxString& label: choices)
        m_action_radio_box->SetString(n++, label);
}

void SavePresetDialog::layout()
{
    this->Layout();
    this->Fit();
}

void SavePresetDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int& em = em_unit();

    msw_buttons_rescale(this, em, { wxID_OK, wxID_CANCEL });

    for (Item* item : m_items)
        item->update_valid_bmp();

    //const wxSize& size = wxSize(45 * em, 35 * em);
    SetMinSize(/*size*/wxSize(100, 50));

    Fit();
    Refresh();
}

void SavePresetDialog::update_physical_printers(const std::string& preset_name)
{
    if (m_action == UndefAction)
        return;

    PhysicalPrinterCollection& physical_printers = wxGetApp().preset_bundle->physical_printers;
    if (!physical_printers.has_selection())
        return;

    std::string printer_preset_name = physical_printers.get_selected_printer_preset_name();

    if (m_action == Switch)
        // unselect physical printer, if it was selected
        physical_printers.unselect_printer();
    else
    {
        PhysicalPrinter printer = physical_printers.get_selected_printer();

        if (m_action == ChangePreset)
            printer.delete_preset(printer_preset_name);

        if (printer.add_preset(preset_name))
            physical_printers.save_printer(printer);

        physical_printers.select_printer(printer.get_full_name(preset_name));
    }    
}

void SavePresetDialog::accept()
{
    for (Item* item : m_items) {
        item->accept();
        if (item->type() == Preset::TYPE_PRINTER)
            update_physical_printers(item->preset_name());
    }

    EndModal(wxID_OK);
}

}}    // namespace Slic3r::GUI
