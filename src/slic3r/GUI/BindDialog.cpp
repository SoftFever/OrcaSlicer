#include "BindDialog.hpp"
#include "GUI_App.hpp"

#include <wx/wx.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include "wx/evtloop.h"

#include "libslic3r/Model.hpp"
#include "libslic3r/Polygon.hpp"
#include "MainFrame.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"

namespace Slic3r {
namespace GUI {

 BindMachineDilaog::BindMachineDilaog(Plater *plater /*= nullptr*/)
     : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Log in printer"), wxDefaultPosition, wxDefaultSize, wxCAPTION)
 {
     std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
     SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

     SetBackgroundColour(*wxWHITE);
     wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
     auto m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
     m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
     m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
     m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(38));

     wxBoxSizer *m_sizer_body = new wxBoxSizer(wxHORIZONTAL);

     m_panel_left = new StaticBox(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(201), FromDIP(212)), wxBORDER_NONE);
     m_panel_left->SetMinSize(wxSize(FromDIP(201), FromDIP(212)));
     m_panel_left->SetCornerRadius(8);
     m_panel_left->SetBackgroundColor(BIND_DIALOG_GREY200);
     wxBoxSizer *m_sizere_left_h = new wxBoxSizer(wxHORIZONTAL);
     wxBoxSizer *m_sizere_left_v= new wxBoxSizer(wxVERTICAL);

     auto m_printer_img = new wxStaticBitmap(m_panel_left, wxID_ANY, create_scaled_bitmap("printer_thumbnail", nullptr, 96), wxDefaultPosition, wxSize(FromDIP(100), FromDIP(96)), 0);
     m_printer_name = new wxStaticText(m_panel_left, wxID_ANY, wxEmptyString);
     m_printer_name->SetBackgroundColour(BIND_DIALOG_GREY200);
     m_printer_name->SetFont(::Label::Head_14);
     m_sizere_left_v->Add(m_printer_img, 0, wxALIGN_CENTER, 0);
     m_sizere_left_v->Add(0, 0, 0, wxTOP, 5);
     m_sizere_left_v->Add(m_printer_name, 0, wxALIGN_CENTER, 0);
     m_sizere_left_h->Add(m_sizere_left_v, 1, wxALIGN_CENTER, 0);

     m_panel_left->SetSizer(m_sizere_left_h);
     m_panel_left->Layout();
     m_sizer_body->Add(m_panel_left, 0, wxEXPAND, 0);

     auto m_bind_icon = create_scaled_bitmap("bind_machine", nullptr, 14);
     m_sizer_body->Add(new wxStaticBitmap(this, wxID_ANY, m_bind_icon, wxDefaultPosition, wxSize(FromDIP(34), FromDIP(14)), 0), 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(20));

