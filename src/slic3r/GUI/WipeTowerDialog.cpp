#include <algorithm>
#include <sstream>
#include "WipeTowerDialog.hpp"
#include "BitmapCache.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"
#include "Widgets/Button.hpp"
#include "slic3r/Utils/ColorSpaceConvert.hpp"

#include <wx/sizer.h>

using namespace Slic3r::GUI;

int scale(const int val) { return val * Slic3r::GUI::wxGetApp().em_unit() / 10; }
int ITEM_WIDTH() { return scale(30); }
static const wxColour g_text_color = wxColour(107, 107, 107, 255);

#define ICON_SIZE               wxSize(FromDIP(16), FromDIP(16))
#define TABLE_BORDER            FromDIP(28)
#define HEADER_VERT_PADDING     FromDIP(12)
#define HEADER_BEG_PADDING      FromDIP(30)
#define ICON_GAP                FromDIP(44)
#define HEADER_END_PADDING      FromDIP(24)
#define ROW_VERT_PADDING        FromDIP(6)
#define ROW_BEG_PADDING         FromDIP(20)
#define EDIT_BOXES_GAP          FromDIP(30)
#define ROW_END_PADDING         FromDIP(21)
#define BTN_SIZE                wxSize(FromDIP(58), FromDIP(24))
#define BTN_GAP                 FromDIP(20)
#define TEXT_BEG_PADDING        FromDIP(41)
#define MAX_FLUSH_VALUE         999
#define MIN_WIPING_DIALOG_WIDTH FromDIP(400)

static void update_ui(wxWindow* window)
{
    Slic3r::GUI::wxGetApp().UpdateDarkUI(window);
}

#ifdef _WIN32
#define style wxSP_ARROW_KEYS | wxBORDER_SIMPLE
#else 
#define style wxSP_ARROW_KEYS
#endif

static const int m_max_flush_volume = 750.f;
static const int g_min_flush_volume_from_support = 420.f;
static const int g_flush_volume_to_support = 230;
static const float g_min_flush_multiplier = 0.f;
static const float g_max_flush_multiplier = 3.f;

