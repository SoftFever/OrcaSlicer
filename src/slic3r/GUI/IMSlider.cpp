#include "libslic3r/libslic3r.h"
#include "IMSlider.hpp"
#include "libslic3r/GCode.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "I18N.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/AppConfig.hpp"
#include "GUI_Utils.hpp"
#include "MsgDialog.hpp"
#include "Tab.hpp"
#include "GUI_ObjectList.hpp"

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
#include <boost/algorithm/string/split.hpp>
#include "Field.hpp"
#include "format.hpp"
#include "NotificationManager.hpp"
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>

namespace Slic3r {

using GUI::from_u8;
using GUI::into_u8;
using GUI::format_wxstr;

namespace GUI {

constexpr double min_delta_area = scale_(scale_(25));  // equal to 25 mm2
constexpr double miscalculation = scale_(scale_(1));   // equal to 1 mm2

//static const ImVec2 MIN_RECT_SIZE  = ImVec2(81, 52);
//static const float TOP_MARGIN     =  3.0f;
static const float LEFT_MARGIN    = 13.0f + 100.0f;  // avoid thumbnail toolbar
static const float SLIDER_LENGTH  = 680.0f;
static const float  TEXT_WIDTH_DUMMY = 63.0f;
static const float  ONE_LAYER_MARGIN = 10.0f;
static const ImVec2 ONE_LAYER_OFFSET  = ImVec2(41.0f, 44.0f);
static const ImVec2 HORIZONTAL_SLIDER_SIZE = ImVec2(764.0f, 90.0f);//764 = 680 + handle_dummy_width * 2 + text_right_dummy
static const ImVec2 VERTICAL_SLIDER_SIZE = ImVec2(105.0f, 748.0f);//748 = 680 + text_dummy_height * 2

