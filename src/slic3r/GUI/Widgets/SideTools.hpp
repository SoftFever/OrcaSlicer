#ifndef slic3r_GUI_SIDETOOLS_hpp_
#define slic3r_GUI_SIDETOOLS_hpp_

#include <wx/dcgraph.h>
#include <wx/gdicmn.h>
#include <wx/dcclient.h>
#include "../wxExtensions.hpp"

#define SIDE_TOOLS_GREY900 wxColour(38, 46, 48)
#define SIDE_TOOLS_GREY600 wxColour(144, 144, 144)
#define SIDE_TOOLS_GREY400 wxColour(206, 206, 206)
#define SIDE_TOOLS_BRAND wxColour(0, 174, 66)
#define SIDE_TOOLS_LIGHT_GREEN wxColour(219, 253, 231)

enum WifiSignal {
    NONE,
    WEAK,
    MIDDLE,
    STRONG,
};

#define SIDE_TOOL_CLICK_INTERVAL 20

namespace Slic3r { namespace GUI {

class SideTools : public wxPanel
{
private:
    WifiSignal      m_wifi_type{WifiSignal::NONE};
    wxString        m_dev_name;
    bool            m_hover{false};
    bool            m_click{false};
    bool            m_none_printer{true};
    int             last_printer_signal = 0;

    ScalableBitmap  m_printing_img;
    ScalableBitmap  m_arrow_img;

    ScalableBitmap  m_none_printing_img;
    ScalableBitmap  m_none_arrow_img;
    ScalableBitmap  m_none_add_img;

    ScalableBitmap  m_wifi_none_img;
    ScalableBitmap  m_wifi_weak_img;
    ScalableBitmap  m_wifi_middle_img;
    ScalableBitmap  m_wifi_strong_img;

protected:
    wxStaticBitmap *m_bitmap_info;
    wxStaticBitmap *m_bitmap_bind;
    wxTimer *       m_intetval_timer{nullptr};
    bool            m_is_in_interval {false};

public:
    SideTools(wxWindow *parent, wxWindowID id = wxID_ANY, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize);
    ~SideTools();

    void set_none_printer_mode();
    void on_timer(wxTimerEvent &event);
    void set_current_printer_name(std::string dev_name);
    void set_current_printer_signal(WifiSignal sign);;
    void start_interval();
    void stop_interval(wxTimerEvent &event);
    bool is_in_interval();
    void msw_rescale();

protected:
    void OnPaint(wxPaintEvent &event);
    void render(wxDC &dc);
    void doRender(wxDC &dc);
    void on_mouse_enter(wxMouseEvent &evt);
    void on_mouse_leave(wxMouseEvent &evt);
    void on_mouse_left_down(wxMouseEvent &evt);
    void on_mouse_left_up(wxMouseEvent &evt);
};
}} // namespace Slic3r::GUI

#endif // !slic3r_GUI_SIDETOOLS_hpp_