wxBoxSizer* WipingDialog::create_btn_sizer(long flags)
{
    auto btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer();

    StateColor ok_btn_bg(
        std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    StateColor ok_btn_bd(
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    StateColor ok_btn_text(
        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal)
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
        std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    StateColor calc_btn_bd(
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    StateColor calc_btn_text(
        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal)
    );

    if (flags & wxOK) {
        Button* ok_btn = new Button(this, _L("OK"));
        ok_btn->SetMinSize(BTN_SIZE);
        ok_btn->SetCornerRadius(FromDIP(12));
        ok_btn->SetBackgroundColor(ok_btn_bg);
        ok_btn->SetBorderColor(ok_btn_bd);
        ok_btn->SetTextColor(wxColour("#FFFFFE"));
        ok_btn->SetFocus();
        ok_btn->SetId(wxID_OK);
        btn_sizer->Add(ok_btn, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, BTN_GAP);
    }
    if (flags & wxCANCEL) {
        Button* cancel_btn = new Button(this, _L("Cancel"));
        cancel_btn->SetMinSize(BTN_SIZE);
        cancel_btn->SetCornerRadius(FromDIP(12));
        cancel_btn->SetBackgroundColor(cancel_btn_bg);
        cancel_btn->SetBorderColor(cancel_btn_bd_);
        cancel_btn->SetTextColor(cancel_btn_text);
        cancel_btn->SetId(wxID_CANCEL);
        btn_sizer->Add(cancel_btn, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, BTN_GAP);
    }

    return btn_sizer;

};

// Parent dialog for purging volume adjustments - it fathers WipingPanel widget (that contains all controls) and a button to toggle simple/advanced mode:
WipingDialog::WipingDialog(wxWindow* parent, const std::vector<float>& matrix, const std::vector<float>& extruders, const std::vector<std::string>& extruder_colours,
    int extra_flush_volume, float flush_multiplier)
: wxDialog(parent, wxID_ANY, _(L("Flushing volumes for filament change")), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE/* | wxRESIZE_BORDER*/)
{
    this->SetBackgroundColour(*wxWHITE);
    this->SetMinSize(wxSize(MIN_WIPING_DIALOG_WIDTH, -1));

    // BBS
#ifdef __WINDOWS__
    StateColor calc_btn_bg(
        std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    StateColor calc_btn_bd(
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    StateColor calc_btn_text(
        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal)
    );

    Button* calc_btn = new Button(this, _L("Auto-Calc"));
    calc_btn->SetMinSize(wxSize(FromDIP(75), FromDIP(24)));
    calc_btn->SetCornerRadius(FromDIP(12));
    calc_btn->SetBackgroundColor(calc_btn_bg);
    calc_btn->SetBorderColor(calc_btn_bd);
    calc_btn->SetTextColor(calc_btn_text);
    calc_btn->SetFocus();
    calc_btn->SetId(wxID_RESET);
    calc_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { m_panel_wiping->calc_flushing_volumes(); });
#else
    Button* calc_btn = nullptr;
#endif
    m_panel_wiping = new WipingPanel(this, matrix, extruders, extruder_colours, calc_btn, extra_flush_volume, flush_multiplier);

    auto main_sizer = new wxBoxSizer(wxVERTICAL);

    // set min sizer width according to extruders count
    auto sizer_width = (int)((sqrt(matrix.size()) + 2.8)*ITEM_WIDTH());
    sizer_width = sizer_width > MIN_WIPING_DIALOG_WIDTH ? sizer_width : MIN_WIPING_DIALOG_WIDTH;
    main_sizer->SetMinSize(wxSize(sizer_width, -1));

    main_sizer->Add(m_panel_wiping, 1, wxEXPAND | wxALL, 0);

    auto btn_sizer = create_btn_sizer(wxOK | wxCANCEL);
    main_sizer->Add(btn_sizer, 0, wxBOTTOM | wxRIGHT | wxEXPAND, BTN_GAP);
    SetSizer(main_sizer);
    main_sizer->SetSizeHints(this);

    if (this->FindWindowById(wxID_OK, this)) {
        this->FindWindowById(wxID_OK, this)->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {                 // if OK button is clicked..
            m_output_matrix = m_panel_wiping->read_matrix_values();    // ..query wiping panel and save returned values
            m_output_extruders = m_panel_wiping->read_extruders_values(); // so they can be recovered later by calling get_...()
            EndModal(wxID_OK);
            }, wxID_OK);
    }
    if (this->FindWindowById(wxID_CANCEL, this)) {
        update_ui(static_cast<wxButton*>(this->FindWindowById(wxID_CANCEL, this)));
        this->FindWindowById(wxID_CANCEL, this)->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxCANCEL); });

    }
    this->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) { EndModal(wxCANCEL); });
    this->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_ESCAPE) {
            if (this->IsModal())
                this->EndModal(wxID_CANCEL);
            else 
                this->Close();
        }
        else
            e.Skip();
        });

    wxGetApp().UpdateDlgDarkUI(this);
}