    int m_tick_value = -1;
    ImVec4 m_tick_rect;

bool equivalent_areas(const double& bottom_area, const double& top_area)
{
    return fabs(bottom_area - top_area) <= miscalculation;
}

bool check_color_change(PrintObject *object, size_t frst_layer_id, size_t layers_cnt, bool check_overhangs, std::function<bool(Layer *)> break_condition)
{
    double prev_area = area(object->get_layer(frst_layer_id)->lslices);

    bool detected = false;
    for (size_t i = frst_layer_id + 1; i < layers_cnt; i++) {
        Layer *layer    = object->get_layer(i);
        double cur_area = area(layer->lslices);

        // check for overhangs
        if (check_overhangs && cur_area > prev_area && !equivalent_areas(prev_area, cur_area)) break;

        // Check percent of the area decrease.
        // This value have to be more than min_delta_area and more then 10%
        if ((prev_area - cur_area > min_delta_area) && (cur_area / prev_area < 0.9)) {
            detected = true;
            if (break_condition(layer)) break;
        }

        prev_area = cur_area;
    }
    return detected;
}


static std::string gcode(Type type)
{
    Slic3r::DynamicPrintConfig config = wxGetApp().preset_bundle->full_config();
    switch (type) {
    //BBS
    case Template:    return config.opt_string("template_custom_gcode");
    default:          return "";
    }

    //const PrintConfig& config = GUI::wxGetApp().plater()->fff_print().config();
    //switch (type) {
    ////BBS
    ////case ColorChange: return config.color_change_gcode;
    //case PausePrint:  return config.machine_pause_gcode;
    //case Template:    return config.template_custom_gcode;
    //default:          return "";
    //}
}

static std::string short_and_splitted_time(const std::string &time)
{
    // Parse the dhms time format.
    int days    = 0;
    int hours   = 0;
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
    } else if (minutes > 0) {
        if (minutes > 10 && seconds > 10)
            ::sprintf(buffer, "%dm\n%ds", minutes, seconds);
        else
            ::sprintf(buffer, "%dm%ds", minutes, seconds);
    } else
        ::sprintf(buffer, "%ds", seconds);
    return std::string(buffer);
}


std::string TickCodeInfo::get_color_for_tick(TickCode tick, Type type, const int extruder)
{
    if (mode == SingleExtruder && type == ColorChange && m_use_default_colors) {
#if 1
        if (ticks.empty()) return color_generator.get_opposite_color((*m_colors)[0]);

        auto before_tick_it = std::lower_bound(ticks.begin(), ticks.end(), tick);
        if (before_tick_it == ticks.end()) {
            while (before_tick_it != ticks.begin())
                if (--before_tick_it; before_tick_it->type == ColorChange) break;
            if (before_tick_it->type == ColorChange) return color_generator.get_opposite_color(before_tick_it->color);
            return color_generator.get_opposite_color((*m_colors)[0]);
        }

        if (before_tick_it == ticks.begin()) {
            const std::string &frst_color = (*m_colors)[0];
            if (before_tick_it->type == ColorChange) return color_generator.get_opposite_color(frst_color, before_tick_it->color);

            auto next_tick_it = before_tick_it;
            while (next_tick_it != ticks.end())
                if (++next_tick_it; next_tick_it->type == ColorChange) break;
            if (next_tick_it->type == ColorChange) return color_generator.get_opposite_color(frst_color, next_tick_it->color);

            return color_generator.get_opposite_color(frst_color);
        }

        std::string frst_color = "";
        if (before_tick_it->type == ColorChange)
            frst_color = before_tick_it->color;
        else {
            auto next_tick_it = before_tick_it;
            while (next_tick_it != ticks.end())
                if (++next_tick_it; next_tick_it->type == ColorChange) {
                    frst_color = next_tick_it->color;
                    break;
                }
        }

        while (before_tick_it != ticks.begin())
            if (--before_tick_it; before_tick_it->type == ColorChange) break;

        if (before_tick_it->type == ColorChange) {
            if (frst_color.empty()) return color_generator.get_opposite_color(before_tick_it->color);
            return color_generator.get_opposite_color(before_tick_it->color, frst_color);
        }

        if (frst_color.empty()) return color_generator.get_opposite_color((*m_colors)[0]);
        return color_generator.get_opposite_color((*m_colors)[0], frst_color);
#else
        const std::vector<std::string> &colors = ColorPrintColors::get();
        if (ticks.empty()) return colors[0];
        m_default_color_idx++;

        return colors[m_default_color_idx % colors.size()];
#endif
    }

    std::string color = (*m_colors)[extruder - 1];

    if (type == ColorChange) {
        if (!ticks.empty()) {
            auto before_tick_it = std::lower_bound(ticks.begin(), ticks.end(), tick);
            while (before_tick_it != ticks.begin()) {
                --before_tick_it;
                if (before_tick_it->type == ColorChange && before_tick_it->extruder == extruder) {
                    color = before_tick_it->color;
                    break;
                }
            }
        }

        //TODO
        //color = get_new_color(color);
    }
    return color;
}


bool TickCodeInfo::add_tick(const int tick, Type type, const int extruder, double print_z)
{
    std::string color;
    std::string extra;
    if (type == Custom) // custom Gcode
    {
        //extra = get_custom_code(custom_gcode, print_z);
        //if (extra.empty()) return false;
        //custom_gcode = extra;
    } else if (type == PausePrint) {
        //BBS do not set pause extra message
        //extra = get_pause_print_msg(pause_print_msg, print_z);
        //if (extra.empty()) return false;
        pause_print_msg = extra;
    }
    else {
        color = get_color_for_tick(TickCode{ tick }, type, extruder);
        if (color.empty()) return false;
    }

    if (mode == SingleExtruder) m_use_default_colors = true;

    ticks.emplace(TickCode{tick, type, extruder, color, extra});

    return true;
}

bool TickCodeInfo::edit_tick(std::set<TickCode>::iterator it, double print_z)
{
    std::string edited_value;
    //TODO
    /* BBS
    if (it->type == ColorChange)
        edited_value = get_new_color(it->color);
    else if (it->type == PausePrint)
        edited_value = get_pause_print_msg(it->extra, print_z);
    else
        edited_value = get_custom_code(it->type == Template ? gcode(Template) : it->extra, print_z);
    */
    if (edited_value.empty()) return false;

    TickCode changed_tick = *it;
    if (it->type == ColorChange) {
        if (it->color == edited_value) return false;
        changed_tick.color = edited_value;
    } else if (it->type == Template) {
        //if (gcode(Template) == edited_value) return false;
        //changed_tick.extra = edited_value;
        //changed_tick.type  = Custom;
        ;
    } else if (it->type == Custom || it->type == PausePrint) {
        if (it->extra == edited_value) return false;
        changed_tick.extra = edited_value;
    }

    ticks.erase(it);
    ticks.emplace(changed_tick);

    return true;
}

void TickCodeInfo::switch_code(Type type_from, Type type_to)
{
    for (auto it{ticks.begin()}, end{ticks.end()}; it != end;)
        if (it->type == type_from) {
            TickCode tick = *it;
            tick.type     = type_to;
            tick.extruder = 1;
            ticks.erase(it);
            it = ticks.emplace(tick).first;
        } else
            ++it;
}

bool TickCodeInfo::switch_code_for_tick(std::set<TickCode>::iterator it, Type type_to, const int extruder)
{
    const std::string color = get_color_for_tick(*it, type_to, extruder);
    if (color.empty()) return false;

    TickCode changed_tick = *it;
    changed_tick.type     = type_to;
    changed_tick.extruder = extruder;
    changed_tick.color    = color;

    ticks.erase(it);
    ticks.emplace(changed_tick);

    return true;
}

void TickCodeInfo::erase_all_ticks_with_code(Type type)
{
    for (auto it{ticks.begin()}, end{ticks.end()}; it != end;) {
        if (it->type == type)
            it = ticks.erase(it);
        else
            ++it;
    }
}

bool TickCodeInfo::has_tick_with_code(Type type)
{
    for (const TickCode &tick : ticks)
        if (tick.type == type) return true;

    return false;
}

bool TickCodeInfo::has_tick(int tick) { return ticks.find(TickCode{tick}) != ticks.end(); }

ConflictType TickCodeInfo::is_conflict_tick(const TickCode &tick, Mode out_mode, int only_extruder, double print_z)
{
    if ((tick.type == ColorChange && ((mode == SingleExtruder && out_mode == MultiExtruder) || (mode == MultiExtruder && out_mode == SingleExtruder))) ||
        (tick.type == ToolChange && (mode == MultiAsSingle && out_mode != MultiAsSingle)))
        return ctModeConflict;

    // check ColorChange tick
    if (tick.type == ColorChange) {
        // We should mark a tick as a "MeaninglessColorChange",
        // if it has a ColorChange for unused extruder from current print to end of the print
        std::set<int> used_extruders_for_tick = get_used_extruders_for_tick(tick.tick, only_extruder, print_z, out_mode);

        if (used_extruders_for_tick.find(tick.extruder) == used_extruders_for_tick.end()) return ctMeaninglessColorChange;

        // We should mark a tick as a "Redundant",
        // if it has a ColorChange for extruder that has not been used before
        if (mode == MultiAsSingle && tick.extruder != std::max<int>(only_extruder, 1)) {
            auto it = ticks.lower_bound(tick);
            if (it == ticks.begin() && it->type == ToolChange && tick.extruder == it->extruder) return ctNone;

            while (it != ticks.begin()) {
                --it;
                if (it->type == ToolChange && tick.extruder == it->extruder) return ctNone;
            }

            return ctRedundant;
        }
    }

    // check ToolChange tick
    if (mode == MultiAsSingle && tick.type == ToolChange) {
        // We should mark a tick as a "MeaninglessToolChange",
        // if it has a ToolChange to the same extruder
        auto it = ticks.find(tick);
        if (it == ticks.begin()) return tick.extruder == std::max<int>(only_extruder, 1) ? ctMeaninglessToolChange : ctNone;

        while (it != ticks.begin()) {
            --it;
            if (it->type == ToolChange) return tick.extruder == it->extruder ? ctMeaninglessToolChange : ctNone;
        }
    }

    return ctNone;
}

// Get used extruders for tick.
// Means all extruders(tools) which will be used during printing from current tick to the end
std::set<int> TickCodeInfo::get_used_extruders_for_tick(int tick, int only_extruder, double print_z, Mode force_mode /* = Undef*/) const
{
    Mode e_mode = !force_mode ? mode : force_mode;

    if (e_mode == MultiExtruder) {
        // #ys_FIXME: get tool ordering from _correct_ place
        const ToolOrdering &tool_ordering = GUI::wxGetApp().plater()->fff_print().get_tool_ordering();

        if (tool_ordering.empty()) return {};

        std::set<int> used_extruders;

        auto it_layer_tools = std::lower_bound(tool_ordering.begin(), tool_ordering.end(), LayerTools(print_z));
        for (; it_layer_tools != tool_ordering.end(); ++it_layer_tools) {
            const std::vector<unsigned> &extruders = it_layer_tools->extruders;
            for (const auto &extruder : extruders) used_extruders.emplace(extruder + 1);
        }

        return used_extruders;
    }

    const int default_initial_extruder = e_mode == MultiAsSingle ? std::max(only_extruder, 1) : 1;
    if (ticks.empty() || e_mode == SingleExtruder) return {default_initial_extruder};

    std::set<int> used_extruders;

    auto it_start = ticks.lower_bound(TickCode{tick});
    auto it       = it_start;
    if (it == ticks.begin() && it->type == ToolChange && tick != it->tick) // In case of switch of ToolChange to ColorChange, when tick exists,
                                                                           // we shouldn't change color for extruder, which will be deleted
    {
        used_extruders.emplace(it->extruder);
        if (tick < it->tick) used_extruders.emplace(default_initial_extruder);
    }

    while (it != ticks.begin()) {
        --it;
        if (it->type == ToolChange && tick != it->tick) {
            used_extruders.emplace(it->extruder);
            break;
        }
    }

    if (it == ticks.begin() && used_extruders.empty()) used_extruders.emplace(default_initial_extruder);

    for (it = it_start; it != ticks.end(); ++it)
        if (it->type == ToolChange && tick != it->tick) used_extruders.emplace(it->extruder);

    return used_extruders;
}

IMSlider::IMSlider(int lowerValue, int higherValue, int minValue, int maxValue, long style)
{
    m_lower_value  = lowerValue;
    m_higher_value = higherValue;
    m_min_value    = minValue;
    m_max_value    = maxValue;
    m_style        = style == wxSL_HORIZONTAL || style == wxSL_VERTICAL ? style : wxSL_HORIZONTAL;
    // BBS set to none style by default
    m_extra_style = style == wxSL_VERTICAL ? 0 : 0;
    m_selection   = ssUndef;
    m_is_need_post_tick_changed_event = false;
    m_tick_change_event_type = Type::Unknown;

    m_ticks.set_extruder_colors(&m_extruder_colors);
}

bool IMSlider::init_texture()
{
    bool result = true;
    if (!is_horizontal()) {
        // BBS init image texture id
        result &= IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/one_layer_on.svg", 24, 24, m_one_layer_on_id);
        result &= IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/one_layer_on_hover.svg", 28, 28, m_one_layer_on_hover_id);
        result &= IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/one_layer_off.svg", 28, 28, m_one_layer_off_id);
        result &= IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/one_layer_off_hover.svg", 28, 28, m_one_layer_off_hover_id);
        result &= IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/one_layer_on_dark.svg", 24, 24, m_one_layer_on_dark_id);
        result &= IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/one_layer_on_hover_dark.svg", 28, 28, m_one_layer_on_hover_dark_id);
        result &= IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/one_layer_off_dark.svg", 28, 28, m_one_layer_off_dark_id);
        result &= IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/one_layer_off_hover_dark.svg", 28, 28, m_one_layer_off_hover_dark_id);
        result &= IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/im_gcode_pause.svg", 14, 14, m_pause_icon_id);
        result &= IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/im_slider_delete.svg", 14, 14, m_delete_icon_id);
    }

    return result;
}

int IMSlider::GetActiveValue() const
{
    return m_selection == ssLower ?
    m_lower_value : m_selection == ssHigher ?
                m_higher_value : -1;
}


void IMSlider::SetLowerValue(const int lower_val)
{
    m_selection   = ssLower;
    m_lower_value = lower_val;
    correct_lower_value();
    set_as_dirty();
}

void IMSlider::SetHigherValue(const int higher_val)
{
    m_selection    = ssHigher;
    m_higher_value = higher_val;
    correct_higher_value();
    set_as_dirty();
}

void IMSlider::SetSelectionSpan(const int lower_val, const int higher_val)
{
    m_lower_value  = std::max(lower_val, m_min_value);
    m_higher_value = std::max(std::min(higher_val, m_max_value), m_lower_value);
    if (m_lower_value < m_higher_value) m_is_one_layer = false;

    set_as_dirty();
}

void IMSlider::SetMaxValue(const int max_value)
{
    m_max_value = max_value;
    set_as_dirty();
}

void IMSlider::SetSliderValues(const std::vector<double> &values)
{
    m_values = values;
}

Info IMSlider::GetTicksValues() const
{
    Info                            custom_gcode_per_print_z;
    std::vector<CustomGCode::Item> &values = custom_gcode_per_print_z.gcodes;

    const int val_size = m_values.size();
    if (!m_values.empty())
        for (const TickCode &tick : m_ticks.ticks) {
            if (tick.tick > val_size) break;
            values.emplace_back(CustomGCode::Item{m_values[tick.tick], tick.type, tick.extruder, tick.color, tick.extra});
        }

    if (m_force_mode_apply) custom_gcode_per_print_z.mode = m_mode;

    return custom_gcode_per_print_z;
}

void IMSlider::SetTicksValues(const Info &custom_gcode_per_print_z)
{
    if (!m_can_change_color) {
        m_ticks.erase_all_ticks_with_code(ToolChange);
        return;
    }

    if (m_values.empty()) {
        m_ticks.mode = m_mode;
        return;
    }

    const bool was_empty = m_ticks.empty();

    m_ticks.ticks.clear();
    const std::vector<CustomGCode::Item> &heights = custom_gcode_per_print_z.gcodes;
    for (auto h : heights) {
        int tick = get_tick_from_value(h.print_z);
        if (tick >= 0) m_ticks.ticks.emplace(TickCode{tick, h.type, h.extruder, h.color, h.extra});
    }

    if (!was_empty && m_ticks.empty())
        // Switch to the "Feature type"/"Tool" from the very beginning of a new object slicing after deleting of the old one
        post_ticks_changed_event();

    // init extruder sequence in respect to the extruders count
    if (m_ticks.empty()) m_extruders_sequence.init(m_extruder_colors.size());

    if (custom_gcode_per_print_z.mode && !custom_gcode_per_print_z.gcodes.empty()) m_ticks.mode = custom_gcode_per_print_z.mode;

    set_as_dirty();
}

void IMSlider::SetLayersTimes(const std::vector<float> &layers_times, float total_time)
{
    m_layers_times.clear();
    if (layers_times.empty()) return;
    m_layers_times.resize(layers_times.size(), 0.0);
    m_layers_times[0] = layers_times[0];
    for (size_t i = 1; i < layers_times.size(); i++) m_layers_times[i] = m_layers_times[i - 1] + layers_times[i];

    // Erase duplicates values from m_values and save it to the m_layers_values
    // They will be used for show the correct estimated time for MM print, when "No sparce layer" is enabled
    if (m_is_wipe_tower && m_values.size() != m_layers_times.size()) {
        m_layers_values = m_values;
        sort(m_layers_values.begin(), m_layers_values.end());
        m_layers_values.erase(unique(m_layers_values.begin(), m_layers_values.end()), m_layers_values.end());

        // When whipe tower is used to the end of print, there is one layer which is not marked in layers_times
        // So, add this value from the total print time value
        if (m_layers_values.size() != m_layers_times.size())
            for (size_t i = m_layers_times.size(); i < m_layers_values.size(); i++) m_layers_times.push_back(total_time);
        set_as_dirty();
        set_as_dirty();
    }
}

void IMSlider::SetLayersTimes(const std::vector<double> &layers_times)
{
    m_is_wipe_tower = false;
    m_layers_times  = layers_times;
    for (size_t i = 1; i < m_layers_times.size(); i++) m_layers_times[i] += m_layers_times[i - 1];
}

void IMSlider::SetDrawMode(bool is_sequential_print)
{
    m_draw_mode = is_sequential_print   ? dmSequentialFffPrint  : 
                                          dmRegular; 
}

void IMSlider::SetModeAndOnlyExtruder(const bool is_one_extruder_printed_model, const int only_extruder, bool can_change_color)
{
    m_mode = !is_one_extruder_printed_model ? MultiExtruder : only_extruder < 0 ? SingleExtruder : MultiAsSingle;
    if (!m_ticks.mode || (m_ticks.empty() && m_ticks.mode != m_mode)) m_ticks.mode = m_mode;
    m_only_extruder = only_extruder;

    UseDefaultColors(m_mode == SingleExtruder);

    m_is_wipe_tower = m_mode != SingleExtruder;

    m_can_change_color = can_change_color;
}

void IMSlider::SetExtruderColors( const std::vector<std::string>& extruder_colors)
{
    m_extruder_colors = extruder_colors;
}

bool IMSlider::IsNewPrint()
{
    const Print &print = GUI::wxGetApp().plater()->fff_print();
    std::string  idxs;
    for (auto object : print.objects()) idxs += std::to_string(object->id().id) + "_";

    if (idxs == m_print_obj_idxs) return false;

    m_print_obj_idxs = idxs;
    return true;
}

void IMSlider::post_ticks_changed_event(Type type)
{
    m_tick_change_event_type = type;
    m_is_need_post_tick_changed_event = true;
}

void IMSlider::add_custom_gcode(std::string custom_gcode)
{
    if (m_selection == ssUndef) return;
    const int tick = m_selection == ssLower ? m_lower_value : m_higher_value;

    const auto it = m_ticks.ticks.find(TickCode{ tick });

    if (it != m_ticks.ticks.end()) {
        m_ticks.ticks.erase(it);
    }
    m_ticks.ticks.emplace(TickCode{ tick, Custom, std::max<int>(1, m_only_extruder), "", custom_gcode });

    post_ticks_changed_event(Custom);
}

void IMSlider::add_code_as_tick(Type type, int selected_extruder)
{
    if (m_selection == ssUndef) return;
    const int tick = m_selection == ssLower ? m_lower_value : m_higher_value;

    if (!check_ticks_changed_event(type)) {
        BOOST_LOG_TRIVIAL(trace) << "check ticks change event false";
        return;
    }

    if (type == ColorChange && gcode(ColorChange).empty()) GUI::wxGetApp().plater()->get_notification_manager()->push_notification(GUI::NotificationType::EmptyColorChangeCode);

    const int  extruder = selected_extruder > 0 ? selected_extruder : std::max<int>(1, m_only_extruder);
    const auto it       = m_ticks.ticks.find(TickCode{tick});

    if (it == m_ticks.ticks.end()) {
        // try to add tick
        if (!m_ticks.add_tick(tick, type, extruder, m_values[tick])) return;
    } else if (type == ToolChange || type == ColorChange) {
        // try to switch tick code to ToolChange or ColorChange accordingly
        if (!m_ticks.switch_code_for_tick(it, type, extruder)) return;
    } else
        return;

    post_ticks_changed_event(type);
}

bool IMSlider::check_ticks_changed_event(Type type)
{
    //BBL only support MultiExtruder
    if (m_ticks.mode == m_mode || (type != ColorChange && type != ToolChange) ||
        (m_ticks.mode == SingleExtruder && m_mode == MultiAsSingle) || // All ColorChanges will be applied for 1st extruder
        (m_ticks.mode == MultiExtruder && m_mode == MultiAsSingle))    // Just mark ColorChanges for all unused extruders
        return true;

    if ((m_ticks.mode == SingleExtruder && m_mode == MultiExtruder) || (m_ticks.mode == MultiExtruder && m_mode == SingleExtruder)) {
        if (!m_ticks.has_tick_with_code(ColorChange)) return true;
        /*
        wxString message = (m_ticks.mode == SingleExtruder ? _L("The last color change data was saved for a single extruder printing.") :
                                                             _L("The last color change data was saved for a multi extruder printing.")) +
                           "\n" + _L("Your current changes will delete all saved color changes.") + "\n\n\t" + _L("Are you sure you want to continue?");

        
        GUI::MessageDialog msg(this, message, _L("Notice"), wxYES_NO);
        if (msg.ShowModal() == wxID_YES) {
            m_ticks.erase_all_ticks_with_code(ColorChange);
            post_ticks_changed_event(ColorChange);
        }
        */
        return false;
    }

    return true;
}


// switch on/off one layer mode
bool IMSlider::switch_one_layer_mode()
{
    if (m_show_custom_gcode_window)
        return false;

    m_is_one_layer = !m_is_one_layer;
    if (!m_is_one_layer) {
        SetLowerValue(m_min_value);
        SetHigherValue(m_max_value);
    }
    m_selection == ssLower ? correct_lower_value() : correct_higher_value();
    if (m_selection == ssUndef) m_selection = ssHigher;
    set_as_dirty();
    return true;
}

void IMSlider::draw_background(const ImRect& groove) {
    const ImU32 bg_rect_col = wxGetApp().app_config->get("dark_color_mode") == "1" ? IM_COL32(65, 65, 71, 255) : IM_COL32(255, 255, 255, 255);
    const ImU32 groove_col = wxGetApp().app_config->get("dark_color_mode") == "1" ? IM_COL32(45, 45, 49, 255) : IM_COL32(206, 206, 206, 255);

    if (is_horizontal() || m_ticks.empty()) {
        ImVec2 groove_padding = ImVec2(2.0f, 2.0f) * m_scale;

        ImRect bg_rect = groove;
        bg_rect.Expand(groove_padding);

        // draw bg of slider
        ImGui::RenderFrame(bg_rect.Min, bg_rect.Max, bg_rect_col, false, 0.5 * bg_rect.GetWidth());
        // draw bg of scroll
        ImGui::RenderFrame(groove.Min, groove.Max, groove_col, false, 0.5 * groove.GetWidth());
    }
    else {
        ImVec2 groove_padding = ImVec2(5.0f, 7.0f) * m_scale;

        ImRect bg_rect = groove;
        bg_rect.Expand(groove_padding);

        // draw bg of slider
        ImGui::RenderFrame(bg_rect.Min, bg_rect.Max, bg_rect_col, false, bg_rect.GetWidth() * 0.5);
    }
}

bool IMSlider::horizontal_slider(const char* str_id, int* value, int v_min, int v_max, const ImVec2& pos, const ImVec2& size, float scale)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& context = *GImGui;
    const ImGuiID id = window->GetID(str_id);

