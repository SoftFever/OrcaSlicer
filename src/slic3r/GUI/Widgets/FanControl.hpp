#ifndef slic3r_GUI_FANCONTROL_hpp_
#define slic3r_GUI_FANCONTROL_hpp_

#include "../wxExtensions.hpp"
#include "StaticBox.hpp"
#include "StepCtrl.hpp"
#include "Button.hpp"
#include "PopupWindow.hpp"
#include "../DeviceManager.hpp"
#include "slic3r/GUI/Event.hpp"
#include <wx/simplebook.h>
#include <wx/hyperlink.h>
#include <wx/animate.h>
#include <wx/dynarray.h>


namespace Slic3r {
namespace GUI {


/*************************************************
Description:Fan
**************************************************/
#define SIZE_OF_FAN_OPERATE wxSize(100, 28)

#define DRAW_TEXT_COLOUR wxColour(0x898989)
#define DRAW_OPERATE_LINE_COLOUR wxColour(0xDEDEDE)

struct RotateOffSet
{
    float rotate;
    wxPoint offset;
};

class Fan : public wxWindow
{
public:
    Fan(wxWindow* parent, wxWindowID id, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize);
    ~Fan() {};
    void    post_event(wxCommandEvent&& event);
    void    paintEvent(wxPaintEvent& evt);
    void    render(wxDC& dc);
    void    doRender(wxDC& dc);
    void    msw_rescale();
    void    create(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size);
    void    set_fan_speeds(int g);

private:
    int     m_current_speeds;
    std::vector<RotateOffSet> m_rotate_offsets;

protected:
    std::vector<wxPoint> m_scale_pos_array;

    ScalableBitmap   m_bitmap_bk;
    ScalableBitmap   m_bitmap_scale_0;
    ScalableBitmap   m_bitmap_scale_1;
    ScalableBitmap   m_bitmap_scale_2;
    ScalableBitmap   m_bitmap_scale_3;
    ScalableBitmap   m_bitmap_scale_4;
    ScalableBitmap   m_bitmap_scale_5;
    ScalableBitmap   m_bitmap_scale_6;
    ScalableBitmap   m_bitmap_scale_7;
    ScalableBitmap   m_bitmap_scale_8;
    ScalableBitmap   m_bitmap_scale_9;
    ScalableBitmap   m_bitmap_scale_10;

    std::vector<ScalableBitmap> m_bitmap_scales;

    wxImage          m_img_pointer;

    virtual void     DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO);
};

/*************************************************
Description:FanOperate
**************************************************/
class FanOperate : public wxWindow
{
public:
    FanOperate(wxWindow* parent, wxWindowID id, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize);
    ~FanOperate() {};
    void    post_event(wxCommandEvent&& event);
    void    paintEvent(wxPaintEvent& evt);
    void    render(wxDC& dc);
    void    doRender(wxDC& dc);
    void    msw_rescale();
    void    create(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size);
    void    on_left_down(wxMouseEvent& event);

public:
    void    set_fan_speeds(int g);
    void    add_fan_speeds();
    void    decrease_fan_speeds();
private:
    int     m_current_speeds;
    int     m_min_speeds;
    int     m_max_speeds;
    ScalableBitmap   m_bitmap_add;
    ScalableBitmap   m_bitmap_decrease;
};

/*************************************************
Description:FanControl
**************************************************/
class FanControl : public wxWindow
{
public:
    FanControl(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize);
    ~FanControl() {};

protected:
    MachineObject::FanType m_type;
    MachineObject* m_obj;
    wxStaticText* m_static_name{ nullptr };
    ScalableBitmap* m_bitmap_toggle_off{ nullptr };
    ScalableBitmap* m_bitmap_toggle_on{ nullptr };

    Fan* m_fan{ nullptr };
    FanOperate* m_fan_operate{ nullptr };
    bool m_switch_fan{ false };
    bool m_update_already{false};
    int  m_current_speed{0};
public:
    wxStaticBitmap* m_switch_button{ nullptr };
    void update_obj_state(bool stat) {m_update_already = stat;};
    void command_control_fan();
    void set_machine_obj(MachineObject* obj);
    void set_type(MachineObject::FanType type);
    void set_name(wxString name);
    void set_fan_speed(int g);
    void set_fan_switch(bool s);
    void post_event(wxCommandEvent&& event);
    void on_swith_fan(wxMouseEvent& evt);
    void on_swith_fan(bool on);
    void on_left_down(wxMouseEvent& event);
};


/*************************************************
Description:FanControlPopup
**************************************************/
class FanControlPopup : public PopupWindow
{
public:
    FanControlPopup(wxWindow* parent);
    ~FanControlPopup() {};

private:
    wxBoxSizer* m_sizer_main;
    FanControl* m_part_fan;
    FanControl* m_aux_fan;
    FanControl* m_cham_fan;
    wxWindow* m_line_top;
    wxWindow* m_line_bottom;
    bool      m_is_suppt_cham_fun{true};
    bool      m_is_suppt_aux_fun{true};

public:
    void         show_cham_fan(bool support_cham_fun);
    void         show_aux_fan(bool support_aux_fun);
    void         update_fan_data(MachineObject::FanType type, MachineObject* obj);
    void         on_left_down(wxMouseEvent& evt);
    void         paintEvent(wxPaintEvent& evt);
    void         post_event(int fan_type, wxString speed);
    void         on_show(wxShowEvent& evt);
    virtual void OnDismiss() wxOVERRIDE;
    virtual bool ProcessLeftDown(wxMouseEvent& event) wxOVERRIDE;
};

wxDECLARE_EVENT(EVT_FAN_SWITCH_ON, wxCommandEvent);
wxDECLARE_EVENT(EVT_FAN_SWITCH_OFF, wxCommandEvent);
wxDECLARE_EVENT(EVT_FAN_ADD, wxCommandEvent);
wxDECLARE_EVENT(EVT_FAN_DEC, wxCommandEvent);
wxDECLARE_EVENT(EVT_FAN_CHANGED, wxCommandEvent);

}} // namespace Slic3r::GUI

#endif
