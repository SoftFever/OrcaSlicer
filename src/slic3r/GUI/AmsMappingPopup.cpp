#include "AmsMappingPopup.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/Thread.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_Preview.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/StaticBox.hpp"

#include <wx/progdlg.h>
#include <wx/clipbrd.h>
#include <wx/dcgraph.h>
#include <wx/graphics.h>
#include <miniz.h>
#include <algorithm>
#include "Plater.hpp"
#include "BitmapCache.hpp"
#include "BindDialog.hpp"

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_SET_FINISH_MAPPING, wxCommandEvent);

 MaterialItem::MaterialItem(wxWindow *parent, wxColour mcolour, wxString mname) 
 : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
 {
    m_arraw_bitmap_gray =  ScalableBitmap(this, "drop_down", FromDIP(12));
    m_arraw_bitmap_white =  ScalableBitmap(this, "topbar_dropdown", FromDIP(12));
    m_transparent_mitem = ScalableBitmap(this, "transparent_material_item", FromDIP(32));

    m_material_coloul = mcolour;
    m_material_name = mname;
    m_ams_coloul      = wxColour(0xEE,0xEE,0xEE);

#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    SetSize(MATERIAL_ITEM_SIZE);
    SetMinSize(MATERIAL_ITEM_SIZE);
    SetMaxSize(MATERIAL_ITEM_SIZE);
    SetBackgroundColour(*wxWHITE);

    Bind(wxEVT_PAINT, &MaterialItem::paintEvent, this);
    wxGetApp().UpdateDarkUI(this);
 }

 MaterialItem::~MaterialItem() {}

void MaterialItem::msw_rescale() {
    m_arraw_bitmap_gray  = ScalableBitmap(this, "drop_down", FromDIP(12));
    m_arraw_bitmap_white = ScalableBitmap(this, "topbar_dropdown", FromDIP(12));
    m_transparent_mitem  = ScalableBitmap(this, "transparent_material_item", FromDIP(32));
}

void MaterialItem::set_ams_info(wxColour col, wxString txt, int ctype, std::vector<wxColour> cols)
{
    auto need_refresh = false;
    if (m_ams_cols != cols) { m_ams_cols = cols; need_refresh = true; }
    if (m_ams_ctype != ctype) { m_ams_ctype = ctype; need_refresh = true; }
    if (m_ams_coloul != col) { m_ams_coloul = col; need_refresh = true;}
    if (m_ams_name != txt) { m_ams_name = txt; need_refresh = true; }
    if (need_refresh) { Refresh();}
}

void MaterialItem::disable()
{
    if (IsEnabled()) {
        this->Disable();
        Refresh();
    }
}

void MaterialItem::enable()
{
    if (!IsEnabled()) {
        this->Enable();
        Refresh();
    }
}

void MaterialItem::on_selected()
{
    if (!m_selected) {
        m_selected = true;
        Refresh();
    }
}

void MaterialItem::on_warning()
{
    if (!m_warning) {
        m_warning = true;
        Refresh();
    }
}

void MaterialItem::on_normal()
{
    if (m_selected || m_warning) {
        m_selected = false;
        m_warning  = false;
        Refresh();
    }
}


void MaterialItem::paintEvent(wxPaintEvent &evt) 
{  
    wxPaintDC dc(this);
    render(dc);

    //PrepareDC(buffdc);
    //PrepareDC(dc);
    
}

void MaterialItem::render(wxDC &dc) 
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({0, 0}, size, &dc, {0, 0});

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif

    auto mcolor = m_material_coloul;
    auto acolor = m_ams_coloul;
    change_the_opacity(acolor);
    if (!IsEnabled()) {
        mcolor = wxColour(0x90, 0x90, 0x90);
        acolor = wxColour(0x90, 0x90, 0x90);
    }

    // materials name
    dc.SetFont(::Label::Body_13);

    auto material_name_colour = mcolor.GetLuminance() < 0.6 ? *wxWHITE : wxColour(0x26, 0x2E, 0x30);
    if (mcolor.Alpha() == 0) {material_name_colour = wxColour(0x26, 0x2E, 0x30);}
    dc.SetTextForeground(material_name_colour);

    if (dc.GetTextExtent(m_material_name).x > GetSize().x - 10) {
        dc.SetFont(::Label::Body_10);
    }

    auto material_txt_size = dc.GetTextExtent(m_material_name);
    dc.DrawText(m_material_name, wxPoint((MATERIAL_ITEM_SIZE.x - material_txt_size.x) / 2, (FromDIP(22) - material_txt_size.y) / 2));

    // mapping num
    dc.SetFont(::Label::Body_10);
    dc.SetTextForeground(acolor.GetLuminance() < 0.6 ? *wxWHITE : wxColour(0x26, 0x2E, 0x30));
    if (acolor.Alpha() == 0) {
        dc.SetTextForeground(wxColour(0x26, 0x2E, 0x30));
    }

    wxString mapping_txt = wxEmptyString;
    if (m_ams_name.empty()) {
        mapping_txt = "-";
    } else {
        mapping_txt = m_ams_name;
    }

    auto mapping_txt_size = dc.GetTextExtent(mapping_txt);
    dc.DrawText(mapping_txt, wxPoint((MATERIAL_ITEM_SIZE.x - mapping_txt_size.x) / 2, FromDIP(20) + (FromDIP(14) - mapping_txt_size.y) / 2));
}

void MaterialItem::doRender(wxDC &dc) 
{
    wxSize size = GetSize();
    auto mcolor = m_material_coloul;
    auto acolor = m_ams_coloul;
    change_the_opacity(acolor);

    if (mcolor.Alpha() == 0 || acolor.Alpha() == 0) {
        dc.DrawBitmap(m_transparent_mitem.bmp(), FromDIP(1), FromDIP(1));
    }

    if (!IsEnabled()) {
        mcolor = wxColour(0x90, 0x90, 0x90);
        acolor = wxColour(0x90, 0x90, 0x90);
    }

    //top
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(mcolor));
    dc.DrawRoundedRectangle(FromDIP(1), FromDIP(1), MATERIAL_ITEM_REAL_SIZE.x, FromDIP(18), 5);
    
    //bottom
    if (m_ams_cols.size() > 1) {
        int left = FromDIP(1);
        int gwidth = std::round(MATERIAL_ITEM_REAL_SIZE.x / (m_ams_cols.size() - 1));
        //gradient
        if (m_ams_ctype == 0) {
            for (int i = 0; i < m_ams_cols.size() - 1; i++) {
                auto rect = wxRect(left, FromDIP(18), MATERIAL_ITEM_REAL_SIZE.x, FromDIP(16));
                dc.GradientFillLinear(rect, m_ams_cols[i], m_ams_cols[i + 1], wxEAST);
                left += gwidth;
            }
        }
        else {
            int cols_size = m_ams_cols.size();
            for (int i = 0; i < cols_size; i++) {
                dc.SetBrush(wxBrush(m_ams_cols[i]));
                float x = left + ((float)MATERIAL_ITEM_REAL_SIZE.x) * i / cols_size;
                if (i != cols_size - 1) {
                    dc.DrawRoundedRectangle(x, FromDIP(18), ((float)MATERIAL_ITEM_REAL_SIZE.x) / cols_size + FromDIP(3), FromDIP(16), 3);
                }
                else {
                    dc.DrawRoundedRectangle(x, FromDIP(18), ((float)MATERIAL_ITEM_REAL_SIZE.x) / cols_size , FromDIP(16), 3);
                }
            }
 
        }
    }
    else {
        
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(wxColour(acolor)));
        dc.DrawRoundedRectangle(FromDIP(1), FromDIP(18), MATERIAL_ITEM_REAL_SIZE.x, FromDIP(16), 5);
        ////middle

        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(acolor));
        dc.DrawRectangle(FromDIP(1), FromDIP(18), MATERIAL_ITEM_REAL_SIZE.x, FromDIP(8));
    }
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(mcolor));
    dc.DrawRectangle(FromDIP(1), FromDIP(11), MATERIAL_ITEM_REAL_SIZE.x, FromDIP(8));



    ////border