     m_panel_right = new StaticBox(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(201), FromDIP(212)), wxBORDER_NONE);
     m_panel_right->SetMinSize(wxSize(FromDIP(201), FromDIP(212)));
     m_panel_right->SetCornerRadius(8);
     m_panel_right->SetBackgroundColor(BIND_DIALOG_GREY200);

     m_user_name = new wxStaticText(m_panel_right, wxID_ANY, wxEmptyString);
     m_user_name->SetBackgroundColour(BIND_DIALOG_GREY200);
     m_user_name->SetFont(::Label::Head_14);
     wxBoxSizer *m_sizer_right_h = new wxBoxSizer(wxHORIZONTAL);
     wxBoxSizer *m_sizer_right_v = new wxBoxSizer(wxVERTICAL);


     Bind(wxEVT_WEBREQUEST_STATE, [this](wxWebRequestEvent& evt) {
         switch (evt.GetState()) {
             // Request completed
         case wxWebRequest::State_Completed: {
             wxImage avatar_stream = *evt.GetResponse().GetStream();
             if (avatar_stream.IsOk()) {
                 avatar_stream.Rescale(FromDIP(60), FromDIP(60));
                 auto bitmap = new wxBitmap(avatar_stream);
                 //bitmap->SetSize(wxSize(FromDIP(60), FromDIP(60)));
                 m_avatar->SetBitmap(*bitmap);
                 Layout();
             }
             break;
         }
                                           // Request failed
         case wxWebRequest::State_Failed: {
             break;
         }
         }
    });

     if (wxGetApp().is_user_login()) {
        wxString username_text = from_u8(wxGetApp().getAgent()->get_user_nickanme());
        m_user_name->SetLabelText(username_text);

         m_avatar = new wxStaticBitmap(m_panel_right, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(FromDIP(60), FromDIP(60)), 0);

         wxWebRequest request = wxWebSession::GetDefault().CreateRequest(this, wxGetApp().getAgent()->get_user_avatar());
         if (!request.IsOk()) {
             // todo request fail
         }
         // Start the request
         request.Start();
     }

     m_sizer_right_v->Add(m_avatar, 0, wxALIGN_CENTER, 0);
     m_sizer_right_v->Add(0, 0, 0, wxTOP, 7);
     m_sizer_right_v->Add(m_user_name, 0, wxALIGN_CENTER, 0);
     m_sizer_right_h->Add(m_sizer_right_v, 1, wxALIGN_CENTER, 0);

     m_panel_right->SetSizer(m_sizer_right_h);
     m_panel_right->Layout();
     m_sizer_body->Add(m_panel_right, 0, wxEXPAND, 0);

     m_sizer_main->Add(m_sizer_body, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));

     m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(20));

     m_status_text = new wxStaticText(this, wxID_ANY, _L("Would you like to log in this printer with current account?"), wxDefaultPosition,
                                           wxSize(BIND_DIALOG_BUTTON_PANEL_SIZE.x, -1), wxST_ELLIPSIZE_END);
     m_status_text->SetForegroundColour(wxColour(107, 107, 107));
     m_status_text->SetFont(::Label::Body_13);
     m_status_text->Wrap(-1);

     m_simplebook = new wxSimplebook(this, wxID_ANY, wxDefaultPosition,BIND_DIALOG_BUTTON_PANEL_SIZE, 0);
     m_simplebook->SetBackgroundColour(*wxWHITE);

     m_status_bar = std::make_shared<BBLStatusBarBind>(m_simplebook);

     auto        button_panel   = new wxPanel(m_simplebook, wxID_ANY, wxDefaultPosition, BIND_DIALOG_BUTTON_PANEL_SIZE);
     button_panel->SetBackgroundColour(*wxWHITE);
     wxBoxSizer *m_sizer_button = new wxBoxSizer(wxHORIZONTAL);
     m_sizer_button->Add(0, 0, 1, wxEXPAND, 5);
     m_button_bind = new Button(button_panel, _L("Confirm"));
     StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                             std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
     m_button_bind->SetBackgroundColor(btn_bg_green);
     m_button_bind->SetBorderColor(wxColour(0, 174, 66));
     m_button_bind->SetTextColor(*wxWHITE);
     m_button_bind->SetSize(BIND_DIALOG_BUTTON_SIZE);
     m_button_bind->SetMinSize(BIND_DIALOG_BUTTON_SIZE);
     m_button_bind->SetCornerRadius(10);


     StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

     m_button_cancel = new Button(button_panel, _L("Cancel"));
     m_button_cancel->SetBackgroundColor(btn_bg_white);
     m_button_cancel->SetBorderColor(BIND_DIALOG_GREY900);
     m_button_cancel->SetSize(BIND_DIALOG_BUTTON_SIZE);
     m_button_cancel->SetMinSize(BIND_DIALOG_BUTTON_SIZE);
     m_button_cancel->SetTextColor(BIND_DIALOG_GREY900);
     m_button_cancel->SetCornerRadius(10);

     m_sizer_button->Add(m_button_bind, 0, wxALIGN_CENTER, 0);
     m_sizer_button->Add(0, 0, 0, wxLEFT, FromDIP(13));
     m_sizer_button->Add(m_button_cancel, 0, wxALIGN_CENTER, 0);
     button_panel->SetSizer(m_sizer_button);
     button_panel->Layout();
     m_sizer_button->Fit(button_panel);

     m_simplebook->AddPage(m_status_bar->get_panel(), wxEmptyString, false);
     m_simplebook->AddPage(button_panel, wxEmptyString, false);

     //m_sizer_main->Add(m_sizer_button, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));

     m_sizer_main->Add(m_status_text, 0, wxALIGN_CENTER, 0);
     m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(10));
     m_sizer_main->Add(m_simplebook, 0, wxALIGN_CENTER, 0);
     m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(20));

     SetSizer(m_sizer_main);
     Layout();
     Fit();
     Centre(wxBOTH);

     Bind(wxEVT_SHOW, &BindMachineDilaog::on_show, this);

     m_button_bind->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindMachineDilaog::on_bind_printer), NULL, this);
     m_button_cancel->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindMachineDilaog::on_cancel), NULL, this);
     this->Connect(EVT_BIND_MACHINE_FAIL, wxCommandEventHandler(BindMachineDilaog::on_bind_fail), NULL, this);
     this->Connect(EVT_BIND_MACHINE_SUCCESS, wxCommandEventHandler(BindMachineDilaog::on_bind_success), NULL, this);
     this->Connect(EVT_BIND_UPDATE_MESSAGE, wxCommandEventHandler(BindMachineDilaog::on_update_message), NULL, this);
     m_simplebook->SetSelection(1);
 }

 BindMachineDilaog::~BindMachineDilaog()
 {
     m_button_bind->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindMachineDilaog::on_bind_printer), NULL, this);
     m_button_cancel->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindMachineDilaog::on_cancel), NULL, this);
     this->Disconnect(EVT_BIND_MACHINE_FAIL, wxCommandEventHandler(BindMachineDilaog::on_bind_fail), NULL, this);
     this->Disconnect(EVT_BIND_MACHINE_SUCCESS, wxCommandEventHandler(BindMachineDilaog::on_bind_success), NULL, this);
     this->Disconnect(EVT_BIND_UPDATE_MESSAGE, wxCommandEventHandler(BindMachineDilaog::on_update_message), NULL, this);
 }

 //static  size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
 //{
 //    register int         realsize = size * nmemb;
 //    struct MemoryStruct *mem      = (struct MemoryStruct *) userp;
 //    mem->memory                   = (char *) realloc(mem->memory, mem->size + realsize + 1);
 //    if (mem->memory) {
 //        memcpy(&(mem->memory[mem->size]), contents, realsize);
 //        mem->size += realsize;
 //        mem->memory[mem->size] = 0;
 //    }
 //    return realsize;
 //}


 void BindMachineDilaog::on_cancel(wxCommandEvent &event)
 {
      EndModal(wxID_CANCEL);
 }

 void BindMachineDilaog::on_bind_fail(wxCommandEvent &event)
 {
    //m_status_text->SetLabel(_L("Would you like to log in this printer with current account?"));
    m_simplebook->SetSelection(1);
 }

 void BindMachineDilaog::on_update_message(wxCommandEvent &event)
 {
     m_status_text->SetLabelText(event.GetString());
 }

 void BindMachineDilaog::on_bind_success(wxCommandEvent &event)
 {
     EndModal(wxID_OK);
     MessageDialog msg_wingow(nullptr, _L("Log in successful."), "", wxAPPLY | wxOK);
     if (msg_wingow.ShowModal() == wxOK) { return; }
 }

 void BindMachineDilaog::on_bind_printer(wxCommandEvent &event)
 {
     //check isset info
     if (m_machine_info == nullptr || m_machine_info == NULL) return;

     //check dev_id
     if (m_machine_info->dev_id.empty()) return;

     m_simplebook->SetSelection(0);
     m_bind_job = std::make_shared<BindJob>(m_status_bar, wxGetApp().plater(), m_machine_info->dev_id, m_machine_info->dev_ip);
     m_bind_job->set_event_handle(this);
     m_bind_job->start();
 }

