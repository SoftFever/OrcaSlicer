#include <algorithm>
#include <sstream>
//#include "libslic3r/FlushVolCalc.hpp"
#include "ObjColorDialog.hpp"
#include "BitmapCache.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"
#include "Widgets/Button.hpp"
#include "slic3r/Utils/ColorSpaceConvert.hpp"
#include "MainFrame.hpp"
#include "libslic3r/Config.hpp"
#include "BitmapComboBox.hpp"
#include "Widgets/ComboBox.hpp"
#include <wx/sizer.h>

#include "libslic3r/ObjColorUtils.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI;

int objcolor_scale(const int val) { return val * Slic3r::GUI::wxGetApp().em_unit() / 10; }
int OBJCOLOR_ITEM_WIDTH() { return objcolor_scale(30); }
static const wxColour g_text_color = wxColour(107, 107, 107, 255);
const int HEADER_BORDER  = 5;
const int CONTENT_BORDER = 3;
const int PANEL_WIDTH = 370;
const int COLOR_LABEL_WIDTH = 180;

#undef  ICON_SIZE
#define ICON_SIZE                 wxSize(FromDIP(16), FromDIP(16))
#define MIN_OBJCOLOR_DIALOG_WIDTH FromDIP(400)
#define FIX_SCROLL_HEIGTH         FromDIP(400)
#define BTN_SIZE                  wxSize(FromDIP(58), FromDIP(24))
#define BTN_GAP                   FromDIP(20)

static void update_ui(wxWindow* window)
{
    Slic3r::GUI::wxGetApp().UpdateDarkUI(window);
}

static const char g_min_cluster_color = 1;
//static const char g_max_cluster_color = 15;
static const char g_max_color = 16;
const  StateColor ok_btn_bg(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed),
                     std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
                     std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal));
const StateColor  ok_btn_disable_bg(std::pair<wxColour, int>(wxColour(205, 201, 201), StateColor::Pressed),
                                   std::pair<wxColour, int>(wxColour(205, 201, 201), StateColor::Hovered),
                                   std::pair<wxColour, int>(wxColour(205, 201, 201), StateColor::Normal));
wxBoxSizer* ObjColorDialog::create_btn_sizer(long flags)
{
    auto btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer();

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
        ok_btn->Enable(false);
        ok_btn->SetBackgroundColor(ok_btn_disable_bg);
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
        btn_sizer->Add(cancel_btn, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, BTN_GAP);
        m_button_list[wxCANCEL] = cancel_btn;
    }
    return btn_sizer;
}

void ObjColorDialog::on_dpi_changed(const wxRect &suggested_rect)
{
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
    m_panel_ObjColor->msw_rescale();
    this->Refresh();
};

