#ifndef slic3r_GUI_DoubleSlider_hpp_
#define slic3r_GUI_DoubleSlider_hpp_

#include "libslic3r/CustomGCode.hpp"
#include "wxExtensions.hpp"

#include <wx/window.h>
#include <wx/control.h>
#include <wx/dc.h>
#include <wx/slider.h>

#include <vector>
#include <set>

class wxMenu;

namespace Slic3r {

using namespace CustomGCode;

namespace DoubleSlider {

/* For exporting GCode in GCodeWriter is used XYZF_NUM(val) = PRECISION(val, 3) for XYZ values. 
 * So, let use same value as a permissible error for layer height.
 */
constexpr double epsilon() { return 0.0011; }

// custom message the slider sends to its parent to notify a tick-change:
wxDECLARE_EVENT(wxCUSTOMEVT_TICKSCHANGED, wxEvent);

enum SelectedSlider {
    ssUndef,
    ssLower,
    ssHigher
};

enum FocusedItem {
    fiNone,
    fiRevertIcon,
    fiOneLayerIcon,
    fiCogIcon,
    fiColorBand,
    fiActionIcon,
    fiLowerThumb,
    fiHigherThumb,
    fiSmartWipeTower,
    fiTick
};

enum ConflictType
{
    ctNone,
    ctModeConflict,
    ctMeaninglessColorChange,
    ctMeaninglessToolChange,
    ctRedundant
};

enum MouseAction
{
    maNone,
    maAddMenu,                  // show "Add"  context menu for NOTexist active tick
    maEditMenu,                 // show "Edit" context menu for exist active tick
    maCogIconMenu,              // show context for "cog" icon
    maForceColorEdit,           // force color editing from colored band
    maAddTick,                  // force tick adding
    maDeleteTick,               // force tick deleting
    maCogIconClick,             // LeftMouseClick on "cog" icon
    maOneLayerIconClick,        // LeftMouseClick on "one_layer" icon
    maRevertIconClick,          // LeftMouseClick on "revert" icon
};

enum DrawMode
{
    dmRegular,
    dmSlaPrint,
    dmSequentialFffPrint,
    dmSequentialGCodeView,
};

enum LabelType
{
    ltHeightWithLayer,
    ltHeight,
    ltEstimatedTime,
};

struct TickCode
{
    bool operator<(const TickCode& other) const { return other.tick > this->tick; }
    bool operator>(const TickCode& other) const { return other.tick < this->tick; }

    int         tick = 0;
    Type        type = ColorChange;
    int         extruder = 0;
    std::string color;
    std::string extra;
};

class TickCodeInfo
{
    std::string custom_gcode;
    std::string pause_print_msg;
    bool        m_suppress_plus     = false;
    bool        m_suppress_minus    = false;
    bool        m_use_default_colors= false;
    int         m_default_color_idx = 0;

    std::vector<std::string>* m_colors {nullptr};

    std::string get_color_for_tick(TickCode tick, Type type, const int extruder);

public:
    std::set<TickCode>  ticks {};
    Mode                mode = Undef;

    bool empty() const { return ticks.empty(); }
    void set_pause_print_msg(const std::string& message) { pause_print_msg = message; }

    bool add_tick(const int tick, Type type, int extruder, double print_z);
    bool edit_tick(std::set<TickCode>::iterator it, double print_z);
    void switch_code(Type type_from, Type type_to);
    bool switch_code_for_tick(std::set<TickCode>::iterator it, Type type_to, const int extruder);
    void erase_all_ticks_with_code(Type type);

    bool            has_tick_with_code(Type type);
    bool            has_tick(int tick);
    ConflictType    is_conflict_tick(const TickCode& tick, Mode out_mode, int only_extruder, double print_z);

    // Get used extruders for tick.
    // Means all extruders(tools) which will be used during printing from current tick to the end
    std::set<int>   get_used_extruders_for_tick(int tick, int only_extruder, double print_z, Mode force_mode = Undef) const;

    void suppress_plus (bool suppress) { m_suppress_plus = suppress; }
    void suppress_minus(bool suppress) { m_suppress_minus = suppress; }
    bool suppressed_plus () { return m_suppress_plus; }
    bool suppressed_minus() { return m_suppress_minus; }
    void set_default_colors(bool default_colors_on)  { m_use_default_colors = default_colors_on; }

    void set_extruder_colors(std::vector<std::string>* extruder_colors) { m_colors = extruder_colors; }
};


struct ExtrudersSequence
{
    bool            is_mm_intervals     = true;
    double          interval_by_mm      = 3.0;
    int             interval_by_layers  = 10;
    bool            random_sequence     { false };
    bool            color_repetition    { false };
    std::vector<size_t>  extruders      = { 0 };