void BindMachineDilaog::on_dpi_changed(const wxRect &suggested_rect)
{
    m_button_bind->SetMinSize(BIND_DIALOG_BUTTON_SIZE);
    m_button_cancel->SetMinSize(BIND_DIALOG_BUTTON_SIZE);
}

void BindMachineDilaog::on_show(wxShowEvent &event)
{
    //m_printer_name->SetLabelText(m_machine_info->get_printer_type_string());
    m_printer_name->SetLabelText(from_u8(m_machine_info->dev_name));
    Layout();
}


UnBindMachineDilaog::UnBindMachineDilaog(Plater *plater /*= nullptr*/)
     : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Log out printer"), wxDefaultPosition, wxDefaultSize, wxCAPTION)
 {
     std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
     SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

     SetBackgroundColour(*wxWHITE);
     wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
     auto m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
     m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
     m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
     m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(38));

     wxBoxSizer *m_sizer_body = new wxBoxSizer(wxHORIZONTAL);

     auto  m_panel_left = new StaticBox(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(201), FromDIP(212)), wxBORDER_NONE);
     m_panel_left->SetMinSize(wxSize(FromDIP(201), FromDIP(212)));
     m_panel_left->SetCornerRadius(8);
     m_panel_left->SetBackgroundColor(BIND_DIALOG_GREY200);
     wxBoxSizer *m_sizere_left_h = new wxBoxSizer(wxHORIZONTAL);
     wxBoxSizer *m_sizere_left_v= new wxBoxSizer(wxVERTICAL);

     auto m_printer_img = new wxStaticBitmap(m_panel_left, wxID_ANY, create_scaled_bitmap("printer_thumbnail", nullptr, 96), wxDefaultPosition, wxSize(FromDIP(100), FromDIP(96)),
                                             0);
     m_printer_name     = new wxStaticText(m_panel_left, wxID_ANY, wxEmptyString);
     m_printer_name->SetFont(::Label::Head_14);
     m_printer_name->SetBackgroundColour(BIND_DIALOG_GREY200);
     m_sizere_left_v->Add(m_printer_img, 0, wxALIGN_CENTER, 0);
     m_sizere_left_v->Add(0, 0, 0, wxTOP, 5);
     m_sizere_left_v->Add(m_printer_name, 0, wxALIGN_CENTER, 0);
     m_sizere_left_h->Add(m_sizere_left_v, 1, wxALIGN_CENTER, 0);

     m_panel_left->SetSizer(m_sizere_left_h);
     m_panel_left->Layout();
     m_sizer_body->Add(m_panel_left, 0, wxEXPAND, 0);

     auto m_bind_icon = create_scaled_bitmap("unbind_machine", nullptr, 28);
     m_sizer_body->Add(new wxStaticBitmap(this, wxID_ANY, m_bind_icon, wxDefaultPosition, wxSize(FromDIP(36), FromDIP(28)), 0), 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(20));

     auto m_panel_right = new StaticBox(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(201), FromDIP(212)), wxBORDER_NONE);
     m_panel_right->SetMinSize(wxSize(FromDIP(201), FromDIP(212)));
     m_panel_right->SetCornerRadius(8);
     m_panel_right->SetBackgroundColor(BIND_DIALOG_GREY200);
     m_user_name = new wxStaticText(m_panel_right, wxID_ANY, wxEmptyString);
     m_user_name->SetBackgroundColour(BIND_DIALOG_GREY200);
     m_user_name->SetFont(::Label::Head_14);
     wxBoxSizer *m_sizer_right_h = new wxBoxSizer(wxHORIZONTAL);
     wxBoxSizer *m_sizer_right_v = new wxBoxSizer(wxVERTICAL);

     m_avatar = new wxStaticBitmap(m_panel_right, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(FromDIP(60), FromDIP(60)), 0);

     if (wxGetApp().is_user_login()) {
         wxString username_text = from_u8(wxGetApp().getAgent()->get_user_name());
         m_user_name->SetLabelText(username_text);
         wxString avatar_url = wxGetApp().getAgent()->get_user_avatar();
         wxWebRequest request = wxWebSession::GetDefault().CreateRequest(this, avatar_url);
         if (!request.IsOk()) {
             // todo request fail
         }
         request.Start();
     }


     Bind(wxEVT_WEBREQUEST_STATE, [this](wxWebRequestEvent &evt) {
         switch (evt.GetState()) {
         // Request completed
         case wxWebRequest::State_Completed: {
             wxImage avatar_stream = *evt.GetResponse().GetStream();
             if (avatar_stream.IsOk()) {
                 avatar_stream.Rescale(FromDIP(60), FromDIP(60));
                 auto bitmap = new wxBitmap(avatar_stream);
                 //bitmap->SetSize(wxSize(FromDIP(60), FromDIP(60)));
                 m_avatar->SetBitmap(*bitmap);
                 Layout();
             }
             break;
         }
         // Request failed
         case wxWebRequest::State_Failed: {
             break;
         }
         }
     });



     m_sizer_right_v->Add(m_avatar, 0, wxALIGN_CENTER, 0);
     m_sizer_right_v->Add(0, 0, 0, wxTOP, 7);
     m_sizer_right_v->Add(m_user_name, 0, wxALIGN_CENTER, 0);
     m_sizer_right_h->Add(m_sizer_right_v, 1, wxALIGN_CENTER, 0);

     m_panel_right->SetSizer(m_sizer_right_h);
     m_panel_right->Layout();
     m_sizer_body->Add(m_panel_right, 0, wxEXPAND, 0);

     m_sizer_main->Add(m_sizer_body, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));

     m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(20));

     m_status_text = new wxStaticText(this, wxID_ANY, _L("Would you like to log out the printer?"), wxDefaultPosition,
                                           wxSize(BIND_DIALOG_BUTTON_PANEL_SIZE.x, -1), wxST_ELLIPSIZE_END);
     m_status_text->SetForegroundColour(wxColour(107, 107, 107));
     m_status_text->SetFont(::Label::Body_13);
     m_status_text->Wrap(-1);



     wxBoxSizer *m_sizer_button = new wxBoxSizer(wxHORIZONTAL);

     m_sizer_button->Add(0, 0, 1, wxEXPAND, 5);
     m_button_unbind = new Button(this, _L("Confirm"));
     StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                             std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
     m_button_unbind->SetBackgroundColor(btn_bg_green);
     m_button_unbind->SetBorderColor(wxColour(0, 174, 66));
     m_button_unbind->SetTextColor(*wxWHITE);
     m_button_unbind->SetSize(BIND_DIALOG_BUTTON_SIZE);
     m_button_unbind->SetMinSize(BIND_DIALOG_BUTTON_SIZE);
     m_button_unbind->SetCornerRadius(10);


     StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

     m_button_cancel = new Button(this, _L("Cancel"));
     m_button_cancel->SetBackgroundColor(btn_bg_white);
     m_button_cancel->SetBorderColor(BIND_DIALOG_GREY900);
     m_button_cancel->SetSize(BIND_DIALOG_BUTTON_SIZE);
     m_button_cancel->SetMinSize(BIND_DIALOG_BUTTON_SIZE);
     m_button_cancel->SetTextColor(BIND_DIALOG_GREY900);
     m_button_cancel->SetCornerRadius(10);

     m_sizer_button->Add(m_button_unbind, 0, wxALIGN_CENTER, 0);
     m_sizer_button->Add(0, 0, 0, wxLEFT, FromDIP(13));
     m_sizer_button->Add(m_button_cancel, 0, wxALIGN_CENTER, 0);

     m_sizer_main->Add(m_status_text, 0, wxALIGN_CENTER, 0);
     m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(10));
     m_sizer_main->Add(m_sizer_button, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));
     m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(20));

     SetSizer(m_sizer_main);
     Layout();
     Fit();
     Centre(wxBOTH);

     Bind(wxEVT_SHOW, &UnBindMachineDilaog::on_show, this);
     m_button_unbind->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(UnBindMachineDilaog::on_unbind_printer), NULL, this);
     m_button_cancel->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(UnBindMachineDilaog::on_cancel), NULL, this);
 }

 UnBindMachineDilaog::~UnBindMachineDilaog()
 {
     m_button_unbind->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(UnBindMachineDilaog::on_unbind_printer), NULL, this);
     m_button_cancel->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(UnBindMachineDilaog::on_cancel), NULL, this);
 }


