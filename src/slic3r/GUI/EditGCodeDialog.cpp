#include "EditGCodeDialog.hpp"

#include <vector>
#include <string>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/listbox.h>
#include <wx/statbox.h>
#include <wx/wupdlock.h>

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Tab.hpp"
#include "wxExtensions.hpp"
#include "BitmapCache.hpp"
#include "MsgDialog.hpp"

#define BTN_GAP  FromDIP(20)
#define BTN_SIZE wxSize(FromDIP(58), FromDIP(24))

namespace Slic3r {
namespace GUI {

static wxArrayString get_patterns_list()
{
    wxArrayString patterns;
    for (const wxString& item : {          
          ";comment"//format_wxstr(";%1%",_L("comment"))
        , "M862.3 P \"[printer_model]\" ; printer model check"
        , "M862.1 P[nozzle_diameter]; nozzle diameter check"
        , "M115 U3.12.2; tell printer latest fw version"
        , "G90; use absolute coordinates"
        , "M83; extruder relative mode"
        , "M104 S[first_layer_temperature]; set extruder temp"
        , "M140 S[first_layer_bed_temperature]; set bed temp"
        , "M190 S[first_layer_bed_temperature]; wait for bed temp"
        , "M109 S[first_layer_temperature]; wait for extruder temp"
        , "G28 W; home all without mesh bed level"
        , "G80; mesh bed leveling"
        , "M403 E0 F {\n"
          " + ((filament_type[0] == \"FLEX\") ? 1 : ((filament_type[0] == \"PVA\") ? 2 : 0))\n"
          "}"
        , "{if not OPTION}"
        , "G1"
        , "T[initial_tool]; select extruder"
        , "G92 E0"
        , "{endif}"
    })
        patterns.Add(item);
    return patterns;
}

//------------------------------------------
//          EditGCodeDialog
//------------------------------------------

EditGCodeDialog::EditGCodeDialog(wxWindow* parent, const std::string& key, const std::string& value) :
    DPIDialog(parent, wxID_ANY, format_wxstr(_L("Edit Custom G-code (%1%)"), key), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetFont(wxGetApp().normal_font());
    SetBackgroundColour(*wxWHITE);
    wxGetApp().UpdateDarkUI(this);

    int border = 10;
    int em = em_unit();

    wxStaticText* label_top = new wxStaticText(this, wxID_ANY, _L("Edit your custom G-code using patterns"));
    label_top->SetFont(wxGetApp().bold_font());

    auto* grid_sizer = new wxFlexGridSizer(1, 3, 5, 15);
    grid_sizer->SetFlexibleDirection(wxBOTH);

    m_patterns_list = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(em * 15, em * 30), get_patterns_list(), wxLB_SINGLE | wxLB_NEEDED_SB | wxLB_SORT
#ifdef _WIN32
        | wxBORDER_SIMPLE
#endif
    );
    m_patterns_list->SetFont(wxGetApp().code_font());
    wxGetApp().UpdateDarkUI(m_patterns_list);

    m_add_btn = new ScalableButton(this, wxID_ANY, "add_copies");
    m_add_btn->SetToolTip(_L("Add selected pettern to the G-code"));

    m_gcode_editor = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition, wxSize(em * 45, em * 30), wxTE_MULTILINE
#ifdef _WIN32
    | wxBORDER_SIMPLE
#endif
    );
    m_gcode_editor->SetFont(wxGetApp().code_font());
    wxGetApp().UpdateDarkUI(m_gcode_editor);

    grid_sizer->Add(m_patterns_list,    1, wxEXPAND);
    grid_sizer->Add(m_add_btn,          0, wxALIGN_CENTER_VERTICAL);
    grid_sizer->Add(m_gcode_editor,     2, wxEXPAND);

    grid_sizer->AddGrowableRow(0, 1);
    grid_sizer->AddGrowableCol(0, 1);
    grid_sizer->AddGrowableCol(2, 1);

