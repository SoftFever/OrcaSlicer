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
 }

 MaterialItem::~MaterialItem() {}

void MaterialItem::msw_rescale() {}

void MaterialItem::set_ams_info(wxColour col, wxString txt)
{
    auto need_refresh = false;
    if (m_ams_coloul != col) { m_ams_coloul = col; need_refresh = true;}
    if (m_ams_name != txt) {m_ams_name   = txt;need_refresh = true;}
    if (need_refresh) { Refresh();}
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

    // materials name
    dc.SetFont(::Label::Body_13);

    auto material_name_colour = m_material_coloul.GetLuminance() < 0.5 ? *wxWHITE : wxColour(0x26, 0x2E, 0x30);
    dc.SetTextForeground(material_name_colour);

    if (dc.GetTextExtent(m_material_name).x > GetSize().x - 10) {
        dc.SetFont(::Label::Body_10);

    }

    auto material_txt_size = dc.GetTextExtent(m_material_name);
    dc.DrawText(m_material_name, wxPoint((MATERIAL_ITEM_SIZE.x - material_txt_size.x) / 2, (FromDIP(22) - material_txt_size.y) / 2));

    // mapping num
    dc.SetFont(::Label::Body_10);
    dc.SetTextForeground(m_ams_coloul.GetLuminance() < 0.5 ? *wxWHITE : wxColour(0x26, 0x2E, 0x30));

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
    //top
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(m_material_coloul));
    dc.DrawRoundedRectangle(FromDIP(1), FromDIP(1), MATERIAL_ITEM_REAL_SIZE.x, FromDIP(18), 5);
    
    //bottom
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(wxColour(m_ams_coloul)));
    dc.DrawRoundedRectangle(FromDIP(1), FromDIP(18), MATERIAL_ITEM_REAL_SIZE.x, FromDIP(16), 5);

    ////middle
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(m_material_coloul));
    dc.DrawRectangle(FromDIP(1), FromDIP(11), MATERIAL_ITEM_REAL_SIZE.x, FromDIP(8));

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(m_ams_coloul));
    dc.DrawRectangle(FromDIP(1), FromDIP(18), MATERIAL_ITEM_REAL_SIZE.x, FromDIP(8));

    ////border
    if (m_material_coloul == *wxWHITE || m_ams_coloul == *wxWHITE) {
        dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(0, 0, MATERIAL_ITEM_SIZE.x, MATERIAL_ITEM_SIZE.y, 5);
    }

    if (m_selected) {
        dc.SetPen(wxColour(0x00, 0xAE, 0x42));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(0, 0, MATERIAL_ITEM_SIZE.x, MATERIAL_ITEM_SIZE.y, 5);
    }
}

 AmsMapingPopup::AmsMapingPopup(wxWindow *parent) 
    :wxPopupTransientWindow(parent, wxBORDER_NONE)
 {
     SetSize(wxSize(FromDIP(300), -1));
     SetMinSize(wxSize(FromDIP(300), -1));
     SetMaxSize(wxSize(FromDIP(300), -1));
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

     m_warning_text = new wxStaticText(this, wxID_ANY, wxEmptyString);
     m_warning_text->SetForegroundColour(wxColour(0xFF, 0x6F, 0x00));
     m_warning_text->SetFont(::Label::Body_12);
     auto cant_not_match_tip = _L("Note: Only the AMS slots loaded with the same material type can be selected.");
     m_warning_text->SetLabel(format_text(cant_not_match_tip));
     m_warning_text->SetMinSize(wxSize(FromDIP(280), FromDIP(-1)));
     m_warning_text->Wrap(FromDIP(280));

     m_sizer_main->Add(title_panel, 0, wxEXPAND | wxALL, FromDIP(2));
     m_sizer_main->Add(m_sizer_list, 0, wxEXPAND | wxALL, FromDIP(0));
     m_sizer_main->Add(m_warning_text, 0, wxEXPAND | wxALL, FromDIP(10));

     SetSizer(m_sizer_main);
     Layout();
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

void AmsMapingPopup::update_ams_data(std::map<std::string, Ams*> amsList) 
{ 
    m_has_unmatch_filament = false;
    m_mapping_item_list.clear();
    if (m_amsmapping_sizer_list.size() > 0) {
        for (wxBoxSizer *bz : m_amsmapping_sizer_list) { bz->Clear(true); }
        m_amsmapping_sizer_list.clear();
    }
   
    std::map<std::string, Ams *>::iterator ams_iter;

    BOOST_LOG_TRIVIAL(trace) << "ams_mapping total count " << amsList.size();

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
                }
            }

            tray_datas.push_back(td);
        }
        add_ams_mapping(tray_datas);
    }


    m_warning_text->Show(m_has_unmatch_filament);
    Layout();
    Fit();
}