void UnBindMachineDilaog::on_cancel(wxCommandEvent &event)
{
    EndModal(wxID_CANCEL);
}

void UnBindMachineDilaog::on_unbind_printer(wxCommandEvent &event)
{
    if (!wxGetApp().is_user_login()) {
        m_status_text->SetLabelText(_L("Please log in first."));
        return;
    }

    if (!m_machine_info) {
        m_status_text->SetLabelText(_L("There was a problem connecting to the printer. Please try again."));
        return;
    }

    m_machine_info->set_access_code("");
    int result = wxGetApp().request_user_unbind(m_machine_info->dev_id);
    if (result == 0) {
        DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
        if (!dev) return;
        // clean local machine access code info
        MachineObject* obj = dev->get_local_machine(m_machine_info->dev_id);
        if (obj) {
            obj->set_access_code("");
        }
        dev->erase_user_machine(m_machine_info->dev_id);

        m_status_text->SetLabelText(_L("Log out successful."));
        m_button_cancel->SetLabel(_L("Close"));
        m_button_unbind->Hide();
        EndModal(wxID_OK);
    }
    else {
        m_status_text->SetLabelText(_L("Failed to log out."));
        EndModal(wxID_CANCEL);
        return;
    }
}

 void UnBindMachineDilaog::on_dpi_changed(const wxRect &suggested_rect)
{
      m_button_unbind->SetMinSize(BIND_DIALOG_BUTTON_SIZE);
      m_button_cancel->SetMinSize(BIND_DIALOG_BUTTON_SIZE);
}

void UnBindMachineDilaog::on_show(wxShowEvent &event)
{
    //m_printer_name->SetLabelText(m_machine_info->get_printer_type_string());
    m_printer_name->SetLabelText(from_u8(m_machine_info->dev_name));
    Layout();
}

}} // namespace Slic3r::GUI