    const ImRect draw_region(pos, pos + size);
    ImGui::ItemSize(draw_region);

    float  bottom_dummy        = 44.0f * m_scale;
    float  handle_dummy_width  = 17.0f * m_scale;
    float  text_right_dummy    = 50.0f * scale * m_scale;
    float  groove_y            = 8.0f * m_scale;
    float  draggable_region_y  = 19.0f * m_scale;
    float  handle_radius       = 14.0f * m_scale;
    float  handle_border       = 2.0f * m_scale;
    float  rounding            = 2.0f * m_scale;
    float  text_start_offset   = 8.0f * m_scale;
    ImVec2 text_padding        = ImVec2(5.0f, 2.0f) * m_scale;
    float  triangle_offsets[3] = {-3.5f * m_scale, 3.5f * m_scale, -6.06f * m_scale};


    const ImU32 white_bg = wxGetApp().app_config->get("dark_color_mode") == "1" ? IM_COL32(65, 65, 71, 255) : IM_COL32(255, 255, 255, 255);
    const ImU32 handle_clr = IM_COL32(0, 174, 66, 255);
    const ImU32 handle_border_clr = wxGetApp().app_config->get("dark_color_mode") == "1" ? IM_COL32(65, 65, 71, 255) : IM_COL32(248, 248, 248, 255);

