#ifndef slic3r_GUI_DoubleSlider_hpp_
#define slic3r_GUI_DoubleSlider_hpp_

#include "libslic3r/CustomGCode.hpp"
#include "wxExtensions.hpp"

#include <wx/wx.h>
#include <wx/window.h>
#include <wx/control.h>
#include <wx/dc.h>
#include <wx/slider.h>

#include <vector>
#include <set>

class wxMenu;

namespace Slic3r {

namespace DoubleSlider {

/* For exporting GCode in GCodeWriter is used XYZF_NUM(val) = PRECISION(val, 3) for XYZ values. 
 * So, let use same value as a permissible error for layer height.
 */
static double epsilon() { return 0.0011;}

// custom message the slider sends to its parent to notify a tick-change:
wxDECLARE_EVENT(wxCUSTOMEVT_TICKSCHANGED, wxEvent);

enum SelectedSlider {
    ssUndef,
    ssLower,
    ssHigher
};

enum IconFocus {
    ifNone,
    ifRevert,
    ifCog
};

enum ConflictType
{
    ctNone,
    ctModeConflict,
    ctMeaninglessColorChange,
    ctMeaninglessToolChange,
    ctRedundant
};

using t_mode = CustomGCode::Mode;

struct TickCode
{
    bool operator<(const TickCode& other) const { return other.tick > this->tick; }
    bool operator>(const TickCode& other) const { return other.tick < this->tick; }

    int         tick = 0;
    std::string gcode = ColorChangeCode;
    int         extruder = 0;
    std::string color;
};

class TickCodeInfo
{
    std::string custom_gcode;
    std::string pause_print_msg;
    bool        m_suppress_plus     = false;
    bool        m_suppress_minus    = false;

    std::string get_color_for_tick(TickCode tick, const std::string& code, const int extruder);

public:
    std::set<TickCode> ticks {};
    t_mode              mode = t_mode::SingleExtruder;

    bool empty() const { return ticks.empty(); }
    void set_pause_print_msg(const std::string& message) { pause_print_msg = message; }

    bool add_tick(const int tick, std::string& code, int extruder, double print_z);
    bool edit_tick(std::set<TickCode>::iterator it, double print_z);
    void switch_code(const std::string& code_from, const std::string& code_to);
    bool switch_code_for_tick(std::set<TickCode>::iterator it, const std::string& code_to, const int extruder);
    void erase_all_ticks_with_code(const std::string& gcode);

    bool            has_tick_with_code(const std::string& gcode);
    ConflictType    is_conflict_tick(const TickCode& tick, t_mode out_mode, int only_extruder, double print_z);

    // Get used extruders for tick.
    // Means all extruders(tools) which will be used during printing from current tick to the end
    std::set<int>   get_used_extruders_for_tick(int tick, int only_extruder, double print_z) const;

    void suppress_plus (bool suppress) { m_suppress_plus = suppress; }
    void suppress_minus(bool suppress) { m_suppress_minus = suppress; }
    bool suppressed_plus () { return m_suppress_plus; }
    bool suppressed_minus() { return m_suppress_minus; }
};


struct ExtrudersSequence
{
    bool            is_mm_intervals     = true;
    double          interval_by_mm      = 3.0;
    int             interval_by_layers  = 10;
    std::vector<size_t>  extruders      = { 0 };

    bool operator==(const ExtrudersSequence& other) const
    {
        return  (other.is_mm_intervals      == this->is_mm_intervals    ) &&
                (other.interval_by_mm       == this->interval_by_mm     ) &&
                (other.interval_by_layers   == this->interval_by_layers ) &&
                (other.extruders            == this->extruders          ) ;
    }
    bool operator!=(const ExtrudersSequence& other) const
    {
        return  (other.is_mm_intervals      != this->is_mm_intervals    ) &&
                (other.interval_by_mm       != this->interval_by_mm     ) &&
                (other.interval_by_layers   != this->interval_by_layers ) &&
                (other.extruders            != this->extruders          ) ;
    }

    void add_extruder(size_t pos)
    {
        extruders.insert(extruders.begin() + pos+1, size_t(0));
    }

    void delete_extruder(size_t pos)
    {            
        if (extruders.size() == 1)
            return;// last item can't be deleted
        extruders.erase(extruders.begin() + pos);
    }
};

class Control : public wxControl
{
public:
    Control(
        wxWindow *parent,
        wxWindowID id,
        int lowerValue,
        int higherValue,
        int minValue,
        int maxValue,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxSL_VERTICAL,
        const wxValidator& val = wxDefaultValidator,
        const wxString& name = wxEmptyString);
    ~Control() {}