ObjColorDialog::ObjColorDialog(wxWindow *                      parent,
                               std::vector<Slic3r::RGBA> &     input_colors,
                               bool                            is_single_color,
                               const std::vector<std::string> &extruder_colours,
                               std::vector<unsigned char> &    filament_ids,
                               unsigned char &                 first_extruder_id)
    : DPIDialog(parent ? parent : static_cast<wxWindow *>(wxGetApp().mainframe),
                wxID_ANY,
                _(L("Obj file Import color")),
                wxDefaultPosition,
                wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE /* | wxRESIZE_BORDER*/)
    , m_filament_ids(filament_ids)
    , m_first_extruder_id(first_extruder_id)
{
    std::string icon_path = (boost::format("%1%/images/OrcaSlicerTitle.ico") % Slic3r::resources_dir()).str();
    SetIcon(wxIcon(Slic3r::encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    auto m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));

    this->SetBackgroundColour(*wxWHITE);
    this->SetMinSize(wxSize(MIN_OBJCOLOR_DIALOG_WIDTH, -1));

    m_panel_ObjColor = new ObjColorPanel(this, input_colors, is_single_color, extruder_colours, filament_ids, first_extruder_id);

    auto main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_line_top, 0, wxEXPAND, 0);
    // set min sizer width according to extruders count
    auto sizer_width = (int) (2.8 * OBJCOLOR_ITEM_WIDTH());
    sizer_width      = sizer_width > MIN_OBJCOLOR_DIALOG_WIDTH ? sizer_width : MIN_OBJCOLOR_DIALOG_WIDTH;
    main_sizer->SetMinSize(wxSize(sizer_width, -1));
    main_sizer->Add(m_panel_ObjColor, 1, wxEXPAND | wxALL, 0);

    auto btn_sizer = create_btn_sizer(wxOK | wxCANCEL);
    {
        m_button_list[wxOK]->Bind(wxEVT_UPDATE_UI, ([this](wxUpdateUIEvent &e) {
           if (m_panel_ObjColor->is_ok() == m_button_list[wxOK]->IsEnabled()) { return; }
           m_button_list[wxOK]->Enable(m_panel_ObjColor->is_ok());
           m_button_list[wxOK]->SetBackgroundColor(m_panel_ObjColor->is_ok() ? ok_btn_bg : ok_btn_disable_bg);
         }));
    }
    main_sizer->Add(btn_sizer, 0, wxBOTTOM | wxRIGHT | wxEXPAND, BTN_GAP);
    SetSizer(main_sizer);
    main_sizer->SetSizeHints(this);

    if (this->FindWindowById(wxID_OK, this)) {
        this->FindWindowById(wxID_OK, this)->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {// if OK button is clicked..
              m_panel_ObjColor->update_filament_ids();
              EndModal(wxID_OK);
            }, wxID_OK);
    }
    if (this->FindWindowById(wxID_CANCEL, this)) {
        update_ui(static_cast<wxButton*>(this->FindWindowById(wxID_CANCEL, this)));
        this->FindWindowById(wxID_CANCEL, this)->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxCANCEL); });
    }
    this->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) { EndModal(wxCANCEL); });

    wxGetApp().UpdateDlgDarkUI(this);
}
RGBA     convert_to_rgba(const wxColour &color)
{
    RGBA rgba;
    rgba[0] = std::clamp(color.Red() / 255.f, 0.f, 1.f);
    rgba[1] = std::clamp(color.Green() / 255.f, 0.f, 1.f);
    rgba[2] = std::clamp(color.Blue() / 255.f, 0.f, 1.f);
    rgba[3] = std::clamp(color.Alpha() / 255.f, 0.f, 1.f);
    return rgba;
}
wxColour convert_to_wxColour(const RGBA &color)
{
    auto     r = std::clamp((int) (color[0] * 255.f), 0, 255);
    auto     g = std::clamp((int) (color[1] * 255.f), 0, 255);
    auto     b = std::clamp((int) (color[2] * 255.f), 0, 255);
    auto     a = std::clamp((int) (color[3] * 255.f), 0, 255);
    wxColour wx_color(r,g,b,a);
    return wx_color;
}
// This panel contains all control widgets for both simple and advanced mode (these reside in separate sizers)
ObjColorPanel::ObjColorPanel(wxWindow *                       parent,
                             std::vector<Slic3r::RGBA>&       input_colors,
                             bool                             is_single_color,
                             const std::vector<std::string>&  extruder_colours,
                             std::vector<unsigned char> &    filament_ids,
                             unsigned char &                 first_extruder_id)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize /*,wxBORDER_RAISED*/)
    , m_input_colors(input_colors)
    , m_filament_ids(filament_ids)
    , m_first_extruder_id(first_extruder_id)
{
    if (input_colors.size() == 0) { return; }
    for (const std::string& color : extruder_colours) {
        m_colours.push_back(wxColor(color));
    }
    //deal input_colors
    m_input_colors_size = input_colors.size();
    for (size_t i = 0; i < input_colors.size(); i++) {
        if (color_is_equal(input_colors[i] , UNDEFINE_COLOR)) { // not define color range:0~1
            input_colors[i]=convert_to_rgba( m_colours[0]);
        }
    }
    if (is_single_color && input_colors.size() >=1) {
        m_cluster_colors_from_algo.emplace_back(input_colors[0]);
        m_cluster_colours.emplace_back(convert_to_wxColour(input_colors[0]));
        m_cluster_labels_from_algo.reserve(m_input_colors_size);
        for (size_t i = 0; i < m_input_colors_size; i++) {
            m_cluster_labels_from_algo.emplace_back(0);
        }
        m_cluster_map_filaments.resize(m_cluster_colors_from_algo.size());
        m_color_num_recommend = m_color_cluster_num_by_algo = m_cluster_colors_from_algo.size();
    } else {//cluster deal
        deal_algo(-1);
    }
    //end first cluster
    //draw ui
    auto sizer_width = FromDIP(300);
    // Create two switched panels with their own sizers
    m_sizer_simple          = new wxBoxSizer(wxVERTICAL);
    m_page_simple			= new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_page_simple->SetSizer(m_sizer_simple);
    m_page_simple->SetBackgroundColour(*wxWHITE);

    update_ui(m_page_simple);
    // BBS
    m_sizer_simple->AddSpacer(FromDIP(10));
    // BBS: for tunning flush volumes
    {
        //color cluster results
        wxBoxSizer *  specify_cluster_sizer               = new wxBoxSizer(wxHORIZONTAL);
        wxStaticText *specify_color_cluster_title = new wxStaticText(m_page_simple, wxID_ANY, _L("Specify number of colors:"));
        specify_color_cluster_title->SetFont(Label::Head_14);
        specify_cluster_sizer->Add(specify_color_cluster_title, 0, wxALIGN_CENTER | wxALL, FromDIP(5));

        m_color_cluster_num_by_user_ebox = new wxTextCtrl(m_page_simple, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(25), -1), wxTE_PROCESS_ENTER);
        m_color_cluster_num_by_user_ebox->SetValue(std::to_string(m_color_cluster_num_by_algo).c_str());
        {//event
            auto on_apply_color_cluster_text_modify = [this](wxEvent &e) {
                wxString str        = m_color_cluster_num_by_user_ebox->GetValue();
                int      number = wxAtoi(str);
                if (number > m_color_num_recommend || number < g_min_cluster_color) {
                    number = number < g_min_cluster_color ? g_min_cluster_color : m_color_num_recommend;
                    str    = wxString::Format(("%d"), number);
                    m_color_cluster_num_by_user_ebox->SetValue(str);
                    MessageDialog dlg(nullptr, wxString::Format(_L("The color count should be in range [%d, %d]."), g_min_cluster_color, m_color_num_recommend),
                                      _L("Warning"), wxICON_WARNING | wxOK);
                    dlg.ShowModal();
                }
                e.Skip();
            };
            m_color_cluster_num_by_user_ebox->Bind(wxEVT_TEXT_ENTER, on_apply_color_cluster_text_modify);
            m_color_cluster_num_by_user_ebox->Bind(wxEVT_KILL_FOCUS, on_apply_color_cluster_text_modify);
            m_color_cluster_num_by_user_ebox->Bind(wxEVT_COMMAND_TEXT_UPDATED, [this](wxCommandEvent &) {
                wxString str        = m_color_cluster_num_by_user_ebox->GetValue();
                int    number = wxAtof(str);
                if (number > m_color_num_recommend || number < g_min_cluster_color) {
                    number = number < g_min_cluster_color ? g_min_cluster_color : m_color_num_recommend;
                    str    = wxString::Format(("%d"), number);
                    m_color_cluster_num_by_user_ebox->SetValue(str);
                    m_color_cluster_num_by_user_ebox->SetInsertionPointEnd();
                }
                if (m_last_cluster_num != number) {
                    deal_algo(number, true);
                    Layout();
                    //Fit();
                    Refresh();
                    Update();
                    m_last_cluster_num = number;
                }
            });
            m_color_cluster_num_by_user_ebox->Bind(wxEVT_CHAR, [this](wxKeyEvent &e) {
                int keycode = e.GetKeyCode();
                wxString input_char = wxString::Format("%c", keycode);
                long     value;
                if (!input_char.ToLong(&value))
                    return;
                e.Skip();
            });
        }
        specify_cluster_sizer->AddSpacer(FromDIP(2));
        specify_cluster_sizer->Add(m_color_cluster_num_by_user_ebox, 0, wxALIGN_CENTER | wxALL, 0);
        specify_cluster_sizer->AddSpacer(FromDIP(15));
        wxStaticText *recommend_color_cluster_title = new wxStaticText(m_page_simple, wxID_ANY, "(" + std::to_string(m_color_num_recommend) + " " + _L("Recommended ") + ")");
        specify_cluster_sizer->Add(recommend_color_cluster_title, 0, wxALIGN_CENTER | wxALL, 0);

        m_sizer_simple->Add(specify_cluster_sizer, 0, wxEXPAND | wxLEFT, FromDIP(20));

        wxBoxSizer *  current_filaments_title_sizer  = new wxBoxSizer(wxHORIZONTAL);
        wxStaticText *current_filaments_title = new wxStaticText(m_page_simple, wxID_ANY, _L("Current filament colors:"));
        current_filaments_title->SetFont(Label::Head_14);
        current_filaments_title_sizer->Add(current_filaments_title, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
        m_sizer_simple->Add(current_filaments_title_sizer, 0, wxEXPAND | wxLEFT, FromDIP(20));

        wxBoxSizer *  current_filaments_sizer = new wxBoxSizer(wxHORIZONTAL);
        current_filaments_sizer->AddSpacer(FromDIP(10));
        for (size_t i = 0; i < m_colours.size(); i++) {
            auto extruder_icon_sizer = create_extruder_icon_and_rgba_sizer(m_page_simple, i, m_colours[i]);
            current_filaments_sizer->Add(extruder_icon_sizer, 0, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL, FromDIP(10));
        }
        m_sizer_simple->Add(current_filaments_sizer, 0, wxEXPAND | wxLEFT, FromDIP(20));
        //colors table
        m_scrolledWindow = new wxScrolledWindow(m_page_simple,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxSB_VERTICAL);
        m_sizer_simple->Add(m_scrolledWindow, 0, wxEXPAND | wxALL, FromDIP(5));
        draw_table();
        //buttons
        wxBoxSizer *quick_set_sizer = new wxBoxSizer(wxHORIZONTAL);
        wxStaticText *quick_set_title = new wxStaticText(m_page_simple, wxID_ANY, _L("Quick set:"));
        quick_set_title->SetFont(Label::Head_12);
        quick_set_sizer->Add(quick_set_title, 0, wxALIGN_CENTER | wxALL, 0);
        quick_set_sizer->AddSpacer(FromDIP(10));

        auto calc_approximate_match_btn_sizer = create_approximate_match_btn_sizer(m_page_simple);
        auto calc_add_btn_sizer = create_add_btn_sizer(m_page_simple);
        auto calc_reset_btn_sizer      = create_reset_btn_sizer(m_page_simple);
        quick_set_sizer->Add(calc_add_btn_sizer, 0, wxALIGN_CENTER | wxALL, 0);
        quick_set_sizer->AddSpacer(FromDIP(10));
        quick_set_sizer->Add(calc_approximate_match_btn_sizer, 0, wxALIGN_CENTER | wxALL, 0);
        quick_set_sizer->AddSpacer(FromDIP(10));
        quick_set_sizer->Add(calc_reset_btn_sizer, 0, wxALIGN_CENTER | wxALL, 0);
        quick_set_sizer->AddSpacer(FromDIP(10));
        m_sizer_simple->Add(quick_set_sizer, 0, wxEXPAND | wxLEFT, FromDIP(30));

        wxBoxSizer *warning_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_warning_text = new wxStaticText(m_page_simple, wxID_ANY, "");
        warning_sizer->Add(m_warning_text, 0, wxALIGN_CENTER | wxALL, 0);
        m_sizer_simple->Add(warning_sizer, 0, wxEXPAND | wxLEFT, FromDIP(30));

        m_sizer_simple->AddSpacer(10);
    }
    deal_default_strategy();
    //page_simple//page_advanced
    m_sizer = new wxBoxSizer(wxVERTICAL);
    m_sizer->Add(m_page_simple, 0, wxEXPAND, 0);

    m_sizer->SetSizeHints(this);
    SetSizer(m_sizer);
    this->Layout();
}