void AmsMapingPopup::add_ams_mapping(std::vector<TrayData> tray_data)
{ 
    auto sizer_mapping_list = new wxBoxSizer(wxHORIZONTAL);
    for (auto i = 0; i < tray_data.size(); i++) {
        wxBoxSizer *sizer_mapping_item   = new wxBoxSizer(wxVERTICAL);

        // set number
        auto number = new wxStaticText(this, wxID_ANY, wxString::Format("%02d",tray_data[i].id + 1), wxDefaultPosition, wxDefaultSize, 0);
        number->SetFont(::Label::Body_13);
        number->SetForegroundColour(wxColour(0X6B, 0X6B, 0X6B));
        number->Wrap(-1);
        

        // set button
        MappingItem *m_filament_name = new MappingItem(this);
        m_filament_name->SetSize(wxSize(FromDIP(62), FromDIP(22)));
        m_filament_name->SetMinSize(wxSize(FromDIP(62), FromDIP(22)));
        m_filament_name->SetMaxSize(wxSize(FromDIP(62), FromDIP(22)));
        //m_filament_name->SetCornerRadius(5);
        m_filament_name->SetFont(::Label::Body_12);
        m_mapping_item_list.push_back(m_filament_name);
      
        if (tray_data[i].type == NORMAL) {
            if (is_match_material(tray_data[i].filament_type)) { 
                m_filament_name->set_data(tray_data[i].colour, tray_data[i].name, tray_data[i]);
            } else {
                m_filament_name->set_data(wxColour(0xEE,0xEE,0xEE), tray_data[i].name, tray_data[i], true);
                m_has_unmatch_filament = true;
            }

            m_filament_name->Bind(wxEVT_LEFT_DOWN, [this, tray_data, i, m_filament_name](wxMouseEvent &e) {
                if (!is_match_material(tray_data[i].filament_type)) return;
                m_filament_name->send_event(m_current_filament_id);
                Dismiss();
            });
        }
        

        // temp
        if (tray_data[i].type == EMPTY) {
            m_filament_name->set_data(wxColour(0xCE, 0xCE, 0xCE), "-", tray_data[i]);
            m_filament_name->Bind(wxEVT_LEFT_DOWN, [this, tray_data, i, m_filament_name](wxMouseEvent &e) {
                m_filament_name->send_event(m_current_filament_id);
                Dismiss();
            });
        }

        // third party
        if (tray_data[i].type == THIRD) {
            m_filament_name->set_data(wxColour(0xCE, 0xCE, 0xCE), "?", tray_data[i]);
            m_filament_name->Bind(wxEVT_LEFT_DOWN, [this, tray_data, i, m_filament_name](wxMouseEvent &e) {
                m_filament_name->send_event(m_current_filament_id);
                Dismiss();
            });
        }


        sizer_mapping_item->Add(number, 0, wxALIGN_CENTER_HORIZONTAL, 0);
        sizer_mapping_item->Add(m_filament_name, 0, wxALIGN_CENTER_HORIZONTAL, 0);
        sizer_mapping_list->Add(sizer_mapping_item, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, FromDIP(5));
        m_amsmapping_sizer_list.push_back(sizer_mapping_list);
    }
    m_sizer_list->Add(sizer_mapping_list, 0, wxALIGN_CENTER_HORIZONTAL, 0);
}

void AmsMapingPopup::OnDismiss()
{

}

bool AmsMapingPopup::ProcessLeftDown(wxMouseEvent &event) 
{
    return wxPopupTransientWindow::ProcessLeftDown(event);
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
    SetBackgroundColour(*wxWHITE);
    Bind(wxEVT_PAINT, &MappingItem::paintEvent, this);
}

 MappingItem::~MappingItem() 
{
}


void MappingItem::send_event(int fliament_id) 
{
    wxCommandEvent event(EVT_SET_FINISH_MAPPING);
    event.SetInt(m_tray_data.id);
    wxString param = wxString::Format("%d|%d|%d|%02d|%d", m_coloul.Red(), m_coloul.Green(), m_coloul.Blue(), m_tray_data.id + 1, fliament_id);
    event.SetString(param);
    event.SetEventObject(this->GetParent()->GetParent());
    wxPostEvent(this->GetParent()->GetParent(), event);
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
    dc.SetFont(::Label::Body_12);

    auto txt_colour = m_coloul.GetLuminance() < 0.5 ? *wxWHITE : wxColour(0x26, 0x2E, 0x30);
    txt_colour      = m_unmatch ? wxColour(0xCE, 0xCE, 0xCE) : txt_colour;

    dc.SetTextForeground(txt_colour);

    /*if (dc.GetTextExtent(m_name).x > GetSize().x - 10) {
        dc.SetFont(::Label::Body_10);
        m_name = m_name.substr(0, 3) + "." + m_name.substr(m_name.length() - 1);
    }*/

    auto txt_size = dc.GetTextExtent(m_name);
    dc.DrawText(m_name, wxPoint((GetSize().x - txt_size.x) / 2, (GetSize().y - txt_size.y) / 2));
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
    dc.SetPen(m_coloul);
    dc.SetBrush(wxBrush(m_coloul));
    dc.DrawRoundedRectangle(0, 0, GetSize().x, GetSize().y,5);
    if (m_coloul == *wxWHITE) {
        dc.SetPen(wxPen(wxColour(0xAC, 0xAC, 0xAC),1));
        dc.DrawRoundedRectangle(0, 0, GetSize().x, GetSize().y, 5);
    } 
}

}} // namespace Slic3r::GUI