void WipingPanel::create_panels(wxWindow* parent, const int num) {
    for (size_t i = 0; i < num; i++)
    {
        wxPanel* panel = new wxPanel(parent);
        panel->SetBackgroundColour(i % 2 == 0 ? *wxWHITE : wxColour(238, 238, 238));
        auto sizer = new wxBoxSizer(wxHORIZONTAL);
        panel->SetSizer(sizer);

        wxButton *icon = new wxButton(panel, wxID_ANY, wxString("") << i + 1, wxDefaultPosition, ICON_SIZE, wxBORDER_NONE | wxBU_AUTODRAW);
        icon->SetBackgroundColour(m_colours[i]);
        //auto icon_style = icon->GetWindowStyle() & ~(wxBORDER_NONE | wxBORDER_SIMPLE);
        //icon->SetWindowStyle(m_colours[i].Red() > 224 && m_colours[i].Blue() > 224 && m_colours[i].Green() > 224 ? (icon_style | wxBORDER_SIMPLE) : (icon_style | wxBORDER_NONE));
        auto label_clr = m_colours[i].GetLuminance() < 0.51 ? *wxWHITE : *wxBLACK;
        icon->SetForegroundColour(label_clr);
        icon->SetCanFocus(false);

        sizer->AddSpacer(ROW_BEG_PADDING);
        sizer->Add(icon, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, ROW_VERT_PADDING);

        for (unsigned int j = 0; j < num; ++j) {
            edit_boxes[j][i]->Reparent(panel);
            edit_boxes[j][i]->SetBackgroundColour(panel->GetBackgroundColour());
            sizer->AddSpacer(EDIT_BOXES_GAP);
            sizer->Add(edit_boxes[j][i], 0, wxALIGN_CENTER_VERTICAL, 0);
        }
        sizer->AddSpacer(ROW_END_PADDING);

        m_sizer_advanced->Add(panel, 0, wxRIGHT | wxLEFT | wxEXPAND, TABLE_BORDER);
        panel->Layout();
    }
}

// This panel contains all control widgets for both simple and advanced mode (these reside in separate sizers)
WipingPanel::WipingPanel(wxWindow* parent, const std::vector<float>& matrix, const std::vector<float>& extruders, const std::vector<std::string>& extruder_colours, Button* calc_button,
    int extra_flush_volume, float flush_multiplier)
