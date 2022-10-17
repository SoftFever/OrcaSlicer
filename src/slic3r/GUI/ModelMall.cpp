#include "ModelMall.hpp"
#include "GUI_App.hpp"

#include <wx/wx.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include "wx/evtloop.h"

#include "libslic3r/Model.hpp"
#include "MainFrame.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"

namespace Slic3r {
namespace GUI {
    ModelMallDialog::ModelMallDialog(Plater* plater /*= nullptr*/)
        :DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, _L("3D Models"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    {
        // icon
        std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
        SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

        SetSize(MODEL_MALL_PAGE_SIZE);
        SetMaxSize(MODEL_MALL_PAGE_SIZE);
        SetMinSize(MODEL_MALL_PAGE_SIZE);

        wxBoxSizer* m_sizer_main = new wxBoxSizer(wxVERTICAL);

        m_web_control_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, MODEL_MALL_PAGE_CONTROL_SIZE, wxTAB_TRAVERSAL);
        m_web_control_panel->SetBackgroundColour(*wxWHITE);
        m_web_control_panel->SetSize(MODEL_MALL_PAGE_CONTROL_SIZE);
        m_web_control_panel->SetMaxSize(MODEL_MALL_PAGE_CONTROL_SIZE);
        m_web_control_panel->SetMinSize(MODEL_MALL_PAGE_CONTROL_SIZE);

        wxBoxSizer* m_sizer_web_control = new wxBoxSizer(wxHORIZONTAL);

        auto m_control_back = new ScalableButton(m_web_control_panel, wxID_ANY, "mall_control_back", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true);
        m_control_back->SetBackgroundColour(*wxWHITE);
        m_control_back->SetSize(wxSize(FromDIP(25), FromDIP(30)));
        m_control_back->SetMinSize(wxSize(FromDIP(25), FromDIP(30)));
        m_control_back->SetMaxSize(wxSize(FromDIP(25), FromDIP(30)));

        m_control_back->Bind(wxEVT_LEFT_DOWN, &ModelMallDialog::on_back, this);
        m_control_back->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {SetCursor(wxCursor(wxCURSOR_HAND));});
        m_control_back->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {SetCursor(wxCursor(wxCURSOR_ARROW));});


        auto m_control_forward = new ScalableButton(m_web_control_panel, wxID_ANY, "mall_control_forward", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true);
        m_control_forward->SetBackgroundColour(*wxWHITE);
        m_control_forward->SetSize(wxSize(FromDIP(25), FromDIP(30)));
        m_control_forward->SetMinSize(wxSize(FromDIP(25), FromDIP(30)));
        m_control_forward->SetMaxSize(wxSize(FromDIP(25), FromDIP(30)));

        m_control_forward->Bind(wxEVT_LEFT_DOWN, &ModelMallDialog::on_forward, this);
        m_control_forward->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {SetCursor(wxCursor(wxCURSOR_HAND)); });
        m_control_forward->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {SetCursor(wxCursor(wxCURSOR_ARROW)); });


#ifdef __APPLE__
        m_control_back->SetToolTip(_L("Click to return (Command + Left Arrow)"));
        m_control_forward->SetToolTip(_L("Click to continue (Command + Right Arrow)"));
#else
        m_control_back->SetToolTip(_L("Click to return (Alt + Left Arrow)"));
        m_control_forward->SetToolTip(_L("Click to continue (Alt + Right Arrow)"));
#endif
        

        /* auto m_textCtrl1 = new wxTextCtrl(m_web_control_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(600, 30), 0);
         auto m_button1 = new wxButton(m_web_control_panel, wxID_ANY, wxT("GO"), wxDefaultPosition, wxDefaultSize, 0);
         m_button1->Bind(wxEVT_BUTTON, [this,m_textCtrl1](auto& e) {
             go_to_url(m_textCtrl1->GetValue());
         });*/
        
        m_sizer_web_control->Add( m_control_back, 0, wxALIGN_CENTER | wxLEFT, FromDIP(26) );
        m_sizer_web_control->Add(m_control_forward, 0, wxALIGN_CENTER | wxLEFT, FromDIP(26));
        //m_sizer_web_control->Add(m_button1, 0, wxALIGN_CENTER|wxLEFT, 5);
        //m_sizer_web_control->Add(m_textCtrl1, 0, wxALIGN_CENTER|wxLEFT, 5);

        m_web_control_panel->SetSizer(m_sizer_web_control);
        m_web_control_panel->Layout();
        m_sizer_web_control->Fit(m_web_control_panel);

        m_browser = WebView::CreateWebView(this, wxEmptyString);
        if (m_browser == nullptr) {
            wxLogError("Could not init m_browser");
            return;
        }

        m_browser->SetSize(MODEL_MALL_PAGE_WEB_SIZE);
        m_browser->SetMinSize(MODEL_MALL_PAGE_WEB_SIZE);
        m_browser->SetMaxSize(MODEL_MALL_PAGE_WEB_SIZE);

        m_browser->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &ModelMallDialog::OnScriptMessage, this, m_browser->GetId());

        m_sizer_main->Add(m_web_control_panel, 0, wxEXPAND, 0);
        m_sizer_main->Add(m_browser, 0, wxEXPAND, 0);
        SetSizer(m_sizer_main);
        Layout();
        Fit();

        Centre(wxBOTH);
        Bind(wxEVT_SHOW, &ModelMallDialog::on_show, this);
    }


    ModelMallDialog::~ModelMallDialog()
    {
    }

    void ModelMallDialog::OnScriptMessage(wxWebViewEvent& evt)
    {
        try {
            wxString strInput = evt.GetString();
            json     j = json::parse(strInput);

            wxString strCmd = j["command"];

            if (strCmd == "request_model_download") {

                std::string model_id = "";
                if (j["data"].contains("download_url"))
                    model_id = j["data"]["model_id"].get<std::string>();

                std::string profile_id = "";
                if (j["data"].contains("profile_id"))
                    profile_id = j["data"]["profile_id"].get<std::string>();

                std::string download_url = "";
                if (j["data"].contains("download_url"))
                    download_url = j["data"]["download_url"].get<std::string>();

                if (download_url.empty()) return;
                wxGetApp().plater()->request_model_download(download_url);
            }
          
        }
        catch (std::exception& e) {
            // wxMessageBox(e.what(), "json Exception", MB_OK);
        }
    }

    void ModelMallDialog::on_dpi_changed(const wxRect& suggested_rect)
    {
    }

    void ModelMallDialog::on_show(wxShowEvent& event)
    {
        event.Skip();
    }

    void ModelMallDialog::on_back(wxMouseEvent& evt)
    {
        if (m_browser->CanGoBack()) {
            m_browser->GoBack();
        }
    }

    void ModelMallDialog::on_forward(wxMouseEvent& evt)
    {
        if (m_browser->CanGoForward()) {
            m_browser->GoForward();
        }
    }

    void ModelMallDialog::go_to_url(wxString url)
    {
        //m_browser->LoadURL(url);
        WebView::LoadUrl(m_browser, url);
    }

    void ModelMallDialog::show_control(bool show) 
    {
        m_web_control_panel->Show(show);
        Layout();
        Fit();
    }

    void ModelMallDialog::go_to_mall(wxString url)
    {
        //show_control(true);
        //m_browser->ClearHistory();
        go_to_url(url);
    }

    void ModelMallDialog::go_to_publish(wxString url)
    {
        //show_control(true);
        //m_browser->ClearHistory();
        go_to_url(url);
    }

}
} // namespace Slic3r::GUI