#if __APPLE__
    if (mcolor == *wxWHITE || acolor == *wxWHITE) {
        dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(1, 1, MATERIAL_ITEM_SIZE.x - 1, MATERIAL_ITEM_SIZE.y - 1, 5);
    }

    if (m_selected) {
        dc.SetPen(wxColour(0x00, 0xAE, 0x42));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(1, 1, MATERIAL_ITEM_SIZE.x - 1, MATERIAL_ITEM_SIZE.y - 1, 5);
    }
#else
    if (mcolor == *wxWHITE || acolor == *wxWHITE || acolor.Alpha() == 0) {
        dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(0, 0, MATERIAL_ITEM_SIZE.x, MATERIAL_ITEM_SIZE.y, 5);
    }

    if (m_selected) {
        dc.SetPen(wxColour(0x00, 0xAE, 0x42));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(0, 0, MATERIAL_ITEM_SIZE.x, MATERIAL_ITEM_SIZE.y, 5);
    }
#endif
    //arrow
    if ( (acolor.Red() > 160 && acolor.Green() > 160 && acolor.Blue() > 160) &&
        (acolor.Red() < 180 && acolor.Green() < 180 && acolor.Blue() < 180)) {
        dc.DrawBitmap(m_arraw_bitmap_white.bmp(), size.x - m_arraw_bitmap_white.GetBmpSize().x - FromDIP(7), size.y - m_arraw_bitmap_white.GetBmpSize().y);
    }
    else {
        dc.DrawBitmap(m_arraw_bitmap_gray.bmp(), size.x - m_arraw_bitmap_gray.GetBmpSize().x - FromDIP(7), size.y - m_arraw_bitmap_gray.GetBmpSize().y);
    }

    
}

 AmsMapingPopup::AmsMapingPopup(wxWindow *parent) 
    : PopupWindow(parent, wxBORDER_NONE)
 {
     SetSize(wxSize(FromDIP(252), -1));
     SetMinSize(wxSize(FromDIP(252), -1));
     SetMaxSize(wxSize(FromDIP(252), -1));
     Bind(wxEVT_PAINT, &AmsMapingPopup::paintEvent, this);


     #if __APPLE__
     Bind(wxEVT_LEFT_DOWN, &AmsMapingPopup::on_left_down, this); 
     #endif

     SetBackgroundColour(*wxWHITE);
     m_sizer_main         = new wxBoxSizer(wxVERTICAL);
     //m_sizer_main->Add(0, 0, 1, wxEXPAND, 0);

     auto title_panel = new wxPanel(this, wxID_ANY);
     title_panel->SetBackgroundColour(wxColour(0xF8, 0xF8, 0xF8));
     title_panel->SetSize(wxSize(-1, FromDIP(30)));
     title_panel->SetMinSize(wxSize(-1, FromDIP(30)));
     

     wxBoxSizer *title_sizer_h= new wxBoxSizer(wxHORIZONTAL);

     wxBoxSizer *title_sizer_v = new wxBoxSizer(wxVERTICAL);

     auto title_text = new wxStaticText(title_panel, wxID_ANY, _L("AMS Slots"));
     title_text->SetForegroundColour(wxColour(0x32, 0x3A, 0x3D));
     title_text->SetFont(::Label::Head_13);
     title_sizer_v->Add(title_text, 0, wxALIGN_CENTER, 5);
     title_sizer_h->Add(title_sizer_v, 1, wxALIGN_CENTER, 5);
     title_panel->SetSizer(title_sizer_h);
     title_panel->Layout();
     title_panel->Fit();

     m_sizer_list = new wxBoxSizer(wxVERTICAL);
     for (auto i = 0; i < AMS_TOTAL_COUNT; i++) {
         auto sizer_mapping_list = new wxBoxSizer(wxHORIZONTAL);
         /*auto ams_mapping_item_container = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("ams_mapping_container", this, 78), wxDefaultPosition,
             wxSize(FromDIP(230), FromDIP(78)), 0);*/
         auto ams_mapping_item_container = new MappingContainer(this);
         ams_mapping_item_container->SetSizer(sizer_mapping_list);
         ams_mapping_item_container->Layout();
         //ams_mapping_item_container->Hide();
         m_amsmapping_container_sizer_list.push_back(sizer_mapping_list);
         m_amsmapping_container_list.push_back(ams_mapping_item_container);
         m_sizer_list->Add(ams_mapping_item_container, 0, wxALIGN_CENTER_HORIZONTAL|wxTOP|wxBOTTOM, FromDIP(5));
     }

     m_warning_text = new wxStaticText(this, wxID_ANY, wxEmptyString);
     m_warning_text->SetForegroundColour(wxColour(0xFF, 0x6F, 0x00));
     m_warning_text->SetFont(::Label::Body_12);
     auto cant_not_match_tip = _L("Note: Only the AMS slots loaded with the same material type can be selected.");
     m_warning_text->SetLabel(format_text(cant_not_match_tip));
     m_warning_text->SetMinSize(wxSize(FromDIP(248), FromDIP(-1)));
     m_warning_text->Wrap(FromDIP(248));

     m_sizer_main->Add(title_panel, 0, wxEXPAND | wxALL, FromDIP(2));
     m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(5));
     m_sizer_main->Add(m_sizer_list, 0, wxEXPAND | wxALL, FromDIP(0));
     m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(5));
     m_sizer_main->Add(m_warning_text, 0, wxEXPAND | wxALL, FromDIP(6));

     SetSizer(m_sizer_main);
     Layout();
     Fit();

     Bind(wxEVT_SHOW, [this](wxShowEvent& e) {
         if (e.IsShown() && m_parent_item) {
             wxPoint pos = m_parent_item->ClientToScreen(wxPoint(0, 0));
             pos.y += m_parent_item->GetRect().height;
             this->Move(pos);
         }
     });
 }

 wxString AmsMapingPopup::format_text(wxString &m_msg)
 {
     if (wxGetApp().app_config->get("language") != "zh_CN") { return m_msg; }

     wxString out_txt      = m_msg;
     wxString count_txt    = "";
     int      new_line_pos = 0;

     for (int i = 0; i < m_msg.length(); i++) {
         auto text_size = m_warning_text->GetTextExtent(count_txt);
         if (text_size.x < (FromDIP(280))) {
             count_txt += m_msg[i];
         } else {
             out_txt.insert(i - 1, '\n');
             count_txt = "";
         }
     }
     return out_txt;
 }

void AmsMapingPopup::update_materials_list(std::vector<std::string> list) 
{ 
    m_materials_list = list;
}

void AmsMapingPopup::set_tag_texture(std::string texture) 
{ 
    m_tag_material = texture;
}


bool AmsMapingPopup::is_match_material(std::string material)
{
    return m_tag_material == material ? true : false;
}


void AmsMapingPopup::on_left_down(wxMouseEvent &evt)
{
    auto pos = ClientToScreen(evt.GetPosition());
    for (MappingItem *item : m_mapping_item_list) {
        auto p_rect = item->ClientToScreen(wxPoint(0, 0));
        auto left = item->GetSize();

        if (pos.x > p_rect.x && pos.y > p_rect.y && pos.x < (p_rect.x + item->GetSize().x) && pos.y < (p_rect.y + item->GetSize().y)) {
            if (item->m_tray_data.type == TrayType::NORMAL  && !is_match_material(item->m_tray_data.filament_type)) return;
            item->send_event(m_current_filament_id);
            Dismiss();
        }
    }
}

void AmsMapingPopup::update_ams_data_multi_machines()
{
    m_has_unmatch_filament = false;
    for (auto& ams_container : m_amsmapping_container_list) {
        ams_container->Hide();
    }

    for (wxWindow* mitem : m_mapping_item_list) {
        mitem->Destroy();
        mitem = nullptr;
    }
    m_mapping_item_list.clear();

    if (m_amsmapping_container_sizer_list.size() > 0) {
        for (wxBoxSizer* siz : m_amsmapping_container_sizer_list) {
            siz->Clear(true);
        }
    }

    int m_amsmapping_container_list_index = 0;
    std::vector<TrayData> tray_datas;

    for (int i = 0; i < 4; ++i) {
        TrayData td;
        td.id = i;
        td.type = EMPTY;
        td.colour = wxColour(166, 169, 170);
        td.name = "";
        td.filament_type = "";
        td.ctype = 0;
        tray_datas.push_back(td);
    }

    m_amsmapping_container_list[m_amsmapping_container_list_index]->Show();
    add_ams_mapping(tray_datas, m_amsmapping_container_list[m_amsmapping_container_list_index], m_amsmapping_container_sizer_list[m_amsmapping_container_list_index]);

    m_warning_text->Show(m_has_unmatch_filament);

    Layout();
    Fit();
}