    // calc groove size
    ImVec2 groove_start = ImVec2(pos.x + handle_dummy_width, pos.y + size.y - groove_y - bottom_dummy);
    ImVec2 groove_size = ImVec2(size.x - 2 * handle_dummy_width - text_right_dummy, groove_y);
    ImRect groove = ImRect(groove_start, groove_start + groove_size);

    // set active(draggable) region.
    ImRect draggable_region = ImRect(groove.Min.x, groove.GetCenter().y, groove.Max.x, groove.GetCenter().y);
    draggable_region.Expand(ImVec2(handle_radius, draggable_region_y));
    float mid_y   = draggable_region.GetCenter().y;
    bool  hovered = ImGui::ItemHoverable(draggable_region, id);
    if (hovered && context.IO.MouseDown[0]) {
        ImGui::SetActiveID(id, window);
        ImGui::SetFocusID(id, window);
        ImGui::FocusWindow(window);
    }

    // draw background
    draw_background(groove);

    // set slideable region
    ImRect slideable_region = draggable_region;
    slideable_region.Expand(ImVec2(-handle_radius, 0));

    // initialize the handle
    float  handle_pos = get_pos_from_value(v_min, v_max, *value, groove);
    ImRect handle = ImRect(handle_pos - handle_radius, mid_y - handle_radius, handle_pos + handle_radius, mid_y + handle_radius);

    // update handle position and value
    bool   value_changed = slider_behavior(id, slideable_region, (const ImS32) v_min, (const ImS32) v_max, (ImS32 *) value, &handle);
    ImVec2 handle_center = handle.GetCenter();

    // draw scroll line
    ImRect scroll_line = ImRect(ImVec2(groove.Min.x, mid_y - groove_y / 2), ImVec2(handle_center.x, mid_y + groove_y / 2));
    window->DrawList->AddRectFilled(scroll_line.Min, scroll_line.Max, handle_clr, rounding);

    // draw handle
    window->DrawList->AddCircleFilled(handle_center, handle_radius, handle_border_clr);
    window->DrawList->AddCircleFilled(handle_center, handle_radius - handle_border, handle_clr);

    // draw label
    auto text_utf8 = into_u8(std::to_string(*value));
    ImVec2 text_content_size = ImGui::CalcTextSize(text_utf8.c_str());
    ImVec2 text_size = text_content_size + text_padding * 2;
    ImVec2 text_start = ImVec2(handle_center.x + handle_radius + text_start_offset, handle_center.y - 0.5 * text_size.y);
    ImRect text_rect(text_start, text_start + text_size);
    ImGui::RenderFrame(text_rect.Min, text_rect.Max, white_bg, false, rounding);
    ImVec2 pos_1 = ImVec2(text_rect.Min.x, text_rect.GetCenter().y + triangle_offsets[0]);
    ImVec2 pos_2 = ImVec2(text_rect.Min.x, text_rect.GetCenter().y + triangle_offsets[1]);
    ImVec2 pos_3 = ImVec2(text_rect.Min.x + triangle_offsets[2], text_rect.GetCenter().y);
    window->DrawList->AddTriangleFilled(pos_1, pos_2, pos_3, white_bg);
    ImGui::RenderText(text_start + text_padding, std::to_string(*value).c_str());

    return value_changed;
}

void IMSlider::draw_colored_band(const ImRect& groove, const ImRect& slideable_region) {
    if (m_ticks.empty())
        return;

    const ImU32 blank_col = wxGetApp().app_config->get("dark_color_mode") == "1" ? IM_COL32(65, 65, 71, 255) : IM_COL32(255, 255, 255, 255);

    ImVec2 blank_padding = ImVec2(6.0f, 5.0f) * m_scale;
    float  blank_width   = 1.0f * m_scale;

    ImRect blank_rect = ImRect(groove.GetCenter().x - blank_width, groove.Min.y, groove.GetCenter().x + blank_width, groove.Max.y);

    ImRect main_band = ImRect(blank_rect);
    main_band.Expand(blank_padding);

    auto draw_band = [](const ImU32& clr, const ImRect& band_rc)
    {
        ImGui::RenderFrame(band_rc.Min, band_rc.Max, clr, false, band_rc.GetWidth() * 0.5);
        //cover round corner
        ImGui::RenderFrame(ImVec2(band_rc.Min.x, band_rc.Max.y - band_rc.GetWidth() * 0.5), band_rc.Max, clr, false);
    };
    auto draw_main_band = [&main_band, this](const ImU32& clr) {
        ImGui::RenderFrame(main_band.Min, main_band.Max, clr, false, main_band.GetWidth() * 0.5);
    };
    //draw main colored band
    const int default_color_idx = m_mode == MultiAsSingle ? std::max<int>(m_only_extruder - 1, 0) : 0;
    std::array<float, 4>rgba = decode_color_to_float_array(m_extruder_colors[default_color_idx]);
    ImU32 band_clr = IM_COL32(rgba[0] * 255.0f, rgba[1] * 255.0f, rgba[2] * 255.0f, rgba[3] * 255.0f);
    draw_main_band(band_clr);

    static float tick_pos;
    std::set<TickCode>::const_iterator tick_it = m_ticks.ticks.begin();
    while (tick_it != m_ticks.ticks.end())
    {
        //get position from tick
        tick_pos = get_pos_from_value(GetMinValue(), GetMaxValue(), tick_it->tick, slideable_region);
        //draw colored band
        if (tick_it->type == ToolChange) {
            if ((m_mode == SingleExtruder) || (m_mode == MultiAsSingle))
            {
                ImRect band_rect = ImRect(main_band.Min, ImVec2(main_band.Max.x, tick_pos));

                const std::string clr_str = m_mode == SingleExtruder ? tick_it->color : get_color_for_tool_change_tick(tick_it);

                if (!clr_str.empty()) {
                    std::array<float, 4>rgba = decode_color_to_float_array(clr_str);
                    ImU32 band_clr = IM_COL32(rgba[0] * 255.0f, rgba[1] * 255.0f, rgba[2] * 255.0f, rgba[3] * 255.0f);
                    if (tick_it->tick == 0)
                        draw_main_band(band_clr);
                    else
                        draw_band(band_clr, band_rect);
                }
            }
        }
        tick_it++;
    }

    //draw blank line
    ImGui::RenderFrame(blank_rect.Min, blank_rect.Max, blank_col, false, blank_rect.GetWidth() * 0.5);
}

