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
    int     m_em_unit;

    wxSize  m_bmp_mode_sz;
    wxSize  m_bmp_blinking_sz;

    struct CtrlLine {
        wxCoord           height  { wxDefaultCoord };
        OG_CustomCtrl*    ctrl    { nullptr };
        const Line&       og_line;

        bool draw_just_act_buttons  { false };
        bool is_visible             { true };

        CtrlLine(   wxCoord         height,
                    OG_CustomCtrl*  ctrl,
                    const Line&     og_line,
                    bool            draw_just_act_buttons = false);
        ~CtrlLine() { ctrl = nullptr; }

        void    correct_items_positions();
        void    msw_rescale();
        void    update_visibility(ConfigOptionMode mode);

        void    render(wxDC& dc, wxCoord v_pos);
        wxCoord draw_mode_bmp(wxDC& dc, wxCoord v_pos);
        wxCoord draw_text      (wxDC& dc, wxPoint pos, const wxString& text, const wxColour* color, int width);
        wxPoint draw_blinking_bmp(wxDC& dc, wxPoint pos, bool is_blinking, size_t rect_id = 0);
        wxCoord draw_act_bmps(wxDC& dc, wxPoint pos, const wxBitmap& bmp_undo_to_sys, const wxBitmap& bmp_undo, bool is_blinking, size_t rect_id = 0);

        std::vector<wxRect> rects_blinking;
        std::vector<wxRect> rects_undo_icon;
        std::vector<wxRect> rects_undo_to_sys_icon;
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