void AmsMapingPopup::update_ams_data(std::map<std::string, Ams*> amsList) 
{ 
    m_has_unmatch_filament = false;
    //m_mapping_item_list.clear();

    for (auto& ams_container : m_amsmapping_container_list) {
        ams_container->Hide();
    }


    for (wxWindow *mitem : m_mapping_item_list) {
        mitem->Destroy();
        mitem = nullptr;
    }
     m_mapping_item_list.clear();

    if (m_amsmapping_container_sizer_list.size() > 0) {
        for (wxBoxSizer *siz : m_amsmapping_container_sizer_list) { 
            siz->Clear(true); 
        }
    }
   
    std::map<std::string, Ams *>::iterator ams_iter;

    BOOST_LOG_TRIVIAL(trace) << "ams_mapping total count " << amsList.size();
    int m_amsmapping_container_list_index = 0;

    for (ams_iter = amsList.begin(); ams_iter != amsList.end(); ams_iter++) {
        
        BOOST_LOG_TRIVIAL(trace) << "ams_mapping ams id " << ams_iter->first.c_str();

        auto ams_indx = atoi(ams_iter->first.c_str());
        Ams *ams_group = ams_iter->second;
        std::vector<TrayData>                      tray_datas;
        std::map<std::string, AmsTray *>::iterator tray_iter;

        for (tray_iter = ams_group->trayList.begin(); tray_iter != ams_group->trayList.end(); tray_iter++) {
            AmsTray *tray_data = tray_iter->second;
            TrayData td;

            td.id = ams_indx * AMS_TOTAL_COUNT + atoi(tray_data->id.c_str());

            if (!tray_data->is_exists) {
                td.type = EMPTY;
            } else {
                if (!tray_data->is_tray_info_ready()) {
                    td.type = THIRD;
                } else {
                    td.type   = NORMAL;
                    td.colour = AmsTray::decode_color(tray_data->color);
                    td.name   = tray_data->get_display_filament_type();
                    td.filament_type = tray_data->get_filament_type();
                    td.ctype = tray_data->ctype;
                    for (auto col : tray_data->cols) {
                        td.material_cols.push_back(AmsTray::decode_color(col));
                    }
                }
            }

            tray_datas.push_back(td);
        }

        m_amsmapping_container_list[m_amsmapping_container_list_index]->Show();
        add_ams_mapping(tray_datas, m_amsmapping_container_list[m_amsmapping_container_list_index], m_amsmapping_container_sizer_list[m_amsmapping_container_list_index]);
        m_amsmapping_container_list_index++;
    }


    m_warning_text->Show(m_has_unmatch_filament);
    Layout();
    Fit();
}

std::vector<TrayData> AmsMapingPopup::parse_ams_mapping(std::map<std::string, Ams*> amsList)
{
    std::vector<TrayData> m_tray_data;
    std::map<std::string, Ams *>::iterator ams_iter;

    for (ams_iter = amsList.begin(); ams_iter != amsList.end(); ams_iter++) {

        BOOST_LOG_TRIVIAL(trace) << "ams_mapping ams id " << ams_iter->first.c_str();

        auto ams_indx = atoi(ams_iter->first.c_str());
        Ams* ams_group = ams_iter->second;
        std::vector<TrayData>                      tray_datas;
        std::map<std::string, AmsTray*>::iterator tray_iter;

        for (tray_iter = ams_group->trayList.begin(); tray_iter != ams_group->trayList.end(); tray_iter++) {
            AmsTray* tray_data = tray_iter->second;
            TrayData td;

            td.id = ams_indx * AMS_TOTAL_COUNT + atoi(tray_data->id.c_str());

            if (!tray_data->is_exists) {
                td.type = EMPTY;
            }
            else {
                if (!tray_data->is_tray_info_ready()) {
                    td.type = THIRD;
                }
                else {
                    td.type = NORMAL;
                    td.colour = AmsTray::decode_color(tray_data->color);
                    td.name = tray_data->get_display_filament_type();
                    td.filament_type = tray_data->get_filament_type();
                }
            }

            m_tray_data.push_back(td);
        }
    }

    return m_tray_data;
}

void AmsMapingPopup::add_ams_mapping(std::vector<TrayData> tray_data, wxWindow* container, wxBoxSizer* sizer)
{ 
    sizer->Add(0,0,0,wxLEFT,FromDIP(6));
    for (auto i = 0; i < tray_data.size(); i++) {

        // set number
       /* auto number = new wxStaticText(this, wxID_ANY, wxGetApp().transition_tridid(tray_data[i].id), wxDefaultPosition, wxDefaultSize, 0);
        number->SetFont(::Label::Body_13);
        number->SetForegroundColour(wxColour(0X6B, 0X6B, 0X6B));
        number->Wrap(-1);*/
        

        // set button
        MappingItem *m_mapping_item = new MappingItem(container);
        m_mapping_item->SetSize(wxSize(FromDIP(68 * 0.7), FromDIP(100 * 0.6)));
        m_mapping_item->SetMinSize(wxSize(FromDIP(68 * 0.7), FromDIP(100 * 0.6)));
        m_mapping_item->SetMaxSize(wxSize(FromDIP(68 * 0.7), FromDIP(100 * 0.6)));
        //m_mapping_item->SetCornerRadius(5);
        m_mapping_item->SetFont(::Label::Body_12);
        m_mapping_item_list.push_back(m_mapping_item);

        if (tray_data[i].type == NORMAL) {
            if (is_match_material(tray_data[i].filament_type)) {
                m_mapping_item->set_data(tray_data[i].colour, tray_data[i].name, tray_data[i]);
            } else {
                m_mapping_item->set_data(wxColour(0xEE,0xEE,0xEE), tray_data[i].name, tray_data[i], true);
                m_has_unmatch_filament = true;
            }

            m_mapping_item->Bind(wxEVT_LEFT_DOWN, [this, tray_data, i, m_mapping_item](wxMouseEvent &e) {
                if (!is_match_material(tray_data[i].filament_type)) return;
                m_mapping_item->send_event(m_current_filament_id);
                Dismiss();
            });
        }
        

        // temp
        if (tray_data[i].type == EMPTY) {
            m_mapping_item->set_data(wxColour(0xCE, 0xCE, 0xCE), "-", tray_data[i]);
            m_mapping_item->Bind(wxEVT_LEFT_DOWN, [this, tray_data, i, m_mapping_item](wxMouseEvent &e) {
                m_mapping_item->send_event(m_current_filament_id);
                Dismiss();
            });
        }

        // third party
        if (tray_data[i].type == THIRD) {
            m_mapping_item->set_data(wxColour(0xCE, 0xCE, 0xCE), "?", tray_data[i]);
            m_mapping_item->Bind(wxEVT_LEFT_DOWN, [this, tray_data, i, m_mapping_item](wxMouseEvent &e) {
                m_mapping_item->send_event(m_current_filament_id);
                Dismiss();
            });
        }


        //sizer_mapping_item->Add(number, 0, wxALIGN_CENTER_HORIZONTAL, 0);
        //sizer_mapping_item->Add(m_mapping_item, 0, wxALIGN_CENTER_HORIZONTAL, 0);
        m_mapping_item->set_tray_index(wxGetApp().transition_tridid(tray_data[i].id));
        sizer->Add(0,0,0,wxRIGHT,FromDIP(6));
        sizer->Add(m_mapping_item, 0, wxTOP, FromDIP(1));
    }

}

void AmsMapingPopup::OnDismiss()
{

}

bool AmsMapingPopup::ProcessLeftDown(wxMouseEvent &event) 
{
    return PopupWindow::ProcessLeftDown(event);
}

void AmsMapingPopup::paintEvent(wxPaintEvent &evt) 
{
    wxPaintDC dc(this);
    dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(0, 0, GetSize().x, GetSize().y, 0);
}

 MappingItem::MappingItem(wxWindow *parent) 
 : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    m_transparent_mapping_item = ScalableBitmap(this, "transparent_mapping_item", FromDIP(44));
    SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    Bind(wxEVT_PAINT, &MappingItem::paintEvent, this);
}

 MappingItem::~MappingItem() 
{
}