    void    msw_rescale();

    int     GetMinValue() const { return m_min_value; }
    int     GetMaxValue() const { return m_max_value; }
    double  GetMinValueD() { return m_values.empty() ? 0. : m_values[m_min_value]; }
    double  GetMaxValueD() { return m_values.empty() ? 0. : m_values[m_max_value]; }
    int     GetLowerValue()  const { return m_lower_value; }
    int     GetHigherValue() const { return m_higher_value; }
    int     GetActiveValue() const;
    double  GetLowerValueD()  { return get_double_value(ssLower); }
    double  GetHigherValueD() { return get_double_value(ssHigher); }
    wxSize  DoGetBestSize() const override;
    wxSize  get_min_size()  const ;

    // Set low and high slider position. If the span is non-empty, disable the "one layer" mode.
    void    SetLowerValue (const int lower_val);
    void    SetHigherValue(const int higher_val);
    void    SetSelectionSpan(const int lower_val, const int higher_val);

    void    SetMaxValue(const int max_value);
    void    SetKoefForLabels(const double koef)                { m_label_koef = koef; }
    void    SetSliderValues(const std::vector<double>& values) { m_values = values; }
    void    ChangeOneLayerLock();

    CustomGCode::Info   GetTicksValues() const;
    void                SetTicksValues(const Slic3r::CustomGCode::Info &custom_gcode_per_print_z);

    void    EnableTickManipulation(bool enable = true) { m_is_enabled_tick_manipulation = enable; }
    void    DisableTickManipulation()                  { EnableTickManipulation(false); }

    void    SetManipulationMode(t_mode mode)    { m_mode = mode; }
    t_mode  GetManipulationMode() const         { return m_mode; }
    void    SetModeAndOnlyExtruder(const bool is_one_extruder_printed_model, const int only_extruder)
    {
        m_mode = !is_one_extruder_printed_model ? t_mode::MultiExtruder :
                 only_extruder < 0              ? t_mode::SingleExtruder :
                                                  t_mode::MultiAsSingle;
        m_only_extruder = only_extruder;
    }

    bool is_horizontal() const      { return m_style == wxSL_HORIZONTAL; }
    bool is_one_layer() const       { return m_is_one_layer; }
    bool is_lower_at_min() const    { return m_lower_value == m_min_value; }
    bool is_higher_at_max() const   { return m_higher_value == m_max_value; }
    bool is_full_span() const       { return this->is_lower_at_min() && this->is_higher_at_max(); }