void ObjColorPanel::msw_rescale()
{
    for (unsigned int i = 0; i < m_extruder_icon_list.size(); ++i) {
        auto bitmap = *get_extruder_color_icon(m_colours[i].GetAsString(wxC2S_HTML_SYNTAX).ToStdString(), std::to_string(i + 1), FromDIP(16), FromDIP(16));
        m_extruder_icon_list[i]->SetBitmap(bitmap);
    }
   /* for (unsigned int i = 0; i < m_color_cluster_icon_list.size(); ++i) {
        auto bitmap = *get_extruder_color_icon(m_cluster_colours[i].GetAsString(wxC2S_HTML_SYNTAX).ToStdString(), std::to_string(i + 1), FromDIP(16), FromDIP(16));
        m_color_cluster_icon_list[i]->SetBitmap(bitmap);
    }*/
}

bool ObjColorPanel::is_ok() {
    for (auto item : m_result_icon_list) {
        if (item->bitmap_combox->IsShown()) {
            auto selection = item->bitmap_combox->GetSelection();
            if (selection < 1) {
                return false;
            }
        }
    }
    return true;
}

void ObjColorPanel::update_filament_ids()
{
    if (m_is_add_filament) {
        for (auto c:m_new_add_colors) {
            /*auto evt = new ColorEvent(EVT_ADD_CUSTOM_FILAMENT, c);
            wxQueueEvent(wxGetApp().plater(), evt);*/
            wxGetApp().sidebar().add_custom_filament(c);
        }
    }
   //deal m_filament_ids
   m_filament_ids.clear();
   m_filament_ids.reserve(m_input_colors_size);
   for (size_t i = 0; i < m_input_colors_size; i++) {
       auto label = m_cluster_labels_from_algo[i];
       m_filament_ids.emplace_back(m_cluster_map_filaments[label]);
   }
   m_first_extruder_id = m_cluster_map_filaments[0];
}