void MappingItem::send_event(int fliament_id) 
{
    auto number = wxGetApp().transition_tridid(m_tray_data.id);
    wxCommandEvent event(EVT_SET_FINISH_MAPPING);
    event.SetInt(m_tray_data.id);

    wxString param = wxString::Format("%d|%d|%d|%d|%s|%d", m_coloul.Red(), m_coloul.Green(), m_coloul.Blue(), m_coloul.Alpha(), number, fliament_id);
    event.SetString(param);
    event.SetEventObject(this->GetParent()->GetParent());
    wxPostEvent(this->GetParent()->GetParent()->GetParent(), event);
}

 void MappingItem::msw_rescale() 
{
}

void MappingItem::paintEvent(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    render(dc);

    // PrepareDC(buffdc);
    // PrepareDC(dc);
}

void MappingItem::render(wxDC &dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({0, 0}, size, &dc, {0, 0});

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif

    // materials name
    dc.SetFont(::Label::Head_13);

    auto txt_colour = m_coloul.GetLuminance() < 0.6 ? *wxWHITE : wxColour(0x26, 0x2E, 0x30);
    txt_colour      = m_unmatch ? wxColour(0xCE, 0xCE, 0xCE) : txt_colour;
    if (m_coloul.Alpha() == 0) txt_colour = wxColour(0x26, 0x2E, 0x30);
    dc.SetTextForeground(txt_colour);

    auto txt_size = dc.GetTextExtent(m_tray_index);
    auto top = (GetSize().y - MAPPING_ITEM_REAL_SIZE.y) / 2 + FromDIP(8);
    dc.DrawText(m_tray_index, wxPoint((GetSize().x - txt_size.x) / 2, top));


    top += txt_size.y + FromDIP(7);
    dc.SetFont(::Label::Body_12);
    txt_size = dc.GetTextExtent(m_name);
    dc.DrawText(m_name, wxPoint((GetSize().x - txt_size.x) / 2, top));
}

void MappingItem::set_data(wxColour colour, wxString name, TrayData data, bool unmatch)
{
    m_unmatch = unmatch;
    m_tray_data = data;
    if (m_coloul != colour || m_name != name) {
        m_coloul = colour;
        m_name   = name;
        Refresh();
    }
}

void MappingItem::doRender(wxDC &dc)
{
    wxSize size = GetSize();
    wxColour color = m_coloul;
    change_the_opacity(color);

    dc.SetPen(color);
    dc.SetBrush(wxBrush(color));

    if (m_tray_data.material_cols.size() > 1) {
        int left = 0;
        int gwidth = std::round(MAPPING_ITEM_REAL_SIZE.x / (m_tray_data.material_cols.size() - 1));
        //gradient
        if (m_tray_data.ctype == 0) {
            for (int i = 0; i < m_tray_data.material_cols.size() - 1; i++) {
                auto rect = wxRect(left, (size.y - MAPPING_ITEM_REAL_SIZE.y) / 2, MAPPING_ITEM_REAL_SIZE.x, MAPPING_ITEM_REAL_SIZE.y);
                dc.GradientFillLinear(rect, m_tray_data.material_cols[i], m_tray_data.material_cols[i + 1], wxEAST);
                left += gwidth;
            }
        }
        else {
            int cols_size = m_tray_data.material_cols.size();
            for (int i = 0; i < cols_size; i++) {
                dc.SetBrush(wxBrush(m_tray_data.material_cols[i]));
                float x = (float)MAPPING_ITEM_REAL_SIZE.x * i / cols_size;
                dc.DrawRectangle(x, (size.y - MAPPING_ITEM_REAL_SIZE.y) / 2, (float)MAPPING_ITEM_REAL_SIZE.x / cols_size, MAPPING_ITEM_REAL_SIZE.y);
            }
        }
    }
    else if (color.Alpha() == 0) {
       dc.DrawBitmap( m_transparent_mapping_item.bmp(), 0, (size.y - MAPPING_ITEM_REAL_SIZE.y) / 2);
    }
    else {
        dc.DrawRectangle(0, (size.y - MAPPING_ITEM_REAL_SIZE.y) / 2, MAPPING_ITEM_REAL_SIZE.x, MAPPING_ITEM_REAL_SIZE.y);
    }


    wxColour side_colour = wxColour(0xE4E4E4);

    dc.SetPen(side_colour);
    dc.SetBrush(wxBrush(side_colour));
#ifdef __APPLE__
    dc.DrawRectangle(0, 0, FromDIP(4), size.y);
    dc.DrawRectangle(size.x - FromDIP(4), 0, FromDIP(4), size.y);
#else
    dc.DrawRectangle(0, 0, FromDIP(4), size.y);
    dc.DrawRectangle(size.x - FromDIP(4), 0, FromDIP(4), size.y);
#endif // __APPLE__
}

AmsMapingTipPopup::AmsMapingTipPopup(wxWindow *parent) 
    :PopupWindow(parent, wxBORDER_NONE)
{
    SetBackgroundColour(*wxWHITE);
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);

    m_sizer_main->Add(0, 0, 1, wxTOP, FromDIP(28));

    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_body->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(20));

    m_panel_enable_ams = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(220), -1), wxTAB_TRAVERSAL);
    m_panel_enable_ams->SetBackgroundColour(*wxWHITE);
    wxBoxSizer *sizer_enable_ams = new wxBoxSizer(wxVERTICAL);

    m_title_enable_ams = new wxStaticText(m_panel_enable_ams, wxID_ANY, _L("Enable AMS"), wxDefaultPosition, wxDefaultSize, 0);
    m_title_enable_ams->SetForegroundColour(*wxBLACK);
    m_title_enable_ams->SetBackgroundColour(*wxWHITE);
    m_title_enable_ams->Wrap(-1);
    sizer_enable_ams->Add(m_title_enable_ams, 0, 0, 0);

    m_tip_enable_ams = new wxStaticText(m_panel_enable_ams, wxID_ANY, _L("Print with filaments in the AMS"), wxDefaultPosition, wxDefaultSize, 0);
    m_tip_enable_ams->SetMinSize(wxSize(FromDIP(200), FromDIP(50)));
    m_tip_enable_ams->Wrap(FromDIP(200));
    m_tip_enable_ams->SetForegroundColour(*wxBLACK);
    m_tip_enable_ams->SetBackgroundColour(*wxWHITE);
    sizer_enable_ams->Add(m_tip_enable_ams, 0, wxTOP, 8);

    wxBoxSizer *sizer_enable_ams_img;
    sizer_enable_ams_img = new wxBoxSizer(wxVERTICAL);

    auto img_enable_ams = new wxStaticBitmap(m_panel_enable_ams, wxID_ANY, create_scaled_bitmap("monitor_upgrade_ams", this, 108), wxDefaultPosition,
                                             wxSize(FromDIP(118), FromDIP(108)), 0);
    sizer_enable_ams_img->Add(img_enable_ams, 0, wxALIGN_CENTER_HORIZONTAL, 0);

    sizer_enable_ams->Add(sizer_enable_ams_img, 1, wxEXPAND | wxTOP, FromDIP(20));

    m_panel_enable_ams->SetSizer(sizer_enable_ams);
    m_panel_enable_ams->Layout();
    m_sizer_body->Add(m_panel_enable_ams, 0, 0, 0);

    m_split_lines = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(1, FromDIP(150)), wxTAB_TRAVERSAL);
    m_split_lines->SetBackgroundColour(wxColour(238, 238, 238));

    m_sizer_body->Add(m_split_lines, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(10));

    m_panel_disable_ams = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(220), -1), wxTAB_TRAVERSAL);
    m_panel_disable_ams->SetBackgroundColour(*wxWHITE);
    wxBoxSizer *sizer_disable_ams;
    sizer_disable_ams = new wxBoxSizer(wxVERTICAL);

    m_title_disable_ams = new wxStaticText(m_panel_disable_ams, wxID_ANY, _L("Disable AMS"), wxDefaultPosition, wxDefaultSize, 0);
    m_title_disable_ams->SetBackgroundColour(*wxWHITE);
    m_title_disable_ams->SetForegroundColour(*wxBLACK);
    m_title_disable_ams->Wrap(-1);
    sizer_disable_ams->Add(m_title_disable_ams, 0, 0, 0);

    m_tip_disable_ams = new wxStaticText(m_panel_disable_ams, wxID_ANY, _L("Print with the filament mounted on the back of chassis"), wxDefaultPosition, wxDefaultSize, 0);
    m_tip_disable_ams->SetMinSize(wxSize(FromDIP(200), FromDIP(50)));
    m_tip_disable_ams->Wrap(FromDIP(200));
    m_tip_disable_ams->SetForegroundColour(*wxBLACK);
    m_tip_disable_ams->SetBackgroundColour(*wxWHITE);
    sizer_disable_ams->Add(m_tip_disable_ams, 0, wxTOP, FromDIP(8));

    wxBoxSizer *sizer_disable_ams_img;
    sizer_disable_ams_img = new wxBoxSizer(wxVERTICAL);

    auto img_disable_ams = new wxStaticBitmap(m_panel_disable_ams, wxID_ANY, create_scaled_bitmap("disable_ams_demo_icon", this, 95), wxDefaultPosition,
                                              wxSize(FromDIP(95), FromDIP(109)), 0);
    sizer_disable_ams_img->Add(img_disable_ams, 0, wxALIGN_CENTER_HORIZONTAL, 0);

    sizer_disable_ams->Add(sizer_disable_ams_img, 1, wxEXPAND | wxTOP, FromDIP(20));

    m_panel_disable_ams->SetSizer(sizer_disable_ams);
    m_panel_disable_ams->Layout();
    m_sizer_body->Add(m_panel_disable_ams, 0, 0, 0);

    m_sizer_body->Add(0, 0, 0, wxEXPAND | wxRIGHT, FromDIP(20));

    m_sizer_main->Add(m_sizer_body, 0, wxEXPAND, 0);

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(28));

    this->SetSizer(m_sizer_main);
    this->Layout();
    this->Fit();
    Bind(wxEVT_PAINT, &AmsMapingTipPopup::paintEvent, this);
}

