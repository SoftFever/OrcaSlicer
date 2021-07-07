#include "libslic3r/libslic3r.h"
#include "DoubleSlider.hpp"
#include "libslic3r/GCode.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "I18N.hpp"
#include "ExtruderSequenceDialog.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/AppConfig.hpp"
#include "GUI_Utils.hpp"
#include "MsgDialog.hpp"
#include "Tab.hpp"

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/menu.h>
#include <wx/bmpcbox.h>
#include <wx/statline.h>
#include <wx/dcclient.h>
#include <wx/colordlg.h>

#include <cmath>
#include <boost/algorithm/string/replace.hpp>
#include <random>
#include "Field.hpp"
#include "format.hpp"
#include "NotificationManager.hpp"

namespace Slic3r {

using GUI::from_u8;
using GUI::into_u8;
using GUI::format_wxstr;

namespace DoubleSlider {

wxDEFINE_EVENT(wxCUSTOMEVT_TICKSCHANGED, wxEvent);

static std::string gcode(Type type)
{
    const PrintConfig& config = GUI::wxGetApp().plater()->fff_print().config();
    switch (type) {
    case ColorChange: return config.color_change_gcode;
    case PausePrint:  return config.pause_print_gcode;
    case Template:    return config.template_custom_gcode;
    default:          return "";
    }
}

Control::Control( wxWindow *parent,
                  wxWindowID id,
                  int lowerValue, 
                  int higherValue, 
                  int minValue, 
                  int maxValue,
                  const wxPoint& pos,
                  const wxSize& size,
                  long style,
                  const wxValidator& val,
                  const wxString& name) : 
    wxControl(parent, id, pos, size, wxWANTS_CHARS | wxBORDER_NONE),
    m_lower_value(lowerValue), 
    m_higher_value (higherValue), 
    m_min_value(minValue), 
    m_max_value(maxValue),
    m_style(style == wxSL_HORIZONTAL || style == wxSL_VERTICAL ? style: wxSL_HORIZONTAL),
    m_extra_style(style == wxSL_VERTICAL ? wxSL_AUTOTICKS | wxSL_VALUE_LABEL : 0)
{
#ifdef __WXOSX__ 
    is_osx = true;
#endif //__WXOSX__
    if (!is_osx)
        SetDoubleBuffered(true);// SetDoubleBuffered exists on Win and Linux/GTK, but is missing on OSX

    m_bmp_thumb_higher = (style == wxSL_HORIZONTAL ? ScalableBitmap(this, "thumb_right") : ScalableBitmap(this, "thumb_up"));
    m_bmp_thumb_lower  = (style == wxSL_HORIZONTAL ? ScalableBitmap(this, "thumb_left")  : ScalableBitmap(this, "thumb_down"));
    m_thumb_size = m_bmp_thumb_lower.GetBmpSize();

    m_bmp_add_tick_on  = ScalableBitmap(this, "colorchange_add");
    m_bmp_add_tick_off = ScalableBitmap(this, "colorchange_add_f");
    m_bmp_del_tick_on  = ScalableBitmap(this, "colorchange_del");
    m_bmp_del_tick_off = ScalableBitmap(this, "colorchange_del_f");
    m_tick_icon_dim = m_bmp_add_tick_on.GetBmpWidth();

    m_bmp_one_layer_lock_on    = ScalableBitmap(this, "lock_closed");
    m_bmp_one_layer_lock_off   = ScalableBitmap(this, "lock_closed_f");
    m_bmp_one_layer_unlock_on  = ScalableBitmap(this, "lock_open");
    m_bmp_one_layer_unlock_off = ScalableBitmap(this, "lock_open_f");
    m_lock_icon_dim   = m_bmp_one_layer_lock_on.GetBmpWidth();

    m_bmp_revert               = ScalableBitmap(this, "undo");
    m_revert_icon_dim = m_bmp_revert.GetBmpWidth();
    m_bmp_cog                  = ScalableBitmap(this, "cog");
    m_cog_icon_dim    = m_bmp_cog.GetBmpWidth();

    m_selection = ssUndef;
    m_ticks.set_pause_print_msg(_utf8(L("Place bearings in slots and resume printing")));
    m_ticks.set_extruder_colors(&m_extruder_colors);

    // slider events
    this->Bind(wxEVT_PAINT,       &Control::OnPaint,    this);
    this->Bind(wxEVT_CHAR,        &Control::OnChar,     this);
    this->Bind(wxEVT_LEFT_DOWN,   &Control::OnLeftDown, this);
    this->Bind(wxEVT_MOTION,      &Control::OnMotion,   this);
    this->Bind(wxEVT_LEFT_UP,     &Control::OnLeftUp,   this);
    this->Bind(wxEVT_MOUSEWHEEL,  &Control::OnWheel,    this);
    this->Bind(wxEVT_ENTER_WINDOW,&Control::OnEnterWin, this);
    this->Bind(wxEVT_LEAVE_WINDOW,&Control::OnLeaveWin, this);
    this->Bind(wxEVT_KEY_DOWN,    &Control::OnKeyDown,  this);
    this->Bind(wxEVT_KEY_UP,      &Control::OnKeyUp,    this);
    this->Bind(wxEVT_RIGHT_DOWN,  &Control::OnRightDown,this);
    this->Bind(wxEVT_RIGHT_UP,    &Control::OnRightUp,  this);

    // control's view variables
    SLIDER_MARGIN     = 4 + GUI::wxGetApp().em_unit();

    DARK_ORANGE_PEN   = wxPen(wxColour(237, 107, 33));
    ORANGE_PEN        = wxPen(wxColour(253, 126, 66));
    LIGHT_ORANGE_PEN  = wxPen(wxColour(254, 177, 139));

    DARK_GREY_PEN     = wxPen(wxColour(128, 128, 128));
    GREY_PEN          = wxPen(wxColour(164, 164, 164));
    LIGHT_GREY_PEN    = wxPen(wxColour(204, 204, 204));

    m_line_pens = { &DARK_GREY_PEN, &GREY_PEN, &LIGHT_GREY_PEN };
    m_segm_pens = { &DARK_ORANGE_PEN, &ORANGE_PEN, &LIGHT_ORANGE_PEN };

    m_font = GetFont();
    this->SetMinSize(get_min_size());
}

void Control::msw_rescale()
{
    m_font = GUI::wxGetApp().normal_font();

    m_bmp_thumb_higher.msw_rescale();
    m_bmp_thumb_lower .msw_rescale();
    m_thumb_size = m_bmp_thumb_lower.bmp().GetSize();

    m_bmp_add_tick_on .msw_rescale();
    m_bmp_add_tick_off.msw_rescale();
    m_bmp_del_tick_on .msw_rescale();
    m_bmp_del_tick_off.msw_rescale();
    m_tick_icon_dim = m_bmp_add_tick_on.bmp().GetSize().x;

    m_bmp_one_layer_lock_on   .msw_rescale();
    m_bmp_one_layer_lock_off  .msw_rescale();
    m_bmp_one_layer_unlock_on .msw_rescale();
    m_bmp_one_layer_unlock_off.msw_rescale();
    m_lock_icon_dim = m_bmp_one_layer_lock_on.bmp().GetSize().x;

    m_bmp_revert.msw_rescale();
    m_revert_icon_dim = m_bmp_revert.bmp().GetSize().x;
    m_bmp_cog.msw_rescale();
    m_cog_icon_dim = m_bmp_cog.bmp().GetSize().x;

    SLIDER_MARGIN = 4 + GUI::wxGetApp().em_unit();

    SetMinSize(get_min_size());
    GetParent()->Layout();
}

void Control::sys_color_changed()
{
    GUI::wxGetApp().UpdateDarkUI(GetParent());

    m_bmp_add_tick_on .msw_rescale();
    m_bmp_add_tick_off.msw_rescale();
    m_bmp_del_tick_on .msw_rescale();
    m_bmp_del_tick_off.msw_rescale();
    m_tick_icon_dim = m_bmp_add_tick_on.GetBmpWidth();

    m_bmp_one_layer_lock_on   .msw_rescale();
    m_bmp_one_layer_lock_off  .msw_rescale();
    m_bmp_one_layer_unlock_on .msw_rescale();
    m_bmp_one_layer_unlock_off.msw_rescale();
    m_lock_icon_dim = m_bmp_one_layer_lock_on.GetBmpWidth();

    m_bmp_revert.msw_rescale();
    m_revert_icon_dim = m_bmp_revert.GetBmpWidth();
    m_bmp_cog.msw_rescale();
    m_cog_icon_dim = m_bmp_cog.GetBmpWidth();
}

int Control::GetActiveValue() const
{
    return m_selection == ssLower ?
    m_lower_value : m_selection == ssHigher ?
                m_higher_value : -1;
}

wxSize Control::get_min_size() const
{
    const int min_side = GUI::wxGetApp().em_unit() * ( is_horizontal() ? 5 : 11 );
    return wxSize(min_side, min_side);
}

wxSize Control::DoGetBestSize() const
{
    const wxSize size = wxControl::DoGetBestSize();
    if (size.x > 1 && size.y > 1)
        return size;
    return get_min_size();
}

void Control::SetLowerValue(const int lower_val)
{
    m_selection = ssLower;
    m_lower_value = lower_val;
    correct_lower_value();
    Refresh();
    Update();

    wxCommandEvent e(wxEVT_SCROLL_CHANGED);
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}

void Control::SetHigherValue(const int higher_val)
{
    m_selection = ssHigher;
    m_higher_value = higher_val;
    correct_higher_value();
    Refresh();
    Update();

    wxCommandEvent e(wxEVT_SCROLL_CHANGED);
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}

void Control::SetSelectionSpan(const int lower_val, const int higher_val)
{
    m_lower_value  = std::max(lower_val, m_min_value);
    m_higher_value = std::max(std::min(higher_val, m_max_value), m_lower_value);
    if (m_lower_value < m_higher_value)
        m_is_one_layer = false;

    Refresh();
    Update();

    wxCommandEvent e(wxEVT_SCROLL_CHANGED);
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}

void Control::SetMaxValue(const int max_value)
{
    m_max_value = max_value;
    Refresh();
    Update();
}

void Control::SetSliderValues(const std::vector<double>& values)
{
    m_values = values;
    m_ruler.init(m_values);
}

void Control::draw_scroll_line(wxDC& dc, const int lower_pos, const int higher_pos)
{
    int width;
    int height;
    get_size(&width, &height);

    wxCoord line_beg_x = is_horizontal() ? SLIDER_MARGIN : width*0.5 - 1;
    wxCoord line_beg_y = is_horizontal() ? height*0.5 - 1 : SLIDER_MARGIN;
    wxCoord line_end_x = is_horizontal() ? width - SLIDER_MARGIN + 1 : width*0.5 - 1;
    wxCoord line_end_y = is_horizontal() ? height*0.5 - 1 : height - SLIDER_MARGIN + 1;

    wxCoord segm_beg_x = is_horizontal() ? lower_pos : width*0.5 - 1;
    wxCoord segm_beg_y = is_horizontal() ? height*0.5 - 1 : lower_pos/*-1*/;
    wxCoord segm_end_x = is_horizontal() ? higher_pos : width*0.5 - 1;
    wxCoord segm_end_y = is_horizontal() ? height*0.5 - 1 : higher_pos-1;

    for (size_t id = 0; id < m_line_pens.size(); id++) {
        dc.SetPen(*m_line_pens[id]);
        dc.DrawLine(line_beg_x, line_beg_y, line_end_x, line_end_y);
        dc.SetPen(*m_segm_pens[id]);
        dc.DrawLine(segm_beg_x, segm_beg_y, segm_end_x, segm_end_y);
        if (is_horizontal())
            line_beg_y = line_end_y = segm_beg_y = segm_end_y += 1;
        else
            line_beg_x = line_end_x = segm_beg_x = segm_end_x += 1;
    }
}

double Control::get_scroll_step()
{
    const wxSize sz = get_size();
    const int& slider_len = m_style == wxSL_HORIZONTAL ? sz.x : sz.y;
    return double(slider_len - SLIDER_MARGIN * 2) / (m_max_value - m_min_value);
}

// get position on the slider line from entered value
wxCoord Control::get_position_from_value(const int value)
{
    const double step = get_scroll_step();
    const int val = is_horizontal() ? value : m_max_value - value;
    return wxCoord(SLIDER_MARGIN + int(val*step + 0.5));
}

wxSize Control::get_size() const
{
    int w, h;
    get_size(&w, &h);
    return wxSize(w, h);
}

void Control::get_size(int* w, int* h) const
{
    GetSize(w, h);
    if (m_draw_mode == dmSequentialGCodeView)
        return; // we have no more icons for drawing
    is_horizontal() ? *w -= m_lock_icon_dim : *h -= m_lock_icon_dim;
}

double Control::get_double_value(const SelectedSlider& selection)
{
    if (m_values.empty() || m_lower_value<0)
        return 0.0;
    if (m_values.size() <= size_t(m_higher_value)) {
        correct_higher_value();
        return m_values.back();
    }
    return m_values[selection == ssLower ? m_lower_value : m_higher_value];
}

int Control::get_tick_from_value(double value, bool force_lower_bound/* = false*/)
{
    std::vector<double>::iterator it;
    if (m_is_wipe_tower && !force_lower_bound)
        it = std::find_if(m_values.begin(), m_values.end(),
                          [value](const double & val) { return fabs(value - val) <= epsilon(); });
    else
        it = std::lower_bound(m_values.begin(), m_values.end(), value - epsilon());

    if (it == m_values.end())
        return -1;
    return int(it - m_values.begin());
}

Info Control::GetTicksValues() const
{
    Info custom_gcode_per_print_z;
    std::vector<CustomGCode::Item>& values = custom_gcode_per_print_z.gcodes;

    const int val_size = m_values.size();
    if (!m_values.empty())
        for (const TickCode& tick : m_ticks.ticks) {
            if (tick.tick > val_size)
                break;
            values.emplace_back(CustomGCode::Item{ m_values[tick.tick], tick.type, tick.extruder, tick.color, tick.extra });
        }

    if (m_force_mode_apply)
        custom_gcode_per_print_z.mode = m_mode;

    return custom_gcode_per_print_z;
}

void Control::SetTicksValues(const Info& custom_gcode_per_print_z)
{
    if (m_values.empty()) {
        m_ticks.mode = m_mode;
        return;
    }

    const bool was_empty = m_ticks.empty();

    m_ticks.ticks.clear();
    const std::vector<CustomGCode::Item>& heights = custom_gcode_per_print_z.gcodes;
    for (auto h : heights) {
        int tick = get_tick_from_value(h.print_z);
        if (tick >=0)
            m_ticks.ticks.emplace(TickCode{ tick, h.type, h.extruder, h.color, h.extra });
    }
    
    if (!was_empty && m_ticks.empty())
        // Switch to the "Feature type"/"Tool" from the very beginning of a new object slicing after deleting of the old one
        post_ticks_changed_event();

    if (custom_gcode_per_print_z.mode)
        m_ticks.mode = custom_gcode_per_print_z.mode;

    Refresh();
    Update();
}

void Control::SetLayersTimes(const std::vector<float>& layers_times, float total_time)
{ 
    m_layers_times.clear();
    if (layers_times.empty())
        return;
    m_layers_times.resize(layers_times.size(), 0.0);
    m_layers_times[0] = layers_times[0];
    for (size_t i = 1; i < layers_times.size(); i++)
        m_layers_times[i] = m_layers_times[i - 1] + layers_times[i];

    // Erase duplicates values from m_values and save it to the m_layers_values
    // They will be used for show the correct estimated time for MM print, when "No sparce layer" is enabled
    // See https://github.com/prusa3d/PrusaSlicer/issues/6232
    if (m_is_wipe_tower && m_values.size() != m_layers_times.size()) {
        m_layers_values = m_values;
        sort(m_layers_values.begin(), m_layers_values.end());
        m_layers_values.erase(unique(m_layers_values.begin(), m_layers_values.end()), m_layers_values.end());

        // When whipe tower is used to the end of print, there is one layer which is not marked in layers_times
        // So, add this value from the total print time value
        if (m_layers_values.size() != m_layers_times.size())
            for (size_t i = m_layers_times.size(); i < m_layers_values.size(); i++)
                m_layers_times.push_back(total_time);
        Refresh();
        Update();
    }
}

void Control::SetLayersTimes(const std::vector<double>& layers_times)
{ 
    m_is_wipe_tower = false;
    m_layers_times = layers_times;
    for (size_t i = 1; i < m_layers_times.size(); i++)
        m_layers_times[i] += m_layers_times[i - 1];
}

void Control::SetDrawMode(bool is_sla_print, bool is_sequential_print)
{ 
    m_draw_mode = is_sla_print          ? dmSlaPrint            : 
                  is_sequential_print   ? dmSequentialFffPrint  : 
                                          dmRegular; 
}

void Control::SetModeAndOnlyExtruder(const bool is_one_extruder_printed_model, const int only_extruder)
{
    m_mode = !is_one_extruder_printed_model ? MultiExtruder :
             only_extruder < 0              ? SingleExtruder :
                                              MultiAsSingle;
    if (!m_ticks.mode)
        m_ticks.mode = m_mode;
    m_only_extruder = only_extruder;

    UseDefaultColors(m_mode == SingleExtruder);

    m_is_wipe_tower = m_mode != SingleExtruder;
}

void Control::SetExtruderColors( const std::vector<std::string>& extruder_colors)
{
    m_extruder_colors = extruder_colors;
}

bool Control::IsNewPrint()
{
    if (GUI::wxGetApp().plater()->printer_technology() == ptSLA)
        return false;
    const Print& print = GUI::wxGetApp().plater()->fff_print();
    std::string idxs;
    for (auto object : print.objects())
        idxs += std::to_string(object->id().id) + "_";

    if (idxs == m_print_obj_idxs)
        return false;

    m_print_obj_idxs = idxs;
    return true;
}

void Control::get_lower_and_higher_position(int& lower_pos, int& higher_pos)
{
    const double step = get_scroll_step();
    if (is_horizontal()) {
        lower_pos = SLIDER_MARGIN + int(m_lower_value*step + 0.5);
        higher_pos = SLIDER_MARGIN + int(m_higher_value*step + 0.5);
    }
    else {
        lower_pos = SLIDER_MARGIN + int((m_max_value - m_lower_value)*step + 0.5);
        higher_pos = SLIDER_MARGIN + int((m_max_value - m_higher_value)*step + 0.5);
    }
}

void Control::draw_focus_rect()
{
    if (!m_is_focused) 
        return;
    const wxSize sz = GetSize();
    wxPaintDC dc(this);
    const wxPen pen = wxPen(wxColour(128, 128, 10), 1, wxPENSTYLE_DOT);
    dc.SetPen(pen);
    dc.SetBrush(wxBrush(wxColour(0, 0, 0), wxBRUSHSTYLE_TRANSPARENT));
    dc.DrawRectangle(1, 1, sz.x - 2, sz.y - 2);
}

void Control::render()
{
#ifdef _WIN32 
    GUI::wxGetApp().UpdateDarkUI(this);
#else
    SetBackgroundColour(GetParent()->GetBackgroundColour());
#endif // _WIN32 
    draw_focus_rect();

    wxPaintDC dc(this);
    dc.SetFont(m_font);

    const wxCoord lower_pos = get_position_from_value(m_lower_value);
    const wxCoord higher_pos = get_position_from_value(m_higher_value);

    // draw colored band on the background of a scroll line 
    // and only in a case of no-empty m_values
    draw_colored_band(dc);

    if (m_extra_style & wxSL_AUTOTICKS)
        draw_ruler(dc);

    if (!m_render_as_disabled) {
        // draw line
        draw_scroll_line(dc, lower_pos, higher_pos);

        // draw color print ticks
        draw_ticks(dc);

        // draw both sliders
        draw_thumbs(dc, lower_pos, higher_pos);

        // draw lock/unlock
        draw_one_layer_icon(dc);

        // draw revert bitmap (if it's shown)
        draw_revert_icon(dc);

        // draw cog bitmap (if it's shown)
        draw_cog_icon(dc);

        // draw mouse position
        draw_tick_on_mouse_position(dc);
    }
}

bool Control::is_wipe_tower_layer(int tick) const
{
    if (!m_is_wipe_tower || tick >= (int)m_values.size())
        return false;
    if (tick == 0 || (tick == (int)m_values.size() - 1 && m_values[tick] > m_values[tick - 1]))
        return false;
    if (m_values[tick - 1] == m_values[tick + 1] && m_values[tick] < m_values[tick + 1])
        return true;

    return false;
}

void Control::draw_action_icon(wxDC& dc, const wxPoint pt_beg, const wxPoint pt_end)
{
    const int tick = m_selection == ssLower ? m_lower_value : m_higher_value;

    if (!m_enable_action_icon)
        return;

    // suppress add tick on first layer
    if (tick == 0)
        return;

    if (is_wipe_tower_layer(tick)) {
        m_rect_tick_action = wxRect();
        return;
    }

    wxBitmap* icon = m_focus == fiActionIcon ? &m_bmp_add_tick_off.bmp() : &m_bmp_add_tick_on.bmp();
    if (m_ticks.ticks.find(TickCode{tick}) != m_ticks.ticks.end())
        icon = m_focus == fiActionIcon ? &m_bmp_del_tick_off.bmp() : &m_bmp_del_tick_on.bmp();

    wxCoord x_draw, y_draw;
    is_horizontal() ? x_draw = pt_beg.x - 0.5*m_tick_icon_dim : y_draw = pt_beg.y - 0.5*m_tick_icon_dim;
    if (m_selection == ssLower)
        is_horizontal() ? y_draw = pt_end.y + 3 : x_draw = pt_beg.x - m_tick_icon_dim-2;
    else
        is_horizontal() ? y_draw = pt_beg.y - m_tick_icon_dim-2 : x_draw = pt_end.x + 3;

    if (m_draw_mode == dmSequentialFffPrint)
        dc.DrawBitmap(create_scaled_bitmap("colorchange_add", nullptr, 16, true), x_draw, y_draw);
    else
        dc.DrawBitmap(*icon, x_draw, y_draw);

    //update rect of the tick action icon
    m_rect_tick_action = wxRect(x_draw, y_draw, m_tick_icon_dim, m_tick_icon_dim);
}

void Control::draw_info_line_with_icon(wxDC& dc, const wxPoint& pos, const SelectedSlider selection)
{
    if (m_selection == selection) {
        //draw info line
        dc.SetPen(DARK_ORANGE_PEN);
        const wxPoint pt_beg = is_horizontal() ? wxPoint(pos.x, pos.y - m_thumb_size.y) : wxPoint(pos.x - m_thumb_size.x, pos.y/* - 1*/);
        const wxPoint pt_end = is_horizontal() ? wxPoint(pos.x, pos.y + m_thumb_size.y) : wxPoint(pos.x + m_thumb_size.x, pos.y/* - 1*/);
        dc.DrawLine(pt_beg, pt_end);

        //draw action icon
        if (m_draw_mode == dmRegular || m_draw_mode == dmSequentialFffPrint)
            draw_action_icon(dc, pt_beg, pt_end);
    }
}

void Control::draw_tick_on_mouse_position(wxDC& dc)
{
    if (!m_is_focused || m_moving_pos == wxDefaultPosition)
        return;

    //calculate thumb position on slider line
    int width, height;
    get_size(&width, &height);

    int tick = get_tick_near_point(m_moving_pos);
    if (tick == m_higher_value || tick == m_lower_value)
        return ;

    auto draw_ticks = [this](wxDC& dc, wxPoint pos, int margin=0 )
    {
        wxPoint pt_beg = is_horizontal() ? wxPoint(pos.x+margin, pos.y - m_thumb_size.y) : wxPoint(pos.x - m_thumb_size.x          , pos.y+margin);
        wxPoint pt_end = is_horizontal() ? wxPoint(pos.x+margin, pos.y + m_thumb_size.y) : wxPoint(pos.x - 0.5 * m_thumb_size.x + 1, pos.y+margin);
        dc.DrawLine(pt_beg, pt_end);

        pt_beg = is_horizontal() ? wxPoint(pos.x + margin, pos.y - m_thumb_size.y) : wxPoint(pos.x + 0.5 * m_thumb_size.x, pos.y+margin);
        pt_end = is_horizontal() ? wxPoint(pos.x + margin, pos.y + m_thumb_size.y) : wxPoint(pos.x + m_thumb_size.x + 1,   pos.y+margin);
        dc.DrawLine(pt_beg, pt_end);
    };

    auto draw_touch = [this](wxDC& dc, wxPoint pos, int margin, bool right_side )
    {
        int mult = right_side ? 1 : -1;
        wxPoint pt_beg = is_horizontal() ? wxPoint(pos.x - margin, pos.y + mult * m_thumb_size.y) : wxPoint(pos.x + mult * m_thumb_size.x, pos.y - margin);
        wxPoint pt_end = is_horizontal() ? wxPoint(pos.x + margin, pos.y + mult * m_thumb_size.y) : wxPoint(pos.x + mult * m_thumb_size.x, pos.y + margin);
        dc.DrawLine(pt_beg, pt_end);
    };

    if (tick > 0) // this tick exists and should be marked as a focused
    {
        wxCoord new_pos = get_position_from_value(tick);
        const wxPoint pos = is_horizontal() ? wxPoint(new_pos, height * 0.5) : wxPoint(0.5 * width, new_pos);

        dc.SetPen(DARK_ORANGE_PEN);

        draw_ticks(dc, pos, -2);
        draw_ticks(dc, pos, 2 );
        draw_touch(dc, pos, 2, true);
        draw_touch(dc, pos, 2, false);

        return;
    }

    tick = get_value_from_position(m_moving_pos);
    if (tick > m_max_value || tick < m_min_value || tick == m_higher_value || tick == m_lower_value)
        return;

    wxCoord new_pos = get_position_from_value(tick);
    const wxPoint pos = is_horizontal() ? wxPoint(new_pos, height * 0.5) : wxPoint(0.5 * width, new_pos);

    //draw info line
    dc.SetPen(LIGHT_GREY_PEN);
    draw_ticks(dc, pos);

    if (m_extra_style & wxSL_VALUE_LABEL) {
        wxColour old_clr = dc.GetTextForeground();
        dc.SetTextForeground(GREY_PEN.GetColour());
        draw_tick_text(dc, pos, tick, ltEstimatedTime, false);
        dc.SetTextForeground(old_clr);
    }
}

static wxString short_and_splitted_time(const std::string& time)
{
    // Parse the dhms time format.
    int days = 0;
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    if (time.find('d') != std::string::npos)
        ::sscanf(time.c_str(), "%dd %dh %dm %ds", &days, &hours, &minutes, &seconds);
    else if (time.find('h') != std::string::npos)
        ::sscanf(time.c_str(), "%dh %dm %ds", &hours, &minutes, &seconds);
    else if (time.find('m') != std::string::npos)
        ::sscanf(time.c_str(), "%dm %ds", &minutes, &seconds);
    else if (time.find('s') != std::string::npos)
        ::sscanf(time.c_str(), "%ds", &seconds);

    // Format the dhm time.
    char buffer[64];
    if (days > 0)
        ::sprintf(buffer, "%dd%dh\n%dm", days, hours, minutes);
    else if (hours > 0) {
        if (hours < 10 && minutes < 10 && seconds < 10)
            ::sprintf(buffer, "%dh%dm%ds", hours, minutes, seconds);
        else if (hours > 10 && minutes > 10 && seconds > 10)
            ::sprintf(buffer, "%dh\n%dm\n%ds", hours, minutes, seconds);
        else if ((minutes < 10 && seconds > 10) || (minutes > 10 && seconds < 10))
            ::sprintf(buffer, "%dh\n%dm%ds", hours, minutes, seconds);
        else
            ::sprintf(buffer, "%dh%dm\n%ds", hours, minutes, seconds);
    }
    else if (minutes > 0) {
        if (minutes > 10 && seconds > 10)
            ::sprintf(buffer, "%dm\n%ds", minutes, seconds);
        else
            ::sprintf(buffer, "%dm%ds", minutes, seconds);
    }
    else
        ::sprintf(buffer, "%ds", seconds);
    return wxString::FromUTF8(buffer);
}

wxString Control::get_label(int tick, LabelType label_type/* = ltHeightWithLayer*/) const
{
    const size_t value = tick;

    if (m_label_koef == 1.0 && m_values.empty())
        return wxString::Format("%lu", static_cast<unsigned long>(value));
    if (value >= m_values.size())
        return "ErrVal";

    // When "Print Settings -> Multiple Extruders -> No sparse layer" is enabled, then "Smart" Wipe Tower is used for wiping.
    // As a result, each layer with tool changes is splited for min 3 parts: first tool, wiping, second tool ...
    // So, vertical slider have to respect to this case.
    // see https://github.com/prusa3d/PrusaSlicer/issues/6232.
    // m_values contains data for all layer's parts,
    // but m_layers_values contains just unique Z values.
    // Use this function for correct conversion slider position to number of printed layer
    auto get_layer_number = [this](int value, LabelType label_type) {
        if (label_type == ltEstimatedTime && m_layers_times.empty())
            return size_t(-1);
        double layer_print_z = m_values[is_wipe_tower_layer(value) ? std::max<int>(value - 1, 0) : value];
        auto it = std::lower_bound(m_layers_values.begin(), m_layers_values.end(), layer_print_z - epsilon());
        if (it == m_layers_values.end()) {
            it = std::lower_bound(m_values.begin(), m_values.end(), layer_print_z - epsilon());
            if (it == m_values.end())
                return size_t(-1);
            return size_t(value);
        }
        return size_t(it - m_layers_values.begin());
    };

#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
    if (m_draw_mode == dmSequentialGCodeView) {
        return (Slic3r::GUI::get_app_config()->get("seq_top_gcode_indices") == "1") ?
            wxString::Format("%lu", static_cast<unsigned long>(m_alternate_values[value])) :
            wxString::Format("%lu", static_cast<unsigned long>(m_values[value]));
    }
#else
    if (m_draw_mode == dmSequentialGCodeView)
        return wxString::Format("%lu", static_cast<unsigned long>(m_values[value]));
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
    else {
        if (label_type == ltEstimatedTime) {
            if (m_is_wipe_tower) {
                size_t layer_number = get_layer_number(value, label_type);
                return (layer_number == size_t(-1) || layer_number == m_layers_times.size()) ? "" : short_and_splitted_time(get_time_dhms(m_layers_times[layer_number]));
            }
            return value < m_layers_times.size() ? short_and_splitted_time(get_time_dhms(m_layers_times[value])) : "";
        }
        wxString str = m_values.empty() ?
            wxString::Format("%.*f", 2, m_label_koef * value) :
            wxString::Format("%.*f", 2, m_values[value]);
        if (label_type == ltHeight)
            return str;
        if (label_type == ltHeightWithLayer) {
            size_t layer_number = m_is_wipe_tower ? get_layer_number(value, label_type) + 1 : (m_values.empty() ? value : value + 1);
            return format_wxstr("%1%\n(%2%)", str, layer_number);
        }
    }

    return wxEmptyString;
}

void Control::draw_tick_text(wxDC& dc, const wxPoint& pos, int tick, LabelType label_type/* = ltHeight*/, bool right_side/*=true*/) const
{
    wxCoord text_width, text_height;
    const wxString label = get_label(tick, label_type);
    dc.GetMultiLineTextExtent(label, &text_width, &text_height);
    wxPoint text_pos;
    if (right_side) {
        if (is_horizontal()) {
            int width;
            int height;
            get_size(&width, &height);

            int x_right = pos.x + 1 + text_width;
            int xx = (x_right < width) ? pos.x + 1 : pos.x - text_width - 1;
            text_pos = wxPoint(xx, pos.y + m_thumb_size.x / 2 + 1);
        }
        else
            text_pos = wxPoint(pos.x + m_thumb_size.x + 1, pos.y - 0.5 * text_height - 1);
    }
    else {
        if (is_horizontal()) {
            int x = pos.x - text_width - 1;
            int xx = (x > 0) ? x : pos.x + 1;
            text_pos = wxPoint(xx, pos.y - m_thumb_size.x / 2 - text_height - 1);
        }
        else
            text_pos = wxPoint(std::max(2, pos.x - text_width - 1 - m_thumb_size.x), pos.y - 0.5 * text_height + 1);
    }

    wxColour old_clr = dc.GetTextForeground();
    const wxPen& pen = is_wipe_tower_layer(tick) && (tick == m_lower_value || tick == m_higher_value) ? DARK_ORANGE_PEN : wxPen(old_clr);
    dc.SetPen(pen);
    dc.SetTextForeground(pen.GetColour());

    if (label_type == ltEstimatedTime)
        dc.DrawLabel(label, wxRect(text_pos, wxSize(text_width, text_height)), wxALIGN_RIGHT);
    else
        dc.DrawText(label, text_pos);

    dc.SetTextForeground(old_clr);
}

void Control::draw_thumb_text(wxDC& dc, const wxPoint& pos, const SelectedSlider& selection) const
{
    draw_tick_text(dc, pos, selection == ssLower ? m_lower_value : m_higher_value, ltHeightWithLayer, selection == ssLower);
}

void Control::draw_thumb_item(wxDC& dc, const wxPoint& pos, const SelectedSlider& selection)
{
    wxCoord x_draw = pos.x - int(0.5 * m_thumb_size.x);
    wxCoord y_draw = pos.y - int(0.5 * m_thumb_size.y);
    dc.DrawBitmap(selection == ssLower ? m_bmp_thumb_lower.bmp() : m_bmp_thumb_higher.bmp(), x_draw, y_draw);

    // Update thumb rect
    update_thumb_rect(x_draw, y_draw, selection);
}

void Control::draw_thumb(wxDC& dc, const wxCoord& pos_coord, const SelectedSlider& selection)
{
    //calculate thumb position on slider line
    int width, height;
    get_size(&width, &height);
    const wxPoint pos = is_horizontal() ? wxPoint(pos_coord, height*0.5) : wxPoint(0.5*width, pos_coord);

    // Draw thumb
    draw_thumb_item(dc, pos, selection);

    // Draw info_line
    draw_info_line_with_icon(dc, pos, selection);

    // Draw thumb text
    draw_thumb_text(dc, pos, selection);
}

void Control::draw_thumbs(wxDC& dc, const wxCoord& lower_pos, const wxCoord& higher_pos)
{
    //calculate thumb position on slider line
    int width, height;
    get_size(&width, &height);
    const wxPoint pos_l = is_horizontal() ? wxPoint(lower_pos, height*0.5) : wxPoint(0.5*width, lower_pos);
    const wxPoint pos_h = is_horizontal() ? wxPoint(higher_pos, height*0.5) : wxPoint(0.5*width, higher_pos);

    // Draw lower thumb
    draw_thumb_item(dc, pos_l, ssLower);
    // Draw lower info_line
    draw_info_line_with_icon(dc, pos_l, ssLower);

    // Draw higher thumb
    draw_thumb_item(dc, pos_h, ssHigher);
    // Draw higher info_line
    draw_info_line_with_icon(dc, pos_h, ssHigher);
    // Draw higher thumb text
    draw_thumb_text(dc, pos_h, ssHigher);

    // Draw lower thumb text
    draw_thumb_text(dc, pos_l, ssLower);
}

void Control::draw_ticks_pair(wxDC& dc, wxCoord pos, wxCoord mid, int tick_len)
{
    int mid_space = 9;
    is_horizontal() ? dc.DrawLine(pos, mid - (mid_space + tick_len), pos, mid - mid_space) :
        dc.DrawLine(mid - (mid_space + tick_len), pos, mid - mid_space, pos);
    is_horizontal() ? dc.DrawLine(pos, mid + (mid_space + tick_len), pos, mid + mid_space) :
        dc.DrawLine(mid + (mid_space + tick_len), pos, mid + mid_space, pos);
}

void Control::draw_ticks(wxDC& dc)
{
    if (m_draw_mode == dmSlaPrint)
        return;

    dc.SetPen(m_draw_mode == dmRegular ? DARK_GREY_PEN : LIGHT_GREY_PEN );
    int height, width;
    get_size(&width, &height);
    const wxCoord mid = is_horizontal() ? 0.5*height : 0.5*width;
    for (const TickCode& tick : m_ticks.ticks) {
        if (size_t(tick.tick) >= m_values.size()) {
            // The case when OnPaint is called before m_ticks.ticks data are updated (specific for the vase mode)
            break;
        }

        const wxCoord pos = get_position_from_value(tick.tick);
        draw_ticks_pair(dc, pos, mid, 7);

        // if current tick if focused, we should to use a specific "focused" icon 
        bool focused_tick = m_moving_pos != wxDefaultPosition && tick.tick == get_tick_near_point(m_moving_pos);

        // get icon name if it is
        std::string icon_name;

        // if we have non-regular draw mode, all ticks should be marked with error icon
        if (m_draw_mode != dmRegular)
            icon_name = focused_tick ? "error_tick_f" : "error_tick";
        else if (tick.type == ColorChange || tick.type == ToolChange) { 
            if (m_ticks.is_conflict_tick(tick, m_mode, m_only_extruder, m_values[tick.tick]))
                icon_name = focused_tick ? "error_tick_f" : "error_tick";
        }
        else if (tick.type == PausePrint)
            icon_name = focused_tick ? "pause_print_f" : "pause_print";
        else
            icon_name = focused_tick ? "edit_gcode_f" : "edit_gcode";

        // Draw icon for "Pause print", "Custom Gcode" or conflict tick
        if (!icon_name.empty())  {
            wxBitmap icon = create_scaled_bitmap(icon_name);
            wxCoord x_draw, y_draw;
            is_horizontal() ? x_draw = pos - 0.5 * m_tick_icon_dim : y_draw = pos - 0.5 * m_tick_icon_dim;
            is_horizontal() ? y_draw = mid + 22 : x_draw = mid + m_thumb_size.x + 3;

            dc.DrawBitmap(icon, x_draw, y_draw);
        }
    }
}

std::string Control::get_color_for_tool_change_tick(std::set<TickCode>::const_iterator it) const
{
    const int current_extruder = it->extruder == 0 ? std::max<int>(m_only_extruder, 1) : it->extruder;

    auto it_n = it;
    while (it_n != m_ticks.ticks.begin()) {
        --it_n;
        if (it_n->type == ColorChange && it_n->extruder == current_extruder)
            return it_n->color;
    }

    return m_extruder_colors[current_extruder-1]; // return a color for a specific extruder from the colors list 
}

std::string Control::get_color_for_color_change_tick(std::set<TickCode>::const_iterator it) const
{
    const int def_extruder = std::max<int>(1, m_only_extruder);
    auto it_n = it;
    bool is_tool_change = false;
    while (it_n != m_ticks.ticks.begin()) {
        --it_n;
        if (it_n->type == ToolChange) {
            is_tool_change = true;
            if (it_n->extruder == it->extruder)
                return it->color;
            break;
        }
        if (it_n->type == ColorChange && it_n->extruder == it->extruder)
            return it->color;
    }
    if (!is_tool_change && it->extruder == def_extruder)
        return it->color;

    return "";
}

wxRect Control::get_colored_band_rect()
{
    int height, width;
    get_size(&width, &height);

    const wxCoord mid = is_horizontal() ? 0.5 * height : 0.5 * width;

    return is_horizontal() ?
           wxRect(SLIDER_MARGIN, lround(mid - 0.375 * m_thumb_size.y), 
                  width - 2 * SLIDER_MARGIN + 1, lround(0.75 * m_thumb_size.y)) :
           wxRect(lround(mid - 0.375 * m_thumb_size.x), SLIDER_MARGIN, 
                  lround(0.75 * m_thumb_size.x), height - 2 * SLIDER_MARGIN + 1);
}

void Control::draw_colored_band(wxDC& dc)
{
    if (m_draw_mode != dmRegular)
        return;

    auto draw_band = [](wxDC& dc, const wxColour& clr, const wxRect& band_rc) 
    {
        dc.SetPen(clr);
        dc.SetBrush(clr);
        dc.DrawRectangle(band_rc);
    };

    wxRect main_band = get_colored_band_rect();

    // don't color a band for MultiExtruder mode
    if (m_ticks.empty() || m_mode == MultiExtruder) {
        draw_band(dc, GetParent()->GetBackgroundColour(), main_band);
        return;
    }

    const int default_color_idx = m_mode==MultiAsSingle ? std::max<int>(m_only_extruder - 1, 0) : 0;
    draw_band(dc, wxColour(m_extruder_colors[default_color_idx]), main_band);

    std::set<TickCode>::const_iterator tick_it = m_ticks.ticks.begin();

    while (tick_it != m_ticks.ticks.end())
    {
        if ( (m_mode == SingleExtruder &&  tick_it->type == ColorChange  ) ||
             (m_mode == MultiAsSingle  && (tick_it->type == ToolChange || tick_it->type == ColorChange)) ) 
        {        
            const wxCoord pos = get_position_from_value(tick_it->tick);
            is_horizontal() ? main_band.SetLeft(SLIDER_MARGIN + pos) :
                              main_band.SetBottom(pos - 1);

            const std::string clr_str = m_mode == SingleExtruder ? tick_it->color :
                                        tick_it->type == ToolChange ?
                                        get_color_for_tool_change_tick(tick_it) :
                                        get_color_for_color_change_tick(tick_it);

            if (!clr_str.empty())
                draw_band(dc, wxColour(clr_str), main_band);
        }
        ++tick_it;
    }
}

void Control::Ruler::init(const std::vector<double>& values)
{
    max_values.clear();
    max_values.reserve(std::count(values.begin(), values.end(), values.front()));

    auto it = std::find(values.begin() + 1, values.end(), values.front());
    while (it != values.end()) {
        max_values.push_back(*(it - 1));
        it = std::find(it + 1, values.end(), values.front());
    }
    max_values.push_back(*(it - 1));
}

void Control::Ruler::update(wxWindow* win, const std::vector<double>& values, double scroll_step)
{
    if (values.empty())
        return;
    int DPI = GUI::get_dpi_for_window(win);
    int pixels_per_sm = lround((double)(DPI) * 5.0/25.4);

    if (lround(scroll_step) > pixels_per_sm) {
        long_step = -1.0;
        return;
    }

    int pow = -2;
    int step = 0;
    auto end_it = std::find(values.begin() + 1, values.end(), values.front());

    while (pow < 3) {
        for (int istep : {1, 2, 5}) {
            double val = (double)istep * std::pow(10,pow);
            auto val_it = std::lower_bound(values.begin(), end_it, val - epsilon());

            if (val_it == values.end())
                break;
            int tick = val_it - values.begin();

            // find next tick with istep
            val *= 2;
            val_it = std::lower_bound(values.begin(), end_it, val - epsilon());
            // count of short ticks between ticks
            int short_ticks_cnt = val_it == values.end() ? tick : val_it - values.begin() - tick;

            if (lround(short_ticks_cnt * scroll_step) > pixels_per_sm) {
                step = istep;
                // there couldn't be more then 10 short ticks between ticks
                short_step = 0.1 * short_ticks_cnt;
                break;
            }
        }
        if (step > 0)
            break;
        pow++;
    }

    long_step = step == 0 ? -1.0 : (double)step* std::pow(10, pow);
}

void Control::draw_ruler(wxDC& dc)
{
    if (m_values.empty())
        return;
    m_ruler.update(this->GetParent(), m_values, get_scroll_step());

    int height, width;
    get_size(&width, &height);
    const wxCoord mid = is_horizontal() ? 0.5 * height : 0.5 * width; 

    dc.SetPen(GREY_PEN);
    wxColour old_clr = dc.GetTextForeground();
    dc.SetTextForeground(GREY_PEN.GetColour());

    if (m_ruler.long_step < 0)
        for (size_t tick = 1; tick < m_values.size(); tick++) {
            wxCoord pos = get_position_from_value(tick);
            draw_ticks_pair(dc, pos, mid, 5);
            draw_tick_text(dc, wxPoint(mid, pos), tick);
        }
    else {
        auto draw_short_ticks = [this, mid](wxDC& dc, double& current_tick, int max_tick) {
            while (current_tick < max_tick) {
                wxCoord pos = get_position_from_value(lround(current_tick));
                draw_ticks_pair(dc, pos, mid, 2);
                current_tick += m_ruler.short_step;
                if (current_tick > m_max_value)
                    break;
            }
        };

        double short_tick = std::nan("");
        int tick = 0;
        double value = 0.0;
        size_t sequence = 0;

        int prev_y_pos = -1;
        wxCoord label_height = dc.GetMultiLineTextExtent("0").y - 2;
        int values_size = (int)m_values.size();

        while (tick <= m_max_value) {
            value += m_ruler.long_step;
            if (value > m_ruler.max_values[sequence] && sequence < m_ruler.count()) {
                value = m_ruler.long_step;
                for (; tick < values_size; tick++)
                    if (m_values[tick] < value)
                        break;
                // short ticks from the last tick to the end of current sequence
                assert(! std::isnan(short_tick));
                draw_short_ticks(dc, short_tick, tick);
                sequence++;
            }
            short_tick = tick;

            for (; tick < values_size; tick++) {
                if (m_values[tick] == value)
                    break;
                if (m_values[tick] > value) {
                    if (tick > 0)
                        tick--;
                    break;
                }
            }
            if (tick > m_max_value)
                break;

            wxCoord pos = get_position_from_value(tick);
            draw_ticks_pair(dc, pos, mid, 5);
            if (prev_y_pos < 0 || prev_y_pos - pos >= label_height) {
                draw_tick_text(dc, wxPoint(mid, pos), tick);
                prev_y_pos = pos;
            }

            draw_short_ticks(dc, short_tick, tick);

            if (value == m_ruler.max_values[sequence] && sequence < m_ruler.count()) {
                value = 0.0;
                sequence++;
                tick++;
            }
        }
        // short ticks from the last tick to the end 
        draw_short_ticks(dc, short_tick, m_max_value);
    }

    dc.SetTextForeground(old_clr);
}

void Control::draw_one_layer_icon(wxDC& dc)
{
    if (m_draw_mode == dmSequentialGCodeView)
        return;

    const wxBitmap& icon = m_is_one_layer ?
                     m_focus == fiOneLayerIcon ? m_bmp_one_layer_lock_off.bmp()   : m_bmp_one_layer_lock_on.bmp() :
                     m_focus == fiOneLayerIcon ? m_bmp_one_layer_unlock_off.bmp() : m_bmp_one_layer_unlock_on.bmp();

    int width, height;
    get_size(&width, &height);

    wxCoord x_draw, y_draw;
    is_horizontal() ? x_draw = width-2 : x_draw = 0.5*width - 0.5*m_lock_icon_dim;
    is_horizontal() ? y_draw = 0.5*height - 0.5*m_lock_icon_dim : y_draw = height-2;

    dc.DrawBitmap(icon, x_draw, y_draw);

    //update rect of the lock/unlock icon
    m_rect_one_layer_icon = wxRect(x_draw, y_draw, m_lock_icon_dim, m_lock_icon_dim);
}

void Control::draw_revert_icon(wxDC& dc)
{
    if (m_ticks.empty() || m_draw_mode != dmRegular)
        return;

    int width, height;
    get_size(&width, &height);

    wxCoord x_draw, y_draw;
    is_horizontal() ? x_draw = width-2 : x_draw = 0.25*SLIDER_MARGIN;
    is_horizontal() ? y_draw = 0.25*SLIDER_MARGIN: y_draw = height-2;

    dc.DrawBitmap(m_bmp_revert.bmp(), x_draw, y_draw);

    //update rect of the lock/unlock icon
    m_rect_revert_icon = wxRect(x_draw, y_draw, m_revert_icon_dim, m_revert_icon_dim);
}

void Control::draw_cog_icon(wxDC& dc)
{
    if (m_draw_mode == dmSequentialGCodeView)
        return;

    int width, height;
    get_size(&width, &height);

    wxCoord x_draw, y_draw;
    if (m_draw_mode == dmSequentialGCodeView) {
        is_horizontal() ? x_draw = width - 2 : x_draw = 0.5 * width - 0.5 * m_cog_icon_dim;
        is_horizontal() ? y_draw = 0.5 * height - 0.5 * m_cog_icon_dim : y_draw = height - 2;
    }
    else {
        is_horizontal() ? x_draw = width - 2 : x_draw = width - m_cog_icon_dim - 2;
        is_horizontal() ? y_draw = height - m_cog_icon_dim - 2 : y_draw = height - 2;
    }

    dc.DrawBitmap(m_bmp_cog.bmp(), x_draw, y_draw);

    //update rect of the lock/unlock icon
    m_rect_cog_icon = wxRect(x_draw, y_draw, m_cog_icon_dim, m_cog_icon_dim);
}

void Control::update_thumb_rect(const wxCoord begin_x, const wxCoord begin_y, const SelectedSlider& selection)
{
    const wxRect rect = is_horizontal() ?
        wxRect(begin_x + (selection == ssHigher ? m_thumb_size.x / 2 : 0), begin_y, m_thumb_size.x / 2, m_thumb_size.y) :
        wxRect(begin_x, begin_y + (selection == ssLower ? m_thumb_size.y / 2 : 0), m_thumb_size.x, m_thumb_size.y / 2);

    if (selection == ssLower)
        m_rect_lower_thumb = rect;
    else
        m_rect_higher_thumb = rect;
}

int Control::get_value_from_position(const wxCoord x, const wxCoord y)
{
    const int height = get_size().y;
    const double step = get_scroll_step();
    
    if (is_horizontal()) 
        return int(double(x - SLIDER_MARGIN) / step + 0.5);

    return int(m_min_value + double(height - SLIDER_MARGIN - y) / step + 0.5);
}

bool Control::is_lower_thumb_editable()
{
    if (m_draw_mode == dmSequentialGCodeView)
        return Slic3r::GUI::get_app_config()->get("seq_top_layer_only") == "0";
    return true;
}

bool Control::detect_selected_slider(const wxPoint& pt)
{
    if (is_point_in_rect(pt, m_rect_lower_thumb))
        m_selection = is_lower_thumb_editable() ? ssLower : ssUndef;
    else if(is_point_in_rect(pt, m_rect_higher_thumb))
        m_selection = ssHigher;
    else
        return false; // pt doesn't referenced to any thumb 
    return true;
}

bool Control::is_point_in_rect(const wxPoint& pt, const wxRect& rect)
{
    return  rect.GetLeft() <= pt.x && pt.x <= rect.GetRight() && 
            rect.GetTop()  <= pt.y && pt.y <= rect.GetBottom();
}

int Control::get_tick_near_point(const wxPoint& pt)
{
    for (auto tick : m_ticks.ticks) {
        const wxCoord pos = get_position_from_value(tick.tick);

        if (is_horizontal()) {
            if (pos - 4 <= pt.x && pt.x <= pos + 4)
                return tick.tick;
        }
        else {
            if (pos - 4 <= pt.y && pt.y <= pos + 4) 
                return tick.tick;
        }
    }
    return -1;
}

void Control::ChangeOneLayerLock()
{
    m_is_one_layer = !m_is_one_layer;
    m_selection == ssLower ? correct_lower_value() : correct_higher_value();
    if (!m_selection) m_selection = ssHigher;

    Refresh();
    Update();

    wxCommandEvent e(wxEVT_SCROLL_CHANGED);
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}

void Control::OnLeftDown(wxMouseEvent& event)
{
    if (HasCapture())
        return;
    this->CaptureMouse();

    m_is_left_down = true;
    m_mouse = maNone;

    wxPoint pos = event.GetLogicalPosition(wxClientDC(this));

    if (is_point_in_rect(pos, m_rect_one_layer_icon)) 
        m_mouse = maOneLayerIconClick;
    else if (is_point_in_rect(pos, m_rect_cog_icon))
        m_mouse = maCogIconClick;
    else if (m_draw_mode == dmRegular) {
        if (is_point_in_rect(pos, m_rect_tick_action)) {
            auto it = m_ticks.ticks.find(TickCode{ m_selection == ssLower ? m_lower_value : m_higher_value });
            m_mouse = it == m_ticks.ticks.end() ? maAddTick : maDeleteTick;
        }
        else if (is_point_in_rect(pos, m_rect_revert_icon))
            m_mouse = maRevertIconClick;
    }

    if (m_mouse == maNone)
        detect_selected_slider(pos);

    event.Skip();
}

void Control::correct_lower_value()
{
    if (m_lower_value < m_min_value)
        m_lower_value = m_min_value;
    else if (m_lower_value > m_max_value)
        m_lower_value = m_max_value;
    
    if ((m_lower_value >= m_higher_value && m_lower_value <= m_max_value) || m_is_one_layer)
        m_higher_value = m_lower_value;
}

void Control::correct_higher_value()
{
    if (m_higher_value > m_max_value)
        m_higher_value = m_max_value;
    else if (m_higher_value < m_min_value)
        m_higher_value = m_min_value;
    
    if ((m_higher_value <= m_lower_value && m_higher_value >= m_min_value) || m_is_one_layer)
        m_lower_value = m_higher_value;
}

wxString Control::get_tooltip(int tick/*=-1*/)
{
    if (m_focus == fiNone)
        return "";
    if (m_focus == fiOneLayerIcon)
        return _L("One layer mode");
    if (m_focus == fiRevertIcon)
        return _L("Discard all custom changes");
    if (m_focus == fiCogIcon)
    {
        if (m_draw_mode == dmSequentialGCodeView)
            return _L("Jump to move") + " (Shift + G)";
        else
            return m_mode == MultiAsSingle ?
            GUI::from_u8((boost::format(_u8L("Jump to height %s\n"
                                               "Set ruler mode\n"
                                               "or Set extruder sequence for the entire print")) % "(Shift + G)").str()) :
            GUI::from_u8((boost::format(_u8L("Jump to height %s\n"
                                                "or Set ruler mode")) % "(Shift + G)").str());
    }
    if (m_focus == fiColorBand)
        return m_mode != SingleExtruder ? "" :
               _L("Edit current color - Right click the colored slider segment");
    if (m_focus == fiSmartWipeTower)
        return _L("This is wipe tower layer");
    if (m_draw_mode == dmSlaPrint)
        return ""; // no drawn ticks and no tooltips for them in SlaPrinting mode

    wxString tooltip;
    const auto tick_code_it = m_ticks.ticks.find(TickCode{tick});

    if (tick_code_it == m_ticks.ticks.end() && m_focus == fiActionIcon)    // tick doesn't exist
    {
        if (m_draw_mode == dmSequentialFffPrint)
            return   _L("The sequential print is on.\n"
                        "It's impossible to apply any custom G-code for objects printing sequentually.\n");

        // Show mode as a first string of tooltop
        tooltip = "    " + _L("Print mode") + ": ";
        tooltip += (m_mode == SingleExtruder ? SingleExtruderMode :
                    m_mode == MultiAsSingle  ? MultiAsSingleMode  :
                                               MultiExtruderMode );
        tooltip += "\n\n";

        /* Note: just on OSX!!!
         * Right click event causes a little scrolling.
         * So, as a workaround we use Ctrl+LeftMouseClick instead of RightMouseClick
         * Show this information in tooltip
         * */

        // Show list of actions with new tick
        tooltip += ( m_mode == MultiAsSingle                                ?
                  _L("Add extruder change - Left click")                    :
                     m_mode == SingleExtruder                               ?
                  _L("Add color change - Left click for predefined color or "
                     "Shift + Left click for custom color selection")       :
                  _L("Add color change - Left click")  ) + " " +
                  _L("or press \"+\" key") + "\n" + (
                     is_osx ? 
                  _L("Add another code - Ctrl + Left click") :
                  _L("Add another code - Right click") );
    }

    if (tick_code_it != m_ticks.ticks.end())                                    // tick exists
    {
        if (m_draw_mode == dmSequentialFffPrint)
            return   _L("The sequential print is on.\n"
                        "It's impossible to apply any custom G-code for objects printing sequentually.\n" 
                        "This code won't be processed during G-code generation.");
        
        // Show custom Gcode as a first string of tooltop
        std::string space = "   ";
        tooltip = space;
        auto format_gcode = [space](std::string gcode) {
            boost::replace_all(gcode, "\n", "\n" + space);
            return gcode;
        };
        tooltip +=  
        	tick_code_it->type == ColorChange ?
        		(m_mode == SingleExtruder ?
                	format_wxstr(_L("Color change (\"%1%\")"), gcode(ColorChange)) :
                    format_wxstr(_L("Color change (\"%1%\") for Extruder %2%"), gcode(ColorChange), tick_code_it->extruder)) :
	            tick_code_it->type == PausePrint ?
	                format_wxstr(_L("Pause print (\"%1%\")"), gcode(PausePrint)) :
	            tick_code_it->type == Template ?
	                format_wxstr(_L("Custom template (\"%1%\")"), gcode(Template)) :
		            tick_code_it->type == ToolChange ?
		                format_wxstr(_L("Extruder (tool) is changed to Extruder \"%1%\""), tick_code_it->extruder) :                
		                from_u8(format_gcode(tick_code_it->extra));// tick_code_it->type == Custom

        // If tick is marked as a conflict (exclamation icon),
        // we should to explain why
        ConflictType conflict = m_ticks.is_conflict_tick(*tick_code_it, m_mode, m_only_extruder, m_values[tick]);
        if (conflict != ctNone)
            tooltip += "\n\n" + _L("Note") + "! ";
        if (conflict == ctModeConflict)
            tooltip +=  _L("G-code associated to this tick mark is in a conflict with print mode.\n"
                           "Editing it will cause changes of Slider data.");
        else if (conflict == ctMeaninglessColorChange)
            tooltip +=  _L("There is a color change for extruder that won't be used till the end of print job.\n"
                           "This code won't be processed during G-code generation.");
        else if (conflict == ctMeaninglessToolChange)
            tooltip +=  _L("There is an extruder change set to the same extruder.\n"
                           "This code won't be processed during G-code generation.");
        else if (conflict == ctRedundant)
            tooltip +=  _L("There is a color change for extruder that has not been used before.\n"
                           "Check your settings to avoid redundant color changes.");

        // Show list of actions with existing tick
        if (m_focus == fiActionIcon)
        tooltip += "\n\n" + _L("Delete tick mark - Left click or press \"-\" key") + "\n" + (
                      is_osx ? 
                   _L("Edit tick mark - Ctrl + Left click") :
                   _L("Edit tick mark - Right click") );
    }
    return tooltip;

}

int Control::get_edited_tick_for_position(const wxPoint pos, Type type /*= ColorChange*/)
{
    if (m_ticks.empty())
        return -1;

    int tick = get_value_from_position(pos);
    auto it = std::lower_bound(m_ticks.ticks.begin(), m_ticks.ticks.end(), TickCode{ tick });

    while (it != m_ticks.ticks.begin()) {
        --it;
        if (it->type == type)
            return it->tick;
    }

    return -1;
}

void Control::OnMotion(wxMouseEvent& event)
{
    bool action = false;

    const wxPoint pos = event.GetLogicalPosition(wxClientDC(this));
    int tick = -1;

    if (!m_is_left_down && !m_is_right_down) {
        if (is_point_in_rect(pos, m_rect_one_layer_icon))
            m_focus = fiOneLayerIcon;
        else if (is_point_in_rect(pos, m_rect_tick_action)) {
            m_focus = fiActionIcon;
            tick = m_selection == ssLower ? m_lower_value : m_higher_value;
        }
        else if (!m_ticks.empty() && is_point_in_rect(pos, m_rect_revert_icon))
            m_focus = fiRevertIcon;
        else if (is_point_in_rect(pos, m_rect_cog_icon))
            m_focus = fiCogIcon;
        else if (m_mode == SingleExtruder && is_point_in_rect(pos, get_colored_band_rect()) &&
                 get_edited_tick_for_position(pos) >= 0 )
            m_focus = fiColorBand;
        else if (is_point_in_rect(pos, m_rect_lower_thumb))
            m_focus = fiLowerThumb;
        else if (is_point_in_rect(pos, m_rect_higher_thumb))
            m_focus = fiHigherThumb;
        else {
            tick = get_tick_near_point(pos);
            if (tick < 0 && m_is_wipe_tower) {
                tick = get_value_from_position(pos);
                m_focus = tick > 0 && is_wipe_tower_layer(tick) && (tick == m_lower_value || tick == m_higher_value) ? 
                          fiSmartWipeTower : fiTick;
            }
            else 
                m_focus = fiTick;
        }
        m_moving_pos = pos;
    }
    else if (m_is_left_down || m_is_right_down) {
        if (m_selection == ssLower) {
            int current_value = m_lower_value;
            m_lower_value = get_value_from_position(pos.x, pos.y);
            correct_lower_value();
            action = (current_value != m_lower_value);
        }
        else if (m_selection == ssHigher) {
            int current_value = m_higher_value;
            m_higher_value = get_value_from_position(pos.x, pos.y);
            correct_higher_value();
            action = (current_value != m_higher_value);
        }
        m_moving_pos = wxDefaultPosition;
    }
    Refresh();
    Update();
    event.Skip();

    // Set tooltips with information for each icon
    this->SetToolTip(get_tooltip(tick));

    if (action) {
        wxCommandEvent e(wxEVT_SCROLL_CHANGED);
        e.SetEventObject(this);
        e.SetString("moving");
        ProcessWindowEvent(e);
    }
}

void Control::append_change_extruder_menu_item(wxMenu* menu, bool switch_current_code/* = false*/)
{
    const int extruders_cnt = GUI::wxGetApp().extruders_edited_cnt();
    if (extruders_cnt > 1) {
        std::array<int, 2> active_extruders = get_active_extruders_for_tick(m_selection == ssLower ? m_lower_value : m_higher_value);

        std::vector<wxBitmap*> icons = get_extruder_color_icons(true);

        wxMenu* change_extruder_menu = new wxMenu();

        for (int i = 1; i <= extruders_cnt; i++) {
            const bool is_active_extruder = i == active_extruders[0] || i == active_extruders[1];
            const wxString item_name = wxString::Format(_L("Extruder %d"), i) +
                                       (is_active_extruder ? " (" + _L("active") + ")" : "");

            if (m_mode == MultiAsSingle)
                append_menu_item(change_extruder_menu, wxID_ANY, item_name, "",
                    [this, i](wxCommandEvent&) { add_code_as_tick(ToolChange, i); }, *icons[i-1], menu,
                    [is_active_extruder]() { return !is_active_extruder; }, GUI::wxGetApp().plater());
        }

        const wxString change_extruder_menu_name = m_mode == MultiAsSingle ? 
                                                   (switch_current_code ? _L("Switch code to Change extruder") : _L("Change extruder") ) : 
                                                   _L("Change extruder (N/A)");

        append_submenu(menu, change_extruder_menu, wxID_ANY, change_extruder_menu_name, _L("Use another extruder"),
            active_extruders[1] > 0 ? "edit_uni" : "change_extruder",
            [this]() {return m_mode == MultiAsSingle; }, GUI::wxGetApp().plater());
    }
}

void Control::append_add_color_change_menu_item(wxMenu* menu, bool switch_current_code/* = false*/)
{
    const int extruders_cnt = GUI::wxGetApp().extruders_edited_cnt();
    if (extruders_cnt > 1) {
        int tick = m_selection == ssLower ? m_lower_value : m_higher_value; 
        std::set<int> used_extruders_for_tick = m_ticks.get_used_extruders_for_tick(tick, m_only_extruder, m_values[tick]);

        wxMenu* add_color_change_menu = new wxMenu();

        for (int i = 1; i <= extruders_cnt; i++) {
            const bool is_used_extruder = used_extruders_for_tick.empty() ? true : // #ys_FIXME till used_extruders_for_tick doesn't filled correct for mmMultiExtruder
                                          used_extruders_for_tick.find(i) != used_extruders_for_tick.end();
            const wxString item_name = wxString::Format(_L("Extruder %d"), i) +
                                       (is_used_extruder ? " (" + _L("used") + ")" : "");

            append_menu_item(add_color_change_menu, wxID_ANY, item_name, "",
                [this, i](wxCommandEvent&) { add_code_as_tick(ColorChange, i); }, "", menu,
                []() { return true; }, GUI::wxGetApp().plater());
        }

        const wxString menu_name = switch_current_code ? 
                                   format_wxstr(_L("Switch code to Color change (%1%) for:"), gcode(ColorChange)) : 
                                   format_wxstr(_L("Add color change (%1%) for:"), gcode(ColorChange));
        wxMenuItem* add_color_change_menu_item = menu->AppendSubMenu(add_color_change_menu, menu_name, "");
        add_color_change_menu_item->SetBitmap(create_menu_bitmap("colorchange_add_m"));
    }
}

void Control::OnLeftUp(wxMouseEvent& event)
{
    if (!HasCapture())
        return;
    this->ReleaseMouse();
    m_is_left_down = false;

    switch (m_mouse) {
    case maNone :
        move_current_thumb_to_pos(event.GetLogicalPosition(wxClientDC(this)));
        break;
    case maDeleteTick : 
        delete_current_tick();
        break;
    case maAddTick :
        add_current_tick();
        break;
    case maCogIconClick :
        show_cog_icon_context_menu();
        break;
    case maOneLayerIconClick:
        switch_one_layer_mode();
        break;
    case maRevertIconClick:
        discard_all_thicks();
        break;
    default :
        break;
    }

    Refresh();
    Update();
    event.Skip();

    wxCommandEvent e(wxEVT_SCROLL_CHANGED);
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}

void Control::enter_window(wxMouseEvent& event, const bool enter)
{
    m_is_focused = enter;
    Refresh();
    Update();
    event.Skip();
}

// "condition" have to be true for:
//    -  value increase (if wxSL_VERTICAL)
//    -  value decrease (if wxSL_HORIZONTAL) 
void Control::move_current_thumb(const bool condition)
{
//     m_is_one_layer = wxGetKeyState(WXK_CONTROL);
    int delta = condition ? -1 : 1;
    if (is_horizontal())
        delta *= -1;

    // accelerators
    int accelerator = 0;
    if (wxGetKeyState(WXK_SHIFT))
        accelerator += 5;
    if (wxGetKeyState(WXK_CONTROL))
        accelerator += 5;
    if (accelerator > 0)
        delta *= accelerator;

    if (m_selection == ssUndef)
        m_selection = ssHigher;

    if (m_selection == ssLower) {
        m_lower_value -= delta;
        correct_lower_value();
    }
    else if (m_selection == ssHigher) {
        m_higher_value -= delta;
        correct_higher_value();
    }
    Refresh();
    Update();

    wxCommandEvent e(wxEVT_SCROLL_CHANGED);
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}

void Control::OnWheel(wxMouseEvent& event)
{
    // Set nearest to the mouse thumb as a selected, if there is not selected thumb
    if (m_selection == ssUndef) {
        const wxPoint& pt = event.GetLogicalPosition(wxClientDC(this));
        
        if (is_horizontal())
            m_selection = abs(pt.x - m_rect_lower_thumb.GetRight()) <= 
                          abs(pt.x - m_rect_higher_thumb.GetLeft()) ? 
                          ssLower : ssHigher;
        else
            m_selection = abs(pt.y - m_rect_lower_thumb.GetTop()) <= 
                          abs(pt.y - m_rect_higher_thumb.GetBottom()) ? 
                          ssLower : ssHigher;
    }

    if (m_selection == ssLower && !is_lower_thumb_editable())
        m_selection = ssUndef;

    move_current_thumb((m_draw_mode == dmSequentialGCodeView) ? event.GetWheelRotation() < 0 : event.GetWheelRotation() > 0);
}

void Control::OnKeyDown(wxKeyEvent &event)
{
    const int key = event.GetKeyCode();
    if (m_draw_mode != dmSequentialGCodeView && key == WXK_NUMPAD_ADD) {
        // OnChar() is called immediately after OnKeyDown(), which can cause call of add_tick() twice.
        // To avoid this case we should suppress second add_tick() call.
        m_ticks.suppress_plus(true);
        add_current_tick(true);
    }
    else if (m_draw_mode != dmSequentialGCodeView && (key == WXK_NUMPAD_SUBTRACT || key == WXK_DELETE || key == WXK_BACK)) {
        // OnChar() is called immediately after OnKeyDown(), which can cause call of delete_tick() twice.
        // To avoid this case we should suppress second delete_tick() call.
        m_ticks.suppress_minus(true);
        delete_current_tick();
    }
    else if (m_draw_mode != dmSequentialGCodeView && event.GetKeyCode() == WXK_SHIFT)
        UseDefaultColors(false);
    else if (is_horizontal()) {
        if (m_is_focused) {
            if (key == WXK_LEFT || key == WXK_RIGHT)
                move_current_thumb(key == WXK_LEFT);
            else if (key == WXK_UP || key == WXK_DOWN) {
                if (key == WXK_DOWN)
                    m_selection = ssHigher;
                else if (key == WXK_UP && is_lower_thumb_editable())
                    m_selection = ssLower;
                Refresh();
            }
        }
        else {
            if (key == WXK_LEFT || key == WXK_RIGHT)
                move_current_thumb(key == WXK_LEFT);
        }
    }
    else {
        if (m_is_focused) {
            if (key == WXK_LEFT || key == WXK_RIGHT) {
                if (key == WXK_LEFT)
                    m_selection = ssHigher;
                else if (key == WXK_RIGHT && is_lower_thumb_editable())
                    m_selection = ssLower;
                Refresh();
            }
            else if (key == WXK_UP || key == WXK_DOWN)
                move_current_thumb(key == WXK_UP);
        }
        else {
            if (key == WXK_UP || key == WXK_DOWN)
                move_current_thumb(key == WXK_UP);
        }
    }

    event.Skip(); // !Needed to have EVT_CHAR generated as well
}

void Control::OnKeyUp(wxKeyEvent &event)
{
    if (event.GetKeyCode() == WXK_CONTROL)
        m_is_one_layer = false;
    else if (event.GetKeyCode() == WXK_SHIFT)
        UseDefaultColors(true);

    Refresh();
    Update();
    event.Skip();
}

void Control::OnChar(wxKeyEvent& event)
{
    const int key = event.GetKeyCode();
    if (m_draw_mode != dmSequentialGCodeView) {
        if (key == '+' && !m_ticks.suppressed_plus()) {
            add_current_tick(true);
            m_ticks.suppress_plus(false);
        }
        else if (key == '-' && !m_ticks.suppressed_minus()) {
            delete_current_tick();
            m_ticks.suppress_minus(false);
        }
    }
    if (key == 'G')
        jump_to_value();
}

void Control::OnRightDown(wxMouseEvent& event)
{
    if (HasCapture()) return;
    this->CaptureMouse();

    const wxPoint pos = event.GetLogicalPosition(wxClientDC(this));

    m_mouse = maNone;
    if (m_draw_mode == dmRegular) {
        if (is_point_in_rect(pos, m_rect_tick_action)) {
            const int tick = m_selection == ssLower ? m_lower_value : m_higher_value;
            m_mouse = m_ticks.ticks.find(TickCode{ tick }) == m_ticks.ticks.end() ?
                             maAddMenu : maEditMenu;
        }
        else if (m_mode == SingleExtruder   && !detect_selected_slider(pos) && is_point_in_rect(pos, get_colored_band_rect()))
            m_mouse = maForceColorEdit;
        else if (m_mode == MultiAsSingle    && is_point_in_rect(pos, m_rect_cog_icon))
            m_mouse = maCogIconMenu;
    }
    if (m_mouse != maNone || !detect_selected_slider(pos))
        return;

    if (m_selection == ssLower)
        m_higher_value = m_lower_value;
    else
        m_lower_value = m_higher_value;

    // set slider to "one layer" mode
    m_is_right_down = m_is_one_layer = true; 

    Refresh();
    Update();
    event.Skip();
}

// Get active extruders for tick. 
// Means one current extruder for not existing tick OR 
// 2 extruders - for existing tick (extruder before ToolChange and extruder of current existing tick)
// Use those values to disable selection of active extruders
std::array<int, 2> Control::get_active_extruders_for_tick(int tick) const
{
    int default_initial_extruder = m_mode == MultiAsSingle ? std::max<int>(1, m_only_extruder) : 1;
    std::array<int, 2> extruders = { default_initial_extruder, -1 };
    if (m_ticks.empty())
        return extruders;

    auto it = m_ticks.ticks.lower_bound(TickCode{tick});

    if (it != m_ticks.ticks.end() && it->tick == tick) // current tick exists
        extruders[1] = it->extruder;

    while (it != m_ticks.ticks.begin()) {
        --it;
        if(it->type == ToolChange) {
            extruders[0] = it->extruder;
            break;
        }
    }

    return extruders;
}

// Get used extruders for tick. 
// Means all extruders(tools) which will be used during printing from current tick to the end
std::set<int> TickCodeInfo::get_used_extruders_for_tick(int tick, int only_extruder, double print_z, Mode force_mode/* = Undef*/) const
{
    Mode e_mode = !force_mode ? mode : force_mode;

    if (e_mode == MultiExtruder) {
        // #ys_FIXME: get tool ordering from _correct_ place
        const ToolOrdering& tool_ordering = GUI::wxGetApp().plater()->fff_print().get_tool_ordering();

        if (tool_ordering.empty())
            return {};

        std::set<int> used_extruders;

        auto it_layer_tools = std::lower_bound(tool_ordering.begin(), tool_ordering.end(), LayerTools(print_z));
        for (; it_layer_tools != tool_ordering.end(); ++it_layer_tools) {
            const std::vector<unsigned>& extruders = it_layer_tools->extruders;
            for (const auto& extruder : extruders)
                used_extruders.emplace(extruder+1);
        }

        return used_extruders;
    }

    const int default_initial_extruder = e_mode == MultiAsSingle ? std::max(only_extruder, 1) : 1;
    if (ticks.empty() || e_mode == SingleExtruder)
        return {default_initial_extruder};

    std::set<int> used_extruders;

    auto it_start = ticks.lower_bound(TickCode{tick});
    auto it = it_start;
    if (it == ticks.begin() && it->type == ToolChange &&
        tick != it->tick )  // In case of switch of ToolChange to ColorChange, when tick exists,
                            // we shouldn't change color for extruder, which will be deleted
    {
        used_extruders.emplace(it->extruder);
        if (tick < it->tick)
            used_extruders.emplace(default_initial_extruder);
    }

    while (it != ticks.begin()) {
        --it;
        if (it->type == ToolChange && tick != it->tick) {
            used_extruders.emplace(it->extruder);
            break;
        }
    }

    if (it == ticks.begin() && used_extruders.empty())
        used_extruders.emplace(default_initial_extruder);

    for (it = it_start; it != ticks.end(); ++it)
        if (it->type == ToolChange && tick != it->tick)
            used_extruders.emplace(it->extruder);

    return used_extruders;
}

void Control::show_add_context_menu()
{
    wxMenu menu;

    if (m_mode == SingleExtruder) {
        append_menu_item(&menu, wxID_ANY, _L("Add color change") + " (" + gcode(ColorChange) + ")", "",
            [this](wxCommandEvent&) { add_code_as_tick(ColorChange); }, "colorchange_add_m", &menu);

        UseDefaultColors(false);
    }
    else {
        append_change_extruder_menu_item(&menu);
        append_add_color_change_menu_item(&menu);
    }

    if (!gcode(PausePrint).empty())
        append_menu_item(&menu, wxID_ANY, _L("Add pause print") + " (" + gcode(PausePrint) + ")", "",
            [this](wxCommandEvent&) { add_code_as_tick(PausePrint); }, "pause_print", &menu);

    if (!gcode(Template).empty())
        append_menu_item(&menu, wxID_ANY, _L("Add custom template") + " (" + gcode(Template) + ")", "",
            [this](wxCommandEvent&) { add_code_as_tick(Template); }, "edit_gcode", &menu);

    append_menu_item(&menu, wxID_ANY, _L("Add custom G-code"), "",
        [this](wxCommandEvent&) { add_code_as_tick(Custom); }, "edit_gcode", &menu);

    GUI::wxGetApp().plater()->PopupMenu(&menu);
}

void Control::show_edit_context_menu()
{
    wxMenu menu;

    std::set<TickCode>::iterator it = m_ticks.ticks.find(TickCode{ m_selection == ssLower ? m_lower_value : m_higher_value });

    if (it->type == ToolChange) {
        if (m_mode == MultiAsSingle)
            append_change_extruder_menu_item(&menu);
        append_add_color_change_menu_item(&menu, true);
    }
    else
        append_menu_item(&menu, wxID_ANY, it->type == ColorChange ? _L("Edit color") :
                                          it->type == PausePrint  ? _L("Edit pause print message") :
                                          _L("Edit custom G-code"), "",
            [this](wxCommandEvent&) { edit_tick(); }, "edit_uni", &menu);

    if (it->type == ColorChange && m_mode == MultiAsSingle)
        append_change_extruder_menu_item(&menu, true);

    append_menu_item(&menu, wxID_ANY, it->type == ColorChange ? _L("Delete color change") : 
                                      it->type == ToolChange  ? _L("Delete tool change") :
                                      it->type == PausePrint  ? _L("Delete pause print") :
                                      _L("Delete custom G-code"), "",
        [this](wxCommandEvent&) { delete_current_tick();}, "colorchange_del_f", &menu);

    GUI::wxGetApp().plater()->PopupMenu(&menu);
}

void Control::show_cog_icon_context_menu()
{
    wxMenu menu;

    append_menu_item(&menu, wxID_ANY, _L("Jump to height") + " (Shift+G)", "",
                    [this](wxCommandEvent&) { jump_to_value(); }, "", & menu);

    wxMenu* ruler_mode_menu = new wxMenu();
    if (ruler_mode_menu) {
        append_menu_check_item(ruler_mode_menu, wxID_ANY, _L("None"), _L("Hide ruler"), 
            [this](wxCommandEvent&) { if (m_extra_style != 0) m_extra_style = 0; }, ruler_mode_menu, 
            []() { return true; }, [this]() { return m_extra_style == 0; }, GUI::wxGetApp().plater());

        append_menu_check_item(ruler_mode_menu, wxID_ANY, _L("Show object height"), _L("Show object height on the ruler"),
            [this](wxCommandEvent&) { m_extra_style & wxSL_AUTOTICKS ? m_extra_style ^= wxSL_AUTOTICKS : m_extra_style |= wxSL_AUTOTICKS; }, ruler_mode_menu,
            []() { return true; }, [this]() { return m_extra_style & wxSL_AUTOTICKS; }, GUI::wxGetApp().plater());

        append_menu_check_item(ruler_mode_menu, wxID_ANY, _L("Show estimated print time"), _L("Show estimated print time on the ruler"),
            [this](wxCommandEvent&) { m_extra_style & wxSL_VALUE_LABEL ? m_extra_style ^= wxSL_VALUE_LABEL : m_extra_style |= wxSL_VALUE_LABEL; }, ruler_mode_menu,
            []() { return true; }, [this]() { return m_extra_style & wxSL_VALUE_LABEL; }, GUI::wxGetApp().plater());

        append_submenu(&menu, ruler_mode_menu, wxID_ANY, _L("Ruler mode"), _L("Set ruler mode"), "",
            []() { return true; }, this);
    }

    if (m_mode == MultiAsSingle && m_draw_mode == dmRegular)
        append_menu_item(&menu, wxID_ANY, _L("Set extruder sequence for the entire print"), "",
            [this](wxCommandEvent&) { edit_extruder_sequence(); }, "", &menu);

    if (m_mode != MultiExtruder && m_draw_mode == dmRegular)
        append_menu_item(&menu, wxID_ANY, _L("Set auto color changes"), "",
            [this](wxCommandEvent&) { auto_color_change(); }, "", &menu);

    GUI::wxGetApp().plater()->PopupMenu(&menu);
}

void Control::auto_color_change()
{
    if (!m_ticks.empty()) {
        wxString msg_text = _L("This action will cause deletion of all ticks on vertical slider.") + "\n\n" +
                            _L("This action is not revertible.\nDo you want to proceed?");
        GUI::WarningDialog dialog(m_parent, msg_text, _L("Warning"), wxYES | wxNO);
        if (dialog.ShowModal() == wxID_NO)
            return;    
        m_ticks.ticks.clear();
    }

    int extruders_cnt = GUI::wxGetApp().extruders_edited_cnt();
    int extruder = 2;

    const Print& print = GUI::wxGetApp().plater()->fff_print();  
    double delta_area = scale_(scale_(25)); // equal to 25 mm2

    for (auto object : print.objects()) {
        if (object->layer_count() == 0)
            continue;
        double prev_area = area(object->get_layer(0)->lslices);

        for (size_t i = 1; i < object->layers().size(); i++) {
            Layer* layer = object->get_layer(i);
            double cur_area = area(layer->lslices);

            if (cur_area > prev_area && prev_area - cur_area > scale_(scale_(1)))
                break;

            if (prev_area - cur_area > delta_area) {
                int tick = get_tick_from_value(layer->print_z);
                if (tick >= 0 && !m_ticks.has_tick(tick)) {
                    if (m_mode == SingleExtruder) {
                        m_ticks.set_default_colors(true);
                        m_ticks.add_tick(tick, ColorChange, 1, layer->print_z);
                    }
                    else {
                        m_ticks.add_tick(tick, ToolChange, extruder, layer->print_z);
                        if (++extruder > extruders_cnt)
                            extruder = 1;
                    }
                }

                // allow max 3 auto color changes
                if (m_ticks.ticks.size() == 3)
                    break;
            }

            prev_area = cur_area;
        }
    }

    if (m_ticks.empty())
        GUI::wxGetApp().plater()->get_notification_manager()->push_notification(GUI::NotificationType::EmptyAutoColorChange);

    post_ticks_changed_event();
}

void Control::OnRightUp(wxMouseEvent& event)
{
    if (!HasCapture())
        return;
    this->ReleaseMouse();
    m_is_right_down = m_is_one_layer = false;

    if (m_mouse == maForceColorEdit) {
        wxPoint pos = event.GetLogicalPosition(wxClientDC(this));
        int edited_tick = get_edited_tick_for_position(pos);
        if (edited_tick >= 0)
            edit_tick(edited_tick);
    }
    else if (m_mouse == maAddMenu)
        show_add_context_menu();
    else if (m_mouse == maEditMenu)
        show_edit_context_menu();
    else if (m_mouse == maCogIconMenu)
        show_cog_icon_context_menu();

    Refresh();
    Update();
    event.Skip();
}

static std::string get_new_color(const std::string& color)
{
    wxColour clr(color);
    if (!clr.IsOk())
        clr = wxColour(0, 0, 0); // Don't set alfa to transparence

    auto data = new wxColourData();
    data->SetChooseFull(1);
    data->SetColour(clr);

    wxColourDialog dialog(nullptr, data);
    dialog.CenterOnParent();
    if (dialog.ShowModal() == wxID_OK)
        return dialog.GetColourData().GetColour().GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
    return "";
}

/* To avoid get an empty string from wxTextEntryDialog 
 * Let disable OK button, if TextCtrl is empty
 * OR input value is our of range (min..max), when min a nd max are positive
 * */
static void upgrade_text_entry_dialog(wxTextEntryDialog* dlg, double min = -1.0, double max = -1.0)
{
    GUI::wxGetApp().UpdateDlgDarkUI(dlg);

    // detect TextCtrl and OK button
    wxTextCtrl* textctrl {nullptr};
    wxWindowList& dlg_items = dlg->GetChildren();
    for (auto item : dlg_items) {
        textctrl = dynamic_cast<wxTextCtrl*>(item);
        if (textctrl)
            break;
    }

    if (!textctrl)
        return;

    textctrl->SetInsertionPointEnd();

    wxButton* btn_OK = static_cast<wxButton*>(dlg->FindWindowById(wxID_OK));
    btn_OK->Bind(wxEVT_UPDATE_UI, [textctrl, min, max](wxUpdateUIEvent& evt)
    {
        bool disable = textctrl->IsEmpty();
        if (!disable && min >= 0.0 && max >= 0.0) {
            double value = -1.0;
            if (!textctrl->GetValue().ToDouble(&value))    // input value couldn't be converted to double
                disable = true;
            else
                disable = value < min - epsilon() || value > max + epsilon();       // is input value is out of valid range ?
        }

        evt.Enable(!disable);
    }, btn_OK->GetId());
}

static std::string get_custom_code(const std::string& code_in, double height)
{
    wxString msg_text = _L("Enter custom G-code used on current layer") + ":";
    wxString msg_header = format_wxstr(_L("Custom G-code on current layer (%1% mm)."), height);

    // get custom gcode
    wxTextEntryDialog dlg(nullptr, msg_text, msg_header, code_in,
        wxTextEntryDialogStyle | wxTE_MULTILINE);
    upgrade_text_entry_dialog(&dlg);

#if ENABLE_VALIDATE_CUSTOM_GCODE
    bool valid = true;
    std::string value;
    do {
        if (dlg.ShowModal() != wxID_OK)
            return "";

        value = into_u8(dlg.GetValue());
        valid = GUI::Tab::validate_custom_gcode("Custom G-code", value);
    } while (!valid);
    return value;
#else
    if (dlg.ShowModal() != wxID_OK)
        return "";

    return into_u8(dlg.GetValue());
#endif // ENABLE_VALIDATE_CUSTOM_GCODE
}

static std::string get_pause_print_msg(const std::string& msg_in, double height)
{
    wxString msg_text = _L("Enter short message shown on Printer display when a print is paused") + ":";
    wxString msg_header = format_wxstr(_L("Message for pause print on current layer (%1% mm)."), height);

    // get custom gcode
    wxTextEntryDialog dlg(nullptr, msg_text, msg_header, from_u8(msg_in),
        wxTextEntryDialogStyle);
    upgrade_text_entry_dialog(&dlg);

    if (dlg.ShowModal() != wxID_OK || dlg.GetValue().IsEmpty())
        return "";

    return into_u8(dlg.GetValue());
}

static double get_value_to_jump(double active_value, double min_z, double max_z, DrawMode mode)
{
    wxString msg_text = (mode == dmSequentialGCodeView) ? _L("Enter the move you want to jump to") + ":" : _L("Enter the height you want to jump to") + ":";
    wxString msg_header = (mode == dmSequentialGCodeView) ? _L("Jump to move") : _L("Jump to height");
    wxString msg_in = GUI::double_to_string(active_value);

    // get custom gcode
    wxTextEntryDialog dlg(nullptr, msg_text, msg_header, msg_in, wxTextEntryDialogStyle);
    upgrade_text_entry_dialog(&dlg, min_z, max_z);

    if (dlg.ShowModal() != wxID_OK || dlg.GetValue().IsEmpty())
        return -1.0;

    double value = -1.0;
    return dlg.GetValue().ToDouble(&value) ? value : -1.0;
}

void Control::add_code_as_tick(Type type, int selected_extruder/* = -1*/)
{
    if (m_selection == ssUndef)
        return;
    const int tick = m_selection == ssLower ? m_lower_value : m_higher_value;

    if ( !check_ticks_changed_event(type) )
        return;

    if (type == ColorChange && gcode(ColorChange).empty())
        GUI::wxGetApp().plater()->get_notification_manager()->push_notification(GUI::NotificationType::EmptyColorChangeCode);

    const int extruder = selected_extruder > 0 ? selected_extruder : std::max<int>(1, m_only_extruder);
    const auto it = m_ticks.ticks.find(TickCode{ tick });
    
    if ( it == m_ticks.ticks.end() ) {
        // try to add tick
        if (!m_ticks.add_tick(tick, type, extruder, m_values[tick]))
            return;
    }
    else if (type == ToolChange || type == ColorChange) {
        // try to switch tick code to ToolChange or ColorChange accordingly
        if (!m_ticks.switch_code_for_tick(it, type, extruder))
            return;
    }
    else
        return;

    post_ticks_changed_event(type);
}

void Control::add_current_tick(bool call_from_keyboard /*= false*/)
{
    if (m_selection == ssUndef)
        return;
    const int tick = m_selection == ssLower ? m_lower_value : m_higher_value;
    auto it = m_ticks.ticks.find(TickCode{ tick });

    if (it != m_ticks.ticks.end() ||    // this tick is already exist
        !check_ticks_changed_event(m_mode == MultiAsSingle ? ToolChange : ColorChange))
        return;

    if (m_mode == SingleExtruder)
        add_code_as_tick(ColorChange);
    else
    {
        wxMenu menu;

        if (m_mode == MultiAsSingle)
            append_change_extruder_menu_item(&menu);
        else
            append_add_color_change_menu_item(&menu);

        wxPoint pos = wxDefaultPosition; 
        /* Menu position will be calculated from mouse click position, but...
         * if function is called from keyboard (pressing "+"), we should to calculate it
         * */
        if (call_from_keyboard) {
            int width, height;
            get_size(&width, &height);

            const wxCoord coord = 0.75 * (is_horizontal() ? height : width);
            this->GetPosition(&width, &height);

            pos = is_horizontal() ? 
                  wxPoint(get_position_from_value(tick), height + coord) :
                  wxPoint(width + coord, get_position_from_value(tick));
        }

        GUI::wxGetApp().plater()->PopupMenu(&menu, pos);
    }
}

void Control::delete_current_tick()
{
    if (m_selection == ssUndef)
        return;

    auto it = m_ticks.ticks.find(TickCode{ m_selection == ssLower ? m_lower_value : m_higher_value });
    if (it == m_ticks.ticks.end() ||
        !check_ticks_changed_event(it->type))
        return;

    Type type = it->type;
    m_ticks.ticks.erase(it);
    post_ticks_changed_event(type);
}

void Control::edit_tick(int tick/* = -1*/)
{
    if (tick < 0)
        tick = m_selection == ssLower ? m_lower_value : m_higher_value;
    const std::set<TickCode>::iterator it = m_ticks.ticks.find(TickCode{ tick });

    if (it == m_ticks.ticks.end() ||
        !check_ticks_changed_event(it->type))
        return;

    Type type = it->type;
    if (m_ticks.edit_tick(it, m_values[it->tick]))
        post_ticks_changed_event(type);
}

// switch on/off one layer mode
void Control::switch_one_layer_mode()
{
    m_is_one_layer = !m_is_one_layer;
    if (!m_is_one_layer) {
        SetLowerValue(m_min_value);
        SetHigherValue(m_max_value);
    }
    m_selection == ssLower ? correct_lower_value() : correct_higher_value();
    if (m_selection == ssUndef) m_selection = ssHigher;
}

// discard all custom changes on DoubleSlider
void Control::discard_all_thicks()
{
    SetLowerValue(m_min_value);
    SetHigherValue(m_max_value);

    m_selection == ssLower ? correct_lower_value() : correct_higher_value();
    if (m_selection == ssUndef) m_selection = ssHigher;

    m_ticks.ticks.clear();
    post_ticks_changed_event();
    
}

// Set current thumb position to the nearest tick (if it is)
// OR to a value corresponding to the mouse click (pos)
void Control::move_current_thumb_to_pos(wxPoint pos)
{
    const int tick_val = get_tick_near_point(pos);
    const int mouse_val = tick_val >= 0 && m_draw_mode == dmRegular ? tick_val :
        get_value_from_position(pos);
    if (mouse_val >= 0) {
        if (m_selection == ssLower) {
            SetLowerValue(mouse_val);
            correct_lower_value();
        }
        else { // even m_selection is ssUndef, upper thumb should be selected
            SetHigherValue(mouse_val);
            correct_higher_value();
        }
    }
}

void Control::edit_extruder_sequence()
{
    if (!check_ticks_changed_event(ToolChange))
        return;

    GUI::ExtruderSequenceDialog dlg(m_extruders_sequence);
    if (dlg.ShowModal() != wxID_OK)
        return;
    m_extruders_sequence = dlg.GetValue();

    m_ticks.erase_all_ticks_with_code(ToolChange);

    const int extr_cnt = m_extruders_sequence.extruders.size();
    if (extr_cnt == 1)
        return;

    int tick = 0;
    double value = 0.0;
    int extruder = -1;

    std::random_device rd;  //Will be used to obtain a seed for the random number engine
    std::mt19937 gen(rd()); //Standard mersenne_twister_engine seeded with rd()
    std::uniform_int_distribution<> distrib(0, extr_cnt-1);

    while (tick <= m_max_value)
    {
        bool color_repetition = false;
        if (m_extruders_sequence.random_sequence) {
            int rand_extr = distrib(gen);
            if (m_extruders_sequence.color_repetition)
                color_repetition = rand_extr == extruder;
            else
                while (rand_extr == extruder)
                    rand_extr = distrib(gen);
            extruder = rand_extr;
        }
        else {
            extruder++;
            if (extruder == extr_cnt)
                extruder = 0;
        }

        const int cur_extruder = m_extruders_sequence.extruders[extruder];

        bool meaningless_tick = tick == 0.0 && cur_extruder == extruder;
        if (!meaningless_tick && !color_repetition)
            m_ticks.ticks.emplace(TickCode{tick, ToolChange,cur_extruder + 1, m_extruder_colors[cur_extruder]});

        if (m_extruders_sequence.is_mm_intervals) {
            value += m_extruders_sequence.interval_by_mm;
            tick = get_tick_from_value(value, true);
            if (tick < 0)
                break;
        }
        else
            tick += m_extruders_sequence.interval_by_layers;
    }

    post_ticks_changed_event(ToolChange);
}

void Control::jump_to_value()
{
    double value = get_value_to_jump(m_values[m_selection == ssLower ? m_lower_value : m_higher_value],
                                     m_values[m_min_value], m_values[m_max_value], m_draw_mode);
    if (value < 0.0)
        return;

    int tick_value = get_tick_from_value(value);

    if (m_selection == ssLower)
        SetLowerValue(tick_value);
    else
        SetHigherValue(tick_value);
}

void Control::post_ticks_changed_event(Type type /*= Custom*/)
{
//    m_force_mode_apply = type != ToolChange; // It looks like this condition is no needed now. Leave it for the testing

    wxPostEvent(this->GetParent(), wxCommandEvent(wxCUSTOMEVT_TICKSCHANGED));
}

bool Control::check_ticks_changed_event(Type type)
{
    if ( m_ticks.mode == m_mode                                                     ||
        (type != ColorChange && type != ToolChange)                       ||
        (m_ticks.mode == SingleExtruder && m_mode == MultiAsSingle) || // All ColorChanges will be applied for 1st extruder
        (m_ticks.mode == MultiExtruder  && m_mode == MultiAsSingle) )  // Just mark ColorChanges for all unused extruders
        return true;

    if ((m_ticks.mode == SingleExtruder && m_mode == MultiExtruder ) ||
        (m_ticks.mode == MultiExtruder  && m_mode == SingleExtruder)    )
    {
        if (!m_ticks.has_tick_with_code(ColorChange))
            return true;

        wxString message = (m_ticks.mode == SingleExtruder ?
                            _L("The last color change data was saved for a single extruder printing.") :
                            _L("The last color change data was saved for a multi extruder printing.") 
                            ) + "\n" +
                            _L("Your current changes will delete all saved color changes.") + "\n\n\t" +
                            _L("Are you sure you want to continue?");

        //wxMessageDialog msg(this, message, _L("Notice"), wxYES_NO);
        GUI::MessageDialog msg(this, message, _L("Notice"), wxYES_NO);
        if (msg.ShowModal() == wxID_YES) {
            m_ticks.erase_all_ticks_with_code(ColorChange);
            post_ticks_changed_event(ColorChange);
        }
        return false;
    }
    //          m_ticks_mode == MultiAsSingle
    if( m_ticks.has_tick_with_code(ToolChange) ) {
        wxString message =  m_mode == SingleExtruder ?                          (
                            _L("The last color change data was saved for a multi extruder printing.") + "\n\n" +
                            _L("Select YES if you want to delete all saved tool changes, \n"
                               "NO if you want all tool changes switch to color changes, \n"
                               "or CANCEL to leave it unchanged.") + "\n\n\t" +
                            _L("Do you want to delete all saved tool changes?")  
                            ): ( // MultiExtruder
                            _L("The last color change data was saved for a multi extruder printing with tool changes for whole print.") + "\n\n" +
                            _L("Your current changes will delete all saved extruder (tool) changes.") + "\n\n\t" +
                            _L("Are you sure you want to continue?")                  ) ;

        //wxMessageDialog msg(this, message, _L("Notice"), wxYES_NO | (m_mode == SingleExtruder ? wxCANCEL : 0));
        GUI::MessageDialog msg(this, message, _L("Notice"), wxYES_NO | (m_mode == SingleExtruder ? wxCANCEL : 0));
        const int answer = msg.ShowModal();
        if (answer == wxID_YES) {
            m_ticks.erase_all_ticks_with_code(ToolChange);
            post_ticks_changed_event(ToolChange);
        }
        else if (m_mode == SingleExtruder && answer == wxID_NO) {
            m_ticks.switch_code(ToolChange, ColorChange);
            post_ticks_changed_event(ColorChange);
        }
        return false;
    }

    return true;
}

std::string TickCodeInfo::get_color_for_tick(TickCode tick, Type type, const int extruder)
{
    if (mode == SingleExtruder && type == ColorChange && m_use_default_colors) {
        const std::vector<std::string>& colors = ColorPrintColors::get();
        if (ticks.empty())
            return colors[0];
        m_default_color_idx++;

        return colors[m_default_color_idx % colors.size()];
    }

    std::string color = (*m_colors)[extruder - 1];

    if (type == ColorChange) {
        if (!ticks.empty()) {
            auto before_tick_it = std::lower_bound(ticks.begin(), ticks.end(), tick );
            while (before_tick_it != ticks.begin()) {
                --before_tick_it;
                if (before_tick_it->type == ColorChange && before_tick_it->extruder == extruder) {
                    color = before_tick_it->color;
                    break;
                }
            }
        }

        color = get_new_color(color);
    }
    return color;
}

bool TickCodeInfo::add_tick(const int tick, Type type, const int extruder, double print_z)
{
    std::string color;
    std::string extra;
    if (type == Custom)           // custom Gcode
    {
        extra = get_custom_code(custom_gcode, print_z);
        if (extra.empty())
            return false;
        custom_gcode = extra;
    }
    else if (type == PausePrint) {
        extra = get_pause_print_msg(pause_print_msg, print_z);
        if (extra.empty())
            return false;
        pause_print_msg = extra;
    }
    else {
        color = get_color_for_tick(TickCode{ tick }, type, extruder);
        if (color.empty())
            return false;
    }

    if (mode == SingleExtruder)
        m_use_default_colors = true;

    ticks.emplace(TickCode{ tick, type, extruder, color, extra });
    return true;
}

bool TickCodeInfo::edit_tick(std::set<TickCode>::iterator it, double print_z)
{
    std::string edited_value;
    if (it->type == ColorChange)
        edited_value = get_new_color(it->color);
    else if (it->type == PausePrint)
        edited_value = get_pause_print_msg(it->extra, print_z);
    else
        edited_value = get_custom_code(it->type == Template ? gcode(Template) : it->extra, print_z);

    if (edited_value.empty())
        return false;

    TickCode changed_tick = *it;
    if (it->type == ColorChange) {
        if (it->color == edited_value)
            return false;
        changed_tick.color = edited_value;
    }
    else if (it->type == Template) {
        if (gcode(Template) == edited_value)
            return false;
        changed_tick.extra = edited_value;
        changed_tick.type  = Custom;
    }
    else if (it->type == Custom || it->type == PausePrint) {
        if (it->extra == edited_value)
            return false;
        changed_tick.extra = edited_value;
    }

    ticks.erase(it);
    ticks.emplace(changed_tick);

    return true;
}

void TickCodeInfo::switch_code(Type type_from, Type type_to)
{
    for (auto it{ ticks.begin() }, end{ ticks.end() }; it != end; )
        if (it->type == type_from) {
            TickCode tick = *it;
            tick.type = type_to;
            tick.extruder = 1;
            ticks.erase(it);
            it = ticks.emplace(tick).first;
        }
        else
            ++it;
}

bool TickCodeInfo::switch_code_for_tick(std::set<TickCode>::iterator it, Type type_to, const int extruder)
{
    const std::string color = get_color_for_tick(*it, type_to, extruder);
    if (color.empty())
        return false;

    TickCode changed_tick   = *it;
    changed_tick.type       = type_to;
    changed_tick.extruder   = extruder;
    changed_tick.color      = color;

    ticks.erase(it);
    ticks.emplace(changed_tick);

    return true;
}

void TickCodeInfo::erase_all_ticks_with_code(Type type)
{
    for (auto it{ ticks.begin() }, end{ ticks.end() }; it != end; ) {
        if (it->type == type)
            it = ticks.erase(it);
        else
            ++it;
    }
}

bool TickCodeInfo::has_tick_with_code(Type type)
{
    for (const TickCode& tick : ticks)
        if (tick.type == type)
            return true;

    return false;
}

bool TickCodeInfo::has_tick(int tick)
{
    return ticks.find(TickCode{ tick }) != ticks.end();
}

ConflictType TickCodeInfo::is_conflict_tick(const TickCode& tick, Mode out_mode, int only_extruder, double print_z)
{
    if ((tick.type == ColorChange && (
            (mode == SingleExtruder && out_mode == MultiExtruder ) ||
            (mode == MultiExtruder  && out_mode == SingleExtruder)    )) ||
        (tick.type == ToolChange &&
            (mode == MultiAsSingle && out_mode != MultiAsSingle)) )
        return ctModeConflict;

    // check ColorChange tick
    if (tick.type == ColorChange) {
        // We should mark a tick as a "MeaninglessColorChange", 
        // if it has a ColorChange for unused extruder from current print to end of the print
        std::set<int> used_extruders_for_tick = get_used_extruders_for_tick(tick.tick, only_extruder, print_z, out_mode);

        if (used_extruders_for_tick.find(tick.extruder) == used_extruders_for_tick.end())
            return ctMeaninglessColorChange;

        // We should mark a tick as a "Redundant", 
        // if it has a ColorChange for extruder that has not been used before
        if (mode == MultiAsSingle && tick.extruder != std::max<int>(only_extruder, 1) )
        {
            auto it = ticks.lower_bound( tick );
            if (it == ticks.begin() && it->type == ToolChange && tick.extruder == it->extruder)
                return ctNone;

            while (it != ticks.begin()) {
                --it;
                if (it->type == ToolChange && tick.extruder == it->extruder)
                    return ctNone;
            }

            return ctRedundant;
        }
    }

    // check ToolChange tick
    if (mode == MultiAsSingle && tick.type == ToolChange) {
        // We should mark a tick as a "MeaninglessToolChange", 
        // if it has a ToolChange to the same extruder
        auto it = ticks.find(tick);
        if (it == ticks.begin())
            return tick.extruder == std::max<int>(only_extruder, 1) ? ctMeaninglessToolChange : ctNone;

        while (it != ticks.begin()) {
            --it;
            if (it->type == ToolChange)
                return tick.extruder == it->extruder ? ctMeaninglessToolChange : ctNone;
        }
    }

    return ctNone;
}

} // DoubleSlider

} // Slic3r