: wxPanel(parent,wxID_ANY, wxDefaultPosition, wxDefaultSize/*,wxBORDER_RAISED*/), m_min_flush_volume(extra_flush_volume), m_max_flush_volume(800)
{
    m_number_of_extruders = (int)(sqrt(matrix.size())+0.001);

    for (const std::string& color : extruder_colours) {
        //unsigned char rgb[3];
        //Slic3r::GUI::BitmapCache::parse_color(color, rgb);
        m_colours.push_back(wxColor(color));
    }

    // Create two switched panels with their own sizers
    m_sizer_simple          = new wxBoxSizer(wxVERTICAL);
    m_sizer_advanced        = new wxBoxSizer(wxVERTICAL);
    m_page_simple			= new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_page_advanced			= new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_page_simple->SetSizer(m_sizer_simple);
    m_page_advanced->SetSizer(m_sizer_advanced);

    update_ui(m_page_simple);
    update_ui(m_page_advanced);

    auto gridsizer_simple   = new wxGridSizer(3, 5, 10);
    m_gridsizer_advanced = new wxGridSizer(m_number_of_extruders + 1, 5, 1);

    // First create controls for advanced mode and assign them to m_page_advanced:
    for (unsigned int i = 0; i < m_number_of_extruders; ++i) {
        edit_boxes.push_back(std::vector<wxTextCtrl*>(0));

        for (unsigned int j = 0; j < m_number_of_extruders; ++j) {
#ifdef _WIN32
            wxTextCtrl* text = new wxTextCtrl(m_page_advanced, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(ITEM_WIDTH(), -1), wxTE_CENTER | wxBORDER_NONE | wxTE_PROCESS_ENTER);
            update_ui(text);
            edit_boxes.back().push_back(text);
#else
            edit_boxes.back().push_back(new wxTextCtrl(m_page_advanced, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(ITEM_WIDTH(), -1)));
#endif
            if (i == j) {
                edit_boxes[i][j]->SetValue(wxString("-"));
                edit_boxes[i][j]->SetEditable(false);
                edit_boxes[i][j]->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent&) {});
                edit_boxes[i][j]->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent&) {});
            }
            else {
                edit_boxes[i][j]->SetValue(wxString("") << int(matrix[m_number_of_extruders * j + i]));

                edit_boxes[i][j]->Bind(wxEVT_TEXT, [this, i, j](wxCommandEvent& e) {
                    wxString str = edit_boxes[i][j]->GetValue();
                    int value = wxAtoi(str);
                    if (value > MAX_FLUSH_VALUE) {
                        str = wxString::Format(("%d"), MAX_FLUSH_VALUE);
                        edit_boxes[i][j]->SetValue(str);
                    }
                    });

                auto on_apply_text_modify = [this, i, j](wxEvent &e) {
                    this->update_warning_texts();
                    e.Skip();
                };

                edit_boxes[i][j]->Bind(wxEVT_TEXT_ENTER, on_apply_text_modify);
                edit_boxes[i][j]->Bind(wxEVT_KILL_FOCUS, on_apply_text_modify);
            }
        }
    }

    // BBS
    header_line_panel = new wxPanel(m_page_advanced, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    header_line_panel->SetBackgroundColour(wxColour(238, 238, 238));
    auto header_line_sizer = new wxBoxSizer(wxHORIZONTAL);
    header_line_panel->SetSizer(header_line_sizer);

    header_line_sizer->AddSpacer(HEADER_BEG_PADDING);
    for (unsigned int i = 0; i < m_number_of_extruders; ++i) {
        wxButton* icon = new wxButton(header_line_panel, wxID_ANY, wxString("") << i + 1, wxDefaultPosition, ICON_SIZE, wxBORDER_NONE | wxBU_AUTODRAW);
        icon->SetBackgroundColour(m_colours[i]);
        //auto icon_style = icon->GetWindowStyle() & ~(wxBORDER_NONE | wxBORDER_SIMPLE);
        //icon->SetWindowStyle(m_colours[i].Red() > 224 && m_colours[i].Blue() > 224 && m_colours[i].Green() > 224 ? (icon_style | wxBORDER_SIMPLE) : (icon_style | wxBORDER_NONE));
        auto label_clr = m_colours[i].GetLuminance() < 0.51  ? *wxWHITE : *wxBLACK;
        icon->SetForegroundColour(label_clr);
        icon->SetCanFocus(false);

        header_line_sizer->AddSpacer(ICON_GAP);
        header_line_sizer->Add(icon, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, HEADER_VERT_PADDING);
    }
    header_line_sizer->AddSpacer(HEADER_END_PADDING);

    m_sizer_advanced->Add(header_line_panel, 0, wxEXPAND | wxTOP | wxRIGHT | wxLEFT, TABLE_BORDER);

    create_panels(m_page_advanced, m_number_of_extruders);

    m_sizer_advanced->AddSpacer(10);
    auto info_str = new wxStaticText(m_page_advanced, wxID_ANY, _(L("Flushing volume (mm³) for each filament pair.")), wxDefaultPosition, wxDefaultSize, 0);
    info_str->SetForegroundColour(g_text_color);
    m_sizer_advanced->Add(info_str, 0, wxEXPAND | wxLEFT, TEXT_BEG_PADDING);

#ifndef __WINDOWS__
    StateColor calc_btn_bg(
        std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    StateColor calc_btn_bd(
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    StateColor calc_btn_text(
        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal)
    );

    Button* calc_btn = new Button(this, _L("Auto-Calc"));
    calc_btn->SetMinSize(wxSize(FromDIP(75), FromDIP(24)));
    calc_btn->SetCornerRadius(FromDIP(12));
    calc_btn->SetBackgroundColor(calc_btn_bg);
    calc_btn->SetBorderColor(calc_btn_bd);
    calc_btn->SetTextColor(calc_btn_text);
    calc_btn->SetFocus();
    calc_btn->SetId(wxID_RESET);
    calc_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { this->calc_flushing_volumes(); });

    calc_button = calc_btn;
#endif
    m_sizer_advanced->Add(calc_button, 0, wxALIGN_RIGHT | wxRIGHT, TEXT_BEG_PADDING);
    m_sizer_advanced->AddSpacer(BTN_SIZE.y);

    // BBS: for tunning flush volumes
    {
        wxBoxSizer* param_sizer = new wxBoxSizer(wxHORIZONTAL);
       
        wxStaticText* flush_multiplier_title = new wxStaticText(m_page_advanced, wxID_ANY, _L("Multiplier"));
        param_sizer->Add(flush_multiplier_title);
        param_sizer->AddSpacer(FromDIP(5));
        m_flush_multiplier_ebox = new wxTextCtrl(m_page_advanced, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(50), -1), wxTE_PROCESS_ENTER);
        char flush_multi_str[32] = { 0 };
        snprintf(flush_multi_str, sizeof(flush_multi_str), "%.2f", flush_multiplier);
        m_flush_multiplier_ebox->SetValue(flush_multi_str);
        param_sizer->Add(m_flush_multiplier_ebox);
        param_sizer->AddStretchSpacer(1);
        m_sizer_advanced->Add(param_sizer, 0, wxEXPAND | wxLEFT, TEXT_BEG_PADDING);

        auto multi_desc_label = new wxStaticText(m_page_advanced, wxID_ANY, _(L("Actual Volume = Flushing Volume * Multiplier")), wxDefaultPosition, wxDefaultSize, 0);
        multi_desc_label->SetForegroundColour(g_text_color);
        m_sizer_advanced->Add(multi_desc_label, 0, wxEXPAND | wxLEFT, TEXT_BEG_PADDING);

        wxString min_flush_str = wxString::Format(_L("Suggestion: Actual Volume in range [%d, %d]"), m_min_flush_volume, m_max_flush_volume);
        m_min_flush_label = new wxStaticText(m_page_advanced, wxID_ANY, min_flush_str, wxDefaultPosition, wxDefaultSize, 0);
        m_min_flush_label->SetForegroundColour(g_text_color);
        m_sizer_advanced->Add(m_min_flush_label, 0, wxEXPAND | wxLEFT, TEXT_BEG_PADDING);

        auto on_apply_text_modify = [this](wxEvent& e) {
            wxString str = m_flush_multiplier_ebox->GetValue();
            float      multiplier = wxAtof(str);
            if (multiplier < g_min_flush_multiplier || multiplier > g_max_flush_multiplier) {
                str = wxString::Format(("%.2f"), multiplier < g_min_flush_multiplier ? g_min_flush_multiplier : g_max_flush_multiplier);
                m_flush_multiplier_ebox->SetValue(str);
                MessageDialog dlg(nullptr,
                    wxString::Format(_L("The multiplier should be in range [%.2f, %.2f]."), g_min_flush_multiplier, g_max_flush_multiplier),
                    _L("Warning"), wxICON_WARNING | wxOK);
                dlg.ShowModal();
            }

            this->update_warning_texts();
            e.Skip();
        };
        m_flush_multiplier_ebox->Bind(wxEVT_TEXT_ENTER, on_apply_text_modify);
        m_flush_multiplier_ebox->Bind(wxEVT_KILL_FOCUS, on_apply_text_modify);

        m_sizer_advanced->AddSpacer(10);
    }
    this->update_warning_texts();

    m_page_advanced->Hide(); 

    // Now the same for simple mode:
    gridsizer_simple->Add(new wxStaticText(m_page_simple, wxID_ANY, wxString("")), 0, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL);
    gridsizer_simple->Add(new wxStaticText(m_page_simple, wxID_ANY, wxString(_(L("unloaded")))), 0, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL);
    gridsizer_simple->Add(new wxStaticText(m_page_simple,wxID_ANY,wxString(_(L("loaded")))), 0, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL);

    auto add_spin_ctrl = [this](std::vector<wxSpinCtrl*>& vec, float initial)
    {
        wxSpinCtrl* spin_ctrl = new wxSpinCtrl(m_page_simple, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(ITEM_WIDTH(), -1), style | wxALIGN_RIGHT, 0, 300, (int)initial);
        update_ui(spin_ctrl);
        vec.push_back(spin_ctrl);

#ifdef __WXOSX__
        // On OSX / Cocoa, wxSpinCtrl::GetValue() doesn't return the new value
        // when it was changed from the text control, so the on_change callback
        // gets the old one, and on_kill_focus resets the control to the old value.
        // As a workaround, we get the new value from $event->GetString and store
        // here temporarily so that we can return it from get_value()
        spin_ctrl->Bind(wxEVT_TEXT, ([spin_ctrl](wxCommandEvent e)
        {
            long value;
            const bool parsed = e.GetString().ToLong(&value);
            int tmp_value = parsed && value >= INT_MIN && value <= INT_MAX ? (int)value : INT_MIN;

            // Forcibly set the input value for SpinControl, since the value 
            // inserted from the keyboard or clipboard is not updated under OSX
            if (tmp_value != INT_MIN) {
                spin_ctrl->SetValue(tmp_value);

                // But in SetValue() is executed m_text_ctrl->SelectAll(), so
                // discard this selection and set insertion point to the end of string
                spin_ctrl->GetText()->SetInsertionPointEnd();
            }
        }), spin_ctrl->GetId());
#endif
    };

    for (unsigned int i=0;i<m_number_of_extruders;++i) {
        add_spin_ctrl(m_old, extruders[2 * i]);
        add_spin_ctrl(m_new, extruders[2 * i+1]);

        auto hsizer = new wxBoxSizer(wxHORIZONTAL);
        wxWindow* w = new wxWindow(m_page_simple, wxID_ANY, wxDefaultPosition, ICON_SIZE, wxBORDER_SIMPLE);
        w->SetCanFocus(false);
        w->SetBackgroundColour(m_colours[i]);
        hsizer->Add(w, wxALIGN_CENTER_VERTICAL);
        hsizer->AddSpacer(10);
        hsizer->Add(new wxStaticText(m_page_simple, wxID_ANY, wxString(_(L("Filament #"))) << i + 1 << ": "), 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);

        gridsizer_simple->Add(hsizer, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);
        gridsizer_simple->Add(m_old.back(),0);
        gridsizer_simple->Add(m_new.back(),0);
    }

    m_sizer = new wxBoxSizer(wxVERTICAL);
    m_sizer->Add(m_page_simple, 0, wxEXPAND, 0);
    m_sizer->Add(m_page_advanced, 0, wxEXPAND, 0);

    m_sizer->SetSizeHints(this);
    SetSizer(m_sizer);
    this->Layout();

    toggle_advanced(); // to show/hide what is appropriate
    
    header_line_panel->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxPaintDC dc(header_line_panel);
        wxString from_text = _L("From");
        wxString to_text = _L("To");
        wxSize from_text_size = dc.GetTextExtent(from_text);
        wxSize to_text_size = dc.GetTextExtent(to_text);

        int base_y = (header_line_panel->GetSize().y - from_text_size.y - to_text_size.y) / 2;
        int vol_width = ROW_BEG_PADDING + EDIT_BOXES_GAP / 2 + ICON_SIZE.x;
        int base_x = (vol_width - from_text_size.x - to_text_size.x) / 2;

        // draw from text
        int x = base_x;
        int y = base_y + to_text_size.y;
        dc.DrawText(from_text, x, y);

        // draw to text
        x = base_x + from_text_size.x;
        y = base_y;
        dc.DrawText(to_text, x, y);

        // draw a line
        int p1_x = base_x + from_text_size.x - to_text_size.y;
        int p1_y = base_y;
        int p2_x = base_x + from_text_size.x + from_text_size.y;
        int p2_y = base_y + from_text_size.y + to_text_size.y;
        dc.SetPen(wxPen(wxColour(172, 172, 172, 1)));
        dc.DrawLine(p1_x, p1_y, p2_x, p2_y);
    });
}

