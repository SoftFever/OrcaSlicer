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
     SetSize(wxSize(FromDIP(220), -1));
     SetMinSize(wxSize(FromDIP(220), -1));
     SetMaxSize(wxSize(FromDIP(220), -1));
     Bind(wxEVT_PAINT, &AmsMapingPopup::paintEvent, this);
     SetBackgroundColour(*wxWHITE);

     m_sizer_main         = new wxBoxSizer(wxVERTICAL);
     //m_sizer_main->Add(0, 0, 1, wxEXPAND, 0);

     SetSizer(m_sizer_main);
     Layout();
 }


void AmsMapingPopup::update_materials_list(std::vector<std::string> list) 
{ 
    m_materials_list = list;
}

void AmsMapingPopup::set_tag_texture(std::string texture) 
{ 
    m_tag_material = texture;
}


bool AmsMapingPopup::is_match_material(int id, std::string material)
{
    return m_tag_material == material ? true : false;
}


void AmsMapingPopup::update_ams_data(std::map<std::string, Ams*> amsList) 
{ 
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
                    td.name   = tray_data->type;
                }
            }

            tray_datas.push_back(td);
        }
        add_ams_mapping(tray_datas);
    }

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
        Button *m_filament_name = new Button(this, "", wxEmptyString);
        m_filament_name->SetSize(wxSize(FromDIP(38), FromDIP(20)));
        m_filament_name->SetMinSize(wxSize(FromDIP(38), FromDIP(20)));
        m_filament_name->SetMaxSize(wxSize(FromDIP(38), FromDIP(20)));
        m_filament_name->SetCornerRadius(5);
        m_filament_name->SetFont(::Label::Body_12);
      
        if (tray_data[i].type == NORMAL) {

            if (m_filament_name->GetTextExtent(tray_data[i].name).x > FromDIP(38)) {
                m_filament_name->SetFont(::Label::Body_10);
                auto name = tray_data[i].name.substr(0, 3) + "." + tray_data[i].name.substr(tray_data[i].name.length() - 1);
                m_filament_name->SetLabel(name);
            } else {
                m_filament_name->SetLabel(tray_data[i].name);
            }

            auto material_name_colour = tray_data[i].colour.GetLuminance() < 0.5 ? *wxWHITE : wxColour(0x26, 0x2E, 0x30);
            m_filament_name->SetTextColor(material_name_colour);
            m_filament_name->SetBackgroundColor(tray_data[i].colour);

            if (tray_data[i].colour == *wxWHITE) {
                m_filament_name->SetBorderColor(wxColour(0xAC, 0xAC, 0xAC));
            } else {
                m_filament_name->SetBorderColor(tray_data[i].colour);
            }

            m_filament_name->Bind(wxEVT_BUTTON, [this, tray_data, i](wxCommandEvent &e) {
                if (!is_match_material(tray_data[i].id, tray_data[i].name)) return;
                wxCommandEvent event(EVT_SET_FINISH_MAPPING);
                event.SetInt(tray_data[i].id);
                wxString param = wxString::Format("%d|%d|%d|%02d|%d", tray_data[i].colour.Red(), tray_data[i].colour.Green(), tray_data[i].colour.Blue(), tray_data[i].id + 1, m_current_filament_id);
                event.SetString(param);
                event.SetEventObject(this->GetParent());
                wxPostEvent(this->GetParent(), event);
                Dismiss();
            });
        }
        

        // temp
        if (tray_data[i].type == EMPTY) {
            m_filament_name->SetLabel("-");
            m_filament_name->SetTextColor(*wxWHITE);
            m_filament_name->SetBackgroundColor(wxColour(0x6B, 0x6B, 0x6B));
            m_filament_name->SetBorderColor(wxColour(0x6B, 0x6B, 0x6B));
            m_filament_name->Bind(wxEVT_BUTTON, [this, tray_data, i](wxCommandEvent &e) {
                wxCommandEvent event(EVT_SET_FINISH_MAPPING);
                event.SetInt(tray_data[i].id);
                wxString param = wxString::Format("%d|%d|%d|%02d|%d", 0x6B, 0x6B, 0x6B, tray_data[i].id + 1, m_current_filament_id);
                event.SetString(param);
                event.SetEventObject(this->GetParent());
                wxPostEvent(this->GetParent(), event);
                Dismiss();
            });
        }

        // third party
        if (tray_data[i].type == THIRD) {
            m_filament_name->SetLabel("?");
            m_filament_name->SetTextColor(*wxWHITE);
            m_filament_name->SetBackgroundColor(wxColour(0x6B, 0x6B, 0x6B));
            m_filament_name->SetBorderColor(wxColour(0x6B, 0x6B, 0x6B));
             m_filament_name->Bind(wxEVT_BUTTON, [this, tray_data, i](wxCommandEvent &e) {
                wxCommandEvent event(EVT_SET_FINISH_MAPPING);
                event.SetInt(tray_data[i].id);
                wxString param = wxString::Format("%d|%d|%d|%02d|%d", 0x6B, 0x6B, 0x6B, tray_data[i].id + 1, m_current_filament_id);
                event.SetString(param);
                event.SetEventObject(this->GetParent());
                wxPostEvent(this->GetParent(), event);
                Dismiss();
            });
        }

        sizer_mapping_item->Add(number, 0, wxALIGN_CENTER_HORIZONTAL, 0);
        sizer_mapping_item->Add(m_filament_name, 0, wxALIGN_CENTER_HORIZONTAL, 0);
        sizer_mapping_list->Add(sizer_mapping_item, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, FromDIP(5));
        m_amsmapping_sizer_list.push_back(sizer_mapping_list);
    }
    m_sizer_main->Add(sizer_mapping_list, 0, wxALIGN_CENTER_HORIZONTAL, 0);
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

}} // namespace Slic3r::GUI