void IMSlider::draw_ticks(const ImRect& slideable_region) {
    //if(m_draw_mode != dmRegular)
    //    return;
    //if (m_ticks.empty() || m_mode == MultiExtruder)
    //    return;
    if (m_ticks.empty())
        return;

    ImGuiContext &context       = *GImGui;

    ImVec2 tick_box      = ImVec2(46.0f, 16.0f) * m_scale;
    ImVec2 tick_offset   = ImVec2(19.0f, 11.0f) * m_scale;
    float  tick_width    = 1.0f * m_scale;
    ImVec2 icon_offset   = ImVec2(13.0f, 7.0f) * m_scale;
    ImVec2 icon_size     = ImVec2(14.0f, 14.0f) * m_scale;

    const ImU32 tick_clr = IM_COL32(144, 144, 144, 255);
    const ImU32 tick_hover_box_clr = wxGetApp().app_config->get("dark_color_mode") == "1" ? IM_COL32(65, 65, 71, 255) : IM_COL32(219, 253, 231, 255);

    auto get_tick_pos = [this, slideable_region](int tick)
    {
        int v_min = GetMinValue();
        int v_max = GetMaxValue();
        return get_pos_from_value(v_min, v_max, tick, slideable_region);
    };

    std::set<TickCode>::const_iterator tick_it = m_ticks.ticks.begin();
    while (tick_it != m_ticks.ticks.end())
    {
        float tick_pos = get_tick_pos(tick_it->tick);

        //draw tick hover box when hovered
        ImRect tick_hover_box = ImRect(slideable_region.GetCenter().x - tick_box.x / 2, tick_pos - tick_box.y / 2, slideable_region.GetCenter().x + tick_box.x / 2,
                                       tick_pos + tick_box.y / 2);

        if (ImGui::IsMouseHoveringRect(tick_hover_box.Min, tick_hover_box.Max))
        {
            ImGui::RenderFrame(tick_hover_box.Min, tick_hover_box.Max, tick_hover_box_clr, false);
            if (context.IO.MouseClicked[0]) {
                m_tick_value   = tick_it->tick;
                m_tick_rect    = ImVec4(tick_hover_box.Min.x, tick_hover_box.Min.y, tick_hover_box.Max.x, tick_hover_box.Max.y);
            }
        }
        ++tick_it;
    }

    tick_it = m_ticks.ticks.begin();
    while (tick_it != m_ticks.ticks.end())
    {
        float tick_pos = get_tick_pos(tick_it->tick);

        //draw ticks
        ImRect tick_left  = ImRect(slideable_region.GetCenter().x - tick_offset.x, tick_pos - tick_width, slideable_region.GetCenter().x - tick_offset.y, tick_pos);
        ImRect tick_right = ImRect(slideable_region.GetCenter().x + tick_offset.y, tick_pos - tick_width, slideable_region.GetCenter().x + tick_offset.x, tick_pos);
        ImGui::RenderFrame(tick_left.Min, tick_left.Max, tick_clr, false);
        ImGui::RenderFrame(tick_right.Min, tick_right.Max, tick_clr, false);

        //draw pause icon
        if (tick_it->type == PausePrint) {
            ImTextureID pause_icon_id = m_pause_icon_id;
            ImVec2      icon_pos     = ImVec2(slideable_region.GetCenter().x + icon_offset.x, tick_pos - icon_offset.y);
            button_with_pos(pause_icon_id, icon_size, icon_pos);
            if (ImGui::IsMouseHoveringRect(icon_pos, icon_pos + icon_size)) { 
                if(context.IO.MouseClicked[0])
                    int a = 0;
            }
        }
        ++tick_it;
    }

    tick_it = GetSelection() == ssHigher ? m_ticks.ticks.find(TickCode{this->GetHigherValue()}) :
              GetSelection() == ssLower  ? m_ticks.ticks.find(TickCode{this->GetLowerValue()}) :
                                           m_ticks.ticks.end();
    if (tick_it != m_ticks.ticks.end()) {
        // draw delete icon
        ImTextureID delete_icon_id = m_delete_icon_id;
        ImVec2      icon_pos       = ImVec2(slideable_region.GetCenter().x + icon_offset.x, get_tick_pos(tick_it->tick) - icon_offset.y);
        button_with_pos(m_delete_icon_id, icon_size, icon_pos);
        if (ImGui::IsMouseHoveringRect(icon_pos, icon_pos + icon_size)) {
            if (context.IO.MouseClicked[0]) {
                // delete tick
                Type type = tick_it->type;
                m_ticks.ticks.erase(tick_it);
                post_ticks_changed_event(type);
            }
        }
    }

}