wxBoxSizer *ObjColorPanel::create_approximate_match_btn_sizer(wxWindow *parent)
{
    auto       btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    StateColor calc_btn_bg(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
                           std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal));
    StateColor calc_btn_bd(std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal));
    StateColor calc_btn_text(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal));
    //create btn
    m_quick_approximate_match_btn = new Button(parent, _L("Color match"));
    m_quick_approximate_match_btn->SetToolTip(_L("Approximate color matching."));
    auto cur_btn         = m_quick_approximate_match_btn;
    cur_btn->SetFont(Label::Body_13);
    cur_btn->SetMinSize(wxSize(FromDIP(60), FromDIP(20)));
    cur_btn->SetCornerRadius(FromDIP(10));
    cur_btn->SetBackgroundColor(calc_btn_bg);
    cur_btn->SetBorderColor(calc_btn_bd);
    cur_btn->SetTextColor(calc_btn_text);
    cur_btn->SetFocus();
    btn_sizer->Add(cur_btn, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 0);
    cur_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        deal_approximate_match_btn();
    });
    return btn_sizer;
}

wxBoxSizer *ObjColorPanel::create_add_btn_sizer(wxWindow *parent)
{
    auto       btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    StateColor calc_btn_bg(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
                           std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal));
    StateColor calc_btn_bd(std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal));
    StateColor calc_btn_text(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal));
    // create btn
    m_quick_add_btn = new Button(parent, _L("Append"));
    m_quick_add_btn->SetToolTip(_L("Add consumable extruder after existing extruders."));
    auto cur_btn    = m_quick_add_btn;
    cur_btn->SetFont(Label::Body_13);
    cur_btn->SetMinSize(wxSize(FromDIP(60), FromDIP(20)));
    cur_btn->SetCornerRadius(FromDIP(10));
    cur_btn->SetBackgroundColor(calc_btn_bg);
    cur_btn->SetBorderColor(calc_btn_bd);
    cur_btn->SetTextColor(calc_btn_text);
    cur_btn->SetFocus();
    btn_sizer->Add(cur_btn, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 0);
    cur_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        deal_add_btn();
    });
    return btn_sizer;
}

