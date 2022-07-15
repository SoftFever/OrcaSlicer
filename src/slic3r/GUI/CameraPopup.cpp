#include "CameraPopup.hpp"

#include "I18N.hpp"
#include "Widgets/Label.hpp"
#include "libslic3r/Utils.hpp"

namespace Slic3r {
namespace GUI {

wxIMPLEMENT_CLASS(CameraPopup, wxPopupTransientWindow);

wxBEGIN_EVENT_TABLE(CameraPopup, wxPopupTransientWindow)
    EVT_MOUSE_EVENTS(CameraPopup::OnMouse )
    EVT_SIZE(CameraPopup::OnSize)
    EVT_SET_FOCUS(CameraPopup::OnSetFocus )
    EVT_KILL_FOCUS(CameraPopup::OnKillFocus )
wxEND_EVENT_TABLE()


static const wxFont TEXT_FONT = Label::Body_14;
static wxColour TEXT_COL = wxColour(43, 52, 54);

CameraPopup::CameraPopup(wxWindow *parent, MachineObject* obj)
   : wxPopupTransientWindow(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS),
    m_obj(obj)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif
    m_panel = new wxScrolledWindow(this, wxID_ANY);
    m_panel->SetBackgroundColour(*wxWHITE);

    m_panel->Bind(wxEVT_MOTION, &CameraPopup::OnMouse, this);

    wxBoxSizer * main_sizer = new wxBoxSizer(wxHORIZONTAL);
    wxFlexGridSizer* top_sizer = new wxFlexGridSizer(0, 2, 0, 0);
    top_sizer->AddGrowableCol(0);
    top_sizer->SetFlexibleDirection(wxBOTH);
    top_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);
    m_text_timelapse = new wxStaticText(m_panel, wxID_ANY, _L("Timelapse"));
    m_text_timelapse->Wrap(-1);
    m_text_timelapse->SetFont(TEXT_FONT);
    m_text_timelapse->SetForegroundColour(TEXT_COL);
    m_switch_timelapse = new SwitchButton(m_panel);
    if (obj)
        m_switch_timelapse->SetValue(obj->camera_timelapse);
    m_text_recording = new wxStaticText(m_panel, wxID_ANY, _L("Monitoring Recording"));
    m_text_recording->Wrap(-1);
    m_text_recording->SetFont(TEXT_FONT);
    m_text_recording->SetForegroundColour(TEXT_COL);
    m_switch_recording = new SwitchButton(m_panel);
    if (obj)
        m_switch_recording->SetValue(obj->camera_recording);

    top_sizer->Add(m_text_timelapse, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, FromDIP(5));
    top_sizer->Add(m_switch_timelapse, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxALL, FromDIP(5));
    top_sizer->Add(m_text_recording, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, FromDIP(5));
    top_sizer->Add(m_switch_recording, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxALL, FromDIP(5));
    main_sizer->Add(top_sizer, 0, wxALL, FromDIP(10));
    m_panel->SetSizer(main_sizer);
    m_panel->Layout();

    main_sizer->Fit(m_panel);

    SetClientSize(m_panel->GetSize());
    m_switch_timelapse->Connect(wxEVT_LEFT_DOWN, wxCommandEventHandler(CameraPopup::on_switch_timelapse), NULL, this);
    m_switch_recording->Connect(wxEVT_LEFT_DOWN, wxCommandEventHandler(CameraPopup::on_switch_recording), NULL, this);
}

void CameraPopup::on_switch_timelapse(wxCommandEvent& event)
{
    if (!m_obj) return;

    bool value = m_switch_timelapse->GetValue();
    m_switch_timelapse->SetValue(!value);
    m_obj->command_ipcam_timelapse(!value);
}

void CameraPopup::on_switch_recording(wxCommandEvent& event)
{
    if (!m_obj) return;

    bool value = m_switch_recording->GetValue();
    m_switch_recording->SetValue(!value);
    m_obj->command_ipcam_record(!value);
}

void CameraPopup::Popup(wxWindow *WXUNUSED(focus))
{
    wxPoint curr_position = this->GetPosition();
    wxSize win_size = this->GetSize();
    curr_position.x -= win_size.x;
    this->SetPosition(curr_position);
    wxPopupTransientWindow::Popup();
}

void CameraPopup::OnDismiss() {
    wxPopupTransientWindow::OnDismiss();
}

bool CameraPopup::ProcessLeftDown(wxMouseEvent &event)
{
    return wxPopupTransientWindow::ProcessLeftDown(event);
}
bool CameraPopup::Show(bool show)
{
    return wxPopupTransientWindow::Show(show);
}

void CameraPopup::OnSize(wxSizeEvent &event)
{
    event.Skip();
}

void CameraPopup::OnSetFocus(wxFocusEvent &event)
{
    event.Skip();
}

void CameraPopup::OnKillFocus(wxFocusEvent &event)
{
    event.Skip();
}

void CameraPopup::OnMouse(wxMouseEvent &event)
{
    event.Skip();
}


}
}