bool IMSlider::vertical_slider(const char* str_id, int* higher_value, int* lower_value, std::string& higher_label, std::string& lower_label,int v_min, int v_max, const ImVec2& pos,const ImVec2& size, SelectedSlider& selection, bool one_layer_flag, float scale)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& context = *GImGui;
    const ImGuiID id = window->GetID(str_id);

    const ImRect draw_region(pos, pos + size);
    ImGui::ItemSize(draw_region);

    float  right_dummy         = 24.0f * m_scale;
    float  text_dummy_height   = 34.0f * scale * m_scale;
    float  groove_x            = 8.0f * m_scale;
    float  draggable_region_x  = 40.0f * m_scale;
    float  handle_radius       = 14.0f * m_scale;
    float  handle_border       = 2.0f * m_scale;
    float  rounding            = 2.0f * m_scale;
    float  line_width          = 2.0f * m_scale;
    float  line_offset         = 9.0f * m_scale;
    float  one_handle_offset   = 26.0f * m_scale;
    float  bar_width           = 12.0f * m_scale;
    ImVec2 text_padding        = ImVec2(5.0f, 2.0f) * m_scale;
    ImVec2 triangle_offsets[3] = {ImVec2(2.0f, 0.0f) * m_scale, ImVec2(0.0f, 8.0f) * m_scale, ImVec2(9.0f, 0.0f) * m_scale};
    ImVec2 text_content_size;
    ImVec2 text_size;

    const ImU32 white_bg = wxGetApp().app_config->get("dark_color_mode") == "1" ? IM_COL32(65, 65, 71, 255) : IM_COL32(255, 255, 255, 255);
    const ImU32 handle_clr = IM_COL32(0, 174, 66, 255);
    const ImU32 handle_border_clr = wxGetApp().app_config->get("dark_color_mode") == "1" ? IM_COL32(65, 65, 71, 255) : IM_COL32(248, 248, 248, 255);

    // calc slider groove size
    ImVec2 groove_start = ImVec2(pos.x + size.x - groove_x - right_dummy, pos.y + text_dummy_height);
    ImVec2 groove_size = ImVec2(groove_x, size.y - 2 * text_dummy_height);
    ImRect groove = ImRect(groove_start, groove_start + groove_size);

    // set active(draggable) region.
    ImRect draggable_region = ImRect(groove.GetCenter().x, groove.Min.y, groove.GetCenter().x, groove.Max.y);
    draggable_region.Expand(ImVec2(draggable_region_x, 0));
    float mid_x = draggable_region.GetCenter().x;
    bool hovered = ImGui::ItemHoverable(draggable_region, id) && !ImGui::ItemHoverable(m_tick_rect, id);
    if (hovered && context.IO.MouseDown[0]) {
        ImGui::SetActiveID(id, window);
        ImGui::SetFocusID(id, window);
        ImGui::FocusWindow(window);
    }

    // draw background
    draw_background(groove);


    // Processing interacting
    // set slideable region
    ImRect higher_slideable_region = ImRect(draggable_region.Min, draggable_region.Max - ImVec2(0, handle_radius));
    ImRect lower_slideable_region = ImRect(draggable_region.Min + ImVec2(0, handle_radius), draggable_region.Max);
    ImRect one_slideable_region = draggable_region;

    // initialize the handles.
    float higher_handle_pos = get_pos_from_value(v_min, v_max, *higher_value, higher_slideable_region);
    ImRect higher_handle = ImRect(mid_x - handle_radius, higher_handle_pos - handle_radius, mid_x + handle_radius, higher_handle_pos + handle_radius);

    float  lower_handle_pos = get_pos_from_value(v_min, v_max, *lower_value, lower_slideable_region);
    ImRect lower_handle = ImRect(mid_x - handle_radius, lower_handle_pos - handle_radius, mid_x + handle_radius, lower_handle_pos + handle_radius);

    ImRect one_handle = ImRect(higher_handle.Min - ImVec2(one_handle_offset, 0), higher_handle.Max - ImVec2(one_handle_offset, 0));

    //static bool become_del_handle = false;
    bool value_changed = false;
    if (!one_layer_flag) 
    {
        // select higher handle by default
        static bool h_selected = true;
        if (ImGui::ItemHoverable(higher_handle, id) && context.IO.MouseClicked[0]) {
            selection = ssHigher; 
            h_selected = true;
        }
        if (ImGui::ItemHoverable(lower_handle, id) && context.IO.MouseClicked[0]) {
            selection = ssLower; 
            h_selected = false;
        }

        // update handle position and value
        if (h_selected)
        {
            value_changed = slider_behavior(id, higher_slideable_region, v_min, v_max,
                higher_value, &higher_handle, ImGuiSliderFlags_Vertical, 
                m_tick_value, m_tick_rect);
        }
        if (!h_selected) {
            value_changed = slider_behavior(id, lower_slideable_region, v_min, v_max,
                lower_value, &lower_handle, ImGuiSliderFlags_Vertical,
                m_tick_value, m_tick_rect);
        }

        ImVec2 higher_handle_center = higher_handle.GetCenter();
        ImVec2 lower_handle_center = lower_handle.GetCenter();
        if (higher_handle_center.y + handle_radius > lower_handle_center.y && h_selected)
        {
            lower_handle = higher_handle;
            lower_handle.TranslateY(handle_radius);
            lower_handle_center.y = higher_handle_center.y + handle_radius;
            *lower_value = *higher_value;
        }
        if (higher_handle_center.y + handle_radius > lower_handle_center.y && !h_selected)
        {
            higher_handle = lower_handle;
            higher_handle.TranslateY(-handle_radius);
            higher_handle_center.y = lower_handle_center.y - handle_radius;
            *higher_value = *lower_value;
        }

        // judge whether to open menu
        if (ImGui::ItemHoverable(h_selected ? higher_handle : lower_handle, id) && context.IO.MouseClicked[1])
            m_show_menu = true;
        if ((!ImGui::ItemHoverable(h_selected ? higher_handle : lower_handle, id) && context.IO.MouseClicked[1]) ||
            context.IO.MouseClicked[0])
            m_show_menu = false;


        if (!m_ticks.empty()) {
            // draw ticks
            draw_ticks(h_selected ? higher_slideable_region : lower_slideable_region);
            // draw colored band
            draw_colored_band(groove, h_selected ? higher_slideable_region : lower_slideable_region);
        }
        else {
            // draw scroll line
            ImRect scroll_line = ImRect(ImVec2(mid_x - groove_x / 2, higher_handle_center.y), ImVec2(mid_x + groove_x / 2, lower_handle_center.y));
            window->DrawList->AddRectFilled(scroll_line.Min, scroll_line.Max, handle_clr, rounding);
        }

        // draw handles
        window->DrawList->AddCircleFilled(higher_handle_center, handle_radius, handle_border_clr);
        window->DrawList->AddCircleFilled(higher_handle_center, handle_radius - handle_border, handle_clr);
        window->DrawList->AddCircleFilled(lower_handle_center, handle_radius, handle_border_clr);
        window->DrawList->AddCircleFilled(lower_handle_center, handle_radius - handle_border, handle_clr);
        if (h_selected) {
            window->DrawList->AddCircleFilled(higher_handle_center, handle_radius, handle_border_clr);
            window->DrawList->AddCircleFilled(higher_handle_center, handle_radius - handle_border, handle_clr);
            window->DrawList->AddLine(higher_handle_center + ImVec2(-line_offset, 0.0f), higher_handle_center + ImVec2(line_offset, 0.0f), white_bg, line_width);
            window->DrawList->AddLine(higher_handle_center + ImVec2(0.0f, -line_offset), higher_handle_center + ImVec2(0.0f, line_offset), white_bg, line_width);
        }
        if (!h_selected) {
            window->DrawList->AddLine(lower_handle_center + ImVec2(-line_offset, 0.0f), lower_handle_center + ImVec2(line_offset, 0.0f), white_bg, line_width);
            window->DrawList->AddLine(lower_handle_center + ImVec2(0.0f, -line_offset), lower_handle_center + ImVec2(0.0f, line_offset), white_bg, line_width);
        }

        // draw higher label
        auto text_utf8 = into_u8(higher_label);
        text_content_size = ImGui::CalcTextSize(text_utf8.c_str());
        text_size = text_content_size + text_padding * 2;
        ImVec2 text_start = ImVec2(higher_handle.Min.x - text_size.x - triangle_offsets[2].x, higher_handle_center.y - text_size.y);
        ImRect text_rect(text_start, text_start + text_size);
        ImGui::RenderFrame(text_rect.Min, text_rect.Max, white_bg, false, rounding);
        ImVec2 pos_1 = text_rect.Max - triangle_offsets[0];
        ImVec2 pos_2 = pos_1 - triangle_offsets[1];
        ImVec2 pos_3 = pos_1 + triangle_offsets[2];
        window->DrawList->AddTriangleFilled(pos_1, pos_2, pos_3, white_bg);
        ImGui::RenderText(text_start + text_padding, higher_label.c_str());
        // draw lower label
        text_utf8 = into_u8(lower_label);
        text_content_size = ImGui::CalcTextSize(text_utf8.c_str());
        text_size = text_content_size + text_padding * 2;
        text_start        = ImVec2(lower_handle.Min.x - text_size.x - triangle_offsets[2].x, lower_handle_center.y);
        text_rect = ImRect(text_start, text_start + text_size);
        ImGui::RenderFrame(text_rect.Min, text_rect.Max, white_bg, false, rounding);
        pos_1 = ImVec2(text_rect.Max.x, text_rect.Min.y) - triangle_offsets[0];
        pos_2 = pos_1 + triangle_offsets[1];
        pos_3 = pos_1 + triangle_offsets[2];
        window->DrawList->AddTriangleFilled(pos_1, pos_2, pos_3, white_bg);
        ImGui::RenderText(text_start + text_padding, lower_label.c_str());
    }
    if (one_layer_flag) 
    {
        // update handle position
        value_changed = slider_behavior(id, one_slideable_region, v_min, v_max,
            higher_value, &one_handle, ImGuiSliderFlags_Vertical,
            m_tick_value, m_tick_rect);

        ImVec2 handle_center = one_handle.GetCenter();

        // judge whether to open menu
        if (ImGui::ItemHoverable(one_handle, id) && context.IO.MouseClicked[1])
            m_show_menu = true;
        if ((!ImGui::ItemHoverable(one_handle, id) && context.IO.MouseClicked[1]) ||
            context.IO.MouseClicked[0])
            m_show_menu = false;
        
        ImVec2 bar_center = higher_handle.GetCenter();

        if (!m_ticks.empty()) {
            // draw ticks
            draw_ticks(one_slideable_region);
            // draw colored band
            draw_colored_band(groove, one_slideable_region);
        }

        // draw handle
        window->DrawList->AddLine(ImVec2(mid_x - bar_width, handle_center.y), ImVec2(mid_x + bar_width, handle_center.y), handle_clr, line_width);
        window->DrawList->AddCircleFilled(handle_center, handle_radius, handle_border_clr);
        window->DrawList->AddCircleFilled(handle_center, handle_radius - handle_border, handle_clr);
        window->DrawList->AddLine(handle_center + ImVec2(-line_offset, 0.0f), handle_center + ImVec2(line_offset, 0.0f), white_bg, line_width);
        window->DrawList->AddLine(handle_center + ImVec2(0.0f, -line_offset), handle_center + ImVec2(0.0f, line_offset), white_bg, line_width);

        // draw label
        auto text_utf8 = into_u8(higher_label);
        text_content_size = ImGui::CalcTextSize(text_utf8.c_str());
        text_size = text_content_size + text_padding * 2;
        ImVec2 text_start = ImVec2(one_handle.Min.x - text_size.x, handle_center.y - 0.5 * text_size.y);
        ImRect text_rect = ImRect(text_start, text_start + text_size);
        ImGui::RenderFrame(text_rect.Min, text_rect.Max, white_bg, false, rounding);
        ImGui::RenderText(text_start + text_padding, higher_label.c_str());
    }

    return value_changed;
}