void AmsMapingTipPopup::paintEvent(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(0, 0, GetSize().x, GetSize().y, 0);
}

void AmsMapingTipPopup::OnDismiss() {}

bool AmsMapingTipPopup::ProcessLeftDown(wxMouseEvent &event) { 
    return PopupWindow::ProcessLeftDown(event); }


AmsHumidityTipPopup::AmsHumidityTipPopup(wxWindow* parent)
    :PopupWindow(parent, wxBORDER_NONE)
{
    SetBackgroundColour(*wxWHITE);

    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    close_img = ScalableBitmap(this, "hum_popup_close", 24);

    m_staticText = new Label(this, _L("Current Cabin humidity"));
    m_staticText->SetFont(::Label::Head_24);

    humidity_level_list = new AmsHumidityLevelList(this);
    curr_humidity_img = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("hum_level1_light", this, 132), wxDefaultPosition, wxSize(FromDIP(132), FromDIP(132)), 0);

    m_staticText_note = new Label(this, _L("Please change the desiccant when it is too wet. The indicator may not represent accurately in following cases : when the lid is open or the desiccant pack is changed. it take hours to absorb the moisture, low temperatures also slow down the process."));
    m_staticText_note->SetMinSize(wxSize(FromDIP(680), -1));
    m_staticText_note->SetMaxSize(wxSize(FromDIP(680), -1));
    m_staticText_note->Wrap(FromDIP(680));
   

    Bind(wxEVT_LEFT_UP, [this](auto& e) {

        auto rect = ClientToScreen(wxPoint(0, 0));

        auto close_left     = rect.x + GetSize().x - close_img.GetBmpWidth() - FromDIP(38);
        auto close_right    = close_left + close_img.GetBmpWidth();
        auto close_top      = rect.y + FromDIP(24);
        auto close_bottom   = close_top + close_img.GetBmpHeight();

        auto mouse_pos = ClientToScreen(e.GetPosition());
        if (mouse_pos.x > close_left 
            && mouse_pos.y > close_top
            && mouse_pos.x < close_right
            && mouse_pos.y < close_bottom
            )
        {
            Dismiss();
        }
        });

    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(24));
    main_sizer->Add(m_staticText, 0, wxALIGN_CENTER, 0);
    main_sizer->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(28));
    main_sizer->Add(curr_humidity_img, 0, wxALIGN_CENTER|wxLEFT|wxRIGHT, FromDIP(35));
    main_sizer->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(15));
    main_sizer->Add(humidity_level_list, 0, wxALIGN_CENTER|wxLEFT|wxRIGHT, FromDIP(35));
    main_sizer->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(6));
    main_sizer->Add(m_staticText_note, 0, wxALIGN_CENTER, 0);
    main_sizer->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(5));
    main_sizer->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(25));
    SetSizer(main_sizer);
    Layout();
    Fit();

    Bind(wxEVT_PAINT, &AmsHumidityTipPopup::paintEvent, this);
    wxGetApp().UpdateDarkUIWin(this);
}

void AmsHumidityTipPopup::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AmsHumidityTipPopup::OnDismiss() {}

bool AmsHumidityTipPopup::ProcessLeftDown(wxMouseEvent& event) {
    return PopupWindow::ProcessLeftDown(event);
}

void AmsHumidityTipPopup::set_humidity_level(int level)
{
    current_humidity_level = level;
    if (current_humidity_level<= 0) {return;}

    std::string mode_string = wxGetApp().dark_mode()?"_dark":"_light";

    curr_humidity_img->SetBitmap(create_scaled_bitmap("hum_level" + std::to_string(current_humidity_level) + mode_string, this, 132));
    curr_humidity_img->Refresh();
    curr_humidity_img->Update();
}

void AmsHumidityTipPopup::render(wxDC& dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({ 0, 0 }, size, &dc, { 0, 0 });

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void AmsHumidityTipPopup::doRender(wxDC& dc)
{
    //close
    dc.DrawBitmap(close_img.bmp(), GetSize().x - close_img.GetBmpWidth() - FromDIP(38), FromDIP(24));

    //background
    dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(0, 0, GetSize().x, GetSize().y, 0);
}

AmsTutorialPopup::AmsTutorialPopup(wxWindow* parent)
:PopupWindow(parent, wxBORDER_NONE)
{
    Bind(wxEVT_PAINT, &AmsTutorialPopup::paintEvent, this);
    SetBackgroundColour(*wxWHITE);

    wxBoxSizer* sizer_main;
    sizer_main = new wxBoxSizer(wxVERTICAL);

    text_title = new Label(this, Label::Head_14, _L("Config which AMS slot should be used for a filament used in the print job"));
    text_title->SetSize(wxSize(FromDIP(350), -1));
    text_title->Wrap(FromDIP(350));
    sizer_main->Add(text_title, 0, wxALIGN_CENTER | wxTOP, 18);


    sizer_main->Add(0, 0, 0, wxTOP, 30);

    wxBoxSizer* sizer_top;
    sizer_top = new wxBoxSizer(wxHORIZONTAL);

    img_top = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("ams_item_examples", this, 30), wxDefaultPosition, wxSize(FromDIP(50), FromDIP(30)), 0);
    sizer_top->Add(img_top, 0, wxALIGN_CENTER, 0);


    sizer_top->Add(0, 0, 0, wxLEFT, 10);

    wxBoxSizer* sizer_top_tips = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* sizer_tip_top = new wxBoxSizer(wxHORIZONTAL);

    arrows_top = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("ams_arrow", this, 8), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(8)), 0);
    sizer_tip_top->Add(arrows_top, 0, wxALIGN_CENTER, 0);

    tip_top = new wxStaticText(this, wxID_ANY, _L("Filament used in this print job"), wxDefaultPosition, wxDefaultSize, 0);
    tip_top->SetForegroundColour(wxColour("#686868"));
    
    sizer_tip_top->Add(tip_top, 0, wxALL, 0);


    sizer_top_tips->Add(sizer_tip_top, 0, wxEXPAND, 0);


    sizer_top_tips->Add(0, 0, 0, wxTOP, 6);

    wxBoxSizer* sizer_tip_bottom = new wxBoxSizer(wxHORIZONTAL);

    arrows_bottom = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("ams_arrow", this, 8), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(8)), 0);
    tip_bottom = new wxStaticText(this, wxID_ANY, _L("AMS slot used for this filament"), wxDefaultPosition, wxDefaultSize, 0);
    tip_bottom->SetForegroundColour(wxColour("#686868"));


    sizer_tip_bottom->Add(arrows_bottom, 0, wxALIGN_CENTER, 0);
    sizer_tip_bottom->Add(tip_bottom, 0, wxALL, 0);


    sizer_top_tips->Add(sizer_tip_bottom, 0, wxEXPAND, 0);


    sizer_top->Add(sizer_top_tips, 0, wxALIGN_CENTER, 0);


    

    wxBoxSizer* sizer_middle = new wxBoxSizer(wxHORIZONTAL);

    img_middle= new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("ams_item_examples", this, 30), wxDefaultPosition, wxSize(FromDIP(50), FromDIP(30)), 0);
    sizer_middle->Add(img_middle, 0, wxALIGN_CENTER, 0);

    tip_middle = new wxStaticText(this, wxID_ANY, _L("Click to select AMS slot manually"), wxDefaultPosition, wxDefaultSize, 0);
    tip_middle->SetForegroundColour(wxColour("#686868"));
    sizer_middle->Add(0, 0, 0,wxLEFT, 15);
    sizer_middle->Add(tip_middle, 0, wxALIGN_CENTER, 0);


    sizer_main->Add(sizer_top, 0, wxLEFT, 40);
    sizer_main->Add(0, 0, 0, wxTOP, 10);
    sizer_main->Add(sizer_middle, 0, wxLEFT, 40);
    sizer_main->Add(0, 0, 0, wxTOP, 10);


    img_botton = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("ams_mapping_examples", this, 87), wxDefaultPosition, wxDefaultSize, 0);
    sizer_main->Add(img_botton, 0, wxLEFT | wxRIGHT, 40);
    sizer_main->Add(0, 0, 0, wxTOP, 12);

    SetSizer(sizer_main);
    Layout();
    Fit();
}