wxBoxSizer *ObjColorPanel::create_reset_btn_sizer(wxWindow *parent)
{
    auto       btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    StateColor calc_btn_bg(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
                           std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal));
    StateColor calc_btn_bd(std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal));
    StateColor calc_btn_text(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal));
    // create btn
    m_quick_reset_btn = new Button(parent, _L("Reset"));
    m_quick_add_btn->SetToolTip(_L("Reset mapped extruders."));
    auto cur_btn      = m_quick_reset_btn;
    cur_btn->SetFont(Label::Body_13);
    cur_btn->SetMinSize(wxSize(FromDIP(60), FromDIP(20)));
    cur_btn->SetCornerRadius(FromDIP(10));
    cur_btn->SetBackgroundColor(calc_btn_bg);
    cur_btn->SetBorderColor(calc_btn_bd);
    cur_btn->SetTextColor(calc_btn_text);
    cur_btn->SetFocus();
    btn_sizer->Add(cur_btn, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 0);
    cur_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        deal_reset_btn();
    });
    return btn_sizer;
}

wxBoxSizer *ObjColorPanel::create_extruder_icon_and_rgba_sizer(wxWindow *parent, int id, const wxColour &color)
{
    auto icon_sizer = new wxBoxSizer(wxHORIZONTAL);
    wxButton *icon       = new wxButton(parent, wxID_ANY, {}, wxDefaultPosition, ICON_SIZE, wxBORDER_NONE | wxBU_AUTODRAW);
    icon->SetBitmap(*get_extruder_color_icon(color.GetAsString(wxC2S_HTML_SYNTAX).ToStdString(), std::to_string(id + 1), FromDIP(16), FromDIP(16)));
    icon->SetCanFocus(false);
    m_extruder_icon_list.emplace_back(icon);
    icon_sizer->Add(icon, 0, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL, FromDIP(10)); // wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM

    icon_sizer->AddSpacer(FromDIP(5));
    return icon_sizer;
}

std::string ObjColorPanel::get_color_str(const wxColour &color) {
    std::string str = ("R:" + std::to_string(color.Red()) +
                          std::string(" G:") + std::to_string(color.Green()) +
                          std::string(" B:") + std::to_string(color.Blue()) +
                          std::string(" A:") + std::to_string(color.Alpha()));
    return str;
}

ComboBox *ObjColorPanel::CreateEditorCtrl(wxWindow *parent, int id) // wxRect labelRect,, const wxVariant &value
{
    std::vector<wxBitmap *> icons = get_extruder_color_icons();
    const double            em          = Slic3r::GUI::wxGetApp().em_unit();
    bool                    thin_icon   = false;
    const int               icon_width  = lround((thin_icon ? 2 : 4.4) * em);
    const int               icon_height = lround(2 * em);
    m_combox_icon_width                 = icon_width;
    m_combox_icon_height                = icon_height;
    wxColour undefined_color(0,255,0,255);
    icons.insert(icons.begin(), get_extruder_color_icon(undefined_color.GetAsString(wxC2S_HTML_SYNTAX).ToStdString(), std::to_string(-1), icon_width, icon_height));
    if (icons.empty())
        return nullptr;

    ::ComboBox *c_editor = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(m_combox_width), -1), 0, nullptr,
                                          wxCB_READONLY | CB_NO_DROP_ICON | CB_NO_TEXT);
    c_editor->SetMinSize(wxSize(FromDIP(m_combox_width), -1));
    c_editor->SetMaxSize(wxSize(FromDIP(m_combox_width), -1));
    c_editor->GetDropDown().SetUseContentWidth(true);
    for (size_t i = 0; i < icons.size(); i++) {
        c_editor->Append(wxString::Format("%d", i), *icons[i]);
        if (i == 0) {
            c_editor->SetItemTooltip(i,undefined_color.GetAsString(wxC2S_HTML_SYNTAX));
        } else {
            c_editor->SetItemTooltip(i, m_colours[i-1].GetAsString(wxC2S_HTML_SYNTAX));
        }
    }
    c_editor->SetSelection(0);
    c_editor->SetName(wxString::Format("%d", id));
    c_editor->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &evt) {
        auto *com_box = static_cast<ComboBox *>(evt.GetEventObject());
        int   i       = atoi(com_box->GetName().c_str());
        if (i < m_cluster_map_filaments.size()) { m_cluster_map_filaments[i] = com_box->GetSelection(); }
        evt.StopPropagation();
    });
    return c_editor;
}