bool IMSlider::render(int canvas_width, int canvas_height)
{
    bool result = false;
    ImGuiWrapper &imgui = *wxGetApp().imgui();
    /* style and colors */
    ImGui::PushStyleVar(ImGuiStyleVar_::ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_::ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_::ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_::ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_::ImGuiCol_Text, ImVec4(0, 0.682f, 0.259f, 1.0f));

    int windows_flag = ImGuiWindowFlags_NoTitleBar
                       | ImGuiWindowFlags_NoCollapse
                       | ImGuiWindowFlags_NoMove
                       | ImGuiWindowFlags_NoResize
                       | ImGuiWindowFlags_NoScrollbar
                       | ImGuiWindowFlags_NoScrollWithMouse;

    float scale = (float) wxGetApp().em_unit() / 10.0f;

    render_input_custom_gcode();

    render_go_to_layer_dialog();

    if (is_horizontal()) {
        float  pos_x = std::max(LEFT_MARGIN, 0.2f * canvas_width);
        float  pos_y = (canvas_height - HORIZONTAL_SLIDER_SIZE.y * m_scale);
        ImVec2 size  = ImVec2(canvas_width - 2 * pos_x, HORIZONTAL_SLIDER_SIZE.y * m_scale);
        imgui.set_next_window_pos(pos_x, pos_y, ImGuiCond_Always);
        imgui.begin(std::string("moves_slider"), windows_flag);
        int value = GetHigherValue();
        if (horizontal_slider("moves_slider", &value, GetMinValue(), GetMaxValue(),ImVec2(pos_x, pos_y), size, scale)) {
            result = true;
            SetHigherValue(value);
        }
        imgui.end();
    } else {
        float  pos_x = canvas_width - (VERTICAL_SLIDER_SIZE.x + TEXT_WIDTH_DUMMY * scale - TEXT_WIDTH_DUMMY + ONE_LAYER_MARGIN) * m_scale;
        float pos_y = std::max(ONE_LAYER_OFFSET.y, 0.15f * canvas_height - (VERTICAL_SLIDER_SIZE.y - SLIDER_LENGTH) * scale);
        ImVec2 size = ImVec2((VERTICAL_SLIDER_SIZE.x + TEXT_WIDTH_DUMMY * scale - TEXT_WIDTH_DUMMY + ONE_LAYER_MARGIN) * m_scale, canvas_height - 2 * pos_y);
        imgui.set_next_window_pos(pos_x, pos_y, ImGuiCond_Always);
        imgui.begin(std::string("laysers_slider"), windows_flag);

        render_menu();

        int higher_value = GetHigherValue();
        int lower_value = GetLowerValue();
        std::string higher_label = get_label(m_higher_value);
        std::string lower_label  = get_label(m_lower_value);
        int temp_higher_value    = higher_value;
        int temp_lower_value     = lower_value;
        if (vertical_slider("laysers_slider", &higher_value, &lower_value, higher_label, lower_label, GetMinValue(), GetMaxValue(),
                  ImVec2(pos_x, pos_y), size, m_selection, is_one_layer(), scale)) {
            if (temp_higher_value != higher_value)
                SetHigherValue(higher_value);
            if (temp_lower_value != lower_value)
                SetLowerValue(lower_value);
            result = true;
        }

        ImGui::Spacing();
        ImGui::SameLine((VERTICAL_SLIDER_SIZE.x - ONE_LAYER_OFFSET.x) * scale * m_scale);
        bool dark_mode = wxGetApp().app_config->get("dark_color_mode") == "1";
        ImTextureID normal_id = dark_mode ? 
            is_one_layer() ? m_one_layer_on_dark_id : m_one_layer_off_dark_id :
            is_one_layer() ? m_one_layer_on_id : m_one_layer_off_id;
        ImTextureID hover_id  = dark_mode ? 
            is_one_layer() ? m_one_layer_on_hover_dark_id : m_one_layer_off_hover_dark_id :
            is_one_layer() ? m_one_layer_on_hover_id : m_one_layer_off_hover_id;
        if (ImGui::ImageButton3(normal_id, hover_id, ImVec2(28 * m_scale, 28 * m_scale))) {
            switch_one_layer_mode();
        }

        imgui.end();
    }

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);

    return result;
}

void IMSlider::render_input_custom_gcode()
{
    if (!m_show_custom_gcode_window)
        return;
    ImGuiWrapper& imgui = *wxGetApp().imgui();
    static bool move_to_center = true;
    static bool set_focus_when_appearing = true;
    if (move_to_center) {
        auto pos_x = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size().get_width() / 2;
        auto pos_y = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size().get_height() / 2;
        imgui.set_next_window_pos(pos_x, pos_y, ImGuiCond_Always, 0.5f, 0.5f);
        move_to_center = false;
    }

    imgui.push_common_window_style(m_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.f * m_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 3) * m_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 7) * m_scale);
    int windows_flag = 
        ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;
    imgui.begin(_u8L("Custom G-code"), windows_flag);
    imgui.text(_u8L("Enter Custom G-code used on current layer:"));
    if (set_focus_when_appearing) {
        ImGui::SetKeyboardFocusHere(0);
        set_focus_when_appearing = false;
    }
    int text_height = 6;
    ImGui::InputTextMultiline("##text", m_custom_gcode, sizeof(m_custom_gcode), ImVec2(-1, ImGui::GetTextLineHeight() * text_height));
    //text_height = 5;
    //for (int i = 0; m_custom_gcode[i] != '\0'; ++i){
    //    if ('\n' == m_custom_gcode[i] && text_height < 12)
    //        ++text_height;
    //}

    ImGui::NewLine();
    ImGui::SameLine(ImGui::GetStyle().WindowPadding.x * 14);
    imgui.push_confirm_button_style();
    if (imgui.bbl_button(_L("OK")) || ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_Enter))) {
        m_show_custom_gcode_window = false;
        add_custom_gcode(m_custom_gcode);
        move_to_center = true;
        set_focus_when_appearing = true;
    }
    imgui.pop_confirm_button_style();

    ImGui::SameLine();
    imgui.push_cancel_button_style();
    if (imgui.bbl_button(_L("Cancel")) || ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_Escape))) {
        m_show_custom_gcode_window = false;
        move_to_center = true;
        set_focus_when_appearing = true;
    }
    imgui.pop_cancel_button_style();

    imgui.end();
    ImGui::PopStyleVar(3);
    imgui.pop_common_window_style();
}

void IMSlider::do_go_to_layer(size_t layer_number) {
    clamp((int)layer_number, m_min_value, m_max_value);
    GetSelection() == ssLower ? SetLowerValue(layer_number) : SetHigherValue(layer_number);
}

void IMSlider::render_go_to_layer_dialog(){
    if (!m_show_go_to_layer_dialog)
        return;
    ImGuiWrapper& imgui = *wxGetApp().imgui();
    static bool move_to_center = true;
    static bool set_focus_when_appearing = true;
    if (move_to_center) {
        auto pos_x = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size().get_width() / 2;
        auto pos_y = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size().get_height() / 2;
        imgui.set_next_window_pos(pos_x, pos_y, ImGuiCond_Always, 0.5f, 0.5f);
        move_to_center = false;
    }

    imgui.push_common_window_style(m_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.f * m_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 3) * m_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 7) * m_scale);
    int windows_flag =
        ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;
    imgui.begin(_u8L("Go to layer"), windows_flag);
    imgui.text(_u8L("Layer number") + " (" + std::to_string(m_min_value) + " - " + std::to_string(m_max_value) + "):");
    ImGui::PushItemWidth(210 * m_scale);
    if (set_focus_when_appearing) {
        ImGui::SetKeyboardFocusHere(0);
        set_focus_when_appearing = false;
    }
    ImGui::InputText("##input_layer_number", m_layer_number, sizeof(m_layer_number));

    ImGui::NewLine();
    ImGui::SameLine(GImGui->Style.WindowPadding.x * 6);
    imgui.push_confirm_button_style();
    bool disable_button = false;
    if (strlen(m_layer_number) == 0)
        disable_button = true;
    else {
        for (size_t i = 0; i< strlen(m_layer_number); i++)
            if (!isdigit(m_layer_number[i]))
                disable_button = true;
        if (!disable_button && (m_min_value > atoi(m_layer_number) || atoi(m_layer_number) > m_max_value))
            disable_button = true;
    }
    if (disable_button) {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        imgui.push_button_disable_style();
    }
    if (imgui.bbl_button(_L("OK"))) {
        do_go_to_layer(atoi(m_layer_number));
        m_show_go_to_layer_dialog = false;
        move_to_center = true;
        set_focus_when_appearing = true;
    }
    if (disable_button) {
        ImGui::PopItemFlag();
        imgui.pop_button_disable_style();
    }
    imgui.pop_confirm_button_style();

    ImGui::SameLine();
    imgui.push_cancel_button_style();
    if (imgui.bbl_button(_L("Cancel"))) {
        m_show_go_to_layer_dialog = false;
        move_to_center = true;
        set_focus_when_appearing = true;
    }
    imgui.pop_cancel_button_style();

    imgui.end();
    ImGui::PopStyleVar(3);
    imgui.pop_common_window_style();
}