void AmsTutorialPopup::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(0, 0, GetSize().x, GetSize().y, 0);
}

void AmsTutorialPopup::OnDismiss() {}

bool AmsTutorialPopup::ProcessLeftDown(wxMouseEvent& event) {
    return PopupWindow::ProcessLeftDown(event);
}


AmsIntroducePopup::AmsIntroducePopup(wxWindow* parent)
:PopupWindow(parent, wxBORDER_NONE)
{
    Bind(wxEVT_PAINT, &AmsIntroducePopup::paintEvent, this);
    SetBackgroundColour(*wxWHITE);

    SetMinSize(wxSize(FromDIP(200), FromDIP(200)));
    SetMaxSize(wxSize(FromDIP(200), FromDIP(200)));

    wxBoxSizer* bSizer4 = new wxBoxSizer(wxVERTICAL);

    m_staticText_top = new Label(this, _L("Do not Enable AMS"));
    m_staticText_top->SetFont(::Label::Head_13);
   // m_staticText_top->SetForegroundColour(wxColour(0x323A3D));
    m_staticText_top->Wrap(-1);
    bSizer4->Add(m_staticText_top, 0, wxALL, 5);

    m_staticText_bottom =  new Label(this, _L("Print using materials mounted on the back of the case"));
    m_staticText_bottom->Wrap(-1);
    m_staticText_bottom->SetFont(::Label::Body_13);
    m_staticText_bottom->SetForegroundColour(wxColour(0x6B6B6B));
    bSizer4->Add(m_staticText_bottom, 0, wxALL, 5);

    wxBoxSizer* bSizer5;
    bSizer5 = new wxBoxSizer(wxHORIZONTAL);

    m_img_enable_ams = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("monitor_upgrade_ams", this, FromDIP(140)), wxDefaultPosition, wxDefaultSize, 0);
    m_img_disable_ams = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("disable_ams_demo_icon", this, FromDIP(110)), wxDefaultPosition, wxDefaultSize, 0);

    m_img_enable_ams->SetMinSize(wxSize(FromDIP(96), FromDIP(110)));
    m_img_disable_ams->SetMinSize(wxSize(FromDIP(96), FromDIP(110)));

    bSizer5->Add(m_img_enable_ams, 0, wxALIGN_CENTER, 0);
    bSizer5->Add(m_img_disable_ams, 0, wxALIGN_CENTER, 0);

    m_img_disable_ams->Hide();
    m_img_disable_ams->Hide();


    bSizer4->Add(bSizer5, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(1));


    SetSizer(bSizer4);
    Layout();
    Fit();

    wxGetApp().UpdateDarkUIWin(this);
}

void AmsIntroducePopup::set_mode(bool enable_ams) 
{
    if (enable_ams) {
        m_staticText_top->SetLabelText(_L("Enable AMS"));
        m_staticText_bottom->SetLabelText(_L("Print with filaments in ams"));
        m_img_enable_ams->Show();
        m_img_disable_ams->Hide();
    }
    else {
        m_staticText_top->SetLabelText(_L("Do not Enable AMS"));
        m_staticText_bottom->SetLabelText(_L("Print with filaments mounted on the back of the chassis"));
        m_staticText_bottom->SetMinSize(wxSize(FromDIP(180), -1));
        m_staticText_bottom->Wrap(FromDIP(180));
        m_img_enable_ams->Hide();
        m_img_disable_ams->Show();
    }
    Layout();
    Fit();
}

void AmsIntroducePopup::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(0, 0, GetSize().x, GetSize().y, 0);
}


void AmsIntroducePopup::OnDismiss() {}

bool AmsIntroducePopup::ProcessLeftDown(wxMouseEvent& event) {
    return PopupWindow::ProcessLeftDown(event);
}


MappingContainer::MappingContainer(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__
    SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    Bind(wxEVT_PAINT, &MappingContainer::paintEvent, this);

    ams_mapping_item_container = create_scaled_bitmap("ams_mapping_container", this, 78);

    SetMinSize(wxSize(FromDIP(230), FromDIP(78)));
    SetMaxSize(wxSize(FromDIP(230), FromDIP(78)));
}

MappingContainer::~MappingContainer()
{
}


void MappingContainer::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void MappingContainer::render(wxDC& dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({ 0, 0 }, size, &dc, { 0, 0 });

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void MappingContainer::doRender(wxDC& dc)
{
    dc.DrawBitmap(ams_mapping_item_container, 0, 0);
}

AmsReplaceMaterialDialog::AmsReplaceMaterialDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Auto Refill"), wxDefaultPosition, wxDefaultSize, wxSYSTEM_MENU | wxCAPTION | wxCLOSE_BOX)
{

#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__
    SetBackgroundColour(*wxWHITE);
    create();
    wxGetApp().UpdateDlgDarkUI(this);
}

void AmsReplaceMaterialDialog::create()
{
    SetSize(wxSize(FromDIP(445), -1));
    SetMinSize(wxSize(FromDIP(445), -1));
    SetMaxSize(wxSize(FromDIP(445), -1));

    // set icon for dialog
    std::string icon_path = (boost::format("%1%/images/OrcaSlicerTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    m_main_sizer = new wxBoxSizer(wxVERTICAL);
    auto m_top_line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_top_line->SetBackgroundColour(wxColour(166, 169, 170));
    m_main_sizer->Add(m_top_line, 0, wxEXPAND, 0);


    auto label_title = new Label(this, _L("Auto Refill"));
    label_title->SetFont(Label::Head_14);
    label_title->SetForegroundColour(0x009688);
    label_txt = new Label(this, _L("When the current material run out, the printer will continue to print in the following order."));
    label_txt->SetFont(Label::Body_13);
    label_txt->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3C")));
    label_txt->SetMinSize(wxSize(FromDIP(380), -1));
    label_txt->SetMaxSize(wxSize(FromDIP(380), -1));
    label_txt->Wrap(FromDIP(380));

    m_scrollview_groups = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL | wxVSCROLL);
    m_scrollview_groups->SetScrollRate(5, 5);
    //m_scrollview_groups->SetMinSize(wxSize(400, 400));
    //m_scrollview_groups->SetMaxSize(wxSize(400, 400));
    m_scrollview_sizer = new wxBoxSizer(wxVERTICAL);

    m_groups_sizer = new wxWrapSizer( wxHORIZONTAL, wxWRAPSIZER_DEFAULT_FLAGS );

    m_scrollview_sizer->Add( m_groups_sizer, 0, wxALIGN_CENTER, 0 );
    m_scrollview_groups->SetSizer(m_scrollview_sizer);
    m_scrollview_groups->Layout();
    


    auto m_button_sizer = new wxBoxSizer(wxHORIZONTAL);
    StateColor btn_bg_white(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled),
        std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Pressed),
        std::pair<wxColour, int>(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Normal));

    StateColor btn_bd_white(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    StateColor btn_text_white(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));


    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal));
    m_button_sizer->Add( 0, 0, 1, wxEXPAND, 0 );

    m_main_sizer->Add(0,0,0, wxTOP, FromDIP(12));
    m_main_sizer->Add(label_title,0, wxLEFT, FromDIP(30));
    m_main_sizer->Add(0,0,0, wxTOP, FromDIP(4));
    m_main_sizer->Add(label_txt,0, wxLEFT, FromDIP(30));
    m_main_sizer->Add(0,0,0, wxTOP, FromDIP(16));
    m_main_sizer->Add(m_scrollview_groups, 1, wxALIGN_CENTER, 0);
    m_main_sizer->Add(0,0,0, wxTOP, FromDIP(20));
    m_main_sizer->Add(m_button_sizer,0,wxALIGN_CENTER, FromDIP(16));
    m_main_sizer->Add(0,0,0, wxTOP, FromDIP(20));
    

    CenterOnParent();
    SetSizer(m_main_sizer);
    Layout();
    Fit();
}