void ObjColorPanel::deal_approximate_match_btn()
{
    auto calc_color_distance = [](wxColour c1, wxColour c2) {
        float lab[2][3];
        RGB2Lab(c1.Red(), c1.Green(), c1.Blue(), &lab[0][0], &lab[0][1], &lab[0][2]);
        RGB2Lab(c2.Red(), c2.Green(), c2.Blue(), &lab[1][0], &lab[1][1], &lab[1][2]);

        return DeltaE76(lab[0][0], lab[0][1], lab[0][2], lab[1][0], lab[1][1], lab[1][2]);
    };
    m_warning_text->SetLabelText("");
    if (m_result_icon_list.size() == 0) { return; }
    auto map_count = m_result_icon_list[0]->bitmap_combox->GetCount() -1;
    if (map_count < 1) { return; }
    for (size_t i = 0; i < m_cluster_colours.size(); i++) {
        auto    c = m_cluster_colours[i];
        std::vector<ColorDistValue> color_dists;
        color_dists.resize(map_count);
        for (size_t j = 0; j < map_count; j++) {
            auto tip_color       = m_result_icon_list[0]->bitmap_combox->GetItemTooltip(j+1);
            wxColour candidate_c(tip_color);
            color_dists[j].distance = calc_color_distance(c, candidate_c);
            color_dists[j].id = j + 1;
        }
        std::sort(color_dists.begin(), color_dists.end(), [](ColorDistValue &a, ColorDistValue& b) {
            return a.distance < b.distance;
            });
        auto new_index= color_dists[0].id;
        m_result_icon_list[i]->bitmap_combox->SetSelection(new_index);
        m_cluster_map_filaments[i] = new_index;
    }
}

void ObjColorPanel::show_sizer(wxSizer *sizer, bool show)
{
    wxSizerItemList items = sizer->GetChildren();
    for (wxSizerItemList::iterator it = items.begin(); it != items.end(); ++it) {
        wxSizerItem *item   = *it;
        if (wxWindow *window = item->GetWindow()) {
            window->Show(show);
        }
        if (wxSizer *son_sizer = item->GetSizer()) {
            show_sizer(son_sizer, show);
        }
    }
}

void ObjColorPanel::redraw_part_table() {
    //show all and set -1
    deal_reset_btn();
    for (size_t i = 0; i < m_row_sizer_list.size(); i++) {
        show_sizer(m_row_sizer_list[i], true);
    }
    if (m_cluster_colours.size() < m_row_sizer_list.size()) { // show part
        for (size_t i = m_cluster_colours.size(); i < m_row_sizer_list.size(); i++) {
            show_sizer(m_row_sizer_list[i], false);
            //m_row_panel_list[i]->Show(false); // show_sizer(m_left_color_cluster_boxsizer_list[i],false);
           // m_result_icon_list[i]->bitmap_combox->Show(false);
        }
    } else if (m_cluster_colours.size() > m_row_sizer_list.size()) {
        for (size_t i = m_row_sizer_list.size(); i < m_cluster_colours.size(); i++) {
            int      id                       = i;
            wxPanel *row_panel = new wxPanel(m_scrolledWindow);
            row_panel->SetBackgroundColour((i+1) % 2 == 0 ? *wxWHITE : wxColour(238, 238, 238));
            auto row_sizer = new wxGridSizer(1, 2, 1, 3);
            row_panel->SetSizer(row_sizer);

            row_panel->SetMinSize(wxSize(FromDIP(PANEL_WIDTH), -1));
            row_panel->SetMaxSize(wxSize(FromDIP(PANEL_WIDTH), -1));

            auto cluster_color_icon_sizer = create_color_icon_and_rgba_sizer(row_panel, id, m_cluster_colours[id]);
            row_sizer->Add(cluster_color_icon_sizer, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL, FromDIP(CONTENT_BORDER));
            // result_combox
            create_result_button_sizer(row_panel, id);
            row_sizer->Add(m_result_icon_list[id]->bitmap_combox, 0, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL, 0);

            m_row_sizer_list.emplace_back(row_sizer);
            m_gridsizer->Add(row_panel, 0, wxALIGN_LEFT | wxALL, FromDIP(HEADER_BORDER));
        }
        m_gridsizer->Layout();
    }
    for (size_t i = 0; i < m_cluster_colours.size(); i++) { // update data
        // m_color_cluster_icon_list//m_color_cluster_text_list
        update_color_icon_and_rgba_sizer(i, m_cluster_colours[i]);
    }
    m_scrolledWindow->Refresh();
}