static float to_radians(float degree)
{
    return degree / 180.f * M_PI;
}

float DeltaHS_BBS(float h1, float s1, float v1, float h2, float s2, float v2)
{
    float h1_rad = to_radians(h1);
    float h2_rad = to_radians(h2);

    float dx = std::cos(h1_rad) * s1 * v1 - cos(h2_rad) * s2 * v2;
    float dy = std::sin(h1_rad) * s1 * v1 - sin(h2_rad) * s2 * v2;
    float dxy = std::sqrt(dx * dx + dy * dy);
    return std::min(1.2f, dxy);
}

static float get_luminance(float r, float g, float b)
{
    return r * 0.3 + g * 0.59 + b * 0.11;
}

static float calc_triangle_3rd_edge(float edge_a, float edge_b, float degree_ab)
{
    return std::sqrt(edge_a * edge_a + edge_b * edge_b - 2 * edge_a * edge_b * std::cos(to_radians(degree_ab)));
}

int WipingPanel::calc_flushing_volume(const wxColour& from, const wxColour& to)
{
    float from_hsv_h, from_hsv_s, from_hsv_v;
    float to_hsv_h, to_hsv_s, to_hsv_v;

    // Calculate color distance in HSV color space
    RGB2HSV((float)from.Red() / 255.f, (float)from.Green() / 255.f, (float)from.Blue() / 255.f, &from_hsv_h, &from_hsv_s, &from_hsv_v);
    RGB2HSV((float)to.Red() / 255.f, (float)to.Green() / 255.f, (float)to.Blue() / 255.f, &to_hsv_h, &to_hsv_s, &to_hsv_v);
    float hs_dist = DeltaHS_BBS(from_hsv_h, from_hsv_s, from_hsv_v, to_hsv_h, to_hsv_s, to_hsv_v);

    // 1. Color difference is more obvious if the dest color has high luminance
    // 2. Color difference is more obvious if the source color has low luminance
    float from_lumi = get_luminance((float)from.Red() / 255.f, (float)from.Green() / 255.f, (float)from.Blue() / 255.f);
    float to_lumi = get_luminance((float)to.Red() / 255.f, (float)to.Green() / 255.f, (float)to.Blue() / 255.f);
    float lumi_flush = 0.f;
    if (to_lumi >= from_lumi) {
        lumi_flush = std::pow(to_lumi - from_lumi, 0.7f) * 560.f;
    }
    else {
        lumi_flush = (from_lumi - to_lumi) * 80.f;

        float inter_hsv_v = 0.67 * to_hsv_v + 0.33 * from_hsv_v;
        hs_dist = std::min(inter_hsv_v, hs_dist);
    }
    float hs_flush = 230.f * hs_dist;

    float flush_volume = calc_triangle_3rd_edge(hs_flush, lumi_flush, 120.f);
    flush_volume = std::max(flush_volume, 60.f);

    //float flush_multiplier = std::atof(m_flush_multiplier_ebox->GetValue().c_str());
    flush_volume += m_min_flush_volume;
    return std::min((int)flush_volume, m_max_flush_volume);
}

