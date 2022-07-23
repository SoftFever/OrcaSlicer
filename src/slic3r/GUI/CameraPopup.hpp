#ifndef slic3r_CameraPopup_hpp_
#define slic3r_CameraPopup_hpp_

#include "slic3r/GUI/MonitorBasePanel.h"
#include "DeviceManager.hpp"
#include "GUI.hpp"
#include <wx/panel.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/sizer.h>
#include <wx/gbsizer.h>
#include <wx/webrequest.h>
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


class CameraItem : public wxPanel
{
public:
    CameraItem(wxWindow *parent, std::string off_normal, std::string on_normal, std::string off_hover, std::string on_hover);
    ~CameraItem();

    MachineObject *m_obj{nullptr};
    bool     m_on{false};
    bool     m_hover{false};
    wxBitmap m_bitmap_on_normal;
    wxBitmap m_bitmap_on_hover;
    wxBitmap m_bitmap_off_normal;
    wxBitmap m_bitmap_off_hover;

    void msw_rescale();
    void set_switch(bool is_on);
    bool get_switch_status() { return m_on; };
    void on_enter_win(wxMouseEvent &evt);
    void on_level_win(wxMouseEvent &evt);
    void paintEvent(wxPaintEvent &evt);
    void render(wxDC &dc);
    void doRender(wxDC &dc);
};

}
}
#endif