void ObjColorPanel::draw_table()
{
    auto row                = std::max(m_cluster_colours.size(), m_colours.size()) + 1;
    m_gridsizer             = new wxGridSizer(row, 1, 1, 3); //(int rows, int cols, int vgap, int hgap );

    m_color_cluster_icon_list.clear();
    m_extruder_icon_list.clear();
    float row_height ;
    for (size_t ii = 0; ii < row; ii++) {
        wxPanel *row_panel = new wxPanel(m_scrolledWindow);
        row_panel->SetBackgroundColour(ii % 2 == 0 ? *wxWHITE : wxColour(238, 238, 238));
        auto row_sizer = new wxGridSizer(1, 2, 1, 5);
        row_panel->SetSizer(row_sizer);

        row_panel->SetMinSize(wxSize(FromDIP(PANEL_WIDTH), -1));
        row_panel->SetMaxSize(wxSize(FromDIP(PANEL_WIDTH), -1));
        if (ii == 0) {
            wxStaticText *colors_left_title = new wxStaticText(row_panel, wxID_ANY, _L("Cluster colors"));
            colors_left_title->SetFont(Label::Head_14);
            row_sizer->Add(colors_left_title, 0, wxALIGN_CENTER | wxALL, FromDIP(HEADER_BORDER));

            wxStaticText *colors_middle_title = new wxStaticText(row_panel, wxID_ANY, _L("Map Filament"));
            colors_middle_title->SetFont(Label::Head_14);
            row_sizer->Add(colors_middle_title, 0, wxALIGN_CENTER | wxALL, FromDIP(HEADER_BORDER));
        } else {
            int id = ii - 1;
            if (id < m_cluster_colours.size()) {
                auto cluster_color_icon_sizer = create_color_icon_and_rgba_sizer(row_panel, id, m_cluster_colours[id]);
                row_sizer->Add(cluster_color_icon_sizer, 0, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL, FromDIP(CONTENT_BORDER));
                // result_combox
                create_result_button_sizer(row_panel, id);
                row_sizer->Add(m_result_icon_list[id]->bitmap_combox, 0, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL, FromDIP(CONTENT_BORDER));
            }
        }
        row_height = row_panel->GetSize().GetHeight();
        if (ii>=1) {
            m_row_sizer_list.emplace_back(row_sizer);
        }
        m_gridsizer->Add(row_panel, 0, wxALIGN_LEFT | wxALL, FromDIP(HEADER_BORDER));
    }
    m_scrolledWindow->SetSizer(m_gridsizer);
    int totalHeight = row_height *(row+1) * 2;
    m_scrolledWindow->SetVirtualSize(MIN_OBJCOLOR_DIALOG_WIDTH, totalHeight);
    auto look = FIX_SCROLL_HEIGTH;
    if (totalHeight > FIX_SCROLL_HEIGTH) {
        m_scrolledWindow->SetMinSize(wxSize(MIN_OBJCOLOR_DIALOG_WIDTH, FIX_SCROLL_HEIGTH));
        m_scrolledWindow->SetMaxSize(wxSize(MIN_OBJCOLOR_DIALOG_WIDTH, FIX_SCROLL_HEIGTH));
    }
    else {
        m_scrolledWindow->SetMinSize(wxSize(MIN_OBJCOLOR_DIALOG_WIDTH, totalHeight));
    }
    m_scrolledWindow->EnableScrolling(false, true);
    m_scrolledWindow->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_DEFAULT);//wxSHOW_SB_ALWAYS
    m_scrolledWindow->SetScrollRate(20, 20);
}

void ObjColorPanel::deal_algo(char cluster_number, bool redraw_ui)
{
    if (m_last_cluster_number == cluster_number) {
        return;
    }
    m_last_cluster_number = cluster_number;
    QuantKMeans quant(10);
    quant.apply(m_input_colors, m_cluster_colors_from_algo, m_cluster_labels_from_algo, (int)cluster_number);
    m_cluster_colours.clear();
    m_cluster_colours.reserve(m_cluster_colors_from_algo.size());
    for (size_t i = 0; i < m_cluster_colors_from_algo.size(); i++) {
        m_cluster_colours.emplace_back(convert_to_wxColour(m_cluster_colors_from_algo[i]));
    }
    if (m_cluster_colours.size() == 0) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",m_cluster_colours.size() = 0\n";
        return;
    }
    m_cluster_map_filaments.resize(m_cluster_colors_from_algo.size());
    m_color_cluster_num_by_algo = m_cluster_colors_from_algo.size();
    if (cluster_number == -1) {
        m_color_num_recommend = m_color_cluster_num_by_algo;
    }
    //redraw ui
    if (redraw_ui) {
        redraw_part_table();
        deal_default_strategy();
    }
}

void ObjColorPanel::deal_default_strategy()
{
    deal_add_btn();
    deal_approximate_match_btn();
    m_warning_text->SetLabelText(_L("Note:The color has been selected, you can choose OK \n to continue or manually adjust it."));
}