void IMSlider::render_menu()
{
    ImGuiWrapper::push_menu_style(m_scale);
    std::vector<std::string> colors = wxGetApp().plater()->get_extruder_colors_from_plater_config();
    int extruder_num = colors.size();

    if (m_show_menu) {
        ImGui::OpenPopup("slider_menu_popup");
    }

    ImGui::PushStyleVar(ImGuiStyleVar_::ImGuiStyleVar_ChildRounding, 4.0f * m_scale);
    if (ImGui::BeginPopup("slider_menu_popup")) {
        if ((m_selection == ssLower && GetLowerValueD() == m_zero_layer_height) || (m_selection == ssHigher && GetHigherValueD() == m_zero_layer_height))
        {
            if (menu_item_with_icon(_u8L("Jump to Layer").c_str(), "")) {
                m_show_go_to_layer_dialog = true;
            }
        }
        else
        {
            if (menu_item_with_icon(_u8L("Add Pause").c_str(), "")) {
                add_code_as_tick(PausePrint);
            }
            if (menu_item_with_icon(_u8L("Add Custom G-code").c_str(), "")) {
                m_show_custom_gcode_window = true;
            }
            if (!gcode(Template).empty()) {
                if (menu_item_with_icon(_u8L("Add Custom Template").c_str(), "")) {
                    add_code_as_tick(Template);
                }
            }
            if (menu_item_with_icon(_u8L("Jump to Layer").c_str(), "")) {
                m_show_go_to_layer_dialog = true;
            }
        }

        //BBS render this menu item only when extruder_num > 1
        if (extruder_num > 1) {
            if (!m_can_change_color || m_draw_mode == dmSequentialFffPrint) {
                begin_menu(_u8L("Change Filament").c_str(), false);
            }
            else if (begin_menu(_u8L("Change Filament").c_str())) {
                for (int i = 0; i < extruder_num; i++) {
                    std::array<float, 4> rgba     = decode_color_to_float_array(colors[i]);
                    ImU32                icon_clr = IM_COL32(rgba[0] * 255.0f, rgba[1] * 255.0f, rgba[2] * 255.0f, rgba[3] * 255.0f);
                    if (menu_item_with_icon((_u8L("Filament ") + std::to_string(i + 1)).c_str(), "", ImVec2(14, 14) * m_scale, icon_clr)) add_code_as_tick(ToolChange, i + 1);
                }
                end_menu();
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(1);

    ImGuiWrapper::pop_menu_style();
}

void IMSlider::set_scale(float scale)
{
    if(m_scale != scale) m_scale = scale;
}

void IMSlider::correct_lower_value()
{
    if (m_lower_value < m_min_value)
        m_lower_value = m_min_value;
    else if (m_lower_value > m_max_value)
        m_lower_value = m_max_value;

    if ((m_lower_value >= m_higher_value && m_lower_value <= m_max_value) || m_is_one_layer) m_higher_value = m_lower_value;
}

void IMSlider::correct_higher_value()
{
    if (m_higher_value > m_max_value)
        m_higher_value = m_max_value;
    else if (m_higher_value < m_min_value)
        m_higher_value = m_min_value;

    if ((m_higher_value <= m_lower_value && m_higher_value >= m_min_value) || m_is_one_layer) m_lower_value = m_higher_value;
}

bool IMSlider::is_wipe_tower_layer(int tick) const
{
    if (!m_is_wipe_tower || tick >= (int) m_values.size()) return false;
    if (tick == 0 || (tick == (int) m_values.size() - 1 && m_values[tick] > m_values[tick - 1])) return false;
    if ((m_values[tick - 1] == m_values[tick + 1] && m_values[tick] < m_values[tick + 1]) ||
        (tick > 0 && m_values[tick] < m_values[tick - 1])) // if there is just one wiping on the layer
        return true;

    return false;
}

std::string IMSlider::get_label(int tick, LabelType label_type)
{
    const size_t value = tick;

    if (m_label_koef == 1.0 && m_values.empty()) {
        std::to_string(value);
    }
    if (value >= m_values.size()) return "error";

    auto get_layer_number = [this](int value, LabelType label_type) {
        if (label_type == ltEstimatedTime && m_layers_times.empty()) return size_t(-1);
        double layer_print_z = m_values[is_wipe_tower_layer(value) ? std::max<int>(value - 1, 0) : value];
        auto   it            = std::lower_bound(m_layers_values.begin(), m_layers_values.end(), layer_print_z - epsilon());
        if (it == m_layers_values.end()) {
            it = std::lower_bound(m_values.begin(), m_values.end(), layer_print_z - epsilon());
            if (it == m_values.end()) return size_t(-1);
            return size_t(value);
        }
        return size_t(it - m_layers_values.begin());
    };

    if (m_draw_mode == dmSequentialGCodeView) {
        return std::to_string(tick);

    } else {
        if (label_type == ltEstimatedTime) {
            if (m_is_wipe_tower) {
                size_t layer_number = get_layer_number(value, label_type);
                return (layer_number == size_t(-1) || layer_number == m_layers_times.size()) ? "" : short_and_splitted_time(get_time_dhms(m_layers_times[layer_number]));
            }
            return value < m_layers_times.size() ? short_and_splitted_time(get_time_dhms(m_layers_times[value])) : "";
        }

        char layer_height[64];
        ::sprintf(layer_height, "%.2f", m_values.empty() ? m_label_koef * value : m_values[value]);
        if (label_type == ltHeight) return std::string(layer_height);
        if (label_type == ltHeightWithLayer) {
            char   buffer[64];
            size_t layer_number;
            if (m_values[GetMinValueD()] == m_zero_layer_height) {
                layer_number = m_is_wipe_tower ? get_layer_number(value, label_type): (m_values.empty() ? value : value);
                m_values[value] == m_zero_layer_height ?
                    ::sprintf(buffer, "%5s\n%5s", _u8L("Start").c_str(), _u8L("G-code").c_str()) :
                    ::sprintf(buffer, "%5s\n%5s", std::to_string(layer_number).c_str(), layer_height);
            }
            else {
                layer_number = m_is_wipe_tower ? get_layer_number(value, label_type) + 1 : (m_values.empty() ? value : value + 1);
                ::sprintf(buffer, "%5s\n%5s", std::to_string(layer_number).c_str(), layer_height);
            }
            return std::string(buffer);
        }
    }

    return "";
}

double IMSlider::get_double_value(const SelectedSlider &selection)
{
    if (m_values.empty() || m_lower_value < 0) return 0.0;
    if (m_values.size() <= size_t(m_higher_value)) {
        correct_higher_value();
        return m_values.back();
    }
    return m_values[selection == ssLower ? m_lower_value : m_higher_value];
}

int IMSlider::get_tick_from_value(double value, bool force_lower_bound /* = false*/)
{
    std::vector<double>::iterator it;
    if (m_is_wipe_tower && !force_lower_bound)
        it = std::find_if(m_values.begin(), m_values.end(), [value](const double &val) { return fabs(value - val) <= epsilon(); });
    else
        it = std::lower_bound(m_values.begin(), m_values.end(), value - epsilon());

    if (it == m_values.end()) return -1;
    return int(it - m_values.begin());
}

float IMSlider::get_pos_from_value(int v_min, int v_max, int value, const ImRect& rect) {
    float pos_ratio = (v_max - v_min) != 0 ? ((float)(value - v_min) / (float)(v_max - v_min)) : 0.0f;
    float handle_pos;
    if (is_horizontal()) {
        handle_pos = rect.Min.x + (rect.Max.x - rect.Min.x) * pos_ratio;
    }
    else {
        pos_ratio = 1.0f - pos_ratio;
        handle_pos = rect.Min.y + (rect.Max.y - rect.Min.y) * pos_ratio;
    }
    return handle_pos;
}

std::string IMSlider::get_color_for_tool_change_tick(std::set<TickCode>::const_iterator it) const
{
    const int current_extruder = it->extruder == 0 ? std::max<int>(m_only_extruder, 1) : it->extruder;

    auto it_n = it;
    while (it_n != m_ticks.ticks.begin()) {
        --it_n;
        if (it_n->type == ColorChange && it_n->extruder == current_extruder)
            return it_n->color;
    }

    if ((current_extruder > 0 && (current_extruder - 1) < m_extruder_colors.size()))
    {
        return m_extruder_colors[current_extruder - 1]; // return a color for a specific extruder from the colors list
    }
    return "";
}

// Get active extruders for tick.
// Means one current extruder for not existing tick OR
// 2 extruders - for existing tick (extruder before ToolChange and extruder of current existing tick)
// Use those values to disable selection of active extruders
std::array<int, 2> IMSlider::get_active_extruders_for_tick(int tick) const
{
    int                default_initial_extruder = m_mode == MultiAsSingle ? std::max<int>(1, m_only_extruder) : 1;
    std::array<int, 2> extruders                = {default_initial_extruder, -1};
    if (m_ticks.empty()) return extruders;

    auto it = m_ticks.ticks.lower_bound(TickCode{tick});

    if (it != m_ticks.ticks.end() && it->tick == tick) // current tick exists
        extruders[1] = it->extruder;

    while (it != m_ticks.ticks.begin()) {
        --it;
        if (it->type == ToolChange) {
            extruders[0] = it->extruder;
            break;
        }
    }

    return extruders;
}

}

} // Slic3r