    bool operator==(const ExtrudersSequence& other) const
    {
        return  (other.is_mm_intervals      == this->is_mm_intervals    ) &&
                (other.interval_by_mm       == this->interval_by_mm     ) &&
                (other.interval_by_layers   == this->interval_by_layers ) &&
                (other.random_sequence      == this->random_sequence    ) &&
                (other.color_repetition     == this->color_repetition   ) &&
                (other.extruders            == this->extruders          ) ;
    }
    bool operator!=(const ExtrudersSequence& other) const
    {
        return  (other.is_mm_intervals      != this->is_mm_intervals    ) ||
                (other.interval_by_mm       != this->interval_by_mm     ) ||
                (other.interval_by_layers   != this->interval_by_layers ) ||
                (other.random_sequence      != this->random_sequence    ) ||
                (other.color_repetition     != this->color_repetition   ) ||
                (other.extruders            != this->extruders          ) ;
    }

    void add_extruder(size_t pos, size_t extruder_id = size_t(0))
    {
        extruders.insert(extruders.begin() + pos+1, extruder_id);
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
    void    sys_color_changed();

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
    void    SetSliderValues(const std::vector<double>& values);
    void    ChangeOneLayerLock();
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
    void    SetSliderAlternateValues(const std::vector<double>& values) { m_alternate_values = values; }
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER

    Info    GetTicksValues() const;
    void    SetTicksValues(const Info &custom_gcode_per_print_z);
    void    SetLayersTimes(const std::vector<float>& layers_times, float total_time);
    void    SetLayersTimes(const std::vector<double>& layers_times);

    void    SetDrawMode(bool is_sla_print, bool is_sequential_print);
    void    SetDrawMode(DrawMode mode) { m_draw_mode = mode; }

    void    SetManipulationMode(Mode mode)  { m_mode = mode; }
    Mode    GetManipulationMode() const     { return m_mode; }
    void    SetModeAndOnlyExtruder(const bool is_one_extruder_printed_model, const int only_extruder);
    void    SetExtruderColors(const std::vector<std::string>& extruder_colors);

    bool    IsNewPrint();

    void set_render_as_disabled(bool value) { m_render_as_disabled = value; }
    bool is_rendering_as_disabled() const { return m_render_as_disabled; }

    bool is_horizontal() const      { return m_style == wxSL_HORIZONTAL; }
    bool is_one_layer() const       { return m_is_one_layer; }
    bool is_lower_at_min() const    { return m_lower_value == m_min_value; }
    bool is_higher_at_max() const   { return m_higher_value == m_max_value; }
    bool is_full_span() const       { return this->is_lower_at_min() && this->is_higher_at_max(); }

    void OnPaint(wxPaintEvent& ) { render(); }
    void OnLeftDown(wxMouseEvent& event);
    void OnMotion(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnEnterWin(wxMouseEvent& event) { enter_window(event, true); }
    void OnLeaveWin(wxMouseEvent& event) { enter_window(event, false); }
    void UseDefaultColors(bool def_colors_on)   { m_ticks.set_default_colors(def_colors_on); }
    void OnWheel(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent &event);
    void OnKeyUp(wxKeyEvent &event);
    void OnChar(wxKeyEvent &event);
    void OnRightDown(wxMouseEvent& event);
    void OnRightUp(wxMouseEvent& event);

    void add_code_as_tick(Type type, int selected_extruder = -1);
    // add default action for tick, when press "+"
    void add_current_tick(bool call_from_keyboard = false);
    // delete current tick, when press "-"
    void delete_current_tick();
    void edit_tick(int tick = -1);
    void switch_one_layer_mode();
    void discard_all_thicks();
    void move_current_thumb_to_pos(wxPoint pos);
    void edit_extruder_sequence();
    void jump_to_value();
    void enable_action_icon(bool enable) { m_enable_action_icon = enable; }
    void show_add_context_menu();
    void show_edit_context_menu();
    void show_cog_icon_context_menu();
    void auto_color_change();

    ExtrudersSequence m_extruders_sequence;

protected:

    void    render();
    void    draw_focus_rect();
    void    draw_action_icon(wxDC& dc, const wxPoint pt_beg, const wxPoint pt_end);
    void    draw_scroll_line(wxDC& dc, const int lower_pos, const int higher_pos);
    void    draw_thumb(wxDC& dc, const wxCoord& pos_coord, const SelectedSlider& selection);
    void    draw_thumbs(wxDC& dc, const wxCoord& lower_pos, const wxCoord& higher_pos);
    void    draw_ticks_pair(wxDC& dc, wxCoord pos, wxCoord mid, int tick_len);
    void    draw_ticks(wxDC& dc);
    void    draw_colored_band(wxDC& dc);
    void    draw_ruler(wxDC& dc);
    void    draw_one_layer_icon(wxDC& dc);
    void    draw_revert_icon(wxDC& dc);
    void    draw_cog_icon(wxDC &dc);
    void    draw_thumb_item(wxDC& dc, const wxPoint& pos, const SelectedSlider& selection);
    void    draw_info_line_with_icon(wxDC& dc, const wxPoint& pos, SelectedSlider selection);
    void    draw_tick_on_mouse_position(wxDC &dc);
    void    draw_tick_text(wxDC& dc, const wxPoint& pos, int tick, LabelType label_type = ltHeight, bool right_side = true) const;
    void    draw_thumb_text(wxDC& dc, const wxPoint& pos, const SelectedSlider& selection) const;

    void    update_thumb_rect(const wxCoord begin_x, const wxCoord begin_y, const SelectedSlider& selection);
    bool    is_lower_thumb_editable();
    bool    detect_selected_slider(const wxPoint& pt);
    void    correct_lower_value();
    void    correct_higher_value();
    void    move_current_thumb(const bool condition);
    void    enter_window(wxMouseEvent& event, const bool enter);
    bool    is_wipe_tower_layer(int tick) const;

private:

    bool    is_point_in_rect(const wxPoint& pt, const wxRect& rect);
    int     get_tick_near_point(const wxPoint& pt);

    double      get_scroll_step();
    wxString    get_label(int tick, LabelType label_type = ltHeightWithLayer) const;
    void        get_lower_and_higher_position(int& lower_pos, int& higher_pos);
    int         get_value_from_position(const wxCoord x, const wxCoord y);
    int         get_value_from_position(const wxPoint pos) { return get_value_from_position(pos.x, pos.y); }
    wxCoord     get_position_from_value(const int value);
    wxSize      get_size() const;
    void        get_size(int* w, int* h) const;
    double      get_double_value(const SelectedSlider& selection);
    int         get_tick_from_value(double value, bool force_lower_bound = false);
    wxString    get_tooltip(int tick = -1);
    int         get_edited_tick_for_position(wxPoint pos, Type type = ColorChange);

    std::string get_color_for_tool_change_tick(std::set<TickCode>::const_iterator it) const;
    std::string get_color_for_color_change_tick(std::set<TickCode>::const_iterator it) const;
    wxRect      get_colored_band_rect();

    // Get active extruders for tick. 
    // Means one current extruder for not existing tick OR 
    // 2 extruders - for existing tick (extruder before ToolChangeCode and extruder of current existing tick)
    // Use those values to disable selection of active extruders
    std::array<int, 2> get_active_extruders_for_tick(int tick) const;

    void    post_ticks_changed_event(Type type = Custom);
    bool    check_ticks_changed_event(Type type);

    void    append_change_extruder_menu_item (wxMenu*, bool switch_current_code = false);
    void    append_add_color_change_menu_item(wxMenu*, bool switch_current_code = false);

    bool        is_osx { false };
    wxFont      m_font;
    int         m_min_value;
    int         m_max_value;
    int         m_lower_value;
    int         m_higher_value;

    bool        m_render_as_disabled{ false };

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
    bool        m_force_mode_apply = true;
    bool        m_enable_action_icon = true;
    bool        m_is_wipe_tower = false; //This flag indicates that there is multiple extruder print with wipe tower

    DrawMode    m_draw_mode = dmRegular;

    Mode        m_mode = SingleExtruder;
    int         m_only_extruder = -1;

    MouseAction m_mouse = maNone;
    FocusedItem m_focus = fiNone;
    wxPoint     m_moving_pos = wxDefaultPosition;

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
    long        m_extra_style;
    float       m_label_koef{ 1.0 };

    std::vector<double> m_values;
    TickCodeInfo        m_ticks;
    std::vector<double> m_layers_times;
    std::vector<double> m_layers_values;
    std::vector<std::string>    m_extruder_colors;
    std::string         m_print_obj_idxs;

#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
    std::vector<double> m_alternate_values;
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER

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

    struct Ruler {
        double long_step;
        double short_step;
        std::vector<double> max_values;// max value for each object/instance in sequence print
                                       // > 1 for sequential print

        void init(const std::vector<double>& values);
        void update(wxWindow* win, const std::vector<double>& values, double scroll_step);
        bool is_ok() { return long_step > 0 && short_step > 0; }
        size_t count() { return max_values.size(); }
    } m_ruler;
};

} // DoubleSlider;

} // Slic3r



#endif // slic3r_GUI_DoubleSlider_hpp_
