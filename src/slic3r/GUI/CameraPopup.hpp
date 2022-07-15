#ifndef slic3r_CameraPopup_hpp_
#define slic3r_CameraPopup_hpp_

#include "slic3r/GUI/MonitorBasePanel.h"
#include "DeviceManager.hpp"
#include "GUI.hpp"
#include <wx/panel.h>
#include "Widgets/SwitchButton.hpp"

namespace Slic3r {
namespace GUI {

class CameraPopup : public wxPopupTransientWindow
{
public:
    CameraPopup(wxWindow *parent, MachineObject* obj = nullptr);
    virtual ~CameraPopup() {}

    // wxPopupTransientWindow virtual methods are all overridden to log them
    virtual void Popup(wxWindow *focus = NULL) wxOVERRIDE;
    virtual void OnDismiss() wxOVERRIDE;
    virtual bool ProcessLeftDown(wxMouseEvent &event) wxOVERRIDE;
    virtual bool Show(bool show = true) wxOVERRIDE;

protected:
    void on_switch_timelapse(wxCommandEvent& event);
    void on_switch_recording(wxCommandEvent& event);

private:
    MachineObject* m_obj { nullptr };
    wxStaticText* m_text_recording;
    SwitchButton* m_switch_recording;
    wxStaticText* m_text_timelapse;
    SwitchButton* m_switch_timelapse;
    wxScrolledWindow *m_panel;

    void OnMouse(wxMouseEvent &event);
    void OnSize(wxSizeEvent &event);
    void OnSetFocus(wxFocusEvent &event);
    void OnKillFocus(wxFocusEvent &event);

private:
    wxDECLARE_ABSTRACT_CLASS(CameraPopup);
    wxDECLARE_EVENT_TABLE();
};

}
}
#endif