    auto btn_sizer = create_btn_sizer(wxOK | wxCANCEL);
    for(auto btn : m_button_list)
        wxGetApp().UpdateDarkUI(btn.second);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(label_top           , 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(grid_sizer          , 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(btn_sizer                , 0, wxEXPAND | wxALL, border);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);

    this->Fit();
    this->Layout();

    this->CenterOnScreen();

    wxGetApp().UpdateDlgDarkUI(this);

    m_patterns_list->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent& evt) {
        wxString val = m_patterns_list->GetString(m_patterns_list->GetSelection());
        assert(!val.IsEmpty());
        auto insert_to = m_gcode_editor->GetInsertionPoint();
        m_gcode_editor->WriteText(val);
    });
}

void EditGCodeDialog::on_dpi_changed(const wxRect&suggested_rect)
{
    const int& em = em_unit();

    //m_optgroup->msw_rescale();

//    msw_buttons_rescale(this, em, { wxID_OK, wxID_CANCEL });

    for (auto button_item : m_button_list)
    {
        if (button_item.first == wxRESET)
        {
            button_item.second->SetMinSize(wxSize(FromDIP(75), FromDIP(24)));
            button_item.second->SetCornerRadius(FromDIP(12));
        }
        if (button_item.first == wxOK) {
            button_item.second->SetMinSize(BTN_SIZE);
            button_item.second->SetCornerRadius(FromDIP(12));
        }
        if (button_item.first == wxCANCEL) {
            button_item.second->SetMinSize(BTN_SIZE);
            button_item.second->SetCornerRadius(FromDIP(12));
        }
    }

    const wxSize& size = wxSize(45 * em, 35 * em);
    SetMinSize(size);

    Fit();
    Refresh();
}

void EditGCodeDialog::on_sys_color_changed()
{
    m_add_btn->sys_color_changed();
}

void EditGCodeDialog::OnOK(wxEvent& event)
{
    event.Skip();
}

//Orca
wxBoxSizer* EditGCodeDialog::create_btn_sizer(long flags)
{
    auto btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer();

    StateColor ok_btn_bg(
        std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    StateColor ok_btn_bd(
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    StateColor ok_btn_text(
        std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal)
    );

    StateColor cancel_btn_bg(
        std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal)
    );

    StateColor cancel_btn_bd_(
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal)
    );

    StateColor cancel_btn_text(
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal)
    );


    StateColor calc_btn_bg(
        std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    StateColor calc_btn_bd(
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    StateColor calc_btn_text(
        std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal)
    );

    if (flags & wxOK) {
        Button* ok_btn = new Button(this, _L("OK"));
        ok_btn->SetMinSize(BTN_SIZE);
        ok_btn->SetCornerRadius(FromDIP(12));
        ok_btn->SetBackgroundColor(ok_btn_bg);
        ok_btn->SetBorderColor(ok_btn_bd);
        ok_btn->SetTextColor(ok_btn_text);
        ok_btn->SetFocus();
        ok_btn->SetId(wxID_OK);
        btn_sizer->Add(ok_btn, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, BTN_GAP);
        m_button_list[wxOK] = ok_btn;
    }
    if (flags & wxCANCEL) {
        Button* cancel_btn = new Button(this, _L("Cancel"));
        cancel_btn->SetMinSize(BTN_SIZE);
        cancel_btn->SetCornerRadius(FromDIP(12));
        cancel_btn->SetBackgroundColor(cancel_btn_bg);
        cancel_btn->SetBorderColor(cancel_btn_bd_);
        cancel_btn->SetTextColor(cancel_btn_text);
        cancel_btn->SetId(wxID_CANCEL);
        btn_sizer->Add(cancel_btn, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, BTN_GAP / 2);
        m_button_list[wxCANCEL] = cancel_btn;
    }

    return btn_sizer;
}

}}    // namespace Slic3r::GUI