void WipingPanel::update_warning_texts()
{
    static const wxColour g_warning_color = *wxRED;
    static const wxColour g_normal_color = *wxBLACK;

    wxString multi_str = m_flush_multiplier_ebox->GetValue();
    float multiplier = wxAtof(multi_str);

    bool has_exception_flush = false;
    for (int i = 0; i < edit_boxes.size(); i++) {
        auto& box_vec = edit_boxes[i];
        for (int j = 0; j < box_vec.size(); j++) {
            if (i == j)
                continue;

            auto text_box = box_vec[j];
            wxString str = text_box->GetValue();
            int actual_volume = wxAtoi(str) * multiplier;
            if (actual_volume < m_min_flush_volume || actual_volume > m_max_flush_volume) {
                if (text_box->GetForegroundColour() != g_warning_color) {
                    text_box->SetForegroundColour(g_warning_color);
                    text_box->Refresh();
                }
                has_exception_flush = true;
            }
            else {
                if (text_box->GetForegroundColour() != g_normal_color) {
                    text_box->SetForegroundColour(StateColor::darkModeColorFor(g_normal_color));
                    text_box->Refresh();
                }
            }
        }
    }

    if (has_exception_flush && m_min_flush_label->GetForegroundColour() != g_warning_color) {
        m_min_flush_label->SetForegroundColour(g_warning_color);
        m_min_flush_label->Refresh();
    }
    else if (!has_exception_flush && m_min_flush_label->GetForegroundColour() != g_text_color) {
        m_min_flush_label->SetForegroundColour(g_text_color);
        m_min_flush_label->Refresh();
    }
}