std::vector<bool> AmsReplaceMaterialDialog::GetStatus(unsigned int status)
{
    std::vector<bool> listStatus;
    bool current = false;
    for (int i = 0; i < 16; i++) {
        if (status & (1 << i)) {
            current = true;
        }
        else {
            current = false;
        }
        listStatus.push_back(current);
    }
    return listStatus;
}

void AmsReplaceMaterialDialog::update_mapping_result( std::vector<FilamentInfo> result)
{
    m_tray_used.clear();
    for (int i = 0; i < result.size(); i++) {
        m_tray_used.push_back(wxGetApp().transition_tridid(result[i].tray_id).ToStdString());
    }
}

void AmsReplaceMaterialDialog::update_machine_obj(MachineObject* obj)
{
    if (obj) {m_obj = obj;}
    else {return;}

    AmsTray* tray_list[4*4];
    for (auto i = 0; i < 4*4; i++) {
        tray_list[i] = nullptr;
    }

    try {
        for (auto ams_info : obj->amsList) {
            int ams_id_int = atoi(ams_info.first.c_str()) * 4;

            for (auto tray_info : ams_info.second->trayList) {
                int tray_id_int = atoi(tray_info.first.c_str());
                tray_id_int =  ams_id_int + tray_id_int;
                tray_list[tray_id_int] = tray_info.second;
            }
        }
    }
    catch (...) {}

    //creat group
    int group_index = 0;
    for (int filam : m_obj->filam_bak) {
         auto status_list = GetStatus(filam);

         std::map<std::string, wxColour> group_info;
         std::string    group_material;
         bool   is_in_tray = false;

         //get color & material
         for (auto i = 0; i < status_list.size(); i++) {
             if (status_list[i] && tray_list[i] != nullptr) {
                 auto tray_name = wxGetApp().transition_tridid(i).ToStdString();
                 auto it = std::find(m_tray_used.begin(), m_tray_used.end(), tray_name);
                 if (it != m_tray_used.end()) {
                     is_in_tray = true;
                 }

                 group_info[tray_name] = AmsTray::decode_color(tray_list[i]->color);
                 group_material = tray_list[i]->get_display_filament_type();
             }
         }

         if (is_in_tray || m_tray_used.size() <= 0) {
             m_groups_sizer->Add(create_backup_group(wxString::Format("%s%d", _L("Group"), group_index + 1), group_info, group_material, status_list), 0, wxALL, FromDIP(10));
             group_index++;
         } 
    }

    if (group_index > 0) {
        auto height = 0;
        if (group_index > 6) {
            height = FromDIP(550);
        }
        else {
            height = FromDIP(200) * (std::ceil(group_index / 2.0));
        }
        m_scrollview_groups->SetMinSize(wxSize(FromDIP(400), height));
        m_scrollview_groups->SetMaxSize(wxSize(FromDIP(400), height));
    } else {
        if (!obj->is_support_filament_backup) {
            label_txt->SetLabel(_L("The printer does not currently support auto refill."));
        }
        else if (!obj->ams_auto_switch_filament_flag) {
            label_txt->SetLabelText(_L("AMS filament backup is not enabled, please enable it in the AMS settings."));
        }
        else {
            label_txt->SetLabelText(_L("If there are two identical filaments in AMS, AMS filament backup will be enabled. \n(Currently supporting automatic supply of consumables with the same brand, material type, and color)"));
        } 

        label_txt->SetMinSize(wxSize(FromDIP(380), -1));
        label_txt->SetMaxSize(wxSize(FromDIP(380), -1));
        label_txt->Wrap(FromDIP(380));

    }
   
    m_scrollview_groups->Layout();
    Layout();
    Fit();
}

AmsRMGroup* AmsReplaceMaterialDialog::create_backup_group(wxString gname, std::map<std::string, wxColour> group_info, wxString material, std::vector<bool> status_list)
{
    auto grp = new AmsRMGroup(m_scrollview_groups, group_info, material, gname);
    return grp;
}

void AmsReplaceMaterialDialog::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(0, 0, GetSize().x, GetSize().y, 0);
}

void AmsReplaceMaterialDialog::on_dpi_changed(const wxRect& suggested_rect)
{

}

AmsRMGroup::AmsRMGroup(wxWindow* parent, std::map<std::string, wxColour> group_info, wxString mname, wxString group_index)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__
    m_group_info.clear();
    m_group_info = group_info;
    m_material_name = mname;
    m_group_index   = group_index;

    wxWindow::Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    SetSize(wxSize(FromDIP(166), FromDIP(166)));
    SetMinSize(wxSize(FromDIP(166), FromDIP(166)));
    SetMaxSize(wxSize(FromDIP(166), FromDIP(166)));
    SetBackgroundColour(*wxWHITE);

    backup_current_use_white    =  ScalableBitmap(this, "backup_current_use1",8);
    backup_current_use_black    =  ScalableBitmap(this, "backup_current_use2", 8);
    bitmap_backup_tips_0        =  ScalableBitmap(this, "backup_tips_img", 90);
    bitmap_editable             = ScalableBitmap(this, "ams_editable", 14);
    bitmap_bg                   = ScalableBitmap(this, "back_up_ts_bk", 162);
    bitmap_editable_light       = ScalableBitmap(this, "ams_editable_light", 14);

    Bind(wxEVT_PAINT, &AmsRMGroup::paintEvent, this);
    Bind(wxEVT_LEFT_DOWN, &AmsRMGroup::on_mouse_move, this);
    wxGetApp().UpdateDarkUI(this);
}

double AmsRMGroup::GetAngle(wxPoint pointA, wxPoint pointB)
{
    double deltaX = pointA.x - pointB.x;
    double deltaY = pointA.y - pointB.y;
    double angle = atan2(deltaY, deltaX);

    angle = angle * 180.0 / M_PI;

    if (angle < 0)
        angle += 360.0;

    return angle;
}

void AmsRMGroup::on_mouse_move(wxMouseEvent& evt)
{
    wxSize     size = GetSize();
    auto mouseX = evt.GetPosition().x;
    auto mouseY = evt.GetPosition().y;

    auto click_angle = 360.0 - GetAngle(wxPoint(mouseX,mouseY), wxPoint(size.x / 2, size.x / 2));


    float ev_angle = 360.0 / m_group_info.size();
    float startAngle = 0.0;
    float endAngle = 0.0;

    for (auto iter = m_group_info.rbegin(); iter != m_group_info.rend(); ++iter) {
        std::string tray_name = iter->first;
        wxColour tray_color = iter->second;

        int x = size.x / 2;
        int y = size.y / 2;
        int radius = size.x / 2;
        endAngle += ev_angle;

        if (click_angle >= startAngle && click_angle < endAngle) {
            //to do
            set_index(tray_name);
            Refresh();
            return;
        }
      
        startAngle += ev_angle;
    }

    evt.Skip();
}