    void OnPaint(wxPaintEvent& ) { render();}
    void OnLeftDown(wxMouseEvent& event);
    void OnMotion(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnEnterWin(wxMouseEvent& event) { enter_window(event, true); }
    void OnLeaveWin(wxMouseEvent& event) { enter_window(event, false); }
    void OnWheel(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent &event);
    void OnKeyUp(wxKeyEvent &event);
    void OnChar(wxKeyEvent &event);
    void OnRightDown(wxMouseEvent& event);
    void OnRightUp(wxMouseEvent& event);

    void add_code_as_tick(std::string code, int selected_extruder = -1);
    // add default action for tick, when press "+"
    void add_current_tick(bool call_from_keyboard = false);
    // delete current tick, when press "-"
    void delete_current_tick();
    void edit_tick();
    void edit_extruder_sequence();

    ExtrudersSequence m_extruders_sequence;

protected:

    void    render();
    void    draw_focus_rect();
    void    draw_action_icon(wxDC& dc, const wxPoint pt_beg, const wxPoint pt_end);
    void    draw_scroll_line(wxDC& dc, const int lower_pos, const int higher_pos);
    void    draw_thumb(wxDC& dc, const wxCoord& pos_coord, const SelectedSlider& selection);
    void    draw_thumbs(wxDC& dc, const wxCoord& lower_pos, const wxCoord& higher_pos);
    void    draw_ticks(wxDC& dc);
    void    draw_colored_band(wxDC& dc);
    void    draw_one_layer_icon(wxDC& dc);
    void    draw_revert_icon(wxDC& dc);
    void    draw_cog_icon(wxDC &dc);
    void    draw_thumb_item(wxDC& dc, const wxPoint& pos, const SelectedSlider& selection);
    void    draw_info_line_with_icon(wxDC& dc, const wxPoint& pos, SelectedSlider selection);
    void    draw_thumb_text(wxDC& dc, const wxPoint& pos, const SelectedSlider& selection) const;

    void    update_thumb_rect(const wxCoord& begin_x, const wxCoord& begin_y, const SelectedSlider& selection);
    void    detect_selected_slider(const wxPoint& pt);
    void    correct_lower_value();
    void    correct_higher_value();
    void    move_current_thumb(const bool condition);
    void    enter_window(wxMouseEvent& event, const bool enter);

private:

    bool    is_point_in_rect(const wxPoint& pt, const wxRect& rect);
    int     get_tick_near_point(const wxPoint& pt);

    double      get_scroll_step();
    wxString    get_label(const SelectedSlider& selection) const;
    void        get_lower_and_higher_position(int& lower_pos, int& higher_pos);
    int         get_value_from_position(const wxCoord x, const wxCoord y);
    wxCoord     get_position_from_value(const int value);
    wxSize      get_size();
    void        get_size(int *w, int *h);
    double      get_double_value(const SelectedSlider& selection);
    wxString    get_tooltip(IconFocus icon_focus);

    std::string get_color_for_tool_change_tick(std::set<TickCode>::const_iterator it) const;
    std::string get_color_for_color_change_tick(std::set<TickCode>::const_iterator it) const;

    // Get active extruders for tick. 
    // Means one current extruder for not existing tick OR 
    // 2 extruders - for existing tick (extruder before ToolChangeCode and extruder of current existing tick)
    // Use those values to disable selection of active extruders
    std::array<int, 2> get_active_extruders_for_tick(int tick) const;

    void    post_ticks_changed_event(const std::string& gcode = "");
    bool    check_ticks_changed_event(const std::string& gcode);

    void    append_change_extruder_menu_item (wxMenu*, bool switch_current_code = false);
    void    append_add_color_change_menu_item(wxMenu*, bool switch_current_code = false);

    bool        is_osx { false };
    wxFont      m_font;
    int         m_min_value;
    int         m_max_value;
    int         m_lower_value;
    int         m_higher_value;
    ScalableBitmap    m_bmp_thumb_higher;
    ScalableBitmap    m_bmp_thumb_lower;
    ScalableBitmap    m_bmp_add_tick_on;
    ScalableBitmap    m_bmp_add_tick_off;
    ScalableBitmap    m_bmp_del_tick_on;
    ScalableBitmap    m_bmp_del_tick_off;
    ScalableBitmap    m_bmp_one_layer_lock_on;
    ScalableBitmap    m_bmp_one_layer_lock_off;
    ScalableBitmap    m_bmp_one_layer_unlock_on;
    ScalableBitmap    m_bmp_one_layer_unlock_off;
    ScalableBitmap    m_bmp_revert;
    ScalableBitmap    m_bmp_cog;
    SelectedSlider    m_selection;
    bool        m_is_left_down = false;
    bool        m_is_right_down = false;
    bool        m_is_one_layer = false;
    bool        m_is_focused = false;
    bool        m_is_action_icon_focesed = false;
    bool        m_is_one_layer_icon_focesed = false;
    bool        m_is_enabled_tick_manipulation = true;
    bool        m_show_context_menu = false;
    bool        m_show_edit_menu = false;
    bool        m_force_edit_extruder_sequence = false;
    bool        m_force_mode_apply = true;
    bool        m_force_add_tick    = false;
    bool        m_force_delete_tick = false;
    t_mode      m_mode = t_mode::SingleExtruder;
    int         m_only_extruder = -1;

    wxRect      m_rect_lower_thumb;
    wxRect      m_rect_higher_thumb;
    wxRect      m_rect_tick_action;
    wxRect      m_rect_one_layer_icon;
    wxRect      m_rect_revert_icon;
    wxRect      m_rect_cog_icon;
    wxSize      m_thumb_size;
    int         m_tick_icon_dim;
    int         m_lock_icon_dim;
    int         m_revert_icon_dim;
    int         m_cog_icon_dim;
    long        m_style;
    float       m_label_koef = 1.0;

    std::vector<double> m_values;
    TickCodeInfo        m_ticks;

// control's view variables
    wxCoord SLIDER_MARGIN; // margin around slider

    wxPen   DARK_ORANGE_PEN;
    wxPen   ORANGE_PEN;
    wxPen   LIGHT_ORANGE_PEN;

    wxPen   DARK_GREY_PEN;
    wxPen   GREY_PEN;
    wxPen   LIGHT_GREY_PEN;

    std::vector<wxPen*> m_line_pens;
    std::vector<wxPen*> m_segm_pens;
};

} // DoubleSlider;

} // Slic3r



#endif // slic3r_GUI_DoubleSlider_hpp_