void WipingPanel::calc_flushing_volumes()
{
    for (int from_idx = 0; from_idx < m_colours.size(); from_idx++) {
        const wxColour& from = m_colours[from_idx];
        bool is_from_support = is_support_filament(from_idx);
        for (int to_idx = 0; to_idx < m_colours.size(); to_idx++) {
            bool is_to_support = is_support_filament(to_idx);
            if (from_idx == to_idx) {
                edit_boxes[to_idx][from_idx]->SetValue(std::to_string(0));
            }
            else {
                int flushing_volume = 0;
                if (is_to_support) {
                    flushing_volume = g_flush_volume_to_support;
                }
                else {
                    const wxColour& to = m_colours[to_idx];
                    flushing_volume = calc_flushing_volume(from, to);
                    if (is_from_support) {
                        flushing_volume = std::max(g_min_flush_volume_from_support, flushing_volume);
                    }
                }

                edit_boxes[to_idx][from_idx]->SetValue(std::to_string(flushing_volume));
            }
        }
    }

    this->update_warning_texts();
}


// Reads values from the (advanced) wiping matrix:
std::vector<float> WipingPanel::read_matrix_values() {
    if (!m_advanced)
        fill_in_matrix();
    std::vector<float> output;
    for (unsigned int i=0;i<m_number_of_extruders;++i) {
        for (unsigned int j=0;j<m_number_of_extruders;++j) {
            double val = 0.;
            edit_boxes[j][i]->GetValue().ToDouble(&val);
            output.push_back((float)val);
        }
    }
    return output;
}