void AmsRMGroup::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AmsRMGroup::render(wxDC& dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({ 0, 0 }, size, &dc, { 0, 0 });

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

wxPoint AmsRMGroup::CalculateEndpoint(const wxPoint& startPoint, int angle, int length)
{
    int endX = startPoint.x + length * cos(angle * M_PI / 180);
    int endY = startPoint.y + length * sin(angle * M_PI / 180);
    return wxPoint(endX, endY);
}

void AmsRMGroup::doRender(wxDC& dc)
{
    wxSize size = GetSize();
    
    float center_mask_radius = FromDIP(52);
    float selected_radius = FromDIP(53);

    float ev_angle = 360.0 / m_group_info.size();
    float startAngle = 0.0;
    float endAngle = 0.0;

    dc.DrawBitmap(bitmap_bg.bmp(), wxPoint((size.x - bitmap_bg.GetBmpSize().x) / 2, (size.y - bitmap_bg.GetBmpSize().y) / 2));

    for (auto iter = m_group_info.rbegin(); iter != m_group_info.rend(); ++iter) {
        std::string tray_name = iter->first;
        wxColour tray_color = iter->second;

        dc.SetPen(*wxTRANSPARENT_PEN);

        if (tray_color == *wxWHITE) dc.SetPen(wxPen(wxColour(0xEEEEEE), 2));
        dc.SetBrush(wxBrush(tray_color));

        int x = size.x / 2;
        int y = size.y / 2;
        int radius;
        if (wxGetApp().dark_mode())
            radius = size.x / 2 - int(size.x * 0.02);
        else
            radius = size.x / 2;
        endAngle += ev_angle;

 
        //draw body
        if (tray_color.Alpha() != 0) {
            dc.DrawEllipticArc(x - radius, y - radius, radius * 2, radius * 2, startAngle, endAngle);
            if (tray_color == *wxWHITE) dc.DrawEllipticArc(x - center_mask_radius, y - center_mask_radius, center_mask_radius * 2, center_mask_radius * 2, startAngle, endAngle);
        }

        //draw selected
        if (!m_selected_index.empty() && m_selected_index == tray_name) {
            dc.SetPen(wxPen(0xCECECE, 2));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawEllipticArc(x - radius, y - radius, radius * 2, radius * 2, startAngle, endAngle);
            dc.DrawEllipticArc(x - selected_radius, y - selected_radius,selected_radius * 2, selected_radius * 2,  startAngle, endAngle);
        }

        //...
        startAngle += ev_angle;
    }

    //draw text
    startAngle = 0.0;
    endAngle = 0.0;
    for (auto iter = m_group_info.rbegin(); iter != m_group_info.rend(); ++iter) {
        std::string tray_name = iter->first;
        wxColour tray_color = iter->second;

        int x = size.x / 2;
        int y = size.y / 2;
        float radius = size.x / 2 - 15.0;
        endAngle += ev_angle;

        float midAngle = (startAngle + endAngle) / 2;
        float x_center = size.x / 2 + radius * cos(midAngle * M_PI / 180.0);
        float y_center = size.y / 2 + radius * sin(midAngle * M_PI / 180.0);

        //draw tray
        dc.SetFont(::Label::Body_12);
        auto text_size = dc.GetTextExtent(tray_name);
        dc.SetTextForeground(tray_color.GetLuminance() < 0.6 ? *wxWHITE : wxColour(0x262E30));
        if (tray_color.Alpha() == 0) {dc.SetTextForeground(wxColour(0x262E30));}

        dc.DrawText(tray_name, x_center - text_size.x / 2, size.y - y_center - text_size.y / 2);

        //draw split line
        dc.SetPen(wxPen(*wxWHITE, 2));
        if (tray_color.Alpha() == 0) {dc.SetPen(wxPen(wxColour(0xCECECE), 2));}
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        auto pos_sp_start = CalculateEndpoint(wxPoint(x, y), (360 - startAngle),  size.x / 2 - FromDIP(3));
        dc.DrawLine(wxPoint(x, y), pos_sp_start);

        //draw current
        //dc.DrawBitmap(backup_current_use_white.bmp(), x_center - text_size.x / 2 + FromDIP(3), size.y - y_center - text_size.y / 2 + FromDIP(11));
        //...
        startAngle += ev_angle;
    }

    //draw center mask
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(*wxWHITE));

    int x = size.x / 2;
    int y = size.y / 2;
    dc.DrawEllipticArc(x - center_mask_radius, y - center_mask_radius, center_mask_radius * 2, center_mask_radius * 2, 0, 360);

    //draw center icon
    dc.DrawBitmap(bitmap_backup_tips_0.bmp(), wxPoint((size.x - bitmap_backup_tips_0.GetBmpSize().x) / 2, (size.y - bitmap_backup_tips_0.GetBmpSize().y) / 2));
    //dc.DrawBitmap(bitmap_backup_tips_1.bmp(), wxPoint((size.x - bitmap_backup_tips_1.GetBmpSize().x) / 2, (size.y - bitmap_backup_tips_1.GetBmpSize().y) / 2));

    //draw material
    dc.SetTextForeground(wxColour(0x323A3D));
    dc.SetFont(Label::Head_15);
    auto text_size = dc.GetTextExtent(m_material_name);
    dc.DrawText(m_material_name, (size.x - text_size.x) / 2,(size.y - text_size.y) / 2 - FromDIP(12));

    dc.SetFont(Label::Body_13);
    text_size = dc.GetTextExtent(m_group_index);
    dc.DrawText(m_group_index, (size.x - text_size.x) / 2, (size.y - text_size.y) / 2 + FromDIP(10));
}

AmsHumidityLevelList::AmsHumidityLevelList(wxWindow* parent)
 : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    SetSize(wxSize(FromDIP(680), FromDIP(104)));
    SetMinSize(wxSize(FromDIP(680), FromDIP(104)));
    SetMaxSize(wxSize(FromDIP(680), FromDIP(104)));
    SetBackgroundColour(*wxWHITE);

    background_img = ScalableBitmap(this, "humidity_list_background", 104);

    for (int i = 5; i > 0; i--) {
        hum_level_img_light.push_back(ScalableBitmap(this, ("hum_level" + std::to_string(i) + "_light"), 54));
        hum_level_img_dark.push_back(ScalableBitmap(this, ("hum_level" + std::to_string(i) + "_dark"), 54));
    }

    Bind(wxEVT_PAINT, &AmsHumidityLevelList::paintEvent, this);
    wxGetApp().UpdateDarkUI(this);
}

void AmsHumidityLevelList::msw_rescale()
{

}

void AmsHumidityLevelList::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AmsHumidityLevelList::render(wxDC& dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({ 0, 0 }, size, &dc, { 0, 0 });

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void AmsHumidityLevelList::doRender(wxDC& dc)
{
    dc.DrawBitmap(background_img.bmp(), 0,0);

    auto width_center = GetSize().x / 2;
    auto left = width_center - FromDIP(27) - FromDIP(46) * 2 - FromDIP(54) * 2;


    //dry / wet
    dc.SetTextForeground(wxColour(0x989898));
    dc.SetFont(::Label::Head_20);

    auto font_top = GetSize().y - dc.GetTextExtent(_L("DRY")).GetHeight();
    dc.DrawText(_L("DRY"), wxPoint(FromDIP(38), font_top / 2));
    dc.DrawText(_L("WET"), wxPoint(( GetSize().x - FromDIP(38) -  dc.GetTextExtent(_L("DRY")).GetWidth()), font_top / 2));


    //level list
    
    for (int i = 0; i < hum_level_img_light.size(); i++) {
        if (wxGetApp().dark_mode()) {
            dc.DrawBitmap(hum_level_img_dark[i].bmp(), left, (GetSize().y - FromDIP(54)) / 2);
        }
        else {
             dc.DrawBitmap(hum_level_img_light[i].bmp(), left, (GetSize().y - FromDIP(54)) / 2);
        }
        
        left += FromDIP(46) + FromDIP(54);
    }
}

}} // namespace Slic3r::GUI
