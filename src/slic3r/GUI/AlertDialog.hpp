#ifndef __scanner_dialog_alert_hpp
#define __scanner_dialog_alert_hpp

#include <wx/button.h>
#include "GUI.hpp"
#include "GUI_Utils.hpp"
#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/string.h>
#include <wx/stattext.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/statbmp.h>
#include "Widgets/Button.hpp"
#include "Widgets/TextInput.hpp"
#include "DeviceManager.hpp"
#include "format.hpp"

class AlertDialog : public DPIDialog
{

    public:
    AlertDialog(std::string error_message) : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Scanner Alert"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX) 
    {

        wxBoxSizer *main_sizer;
        main_sizer = new wxBoxSizer(wxHORIZONTAL);

        main_sizer->Add(FromDIP(40), 0);

        wxBoxSizer *sizer_top;
        sizer_top = new wxBoxSizer(wxVERTICAL);

        sizer_top->Add(0, FromDIP(40));

        // SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));
        m_body = new wxStaticText(this, wxID_ANY, _L(error_message), wxDefaultPosition, wxDefaultSize, 0);
        m_body->SetFont(Label::Body_15);
        m_body->SetForegroundColour(wxColour(50, 58, 61));
        m_body->Wrap(-1);
        sizer_top->Add(m_body, 0, wxALL, 0);
        sizer_top->Add(0, FromDIP(10));

        StateColor btn_bg_blue(std::pair<wxColour, int>(wxColour(0, 40, 220), StateColor::Pressed), std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal));
    
        m_button_confirm = new Button(this, _L("I undestand"));    
        m_button_confirm->SetBackgroundColor(btn_bg_blue);
        m_button_confirm->SetBorderColor(wxColour(0, 150, 136));
        m_button_confirm->SetTextColor(wxColour(255, 255, 255));
        m_button_confirm->SetSize(wxSize(FromDIP(72), FromDIP(24)));
        m_button_confirm->SetMinSize(wxSize(FromDIP(72), FromDIP(24)));
        m_button_confirm->SetCornerRadius(FromDIP(12));

        m_button_confirm->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event) {
            EndModal(wxID_CANCEL);
        });

        // wxBoxSizer *sizer_connect;
        // sizer_connect = new wxBoxSizer(wxHORIZONTAL);
        sizer_top->Add(m_button_confirm, 0, wxALL | wxALIGN_CENTER_VERTICAL, 0);

        sizer_top->Add(FromDIP(20), 0);
        main_sizer->Add(sizer_top, 0);

        SetSizer(main_sizer);
        SetBackgroundColour(*wxWHITE);
    }

    ~AlertDialog() {}


    protected:
    void on_dpi_changed(const wxRect &suggested_rect)
    {
        Fit();
        Refresh();
    }

    wxStaticText *               m_body{nullptr};
    Button * m_button_confirm {nullptr};

};

#endif