// Reads values from simple mode to save them for next time:
std::vector<float> WipingPanel::read_extruders_values() {
    std::vector<float> output;
    for (unsigned int i=0;i<m_number_of_extruders;++i) {
        output.push_back(m_old[i]->GetValue());
        output.push_back(m_new[i]->GetValue());
    }
    return output;
}

// This updates the "advanced" matrix based on values from "simple" mode
void WipingPanel::fill_in_matrix() {
    for (unsigned i=0;i<m_number_of_extruders;++i) {
        for (unsigned j=0;j<m_number_of_extruders;++j) {
            if (i==j) continue;
                edit_boxes[j][i]->SetValue(wxString("")<< (m_old[i]->GetValue() + m_new[j]->GetValue()));
        }
    }
}



// Function to check if simple and advanced settings are matching
bool WipingPanel::advanced_matches_simple() {
    for (unsigned i=0;i<m_number_of_extruders;++i) {
        for (unsigned j=0;j<m_number_of_extruders;++j) {
            if (i==j) continue;
            if (edit_boxes[j][i]->GetValue() != (wxString("")<< (m_old[i]->GetValue() + m_new[j]->GetValue())))
                return false;
        }
    }
    return true;
}


// Switches the dialog from simple to advanced mode and vice versa
void WipingPanel::toggle_advanced(bool user_action) {
    if (user_action)
        m_advanced = !m_advanced;                // user demands a change -> toggle
    else {
        // BBS: show advanced mode by default
        //m_advanced = !advanced_matches_simple(); // if called from constructor, show what is appropriate
        m_advanced = true;
    }

    (m_advanced ? m_page_advanced : m_page_simple)->Show();
    (!m_advanced ? m_page_advanced : m_page_simple)->Hide();

    if (m_advanced)
        if (user_action) fill_in_matrix();  // otherwise keep values loaded from config

   m_sizer->Layout();
   Refresh();
}