void ObjColorPanel::deal_add_btn()
{
    if (m_colours.size() > g_max_color) { return; }
    deal_reset_btn();
    std::vector<wxBitmap *> new_icons;
    auto  new_color_size = m_cluster_colors_from_algo.size();
    new_icons.reserve(new_color_size);
    m_new_add_colors.clear();
    m_new_add_colors.reserve(new_color_size);
    int new_index = m_colours.size() + 1;
    bool is_exceed = false;
    for (size_t i = 0; i < new_color_size; i++) {
        if (m_colours.size() + new_icons.size() >= g_max_color) {
            is_exceed = true;
            break;
        }
        wxColour cur_color = convert_to_wxColour(m_cluster_colors_from_algo[i]);
        m_new_add_colors.emplace_back(cur_color);
        new_icons.emplace_back(get_extruder_color_icon(cur_color.GetAsString(wxC2S_HTML_SYNTAX).ToStdString(),
                std::to_string(new_index), m_combox_icon_width, m_combox_icon_height));
        new_index++;
    }
    new_index = m_colours.size() + 1;
    for (size_t i = 0; i < m_result_icon_list.size(); i++) {
        auto item = m_result_icon_list[i];
        for (size_t k = 0; k < new_icons.size(); k++) {
            item->bitmap_combox->Append(wxString::Format("%d", item->bitmap_combox->GetCount()), *new_icons[k]);
            item->bitmap_combox->SetItemTooltip(item->bitmap_combox->GetCount() -1,m_new_add_colors[k].GetAsString(wxC2S_HTML_SYNTAX));
        }
        item->bitmap_combox->SetSelection(new_index);
        m_cluster_map_filaments[i] = new_index;
        new_index++;
    }
    if (is_exceed) {
        deal_approximate_match_btn();
        m_warning_text->SetLabelText(_L("Waring:The count of newly added and \n current extruders exceeds 16."));
    }
    m_is_add_filament = true;
}

void ObjColorPanel::deal_reset_btn()
{
    for (auto item : m_result_icon_list) {
        // delete redundant bitmap
        while (item->bitmap_combox->GetCount() > m_colours.size()+ 1) {
            item->bitmap_combox->DeleteOneItem(item->bitmap_combox->GetCount() - 1);
        }
        item->bitmap_combox->SetSelection(0);
    }
    m_is_add_filament = false;
    m_new_add_colors.clear();
    m_warning_text->SetLabelText("");
}

void ObjColorPanel::create_result_button_sizer(wxWindow *parent, int id)
{
    for (size_t i = m_result_icon_list.size(); i < id + 1; i++) {
        m_result_icon_list.emplace_back(new ButtonState());
    }
    m_result_icon_list[id]->bitmap_combox = CreateEditorCtrl(parent,id);
}

wxBoxSizer *ObjColorPanel::create_color_icon_and_rgba_sizer(wxWindow *parent, int id, const wxColour& color)
{
    auto      icon_sizer = new wxBoxSizer(wxHORIZONTAL);
    icon_sizer->AddSpacer(FromDIP(40));
    wxButton *icon       = new wxButton(parent, wxID_ANY, {}, wxDefaultPosition, ICON_SIZE, wxBORDER_NONE | wxBU_AUTODRAW);
    icon->SetBitmap(*get_extruder_color_icon(color.GetAsString(wxC2S_HTML_SYNTAX).ToStdString(), std::to_string(id + 1), FromDIP(16), FromDIP(16)));
    icon->SetCanFocus(false);
    m_color_cluster_icon_list.emplace_back(icon);
    icon_sizer->Add(icon, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL, 0); // wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM
    icon_sizer->AddSpacer(FromDIP(10));

    std::string   message    = get_color_str(color);
    wxStaticText *rgba_title = new wxStaticText(parent, wxID_ANY, message.c_str());
    m_color_cluster_text_list.emplace_back(rgba_title);
    rgba_title->SetMinSize(wxSize(FromDIP(COLOR_LABEL_WIDTH), -1));
    rgba_title->SetMaxSize(wxSize(FromDIP(COLOR_LABEL_WIDTH), -1));
    //rgba_title->SetFont(Label::Head_12);
    icon_sizer->Add(rgba_title, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL, 0);
    return icon_sizer;
}

void ObjColorPanel::update_color_icon_and_rgba_sizer(int id, const wxColour &color)
{
    if (id < m_color_cluster_text_list.size()) {
        auto icon = m_color_cluster_icon_list[id];
        icon->SetBitmap(*get_extruder_color_icon(color.GetAsString(wxC2S_HTML_SYNTAX).ToStdString(), std::to_string(id + 1), FromDIP(16), FromDIP(16)));
        std::string message = get_color_str(color);
        m_color_cluster_text_list[id]->SetLabelText(message.c_str());
    }
}
