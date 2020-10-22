#ifndef slic3r_OG_CustomCtrl_hpp_
#define slic3r_OG_CustomCtrl_hpp_

#include <wx/stattext.h>
#include <wx/settings.h>

#include <map>
#include <functional>

#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "OptionsGroup.hpp"
#include "I18N.hpp"

// Translate the ifdef 
#ifdef __WXOSX__
    #define wxOSX true
#else
    #define wxOSX false
#endif

//#define BORDER(a, b) ((wxOSX ? a : b))

namespace Slic3r { namespace GUI {

//  Static text shown among the options.
class OG_CustomCtrl :public wxControl
{
    wxFont  m_font;
    int     m_v_gap;
    int     m_h_gap;

    ScalableBitmap    m_bmp_mode_simple;
    ScalableBitmap    m_bmp_mode_advanced;
    ScalableBitmap    m_bmp_mode_expert;
    ScalableBitmap    m_bmp_blinking;

    struct CtrlLine {
        wxCoord           height  { wxDefaultCoord };
        OG_CustomCtrl*    ctrl    { nullptr };
        const Line&       og_line;

        bool draw_just_act_buttons  { false };
        bool is_visible             { true };

        wxRect      m_rect_blinking;
        wxRect      m_rect_undo_icon;
        wxRect      m_rect_undo_to_sys_icon;

        void    update_visibility(ConfigOptionMode mode);

        void    render(wxDC& dc, wxCoord v_pos);
        wxCoord draw_mode_bmp(wxDC& dc, wxCoord v_pos);
        wxCoord draw_text      (wxDC& dc, wxPoint pos, const wxString& text, const wxColour* color, int width);
        wxCoord draw_act_bmps(wxDC& dc, wxPoint pos, const wxBitmap& bmp_blinking, const wxBitmap& bmp_undo_to_sys, const wxBitmap& bmp_undo);
    };

    std::vector<CtrlLine> ctrl_lines;

public:
    OG_CustomCtrl(  wxWindow* parent,
                    OptionsGroup* og,
                    const wxPoint& pos = wxDefaultPosition,
                    const wxSize& size = wxDefaultSize,
                    const wxValidator& val = wxDefaultValidator,
                    const wxString& name = wxEmptyString);
    ~OG_CustomCtrl() {}

    void    OnPaint(wxPaintEvent&);
    void    OnMotion(wxMouseEvent& event);
    void    OnLeftDown(wxMouseEvent& event);
    void    OnLeftUp(wxMouseEvent& event);

    void    init_ctrl_lines();
    bool    update_visibility(ConfigOptionMode mode);
    void    correct_window_position(wxWindow* win, const Line& line, Field* field = nullptr);
    void    correct_widgets_position(wxSizer* widget, const Line& line, Field* field = nullptr);

    void    msw_rescale();
    void    sys_color_changed();

    wxPoint get_pos(const Line& line, Field* field = nullptr);
    int     get_height(const Line& line);

    OptionsGroup*  opt_group;

};

}}

#endif /* slic3r_OG_CustomCtrl_hpp_ */
