#include "libslic3r/libslic3r.h"
#include "GCodeViewer.hpp"

#if ENABLE_GCODE_VIEWER
#include "libslic3r/Print.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Utils.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "Camera.hpp"
#include "I18N.hpp"
#include "GUI_Utils.hpp"
#include "GUI.hpp"
#include "DoubleSlider.hpp"
#include "GLCanvas3D.hpp"
#include "GLToolbar.hpp"
#include "GUI_Preview.hpp"
#include <imgui/imgui_internal.h>

#include <GL/glew.h>
#include <boost/log/trivial.hpp>
#include <boost/nowide/cstdio.hpp>
#include <wx/progdlg.h>
#include <wx/numformatter.h>

#include <array>
#include <algorithm>
#include <chrono>

namespace Slic3r {
namespace GUI {

static unsigned char buffer_id(EMoveType type) {
    return static_cast<unsigned char>(type) - static_cast<unsigned char>(EMoveType::Retract);
}

static EMoveType buffer_type(unsigned char id) {
    return static_cast<EMoveType>(static_cast<unsigned char>(EMoveType::Retract) + id);
}

static std::array<float, 3> decode_color(const std::string& color) {
    static const float INV_255 = 1.0f / 255.0f;

    std::array<float, 3> ret = { 0.0f, 0.0f, 0.0f };
    const char* c = color.data() + 1;
    if (color.size() == 7 && color.front() == '#') {
        for (size_t j = 0; j < 3; ++j) {
            int digit1 = hex_digit_to_int(*c++);
            int digit2 = hex_digit_to_int(*c++);
            if (digit1 == -1 || digit2 == -1)
                break;

            ret[j] = float(digit1 * 16 + digit2) * INV_255;
        }
    }
    return ret;
}

static std::vector<std::array<float, 3>> decode_colors(const std::vector<std::string>& colors) {
    std::vector<std::array<float, 3>> output(colors.size(), { 0.0f, 0.0f, 0.0f });
    for (size_t i = 0; i < colors.size(); ++i) {
        output[i] = decode_color(colors[i]);
    }
    return output;
}

static float round_to_nearest(float value, unsigned int decimals)
{
    float res = 0.0f;
    if (decimals == 0)
        res = std::round(value);
    else {
        char buf[64];
        sprintf(buf, "%.*g", decimals, value);
        res = std::stof(buf);
    }
    return res;
}

void GCodeViewer::VBuffer::reset()
{
    // release gpu memory
    if (id > 0) {
        glsafe(::glDeleteBuffers(1, &id));
        id = 0;
    }

    count = 0;
}

void GCodeViewer::IBuffer::reset()
{
    // release gpu memory
    if (id > 0) {
        glsafe(::glDeleteBuffers(1, &id));
        id = 0;
    }

    count = 0;
}

bool GCodeViewer::Path::matches(const GCodeProcessor::MoveVertex& move) const
{
    switch (move.type)
    {
    case EMoveType::Tool_change:
    case EMoveType::Color_change:
    case EMoveType::Pause_Print:
    case EMoveType::Custom_GCode:
    case EMoveType::Retract:
    case EMoveType::Unretract:
    case EMoveType::Extrude: {
        // use rounding to reduce the number of generated paths
        return type == move.type && move.position[2] <= first.position[2] && role == move.extrusion_role && height == round_to_nearest(move.height, 2) &&
            width == round_to_nearest(move.width, 2) && feedrate == move.feedrate && fan_speed == move.fan_speed &&
            volumetric_rate == round_to_nearest(move.volumetric_rate(), 2) && extruder_id == move.extruder_id &&
            cp_color_id == move.cp_color_id;
    }
    case EMoveType::Travel: {
        return type == move.type && feedrate == move.feedrate && extruder_id == move.extruder_id && cp_color_id == move.cp_color_id;
    }
    default: { return false; }
    }
}

void GCodeViewer::TBuffer::reset()
{
    // release gpu memory
    vertices.reset();
    for (IBuffer& buffer : indices) {
        buffer.reset();
    }

    // release cpu memory
    indices = std::vector<IBuffer>();
    paths = std::vector<Path>();
    render_paths = std::vector<RenderPath>();
}

void GCodeViewer::TBuffer::add_path(const GCodeProcessor::MoveVertex& move, unsigned int b_id, size_t i_id, size_t s_id)
{
    Path::Endpoint endpoint = { b_id, i_id, s_id, move.position };
    // use rounding to reduce the number of generated paths
    paths.push_back({ move.type, move.extrusion_role, endpoint, endpoint, move.delta_extruder,
        round_to_nearest(move.height, 2), round_to_nearest(move.width, 2), move.feedrate, move.fan_speed,
        round_to_nearest(move.volumetric_rate(), 2), move.extruder_id, move.cp_color_id });
}

GCodeViewer::Color GCodeViewer::Extrusions::Range::get_color_at(float value) const
{
    // Input value scaled to the colors range
    const float step = step_size();
    const float global_t = (step != 0.0f) ? std::max(0.0f, value - min) / step : 0.0f; // lower limit of 0.0f

    const size_t color_max_idx = Range_Colors.size() - 1;

    // Compute the two colors just below (low) and above (high) the input value
    const size_t color_low_idx = std::clamp<size_t>(static_cast<size_t>(global_t), 0, color_max_idx);
    const size_t color_high_idx = std::clamp<size_t>(color_low_idx + 1, 0, color_max_idx);

    // Compute how far the value is between the low and high colors so that they can be interpolated
    const float local_t = std::clamp(global_t - static_cast<float>(color_low_idx), 0.0f, 1.0f);

    // Interpolate between the low and high colors to find exactly which color the input value should get
    Color ret;
    for (unsigned int i = 0; i < 3; ++i) {
        ret[i] = lerp(Range_Colors[color_low_idx][i], Range_Colors[color_high_idx][i], local_t);
    }
    return ret;
}

void GCodeViewer::SequentialView::Marker::init()
{
    m_model.init_from(stilized_arrow(16, 2.0f, 4.0f, 1.0f, 8.0f));
}

void GCodeViewer::SequentialView::Marker::set_world_position(const Vec3f& position)
{    
    m_world_position = position;
    m_world_transform = (Geometry::assemble_transform((position + m_z_offset * Vec3f::UnitZ()).cast<double>()) * Geometry::assemble_transform(m_model.get_bounding_box().size()[2] * Vec3d::UnitZ(), { M_PI, 0.0, 0.0 })).cast<float>();
}

void GCodeViewer::SequentialView::Marker::render() const
{
    if (!m_visible)
        return;

    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    shader->start_using();
    shader->set_uniform("uniform_color", m_color);

    glsafe(::glPushMatrix());
    glsafe(::glMultMatrixf(m_world_transform.data()));

    m_model.render();

    glsafe(::glPopMatrix());

    shader->stop_using();

    glsafe(::glDisable(GL_BLEND));

    static float last_window_width = 0.0f;
    static size_t last_text_length = 0;

    ImGuiWrapper& imgui = *wxGetApp().imgui();
    Size cnv_size = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size();
    imgui.set_next_window_pos(0.5f * static_cast<float>(cnv_size.get_width()), static_cast<float>(cnv_size.get_height()), ImGuiCond_Always, 0.5f, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::SetNextWindowBgAlpha(0.25f);
    imgui.begin(std::string("ToolPosition"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    imgui.text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, _u8L("Tool position") + ":");
    ImGui::SameLine();
    char buf[1024];
    sprintf(buf, "X: %.2f, Y: %.2f, Z: %.2f", m_world_position(0), m_world_position(1), m_world_position(2));
    imgui.text(std::string(buf));

    // force extra frame to automatically update window size
    float width = ImGui::GetWindowWidth();
    size_t length = strlen(buf);
    if (width != last_window_width || length != last_text_length) {
        last_window_width = width;
        last_text_length = length;
        wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
        wxGetApp().plater()->get_current_canvas3D()->request_extra_frame();
    }

    imgui.end();
    ImGui::PopStyleVar();
}

const std::vector<GCodeViewer::Color> GCodeViewer::Extrusion_Role_Colors {{
    { 0.75f, 0.75f, 0.75f },   // erNone
    { 1.00f, 0.90f, 0.30f },   // erPerimeter
    { 1.00f, 0.49f, 0.22f },   // erExternalPerimeter
    { 0.12f, 0.12f, 1.00f },   // erOverhangPerimeter
    { 0.69f, 0.19f, 0.16f },   // erInternalInfill
    { 0.59f, 0.33f, 0.80f },   // erSolidInfill
    { 0.94f, 0.25f, 0.25f },   // erTopSolidInfill
    { 1.00f, 0.55f, 0.41f },   // erIroning
    { 0.30f, 0.50f, 0.73f },   // erBridgeInfill
    { 1.00f, 1.00f, 1.00f },   // erGapFill
    { 0.00f, 0.53f, 0.43f },   // erSkirt
    { 0.00f, 1.00f, 0.00f },   // erSupportMaterial
    { 0.00f, 0.50f, 0.00f },   // erSupportMaterialInterface
    { 0.70f, 0.89f, 0.67f },   // erWipeTower
    { 0.37f, 0.82f, 0.58f },   // erCustom
    { 0.00f, 0.00f, 0.00f }    // erMixed
}};

const std::vector<GCodeViewer::Color> GCodeViewer::Options_Colors {{
    { 0.803f, 0.135f, 0.839f },   // Retractions
    { 0.287f, 0.679f, 0.810f },   // Unretractions
    { 0.758f, 0.744f, 0.389f },   // ToolChanges
    { 0.856f, 0.582f, 0.546f },   // ColorChanges
    { 0.322f, 0.942f, 0.512f },   // PausePrints
    { 0.886f, 0.825f, 0.262f }    // CustomGCodes
}};

const std::vector<GCodeViewer::Color> GCodeViewer::Travel_Colors {{
    { 0.219f, 0.282f, 0.609f }, // Move
    { 0.112f, 0.422f, 0.103f }, // Extrude
    { 0.505f, 0.064f, 0.028f }  // Retract
}};

#if ENABLE_SHOW_WIPE_MOVES
const GCodeViewer::Color GCodeViewer::Wipe_Color = { 1.0f, 1.0f, 0.0f };
#endif // ENABLE_SHOW_WIPE_MOVES

const std::vector<GCodeViewer::Color> GCodeViewer::Range_Colors {{
    { 0.043f, 0.173f, 0.478f }, // bluish
    { 0.075f, 0.349f, 0.522f },
    { 0.110f, 0.533f, 0.569f },
    { 0.016f, 0.839f, 0.059f },
    { 0.667f, 0.949f, 0.000f },
    { 0.988f, 0.975f, 0.012f },
    { 0.961f, 0.808f, 0.039f },
    { 0.890f, 0.533f, 0.125f },
    { 0.820f, 0.408f, 0.188f },
    { 0.761f, 0.322f, 0.235f },
    { 0.581f, 0.149f, 0.087f }  // reddish
}};

void GCodeViewer::load(const GCodeProcessor::Result& gcode_result, const Print& print, bool initialized)
{
    init();

    // avoid processing if called with the same gcode_result
    if (m_last_result_id == gcode_result.id)
        return;

    m_last_result_id = gcode_result.id;

    // release gpu memory, if used
    reset();

    load_toolpaths(gcode_result);
    if (m_layers.empty())
        return;

    m_settings_ids = gcode_result.settings_ids;

    if (wxGetApp().is_editor())
        load_shells(print, initialized);
    else {
        Pointfs bed_shape;
        std::string texture;
        std::string model;

        if (!gcode_result.bed_shape.empty()) {
            // bed shape detected in the gcode
            bed_shape = gcode_result.bed_shape;
            auto bundle = wxGetApp().preset_bundle;
            if (bundle != nullptr && !m_settings_ids.printer.empty()) {
                const Preset* preset = bundle->printers.find_preset(m_settings_ids.printer);
                if (preset != nullptr) {
                    model = PresetUtils::system_printer_bed_model(*preset);
                    texture = PresetUtils::system_printer_bed_texture(*preset);
                }
            }
        }
        else {
            // adjust printbed size in dependence of toolpaths bbox
            const double margin = 10.0;
            Vec2d min(m_paths_bounding_box.min(0) - margin, m_paths_bounding_box.min(1) - margin);
            Vec2d max(m_paths_bounding_box.max(0) + margin, m_paths_bounding_box.max(1) + margin);

            Vec2d size = max - min;
            bed_shape = {
                { min(0), min(1) },
                { max(0), min(1) },
                { max(0), min(1) + 0.442265 * size[1]},
                { max(0) - 10.0, min(1) + 0.4711325 * size[1]},
                { max(0) + 10.0, min(1) + 0.5288675 * size[1]},
                { max(0), min(1) + 0.557735 * size[1]},
                { max(0), max(1) },
                { min(0) + 0.557735 * size[0], max(1)},
                { min(0) + 0.5288675 * size[0], max(1) - 10.0},
                { min(0) + 0.4711325 * size[0], max(1) + 10.0},
                { min(0) + 0.442265 * size[0], max(1)},
                { min(0), max(1) } };
        }

        wxGetApp().plater()->set_bed_shape(bed_shape, texture, model, gcode_result.bed_shape.empty());
    }

    m_time_statistics = gcode_result.time_statistics;
}

void GCodeViewer::refresh(const GCodeProcessor::Result& gcode_result, const std::vector<std::string>& str_tool_colors)
{
#if ENABLE_GCODE_VIEWER_STATISTICS
    auto start_time = std::chrono::high_resolution_clock::now();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    if (m_moves_count == 0)
        return;

    wxBusyCursor busy;

    if (m_view_type == EViewType::Tool && !gcode_result.extruder_colors.empty())
        // update tool colors from config stored in the gcode
        m_tool_colors = decode_colors(gcode_result.extruder_colors);
    else
        // update tool colors
        m_tool_colors = decode_colors(str_tool_colors);

    // update ranges for coloring / legend
    m_extrusions.reset_ranges();
    for (size_t i = 0; i < m_moves_count; ++i) {
        // skip first vertex
        if (i == 0)
            continue;

        const GCodeProcessor::MoveVertex& curr = gcode_result.moves[i];

        switch (curr.type)
        {
        case EMoveType::Extrude:
        {
            m_extrusions.ranges.height.update_from(round_to_nearest(curr.height, 2));
            m_extrusions.ranges.width.update_from(round_to_nearest(curr.width, 2));
            m_extrusions.ranges.fan_speed.update_from(curr.fan_speed);
            m_extrusions.ranges.volumetric_rate.update_from(round_to_nearest(curr.volumetric_rate(), 2));
            [[fallthrough]];
        }
        case EMoveType::Travel:
        {
            if (m_buffers[buffer_id(curr.type)].visible)
                m_extrusions.ranges.feedrate.update_from(curr.feedrate);

            break;
        }
        default: { break; }
        }
    }

#if ENABLE_GCODE_VIEWER_STATISTICS
    m_statistics.refresh_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    // update buffers' render paths
    refresh_render_paths(false, false);

    log_memory_used("Refreshed G-code extrusion paths, ");
}

void GCodeViewer::reset()
{
    m_initialized = false;
    m_gl_data_initialized = false;

    m_moves_count = 0;
    for (TBuffer& buffer : m_buffers) {
        buffer.reset();
    }

    m_paths_bounding_box = BoundingBoxf3();
    m_max_bounding_box = BoundingBoxf3();
    m_tool_colors = std::vector<Color>();
    m_extruders_count = 0;
    m_extruder_ids = std::vector<unsigned char>();
    m_extrusions.reset_role_visibility_flags();
    m_extrusions.reset_ranges();
    m_shells.volumes.clear();
    m_layers.reset();
    m_layers_z_range = { 0, 0 };
    m_roles = std::vector<ExtrusionRole>();
    m_time_statistics.reset();
    m_time_estimate_mode = PrintEstimatedTimeStatistics::ETimeMode::Normal;

#if ENABLE_GCODE_VIEWER_STATISTICS
    m_statistics.reset_all();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
}

void GCodeViewer::render() const
{
    auto init_gl_data = [this]() {
        // initializes opengl data of TBuffers
        for (size_t i = 0; i < m_buffers.size(); ++i) {
            TBuffer& buffer = m_buffers[i];
            switch (buffer_type(i)) {
            default: { break; }
            case EMoveType::Tool_change:
            case EMoveType::Color_change:
            case EMoveType::Pause_Print:
            case EMoveType::Custom_GCode:
            case EMoveType::Retract:
            case EMoveType::Unretract: {
                buffer.shader = wxGetApp().is_glsl_version_greater_or_equal_to(1, 20) ? "options_120" : "options_110";
                break;
            }
#if ENABLE_SHOW_WIPE_MOVES
            case EMoveType::Wipe:
#endif // ENABLE_SHOW_WIPE_MOVES
            case EMoveType::Extrude: {
                buffer.shader = "gouraud_light";
                break;
            }
            case EMoveType::Travel: {
                buffer.shader = "toolpaths_lines";
                break;
            }
            }
        }

        // initializes tool marker
        m_sequential_view.marker.init();

        // initializes point sizes
        std::array<int, 2> point_sizes;
        ::glGetIntegerv(GL_ALIASED_POINT_SIZE_RANGE, point_sizes.data());
        m_detected_point_sizes = { static_cast<float>(point_sizes[0]), static_cast<float>(point_sizes[1]) };
        m_gl_data_initialized = true;
    };

#if ENABLE_GCODE_VIEWER_STATISTICS
    m_statistics.reset_opengl();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    // OpenGL data must be initialized after the glContext has been created.
    // This is ensured when this method is called by GLCanvas3D::_render_gcode().
    if (!m_gl_data_initialized)
        init_gl_data();

    if (m_roles.empty())
        return;

    glsafe(::glEnable(GL_DEPTH_TEST));
    render_toolpaths();
    if (m_sequential_view.current.last != m_sequential_view.endpoints.last) {
        m_sequential_view.marker.set_world_position(m_sequential_view.current_position);
        m_sequential_view.marker.render();
    }
    render_shells();
    render_legend();
#if ENABLE_GCODE_VIEWER_STATISTICS
    render_statistics();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
}

void GCodeViewer::update_sequential_view_current(unsigned int first, unsigned int last)
{
    auto is_visible = [this](unsigned int id) {
        for (const TBuffer& buffer : m_buffers) {
            if (buffer.visible) {
                for (const Path& path : buffer.paths) {
                    if (path.first.s_id <= id && id <= path.last.s_id)
                        return true;
                }
            }
        }
        return false;
    };

    int first_diff = static_cast<int>(first) - static_cast<int>(m_sequential_view.last_current.first);
    int last_diff = static_cast<int>(last) - static_cast<int>(m_sequential_view.last_current.last);

    unsigned int new_first = first;
    unsigned int new_last = last;

    if (m_sequential_view.skip_invisible_moves) {
        while (!is_visible(new_first)) {
            if (first_diff > 0)
                ++new_first;
            else
                --new_first;
        }

        while (!is_visible(new_last)) {
            if (last_diff > 0)
                ++new_last;
            else
                --new_last;
        }
    }

    m_sequential_view.current.first = new_first;
    m_sequential_view.current.last = new_last;
    m_sequential_view.last_current = m_sequential_view.current;

    refresh_render_paths(true, true);

    if (new_first != first || new_last != last)
        wxGetApp().plater()->update_preview_moves_slider();
}

bool GCodeViewer::is_toolpath_move_type_visible(EMoveType type) const
{
    size_t id = static_cast<size_t>(buffer_id(type));
    return (id < m_buffers.size()) ? m_buffers[id].visible : false;
}

void GCodeViewer::set_toolpath_move_type_visible(EMoveType type, bool visible)
{
    size_t id = static_cast<size_t>(buffer_id(type));
    if (id < m_buffers.size())
        m_buffers[id].visible = visible;
}

unsigned int GCodeViewer::get_options_visibility_flags() const
{
    auto set_flag = [](unsigned int flags, unsigned int flag, bool active) {
        return active ? (flags | (1 << flag)) : flags;
    };

    unsigned int flags = 0;
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Travel), is_toolpath_move_type_visible(EMoveType::Travel));
#if ENABLE_SHOW_WIPE_MOVES
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Wipe), is_toolpath_move_type_visible(EMoveType::Wipe));
#endif // ENABLE_SHOW_WIPE_MOVES
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Retractions), is_toolpath_move_type_visible(EMoveType::Retract));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Unretractions), is_toolpath_move_type_visible(EMoveType::Unretract));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::ToolChanges), is_toolpath_move_type_visible(EMoveType::Tool_change));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::ColorChanges), is_toolpath_move_type_visible(EMoveType::Color_change));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::PausePrints), is_toolpath_move_type_visible(EMoveType::Pause_Print));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::CustomGCodes), is_toolpath_move_type_visible(EMoveType::Custom_GCode));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Shells), m_shells.visible);
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::ToolMarker), m_sequential_view.marker.is_visible());
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Legend), is_legend_enabled());
    return flags;
}

void GCodeViewer::set_options_visibility_from_flags(unsigned int flags)
{
    auto is_flag_set = [flags](unsigned int flag) {
        return (flags & (1 << flag)) != 0;
    };

    set_toolpath_move_type_visible(EMoveType::Travel, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Travel)));
#if ENABLE_SHOW_WIPE_MOVES
    set_toolpath_move_type_visible(EMoveType::Wipe, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Wipe)));
#endif // ENABLE_SHOW_WIPE_MOVES
    set_toolpath_move_type_visible(EMoveType::Retract, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Retractions)));
    set_toolpath_move_type_visible(EMoveType::Unretract, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Unretractions)));
    set_toolpath_move_type_visible(EMoveType::Tool_change, is_flag_set(static_cast<unsigned int>(Preview::OptionType::ToolChanges)));
    set_toolpath_move_type_visible(EMoveType::Color_change, is_flag_set(static_cast<unsigned int>(Preview::OptionType::ColorChanges)));
    set_toolpath_move_type_visible(EMoveType::Pause_Print, is_flag_set(static_cast<unsigned int>(Preview::OptionType::PausePrints)));
    set_toolpath_move_type_visible(EMoveType::Custom_GCode, is_flag_set(static_cast<unsigned int>(Preview::OptionType::CustomGCodes)));
    m_shells.visible = is_flag_set(static_cast<unsigned int>(Preview::OptionType::Shells));
    m_sequential_view.marker.set_visible(is_flag_set(static_cast<unsigned int>(Preview::OptionType::ToolMarker)));
    enable_legend(is_flag_set(static_cast<unsigned int>(Preview::OptionType::Legend)));
}

void GCodeViewer::set_layers_z_range(const std::array<unsigned int, 2>& layers_z_range)
{
    bool keep_sequential_current_first = layers_z_range[0] >= m_layers_z_range[0];
    bool keep_sequential_current_last = layers_z_range[1] <= m_layers_z_range[1];
    m_layers_z_range = layers_z_range;
    refresh_render_paths(keep_sequential_current_first, keep_sequential_current_last);
    wxGetApp().plater()->update_preview_moves_slider();
}

void GCodeViewer::export_toolpaths_to_obj(const char* filename) const
{
    if (filename == nullptr)
        return;

    if (!has_data())
        return;

    wxBusyCursor busy;

    // the data needed is contained into the Extrude TBuffer
    const TBuffer& buffer = m_buffers[buffer_id(EMoveType::Extrude)];
    if (!buffer.has_data())
        return;

    // collect color information to generate materials
    std::vector<Color> colors;
    for (const RenderPath& path : buffer.render_paths) {
        colors.push_back(path.color);
    }

    // save materials file
    boost::filesystem::path mat_filename(filename);
    mat_filename.replace_extension("mtl");
    FILE* fp = boost::nowide::fopen(mat_filename.string().c_str(), "w");
    if (fp == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "GCodeViewer::export_toolpaths_to_obj: Couldn't open " << mat_filename.string().c_str() << " for writing";
        return;
    }

    fprintf(fp, "# G-Code Toolpaths Materials\n");
    fprintf(fp, "# Generated by %s based on Slic3r\n", SLIC3R_BUILD_ID);

    unsigned int colors_count = 1;
    for (const Color& color : colors) {
        fprintf(fp, "\nnewmtl material_%d\n", colors_count++);
        fprintf(fp, "Ka 1 1 1\n");
        fprintf(fp, "Kd %f %f %f\n", color[0], color[1], color[2]);
        fprintf(fp, "Ks 0 0 0\n");
    }

    fclose(fp);

    // save geometry file
    fp = boost::nowide::fopen(filename, "w");
    if (fp == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "GCodeViewer::export_toolpaths_to_obj: Couldn't open " << filename << " for writing";
        return;
    }

    fprintf(fp, "# G-Code Toolpaths\n");
    fprintf(fp, "# Generated by %s based on Slic3r\n", SLIC3R_BUILD_ID);
    fprintf(fp, "\nmtllib ./%s\n", mat_filename.filename().string().c_str());

    // get vertices data from vertex buffer on gpu
    size_t floats_per_vertex = buffer.vertices.vertex_size_floats();
    std::vector<float> vertices = std::vector<float>(buffer.vertices.count * floats_per_vertex);
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, buffer.vertices.id));
    glsafe(::glGetBufferSubData(GL_ARRAY_BUFFER, 0, buffer.vertices.data_size_bytes(), vertices.data()));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));

    // get indices data from index buffer on gpu
    MultiIndexBuffer indices;
    for (size_t i = 0; i < buffer.indices.size(); ++i) {
        indices.push_back(IndexBuffer(buffer.indices[i].count));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.indices[i].id));
        glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(indices.back().size() * sizeof(unsigned int)), indices.back().data()));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    }

    auto get_vertex = [&vertices, floats_per_vertex](unsigned int id) {
        // extract vertex from vector of floats
        unsigned int base_id = id * floats_per_vertex;
        return Vec3f(vertices[base_id + 0], vertices[base_id + 1], vertices[base_id + 2]);
    };

    struct Segment
    {
        Vec3f v1;
        Vec3f v2;
        Vec3f dir;
        Vec3f right;
        Vec3f up;
        Vec3f rl_displacement;
        Vec3f tb_displacement;
        float length;
    };

    auto generate_segment = [get_vertex](unsigned int start_id, unsigned int end_id, float half_width, float half_height) {
        auto local_basis = [](const Vec3f& dir) {
            // calculate local basis (dir, right, up) on given segment
            std::array<Vec3f, 3> ret;
            ret[0] = dir.normalized();
            if (std::abs(ret[0][2]) < EPSILON) {
                // segment parallel to XY plane
                ret[1] = { ret[0][1], -ret[0][0], 0.0f };
                ret[2] = Vec3f::UnitZ();
            }
            else if (std::abs(std::abs(ret[0].dot(Vec3f::UnitZ())) - 1.0f) < EPSILON) {
                // segment parallel to Z axis
                ret[1] = Vec3f::UnitX();
                ret[2] = Vec3f::UnitY();
            }
            else {
                ret[0] = dir.normalized();
                ret[1] = ret[0].cross(Vec3f::UnitZ()).normalized();
                ret[2] = ret[1].cross(ret[0]);
            }
            return ret;
        };

        Vec3f v1 = get_vertex(start_id) - half_height * Vec3f::UnitZ();
        Vec3f v2 = get_vertex(end_id) - half_height * Vec3f::UnitZ();
        float length = (v2 - v1).norm();
        const auto&& [dir, right, up] = local_basis(v2 - v1);
        return Segment({ v1, v2, dir, right, up, half_width * right, half_height * up, length });
    };

    size_t out_vertices_count = 0;
    unsigned int indices_per_segment = buffer.indices_per_segment();
    unsigned int start_vertex_offset = buffer.start_segment_vertex_offset();
    unsigned int end_vertex_offset = buffer.end_segment_vertex_offset();

    for (size_t i = 0; i < buffer.render_paths.size(); ++i) {
        // get paths segments from buffer paths
        const RenderPath& render_path = buffer.render_paths[i];
        const IndexBuffer& ibuffer = indices[render_path.index_buffer_id];
        const Path& path = buffer.paths[render_path.path_id];
        float half_width = 0.5f * path.width;
        // clamp height to avoid artifacts due to z-fighting when importing the obj file into blender and similar
        float half_height = std::max(0.5f * path.height, 0.005f);

        // generates vertices/normals/triangles
        std::vector<Vec3f> out_vertices;
        std::vector<Vec3f> out_normals;
        using Triangle = std::array<size_t, 3>;
        std::vector<Triangle> out_triangles;
        for (size_t j = 0; j < render_path.offsets.size(); ++j) {
            unsigned int start = static_cast<unsigned int>(render_path.offsets[j] / sizeof(unsigned int));
            unsigned int end = start + render_path.sizes[j];

            for (size_t k = start; k < end; k += static_cast<size_t>(indices_per_segment)) {
                Segment curr = generate_segment(ibuffer[k + start_vertex_offset], ibuffer[k + end_vertex_offset], half_width, half_height);
                if (k == start) {
                    // starting endpoint vertices/normals
                    out_vertices.push_back(curr.v1 + curr.rl_displacement); out_normals.push_back(curr.right);  // right
                    out_vertices.push_back(curr.v1 + curr.tb_displacement); out_normals.push_back(curr.up);     // top
                    out_vertices.push_back(curr.v1 - curr.rl_displacement); out_normals.push_back(-curr.right); // left
                    out_vertices.push_back(curr.v1 - curr.tb_displacement); out_normals.push_back(-curr.up);    // bottom
                    out_vertices_count += 4;

                    // starting cap triangles
                    size_t base_id = out_vertices_count - 4 + 1;
                    out_triangles.push_back({ base_id + 0, base_id + 1, base_id + 2 });
                    out_triangles.push_back({ base_id + 0, base_id + 2, base_id + 3 });
                }
                else {
                    // for the endpoint shared by the current and the previous segments
                    // we keep the top and bottom vertices of the previous vertices
                    // and add new left/right vertices for the current segment
                    out_vertices.push_back(curr.v1 + curr.rl_displacement); out_normals.push_back(curr.right);  // right
                    out_vertices.push_back(curr.v1 - curr.rl_displacement); out_normals.push_back(-curr.right); // left
                    out_vertices_count += 2;

                    size_t first_vertex_id = k - static_cast<size_t>(indices_per_segment);
                    Segment prev = generate_segment(ibuffer[first_vertex_id + start_vertex_offset], ibuffer[first_vertex_id + end_vertex_offset], half_width, half_height);
                    float disp = 0.0f;
                    float cos_dir = prev.dir.dot(curr.dir);
                    if (cos_dir > -0.9998477f) {
                        // if the angle between adjacent segments is smaller than 179 degrees
                        Vec3f med_dir = (prev.dir + curr.dir).normalized();
                        disp = half_width * ::tan(::acos(std::clamp(curr.dir.dot(med_dir), -1.0f, 1.0f)));
                    }

                    Vec3f disp_vec = disp * prev.dir;

                    bool is_right_turn = prev.up.dot(prev.dir.cross(curr.dir)) <= 0.0f;
                    if (cos_dir < 0.7071068f) {
                        // if the angle between two consecutive segments is greater than 45 degrees
                        // we add a cap in the outside corner 
                        // and displace the vertices in the inside corner to the same position, if possible
                        if (is_right_turn) {
                            // corner cap triangles (left)
                            size_t base_id = out_vertices_count - 6 + 1;
                            out_triangles.push_back({ base_id + 5, base_id + 2, base_id + 1 });
                            out_triangles.push_back({ base_id + 5, base_id + 3, base_id + 2 });

                            // update right vertices
                            if (disp > 0.0f && disp < prev.length && disp < curr.length) {
                                base_id = out_vertices.size() - 6;
                                out_vertices[base_id + 0] -= disp_vec;
                                out_vertices[base_id + 4] = out_vertices[base_id + 0];
                            }
                        }
                        else {
                            // corner cap triangles (right)
                            size_t base_id = out_vertices_count - 6 + 1;
                            out_triangles.push_back({ base_id + 0, base_id + 4, base_id + 1 });
                            out_triangles.push_back({ base_id + 0, base_id + 3, base_id + 4 });

                            // update left vertices
                            if (disp > 0.0f && disp < prev.length && disp < curr.length) {
                                base_id = out_vertices.size() - 6;
                                out_vertices[base_id + 2] -= disp_vec;
                                out_vertices[base_id + 5] = out_vertices[base_id + 2];
                            }
                        }
                    }
                    else {
                        // if the angle between two consecutive segments is lesser than 45 degrees
                        // displace the vertices to the same position
                        if (is_right_turn) {
                            size_t base_id = out_vertices.size() - 6;
                            // right
                            out_vertices[base_id + 0] -= disp_vec;
                            out_vertices[base_id + 4] = out_vertices[base_id + 0];
                            // left
                            out_vertices[base_id + 2] += disp_vec;
                            out_vertices[base_id + 5] = out_vertices[base_id + 2];
                        }
                        else {
                            size_t base_id = out_vertices.size() - 6;
                            // right
                            out_vertices[base_id + 0] += disp_vec;
                            out_vertices[base_id + 4] = out_vertices[base_id + 0];
                            // left
                            out_vertices[base_id + 2] -= disp_vec;
                            out_vertices[base_id + 5] = out_vertices[base_id + 2];
                        }
                    }
                }

                // current second endpoint vertices/normals
                out_vertices.push_back(curr.v2 + curr.rl_displacement); out_normals.push_back(curr.right);  // right
                out_vertices.push_back(curr.v2 + curr.tb_displacement); out_normals.push_back(curr.up);     // top
                out_vertices.push_back(curr.v2 - curr.rl_displacement); out_normals.push_back(-curr.right); // left
                out_vertices.push_back(curr.v2 - curr.tb_displacement); out_normals.push_back(-curr.up);    // bottom
                out_vertices_count += 4;

                // sides triangles
                if (k == start) {
                    size_t base_id = out_vertices_count - 8 + 1;
                    out_triangles.push_back({ base_id + 0, base_id + 4, base_id + 5 });
                    out_triangles.push_back({ base_id + 0, base_id + 5, base_id + 1 });
                    out_triangles.push_back({ base_id + 1, base_id + 5, base_id + 6 });
                    out_triangles.push_back({ base_id + 1, base_id + 6, base_id + 2 });
                    out_triangles.push_back({ base_id + 2, base_id + 6, base_id + 7 });
                    out_triangles.push_back({ base_id + 2, base_id + 7, base_id + 3 });
                    out_triangles.push_back({ base_id + 3, base_id + 7, base_id + 4 });
                    out_triangles.push_back({ base_id + 3, base_id + 4, base_id + 0 });
                }
                else {
                    size_t base_id = out_vertices_count - 10 + 1;
                    out_triangles.push_back({ base_id + 4, base_id + 6, base_id + 7 });
                    out_triangles.push_back({ base_id + 4, base_id + 7, base_id + 1 });
                    out_triangles.push_back({ base_id + 1, base_id + 7, base_id + 8 });
                    out_triangles.push_back({ base_id + 1, base_id + 8, base_id + 5 });
                    out_triangles.push_back({ base_id + 5, base_id + 8, base_id + 9 });
                    out_triangles.push_back({ base_id + 5, base_id + 9, base_id + 3 });
                    out_triangles.push_back({ base_id + 3, base_id + 9, base_id + 6 });
                    out_triangles.push_back({ base_id + 3, base_id + 6, base_id + 4 });
                }

                if (k + 2 == end) {
                    // ending cap triangles
                    size_t base_id = out_vertices_count - 4 + 1;
                    out_triangles.push_back({ base_id + 0, base_id + 2, base_id + 1 });
                    out_triangles.push_back({ base_id + 0, base_id + 3, base_id + 2 });
                }
            }
        }

        // save to file
        fprintf(fp, "\n# vertices path %zu\n", i + 1);
        for (const Vec3f& v : out_vertices) {
            fprintf(fp, "v %g %g %g\n", v[0], v[1], v[2]);
        }

        fprintf(fp, "\n# normals path %zu\n", i + 1);
        for (const Vec3f& n : out_normals) {
            fprintf(fp, "vn %g %g %g\n", n[0], n[1], n[2]);
        }

        fprintf(fp, "\n# material path %zu\n", i + 1);
        fprintf(fp, "usemtl material_%zu\n", i + 1);

        fprintf(fp, "\n# triangles path %zu\n", i + 1);
        for (const Triangle& t : out_triangles) {
            fprintf(fp, "f %zu//%zu %zu//%zu %zu//%zu\n", t[0], t[0], t[1], t[1], t[2], t[2]);
        }
    }

    fclose(fp);
}

void GCodeViewer::init()
{
    if (m_initialized)
        return;

    // initializes non opengl data of TBuffers
    for (size_t i = 0; i < m_buffers.size(); ++i) {
        TBuffer& buffer = m_buffers[i];
        switch (buffer_type(i))
        {
        default: { break; }
        case EMoveType::Tool_change:
        case EMoveType::Color_change:
        case EMoveType::Pause_Print:
        case EMoveType::Custom_GCode:
        case EMoveType::Retract:
        case EMoveType::Unretract:
        {
            buffer.render_primitive_type = TBuffer::ERenderPrimitiveType::Point;
            buffer.vertices.format = VBuffer::EFormat::Position;
            break;
        }
#if ENABLE_SHOW_WIPE_MOVES
        case EMoveType::Wipe:
#endif // ENABLE_SHOW_WIPE_MOVES
        case EMoveType::Extrude:
        {
            buffer.render_primitive_type = TBuffer::ERenderPrimitiveType::Triangle;
            buffer.vertices.format = VBuffer::EFormat::PositionNormal3;
            break;
        }
        case EMoveType::Travel:
        {
            buffer.render_primitive_type = TBuffer::ERenderPrimitiveType::Line;
            buffer.vertices.format = VBuffer::EFormat::PositionNormal1;
            break;
        }
        }
    }

    set_toolpath_move_type_visible(EMoveType::Extrude, true);
//    m_sequential_view.skip_invisible_moves = true;

    m_initialized = true;
}

void GCodeViewer::load_toolpaths(const GCodeProcessor::Result& gcode_result)
{
#if ENABLE_GCODE_VIEWER_STATISTICS
    auto start_time = std::chrono::high_resolution_clock::now();
    m_statistics.results_size = SLIC3R_STDVEC_MEMSIZE(gcode_result.moves, GCodeProcessor::MoveVertex);
    m_statistics.results_time = gcode_result.time;
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    // vertices data
    m_moves_count = gcode_result.moves.size();
    if (m_moves_count == 0)
        return;

    unsigned int progress_count = 0;
    static const unsigned int progress_threshold = 1000;
    wxProgressDialog* progress_dialog = wxGetApp().is_gcode_viewer() ?
        new wxProgressDialog(_L("Generating toolpaths"), "...",
            100, wxGetApp().plater(), wxPD_AUTO_HIDE | wxPD_APP_MODAL) : nullptr;

    m_extruders_count = gcode_result.extruders_count;

    for (size_t i = 0; i < m_moves_count; ++i) {
        const GCodeProcessor::MoveVertex& move = gcode_result.moves[i];
        if (wxGetApp().is_gcode_viewer())
            // for the gcode viewer we need all moves to correctly size the printbed
            m_paths_bounding_box.merge(move.position.cast<double>());
        else {
            if (move.type == EMoveType::Extrude && move.width != 0.0f && move.height != 0.0f)
                m_paths_bounding_box.merge(move.position.cast<double>());
        }
    }

    // max bounding box (account for tool marker)
    m_max_bounding_box = m_paths_bounding_box;
    m_max_bounding_box.merge(m_paths_bounding_box.max + m_sequential_view.marker.get_bounding_box().size()[2] * Vec3d::UnitZ());

    auto log_memory_usage = [this](const std::string& label, const std::vector<std::vector<float>>& vertices, const std::vector<MultiIndexBuffer>& indices) {
        long long vertices_size = 0;
        for (size_t i = 0; i < vertices.size(); ++i) {
            vertices_size += SLIC3R_STDVEC_MEMSIZE(vertices[i], float);
        }
        long long indices_size = 0;
        for (size_t i = 0; i < indices.size(); ++i) {
            for (size_t j = 0; j < indices[i].size(); ++j) {
                indices_size += SLIC3R_STDVEC_MEMSIZE(indices[i][j], unsigned int);
            }
        }
        log_memory_used(label, vertices_size + indices_size);
    };

    // format data into the buffers to be rendered as points
    auto add_vertices_as_point = [](const GCodeProcessor::MoveVertex& curr, std::vector<float>& buffer_vertices) {
        for (int j = 0; j < 3; ++j) {
            buffer_vertices.push_back(curr.position[j]);
        }
    };
    auto add_indices_as_point = [](const GCodeProcessor::MoveVertex& curr, TBuffer& buffer,
        unsigned int index_buffer_id, IndexBuffer& buffer_indices, size_t move_id) {
            buffer.add_path(curr, index_buffer_id, buffer_indices.size(), move_id);
            buffer_indices.push_back(static_cast<unsigned int>(buffer_indices.size()));
    };

    // format data into the buffers to be rendered as lines
    auto add_vertices_as_line = [](const GCodeProcessor::MoveVertex& prev, const GCodeProcessor::MoveVertex& curr,
        TBuffer& buffer, std::vector<float>& buffer_vertices) {
            // x component of the normal to the current segment (the normal is parallel to the XY plane)
            float normal_x = (curr.position - prev.position).normalized()[1];

            auto add_vertex = [&buffer_vertices, normal_x](const GCodeProcessor::MoveVertex& vertex) {
                // add position
                for (int j = 0; j < 3; ++j) {
                    buffer_vertices.push_back(vertex.position[j]);
                }
                // add normal x component
                buffer_vertices.push_back(normal_x);
            };

            // add previous vertex
            add_vertex(prev);
            // add current vertex
            add_vertex(curr);
    };
    auto add_indices_as_line = [](const GCodeProcessor::MoveVertex& prev, const GCodeProcessor::MoveVertex& curr, TBuffer& buffer,
        unsigned int index_buffer_id, IndexBuffer& buffer_indices, size_t move_id) {
            // x component of the normal to the current segment (the normal is parallel to the XY plane)
            float normal_x = (curr.position - prev.position).normalized()[1];

            if (prev.type != curr.type || !buffer.paths.back().matches(curr)) {
                // add starting index
                buffer_indices.push_back(static_cast<unsigned int>(buffer_indices.size()));
                buffer.add_path(curr, index_buffer_id, buffer_indices.size() - 1, move_id - 1);
                buffer.paths.back().first.position = prev.position;
            }

            Path& last_path = buffer.paths.back();
            if (last_path.first.i_id != last_path.last.i_id) {
                // add previous index
                buffer_indices.push_back(static_cast<unsigned int>(buffer_indices.size()));
            }

            // add current index
            buffer_indices.push_back(static_cast<unsigned int>(buffer_indices.size()));
            last_path.last = { index_buffer_id, buffer_indices.size() - 1, move_id, curr.position };
    };

    // format data into the buffers to be rendered as solid
    auto add_vertices_as_solid = [](const GCodeProcessor::MoveVertex& prev, const GCodeProcessor::MoveVertex& curr, TBuffer& buffer,
        std::vector<float>& buffer_vertices, size_t move_id) {
            static Vec3f prev_dir;
            static Vec3f prev_up;
            static float prev_length;
            auto store_vertex = [](std::vector<float>& buffer_vertices, const Vec3f& position, const Vec3f& normal) {
                // append position
                for (int j = 0; j < 3; ++j) {
                    buffer_vertices.push_back(position[j]);
                }
                // append normal
                for (int j = 0; j < 3; ++j) {
                    buffer_vertices.push_back(normal[j]);
                }
            };
            auto extract_position_at = [](const std::vector<float>& vertices, size_t id) {
                return Vec3f(vertices[id + 0], vertices[id + 1], vertices[id + 2]);
            };
            auto update_position_at = [](std::vector<float>& vertices, size_t id, const Vec3f& position) {
                vertices[id + 0] = position[0];
                vertices[id + 1] = position[1];
                vertices[id + 2] = position[2];
            };

            if (prev.type != curr.type || !buffer.paths.back().matches(curr)) {
                buffer.add_path(curr, 0, 0, move_id - 1);
                buffer.paths.back().first.position = prev.position;
            }

            unsigned int starting_vertices_size = static_cast<unsigned int>(buffer_vertices.size() / buffer.vertices.vertex_size_floats());

            Vec3f dir = (curr.position - prev.position).normalized();
            Vec3f right = (std::abs(std::abs(dir.dot(Vec3f::UnitZ())) - 1.0f) < EPSILON) ? -Vec3f::UnitY() : Vec3f(dir[1], -dir[0], 0.0f).normalized();
            Vec3f left = -right;
            Vec3f up = right.cross(dir);
            Vec3f down = -up;

            Path& last_path = buffer.paths.back();

            float half_width = 0.5f * last_path.width;
            float half_height = 0.5f * last_path.height;

            Vec3f prev_pos = prev.position - half_height * up;
            Vec3f curr_pos = curr.position - half_height * up;

            float length = (curr_pos - prev_pos).norm();
            if (last_path.vertices_count() == 1) {
                // 1st segment

                // vertices 1st endpoint
                store_vertex(buffer_vertices, prev_pos + half_height * up, up);
                store_vertex(buffer_vertices, prev_pos + half_width * right, right);
                store_vertex(buffer_vertices, prev_pos + half_height * down, down);
                store_vertex(buffer_vertices, prev_pos + half_width * left, left);

                // vertices 2nd endpoint
                store_vertex(buffer_vertices, curr_pos + half_height * up, up);
                store_vertex(buffer_vertices, curr_pos + half_width * right, right);
                store_vertex(buffer_vertices, curr_pos + half_height * down, down);
                store_vertex(buffer_vertices, curr_pos + half_width * left, left);
            }
            else {
                // any other segment
                float displacement = 0.0f;
                float cos_dir = prev_dir.dot(dir);
                if (cos_dir > -0.9998477f) {
                    // if the angle between adjacent segments is smaller than 179 degrees
                    Vec3f med_dir = (prev_dir + dir).normalized();
                    displacement = half_width * ::tan(::acos(std::clamp(dir.dot(med_dir), -1.0f, 1.0f)));
                }

                Vec3f displacement_vec = displacement * prev_dir;
                bool can_displace = displacement > 0.0f && displacement < prev_length&& displacement < length;

                size_t prev_right_id = (starting_vertices_size - 3) * buffer.vertices.vertex_size_floats();
                size_t prev_left_id = (starting_vertices_size - 1) * buffer.vertices.vertex_size_floats();
                Vec3f prev_right_pos = extract_position_at(buffer_vertices, prev_right_id);
                Vec3f prev_left_pos = extract_position_at(buffer_vertices, prev_left_id);

                bool is_right_turn = prev_up.dot(prev_dir.cross(dir)) <= 0.0f;
                // whether the angle between adjacent segments is greater than 45 degrees
                bool is_sharp = cos_dir < 0.7071068f;

                bool right_displaced = false;
                bool left_displaced = false;

                // displace the vertex (inner with respect to the corner) of the previous segment 2nd enpoint, if possible
                if (can_displace) {
                    if (is_right_turn) {
                        prev_right_pos -= displacement_vec;
                        update_position_at(buffer_vertices, prev_right_id, prev_right_pos);
                        right_displaced = true;
                    }
                    else {
                        prev_left_pos -= displacement_vec;
                        update_position_at(buffer_vertices, prev_left_id, prev_left_pos);
                        left_displaced = true;
                    }
                }

                if (!is_sharp) {
                    // displace the vertex (outer with respect to the corner) of the previous segment 2nd enpoint, if possible
                    if (can_displace) {
                        if (is_right_turn) {
                            prev_left_pos += displacement_vec;
                            update_position_at(buffer_vertices, prev_left_id, prev_left_pos);
                            left_displaced = true;
                        }
                        else {
                            prev_right_pos += displacement_vec;
                            update_position_at(buffer_vertices, prev_right_id, prev_right_pos);
                            right_displaced = true;
                        }
                    }

                    // vertices 1st endpoint (top and bottom are from previous segment 2nd endpoint)
                    // vertices position matches that of the previous segment 2nd endpoint, if displaced
                    store_vertex(buffer_vertices, right_displaced ? prev_right_pos : prev_pos + half_width * right, right);
                    store_vertex(buffer_vertices, left_displaced ? prev_left_pos : prev_pos + half_width * left, left);
                }
                else {
                    // vertices 1st endpoint (top and bottom are from previous segment 2nd endpoint)
                    // the inner corner vertex position matches that of the previous segment 2nd endpoint, if displaced
                    if (is_right_turn) {
                        store_vertex(buffer_vertices, right_displaced ? prev_right_pos : prev_pos + half_width * right, right);
                        store_vertex(buffer_vertices, prev_pos + half_width * left, left);
                    }
                    else {
                        store_vertex(buffer_vertices, prev_pos + half_width * right, right);
                        store_vertex(buffer_vertices, left_displaced ? prev_left_pos : prev_pos + half_width * left, left);
                    }
                }

                // vertices 2nd endpoint
                store_vertex(buffer_vertices, curr_pos + half_height * up, up);
                store_vertex(buffer_vertices, curr_pos + half_width * right, right);
                store_vertex(buffer_vertices, curr_pos + half_height * down, down);
                store_vertex(buffer_vertices, curr_pos + half_width * left, left);
            }

            last_path.last = { 0, 0, move_id, curr.position };
            prev_dir = dir;
            prev_up = up;
            prev_length = length;
    };

    auto add_indices_as_solid = [](const GCodeProcessor::MoveVertex& prev, const GCodeProcessor::MoveVertex& curr, TBuffer& buffer,
        size_t& buffer_vertices_size, unsigned int index_buffer_id, IndexBuffer& buffer_indices, size_t move_id) {
            static Vec3f prev_dir;
            static Vec3f prev_up;
            static float prev_length;
            auto store_triangle = [](IndexBuffer& buffer_indices, unsigned int i1, unsigned int i2, unsigned int i3) {
                buffer_indices.push_back(i1);
                buffer_indices.push_back(i2);
                buffer_indices.push_back(i3);
            };
            auto append_dummy_cap = [store_triangle](IndexBuffer& buffer_indices, unsigned int id) {
                store_triangle(buffer_indices, id, id, id);
                store_triangle(buffer_indices, id, id, id);
            };

            if (prev.type != curr.type || !buffer.paths.back().matches(curr)) {
                buffer.add_path(curr, index_buffer_id, buffer_indices.size(), move_id - 1);
                buffer.paths.back().first.position = prev.position;
            }

            unsigned int starting_vertices_size = static_cast<unsigned int>(buffer_vertices_size);

            Vec3f dir = (curr.position - prev.position).normalized();
            Vec3f right = (std::abs(std::abs(dir.dot(Vec3f::UnitZ())) - 1.0f) < EPSILON) ? -Vec3f::UnitY() : Vec3f(dir[1], -dir[0], 0.0f).normalized();
            Vec3f up = right.cross(dir);

            Path& last_path = buffer.paths.back();

            float half_width = 0.5f * last_path.width;
            float half_height = 0.5f * last_path.height;

            Vec3f prev_pos = prev.position - half_height * up;
            Vec3f curr_pos = curr.position - half_height * up;

            float length = (curr_pos - prev_pos).norm();
            if (last_path.vertices_count() == 1) {
                // 1st segment
                buffer_vertices_size += 8;

                // triangles starting cap
                store_triangle(buffer_indices, starting_vertices_size + 0, starting_vertices_size + 2, starting_vertices_size + 1);
                store_triangle(buffer_indices, starting_vertices_size + 0, starting_vertices_size + 3, starting_vertices_size + 2);

                // dummy triangles outer corner cap
                append_dummy_cap(buffer_indices, starting_vertices_size);

                // triangles sides
                store_triangle(buffer_indices, starting_vertices_size + 0, starting_vertices_size + 1, starting_vertices_size + 4);
                store_triangle(buffer_indices, starting_vertices_size + 1, starting_vertices_size + 5, starting_vertices_size + 4);
                store_triangle(buffer_indices, starting_vertices_size + 1, starting_vertices_size + 2, starting_vertices_size + 5);
                store_triangle(buffer_indices, starting_vertices_size + 2, starting_vertices_size + 6, starting_vertices_size + 5);
                store_triangle(buffer_indices, starting_vertices_size + 2, starting_vertices_size + 3, starting_vertices_size + 6);
                store_triangle(buffer_indices, starting_vertices_size + 3, starting_vertices_size + 7, starting_vertices_size + 6);
                store_triangle(buffer_indices, starting_vertices_size + 3, starting_vertices_size + 0, starting_vertices_size + 7);
                store_triangle(buffer_indices, starting_vertices_size + 0, starting_vertices_size + 4, starting_vertices_size + 7);

                // triangles ending cap
                store_triangle(buffer_indices, starting_vertices_size + 4, starting_vertices_size + 6, starting_vertices_size + 7);
                store_triangle(buffer_indices, starting_vertices_size + 4, starting_vertices_size + 5, starting_vertices_size + 6);
            }
            else {
                // any other segment
                float displacement = 0.0f;
                float cos_dir = prev_dir.dot(dir);
                if (cos_dir > -0.9998477f) {
                    // if the angle between adjacent segments is smaller than 179 degrees
                    Vec3f med_dir = (prev_dir + dir).normalized();
                    displacement = half_width * ::tan(::acos(std::clamp(dir.dot(med_dir), -1.0f, 1.0f)));
                }

                Vec3f displacement_vec = displacement * prev_dir;
                bool can_displace = displacement > 0.0f && displacement < prev_length && displacement < length;

                bool is_right_turn = prev_up.dot(prev_dir.cross(dir)) <= 0.0f;
                // whether the angle between adjacent segments is greater than 45 degrees
                bool is_sharp = cos_dir < 0.7071068f;

                bool right_displaced = false;
                bool left_displaced = false;

                if (!is_sharp) {
                    if (can_displace) {
                        if (is_right_turn)
                            left_displaced = true;
                        else
                            right_displaced = true;
                    }
                }

                buffer_vertices_size += 6;

                // triangles starting cap
                store_triangle(buffer_indices, starting_vertices_size - 4, starting_vertices_size - 2, starting_vertices_size + 0);
                store_triangle(buffer_indices, starting_vertices_size - 4, starting_vertices_size + 1, starting_vertices_size - 2);

                // triangles outer corner cap
                if (is_right_turn) {
                    if (left_displaced)
                        // dummy triangles
                        append_dummy_cap(buffer_indices, starting_vertices_size);
                    else {
                        store_triangle(buffer_indices, starting_vertices_size - 4, starting_vertices_size + 1, starting_vertices_size - 1);
                        store_triangle(buffer_indices, starting_vertices_size + 1, starting_vertices_size - 2, starting_vertices_size - 1);
                    }
                }
                else {
                    if (right_displaced)
                        // dummy triangles
                        append_dummy_cap(buffer_indices, starting_vertices_size);
                    else {
                        store_triangle(buffer_indices, starting_vertices_size - 4, starting_vertices_size - 3, starting_vertices_size + 0);
                        store_triangle(buffer_indices, starting_vertices_size - 3, starting_vertices_size - 2, starting_vertices_size + 0);
                    }
                }

                // triangles sides
                store_triangle(buffer_indices, starting_vertices_size - 4, starting_vertices_size + 0, starting_vertices_size + 2);
                store_triangle(buffer_indices, starting_vertices_size + 0, starting_vertices_size + 3, starting_vertices_size + 2);
                store_triangle(buffer_indices, starting_vertices_size + 0, starting_vertices_size - 2, starting_vertices_size + 3);
                store_triangle(buffer_indices, starting_vertices_size - 2, starting_vertices_size + 4, starting_vertices_size + 3);
                store_triangle(buffer_indices, starting_vertices_size - 2, starting_vertices_size + 1, starting_vertices_size + 4);
                store_triangle(buffer_indices, starting_vertices_size + 1, starting_vertices_size + 5, starting_vertices_size + 4);
                store_triangle(buffer_indices, starting_vertices_size + 1, starting_vertices_size - 4, starting_vertices_size + 5);
                store_triangle(buffer_indices, starting_vertices_size - 4, starting_vertices_size + 2, starting_vertices_size + 5);

                // triangles ending cap
                store_triangle(buffer_indices, starting_vertices_size + 2, starting_vertices_size + 4, starting_vertices_size + 5);
                store_triangle(buffer_indices, starting_vertices_size + 2, starting_vertices_size + 3, starting_vertices_size + 4);
            }

            last_path.last = { index_buffer_id, buffer_indices.size() - 1, move_id, curr.position };
            prev_dir = dir;
            prev_up = up;
            prev_length = length;
    };

    wxBusyCursor busy;

    // to reduce the peak in memory usage, we split the generation of the vertex and index buffers in two steps.
    // the data are deleted as soon as they are sent to the gpu.
    std::vector<std::vector<float>> vertices(m_buffers.size());
    std::vector<MultiIndexBuffer> indices(m_buffers.size());
#if ENABLE_SHOW_OPTION_POINT_LAYERS
    std::vector<float> options_zs;
#endif // ENABLE_SHOW_OPTION_POINT_LAYERS

    // toolpaths data -> extract vertices from result
    for (size_t i = 0; i < m_moves_count; ++i) {
        // skip first vertex
        if (i == 0)
            continue;

        ++progress_count;
        if (progress_dialog != nullptr && progress_count % progress_threshold == 0) {
            progress_dialog->Update(int(100.0f * float(i) / (2.0f * float(m_moves_count))),
                _L("Generating vertex buffer") + ": " + wxNumberFormatter::ToString(100.0 * double(i) / double(m_moves_count), 0, wxNumberFormatter::Style_None) + "%");
            progress_dialog->Fit();
            progress_count = 0;
        }

        const GCodeProcessor::MoveVertex& prev = gcode_result.moves[i - 1];
        const GCodeProcessor::MoveVertex& curr = gcode_result.moves[i];

        unsigned char id = buffer_id(curr.type);
        TBuffer& buffer = m_buffers[id];
        std::vector<float>& buffer_vertices = vertices[id];

        switch (buffer.render_primitive_type)
        {
        case TBuffer::ERenderPrimitiveType::Point: {
            add_vertices_as_point(curr, buffer_vertices);
            break;
        }
        case TBuffer::ERenderPrimitiveType::Line: {
            add_vertices_as_line(prev, curr, buffer, buffer_vertices);
            break;
        }
        case TBuffer::ERenderPrimitiveType::Triangle: {
            add_vertices_as_solid(prev, curr, buffer, buffer_vertices, i);
            break;
        }
        }

#if ENABLE_SHOW_OPTION_POINT_LAYERS
        EMoveType type = buffer_type(id);
        if (type == EMoveType::Pause_Print || type == EMoveType::Custom_GCode) {
            const float* const last_z = options_zs.empty() ? nullptr : &options_zs.back();
            float z = static_cast<double>(curr.position[2]);
            if (last_z == nullptr || z < *last_z - EPSILON || *last_z + EPSILON < z)
                options_zs.emplace_back(curr.position[2]);
        }
#endif // ENABLE_SHOW_OPTION_POINT_LAYERS
    }

#if ENABLE_SHOW_WIPE_MOVES
    // move the wipe toolpaths half height up to render them on proper position
    std::vector<float>& wipe_vertices = vertices[buffer_id(EMoveType::Wipe)];
    for (size_t i = 2; i < wipe_vertices.size(); i += 3) {
        wipe_vertices[i] += 0.5f * GCodeProcessor::Wipe_Height;
    }
#endif // ENABLE_SHOW_WIPE_MOVES

    log_memory_usage("Loaded G-code generated vertex buffers, ", vertices, indices);

    // toolpaths data -> send vertices data to gpu
    for (size_t i = 0; i < m_buffers.size(); ++i) {
        TBuffer& buffer = m_buffers[i];

        const std::vector<float>& buffer_vertices = vertices[i];
        buffer.vertices.count = buffer_vertices.size() / buffer.vertices.vertex_size_floats();
#if ENABLE_GCODE_VIEWER_STATISTICS
        m_statistics.vertices_gpu_size += buffer_vertices.size() * sizeof(float);
        m_statistics.max_vertices_in_vertex_buffer = std::max(m_statistics.max_vertices_in_vertex_buffer, static_cast<long long>(buffer.vertices.count));
#endif // ENABLE_GCODE_VIEWER_STATISTICS

        glsafe(::glGenBuffers(1, &buffer.vertices.id));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, buffer.vertices.id));
        glsafe(::glBufferData(GL_ARRAY_BUFFER, buffer_vertices.size() * sizeof(float), buffer_vertices.data(), GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
    }

    // dismiss vertices data, no more needed
    std::vector<std::vector<float>>().swap(vertices);

    // toolpaths data -> extract indices from result
    // ensure that at least one index buffer is defined for each multibuffer
    for (auto i : indices) {
        i.push_back(IndexBuffer());
    }
    // paths may have been filled while extracting vertices,
    // so reset them, they will be filled again while extracting indices
    for (TBuffer& buffer : m_buffers) {
        buffer.paths.clear();
    }
    // variable used to keep track of the current size (in vertices) of the vertex buffer
#if ENABLE_SHOW_WIPE_MOVES
    std::vector<size_t> curr_buffer_vertices_size(m_buffers.size(), 0);
#else
    size_t curr_buffer_vertices_size = 0;
#endif // ENABLE_SHOW_WIPE_MOVES
    for (size_t i = 0; i < m_moves_count; ++i) {
        // skip first vertex
        if (i == 0)
            continue;

        ++progress_count;
        if (progress_dialog != nullptr && progress_count % progress_threshold == 0) {
            progress_dialog->Update(int(100.0f * float(m_moves_count + i) / (2.0f * float(m_moves_count))),
                _L("Generating index buffers") + ": " + wxNumberFormatter::ToString(100.0 * double(i) / double(m_moves_count), 0, wxNumberFormatter::Style_None) + "%");
            progress_dialog->Fit();
            progress_count = 0;
        }

        const GCodeProcessor::MoveVertex& prev = gcode_result.moves[i - 1];
        const GCodeProcessor::MoveVertex& curr = gcode_result.moves[i];

        unsigned char id = buffer_id(curr.type);
        TBuffer& buffer = m_buffers[id];
        MultiIndexBuffer& buffer_indices = indices[id];
        if (buffer_indices.empty())
            buffer_indices.push_back(IndexBuffer());

        static const size_t THRESHOLD = 1024 * 1024 * 128;
        // if adding the indices for the current segment exceeds the threshold size of the current index buffer
        // create another index buffer, and move the current path indices into it
        if (buffer_indices.back().size() >= THRESHOLD - static_cast<size_t>(buffer.indices_per_segment())) {
            buffer_indices.push_back(IndexBuffer());
#if ENABLE_SHOW_WIPE_MOVES
            if (buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::Point) {
#else
            if (curr.type == EMoveType::Extrude || curr.type == EMoveType::Travel) {
#endif // ENABLE_SHOW_WIPE_MOVES
                if (!(prev.type != curr.type || !buffer.paths.back().matches(curr))) {
                    Path& last_path = buffer.paths.back();
                    size_t delta_id = last_path.last.i_id - last_path.first.i_id;

                    // move indices of the last path from the previous into the new index buffer
                    IndexBuffer& src_buffer = buffer_indices[buffer_indices.size() - 2];
                    IndexBuffer& dst_buffer = buffer_indices[buffer_indices.size() - 1];
                    std::move(src_buffer.begin() + last_path.first.i_id, src_buffer.end(), std::back_inserter(dst_buffer));
                    src_buffer.erase(src_buffer.begin() + last_path.first.i_id, src_buffer.end());

                    // updates path indices
                    last_path.first.b_id = buffer_indices.size() - 1;
                    last_path.first.i_id = 0;
                    last_path.last.b_id = buffer_indices.size() - 1;
                    last_path.last.i_id = delta_id;
                }
            }
        }

        switch (buffer.render_primitive_type)
        {
        case TBuffer::ERenderPrimitiveType::Point: {
            add_indices_as_point(curr, buffer, static_cast<unsigned int>(buffer_indices.size()) - 1, buffer_indices.back(), i);
            break;
        }
        case TBuffer::ERenderPrimitiveType::Line: {
            add_indices_as_line(prev, curr, buffer, static_cast<unsigned int>(buffer_indices.size()) - 1, buffer_indices.back(), i);
            break;
        }
        case TBuffer::ERenderPrimitiveType::Triangle: {
#if ENABLE_SHOW_WIPE_MOVES
            add_indices_as_solid(prev, curr, buffer, curr_buffer_vertices_size[id], static_cast<unsigned int>(buffer_indices.size()) - 1, buffer_indices.back(), i);
#else
            add_indices_as_solid(prev, curr, buffer, curr_buffer_vertices_size, static_cast<unsigned int>(buffer_indices.size()) - 1, buffer_indices.back(), i);
#endif // ENABLE_SHOW_WIPE_MOVES
            break;
        }
        }
    }

    if (progress_dialog != nullptr) {
        progress_dialog->Update(100, "");
        progress_dialog->Fit();
    }

    log_memory_usage("Loaded G-code generated indices buffers, ", vertices, indices);

    // toolpaths data -> send indices data to gpu
    for (size_t i = 0; i < m_buffers.size(); ++i) {
        TBuffer& buffer = m_buffers[i];

        for (size_t j = 0; j < indices[i].size(); ++j) {
            const IndexBuffer& buffer_indices = indices[i][j];
            buffer.indices.push_back(IBuffer());
            IBuffer& ibuffer = buffer.indices.back();
            ibuffer.count = buffer_indices.size();
#if ENABLE_GCODE_VIEWER_STATISTICS
            m_statistics.indices_gpu_size += ibuffer.count * sizeof(unsigned int);
            m_statistics.max_indices_in_index_buffer = std::max(m_statistics.max_indices_in_index_buffer, static_cast<long long>(ibuffer.count));
#endif // ENABLE_GCODE_VIEWER_STATISTICS

            if (ibuffer.count > 0) {
                glsafe(::glGenBuffers(1, &ibuffer.id));
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibuffer.id));
                glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, buffer_indices.size() * sizeof(unsigned int), buffer_indices.data(), GL_STATIC_DRAW));
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
            }
        }
    }

#if ENABLE_GCODE_VIEWER_STATISTICS
    for (const TBuffer& buffer : m_buffers) {
        m_statistics.paths_size += SLIC3R_STDVEC_MEMSIZE(buffer.paths, Path);
    }
    unsigned int travel_buffer_id = buffer_id(EMoveType::Travel);
    const MultiIndexBuffer& travel_buffer_indices = indices[travel_buffer_id];
    for (size_t i = 0; i < travel_buffer_indices.size(); ++i) {
        m_statistics.travel_segments_count = travel_buffer_indices[i].size() / m_buffers[travel_buffer_id].indices_per_segment();
    }
#if ENABLE_SHOW_WIPE_MOVES
    unsigned int wipe_buffer_id = buffer_id(EMoveType::Wipe);
    const MultiIndexBuffer& wipe_buffer_indices = indices[wipe_buffer_id];
    for (size_t i = 0; i < wipe_buffer_indices.size(); ++i) {
        m_statistics.wipe_segments_count = wipe_buffer_indices[i].size() / m_buffers[wipe_buffer_id].indices_per_segment();
    }
#endif // ENABLE_SHOW_WIPE_MOVES
    unsigned int extrude_buffer_id = buffer_id(EMoveType::Extrude);
    const MultiIndexBuffer& extrude_buffer_indices = indices[extrude_buffer_id];
    for (size_t i = 0; i < extrude_buffer_indices.size(); ++i) {
        m_statistics.extrude_segments_count = extrude_buffer_indices[i].size() / m_buffers[extrude_buffer_id].indices_per_segment();
    }
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    // dismiss indices data, no more needed
    std::vector<MultiIndexBuffer>().swap(indices);

    // layers zs / roles / extruder ids / cp color ids -> extract from result
    size_t last_travel_s_id = 0;
    for (size_t i = 0; i < m_moves_count; ++i) {
        const GCodeProcessor::MoveVertex& move = gcode_result.moves[i];
        if (move.type == EMoveType::Extrude) {
            // layers zs
            const double* const last_z = m_layers.empty() ? nullptr : &m_layers.get_zs().back();
            double z = static_cast<double>(move.position[2]);
            if (last_z == nullptr || z < *last_z - EPSILON || *last_z + EPSILON < z)
                m_layers.append(z, { last_travel_s_id, i });
            else
                m_layers.get_endpoints().back().last = i;
            // extruder ids
            m_extruder_ids.emplace_back(move.extruder_id);
            // roles
            if (i > 0)
                m_roles.emplace_back(move.extrusion_role);
        }
        else if (move.type == EMoveType::Travel) {
            if (i - last_travel_s_id > 1 && !m_layers.empty())
                m_layers.get_endpoints().back().last = i;

            last_travel_s_id = i;
        }
    }

    // set layers z range
    if (!m_layers.empty())
        m_layers_z_range = { 0, static_cast<unsigned int>(m_layers.size() - 1) };

#if ENABLE_SHOW_OPTION_POINT_LAYERS
    // change color of paths whose layer contains option points
    if (!options_zs.empty()) {
        TBuffer& extrude_buffer = m_buffers[buffer_id(EMoveType::Extrude)];
        for (Path& path : extrude_buffer.paths) {
            float z = path.first.position[2];
            if (std::find_if(options_zs.begin(), options_zs.end(), [z](float f) { return f - EPSILON <= z && z <= f + EPSILON; }) != options_zs.end())
                path.cp_color_id = 255 - path.cp_color_id;
        }
    }
#endif // ENABLE_SHOW_OPTION_POINT_LAYERS

    // roles -> remove duplicates
    std::sort(m_roles.begin(), m_roles.end());
    m_roles.erase(std::unique(m_roles.begin(), m_roles.end()), m_roles.end());
    m_roles.shrink_to_fit();

    // extruder ids -> remove duplicates
    std::sort(m_extruder_ids.begin(), m_extruder_ids.end());
    m_extruder_ids.erase(std::unique(m_extruder_ids.begin(), m_extruder_ids.end()), m_extruder_ids.end());
    m_extruder_ids.shrink_to_fit();

    log_memory_usage("Loaded G-code generated extrusion paths, ", vertices, indices);

#if ENABLE_GCODE_VIEWER_STATISTICS
    m_statistics.load_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    if (progress_dialog != nullptr)
        progress_dialog->Destroy();
}

void GCodeViewer::load_shells(const Print& print, bool initialized)
{
    if (print.objects().empty())
        // no shells, return
        return;

    // adds objects' volumes 
    int object_id = 0;
    for (const PrintObject* obj : print.objects()) {
        const ModelObject* model_obj = obj->model_object();

        std::vector<int> instance_ids(model_obj->instances.size());
        for (int i = 0; i < (int)model_obj->instances.size(); ++i) {
            instance_ids[i] = i;
        }

        m_shells.volumes.load_object(model_obj, object_id, instance_ids, "object", initialized);

        ++object_id;
    }

    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptFFF) {
        // adds wipe tower's volume
        double max_z = print.objects()[0]->model_object()->get_model()->bounding_box().max(2);
        const PrintConfig& config = print.config();
        size_t extruders_count = config.nozzle_diameter.size();
        if ((extruders_count > 1) && config.wipe_tower && !config.complete_objects) {
            const DynamicPrintConfig& print_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
            double layer_height = print_config.opt_float("layer_height");
            double first_layer_height = print_config.get_abs_value("first_layer_height", layer_height);
            double nozzle_diameter = print.config().nozzle_diameter.values[0];
            float depth = print.wipe_tower_data(extruders_count, first_layer_height, nozzle_diameter).depth;
            float brim_width = print.wipe_tower_data(extruders_count, first_layer_height, nozzle_diameter).brim_width;

            m_shells.volumes.load_wipe_tower_preview(1000, config.wipe_tower_x, config.wipe_tower_y, config.wipe_tower_width, depth, max_z, config.wipe_tower_rotation_angle,
                !print.is_step_done(psWipeTower), brim_width, initialized);
        }
    }

    // remove modifiers
    while (true) {
        GLVolumePtrs::iterator it = std::find_if(m_shells.volumes.volumes.begin(), m_shells.volumes.volumes.end(), [](GLVolume* volume) { return volume->is_modifier; });
        if (it != m_shells.volumes.volumes.end()) {
            delete (*it);
            m_shells.volumes.volumes.erase(it);
        }
        else
            break;
    } 

    for (GLVolume* volume : m_shells.volumes.volumes) {
        volume->zoom_to_volumes = false;
        volume->color[3] = 0.25f;
        volume->force_native_color = true;
        volume->set_render_color();
    }
}

void GCodeViewer::refresh_render_paths(bool keep_sequential_current_first, bool keep_sequential_current_last) const
{
#if ENABLE_GCODE_VIEWER_STATISTICS
    auto start_time = std::chrono::high_resolution_clock::now();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    auto extrusion_color = [this](const Path& path) {
        Color color;
        switch (m_view_type)
        {
        case EViewType::FeatureType:    { color = Extrusion_Role_Colors[static_cast<unsigned int>(path.role)]; break; }
        case EViewType::Height:         { color = m_extrusions.ranges.height.get_color_at(path.height); break; }
        case EViewType::Width:          { color = m_extrusions.ranges.width.get_color_at(path.width); break; }
        case EViewType::Feedrate:       { color = m_extrusions.ranges.feedrate.get_color_at(path.feedrate); break; }
        case EViewType::FanSpeed:       { color = m_extrusions.ranges.fan_speed.get_color_at(path.fan_speed); break; }
        case EViewType::VolumetricRate: { color = m_extrusions.ranges.volumetric_rate.get_color_at(path.volumetric_rate); break; }
        case EViewType::Tool:           { color = m_tool_colors[path.extruder_id]; break; }
#if ENABLE_SHOW_OPTION_POINT_LAYERS
        case EViewType::ColorPrint:     {
            if (path.cp_color_id >= static_cast<unsigned char>(m_tool_colors.size())) {
                color = { 0.5f, 0.5f, 0.5f };
//                // complementary color
//                color = m_tool_colors[255 - path.cp_color_id];
//                color = { 1.0f - color[0], 1.0f - color[1], 1.0f - color[2] };
            }
            else
                color = m_tool_colors[path.cp_color_id];

            break;
        }
#else
        case EViewType::ColorPrint:     { color = m_tool_colors[path.cp_color_id]; break; }
#endif // ENABLE_SHOW_OPTION_POINT_LAYERS
        default:                        { color = { 1.0f, 1.0f, 1.0f }; break; }
        }

        return color;
    };

    auto travel_color = [this](const Path& path) {
        return (path.delta_extruder < 0.0f) ? Travel_Colors[2] /* Retract */ :
            ((path.delta_extruder > 0.0f) ? Travel_Colors[1] /* Extrude */ :
                Travel_Colors[0] /* Move */);
    };

    auto is_in_layers_range = [this](const Path& path, size_t min_id, size_t max_id) {
        auto in_layers_range = [this, min_id, max_id](size_t id) {
            return m_layers.get_endpoints_at(min_id).first <= id && id <= m_layers.get_endpoints_at(max_id).last;
        };

        return in_layers_range(path.first.s_id) || in_layers_range(path.last.s_id);
    };

    auto is_travel_in_layers_range = [this](size_t path_id, size_t min_id, size_t max_id) {
        auto is_in_z_range = [](const Path& path, double min_z, double max_z) {
            auto in_z_range = [min_z, max_z](double z) {
                return min_z - EPSILON < z&& z < max_z + EPSILON;
            };

            return in_z_range(path.first.position[2]) || in_z_range(path.last.position[2]);
        };

        const TBuffer& buffer = m_buffers[buffer_id(EMoveType::Travel)];
        if (path_id >= buffer.paths.size())
            return false;

        Path path = buffer.paths[path_id];
        size_t first = path_id;
        size_t last = path_id;

        // check adjacent paths
        while (first > 0 && path.first.position.isApprox(buffer.paths[first - 1].last.position)) {
            --first;
            path.first = buffer.paths[first].first;
        }
        while (last < buffer.paths.size() - 1 && path.last.position.isApprox(buffer.paths[last + 1].first.position)) {
            ++last;
            path.last = buffer.paths[last].last;
        }

        size_t min_s_id = m_layers.get_endpoints_at(min_id).first;
        size_t max_s_id = m_layers.get_endpoints_at(max_id).last;

        return (min_s_id <= path.first.s_id && path.first.s_id <= max_s_id) ||
            (min_s_id <= path.last.s_id && path.last.s_id <= max_s_id);
    };

#if ENABLE_GCODE_VIEWER_STATISTICS
    m_statistics.render_paths_size = 0;
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    bool top_layer_only = get_app_config()->get("seq_top_layer_only") == "1";

    SequentialView::Endpoints global_endpoints = { m_moves_count , 0 };
    SequentialView::Endpoints top_layer_endpoints = global_endpoints;
    if (top_layer_only || !keep_sequential_current_first) m_sequential_view.current.first = 0;
    if (!keep_sequential_current_last) m_sequential_view.current.last = m_moves_count;

    // first pass: collect visible paths and update sequential view data
    std::vector<std::tuple<TBuffer*, unsigned int, unsigned int>> paths;
    for (TBuffer& buffer : m_buffers) {
        // reset render paths
        buffer.render_paths.clear();

        if (!buffer.visible)
            continue;

        for (size_t i = 0; i < buffer.paths.size(); ++i) {
            const Path& path = buffer.paths[i];
            if (path.type == EMoveType::Travel) {
                if (!is_travel_in_layers_range(i, m_layers_z_range[0], m_layers_z_range[1]))
                    continue;
            }
            else if (!is_in_layers_range(path, m_layers_z_range[0], m_layers_z_range[1]))
                continue;

            if (path.type == EMoveType::Extrude && !is_visible(path))
                continue;

            // store valid path
            paths.push_back({ &buffer, path.first.b_id, static_cast<unsigned int>(i) });

            global_endpoints.first = std::min(global_endpoints.first, path.first.s_id);
            global_endpoints.last = std::max(global_endpoints.last, path.last.s_id);

            if (top_layer_only) {
                if (path.type == EMoveType::Travel) {
                    if (is_travel_in_layers_range(i, m_layers_z_range[1], m_layers_z_range[1])) {
                        top_layer_endpoints.first = std::min(top_layer_endpoints.first, path.first.s_id);
                        top_layer_endpoints.last = std::max(top_layer_endpoints.last, path.last.s_id);
                    }
                }
                else if (is_in_layers_range(path, m_layers_z_range[1], m_layers_z_range[1])) {
                    top_layer_endpoints.first = std::min(top_layer_endpoints.first, path.first.s_id);
                    top_layer_endpoints.last = std::max(top_layer_endpoints.last, path.last.s_id);
                }
            }
        }
    }

    // update current sequential position
    m_sequential_view.current.first = !top_layer_only && keep_sequential_current_first ? std::clamp(m_sequential_view.current.first, global_endpoints.first, global_endpoints.last) : global_endpoints.first;
    m_sequential_view.current.last = keep_sequential_current_last ? std::clamp(m_sequential_view.current.last, global_endpoints.first, global_endpoints.last) : global_endpoints.last;

    // get the world position from gpu
    bool found = false;
    for (const TBuffer& buffer : m_buffers) {
        // searches the path containing the current position
        for (const Path& path : buffer.paths) {
            if (path.contains(m_sequential_view.current.last)) {
                unsigned int offset = static_cast<unsigned int>(m_sequential_view.current.last - path.first.s_id);
                if (offset > 0) {
                    if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Line)
                        offset = 2 * offset - 1;
                    else if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle) {
                        unsigned int indices_count = buffer.indices_per_segment();
                        offset = indices_count * (offset - 1) + (indices_count - 6);
                    }
                }
                offset += static_cast<unsigned int>(path.first.i_id);

                // gets the index from the index buffer on gpu
                unsigned int index = 0;
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.indices[path.first.b_id].id));
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>(offset * sizeof(unsigned int)), static_cast<GLsizeiptr>(sizeof(unsigned int)), static_cast<void*>(&index)));
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

                // gets the position from the vertices buffer on gpu
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, buffer.vertices.id));
                glsafe(::glGetBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(index * buffer.vertices.vertex_size_bytes()), static_cast<GLsizeiptr>(3 * sizeof(float)), static_cast<void*>(m_sequential_view.current_position.data())));
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
                found = true;
                break;
            }
        }
        if (found)
            break;
    }

    // second pass: filter paths by sequential data and collect them by color
    for (const auto& [buffer, index_buffer_id, path_id] : paths) {
        const Path& path = buffer->paths[path_id];
        if (m_sequential_view.current.last <= path.first.s_id || path.last.s_id <= m_sequential_view.current.first)
            continue;

        Color color;
        switch (path.type)
        {
        case EMoveType::Extrude: {
            if (!top_layer_only ||
                m_sequential_view.current.last == global_endpoints.last ||
                is_in_layers_range(path, m_layers_z_range[1], m_layers_z_range[1]))
                color = extrusion_color(path);
            else
                color = { 0.25f, 0.25f, 0.25f };

            break;
        }
        case EMoveType::Travel: {
            if (!top_layer_only || m_sequential_view.current.last == global_endpoints.last || is_travel_in_layers_range(path_id, m_layers_z_range[1], m_layers_z_range[1]))
                color = (m_view_type == EViewType::Feedrate || m_view_type == EViewType::Tool || m_view_type == EViewType::ColorPrint) ? extrusion_color(path) : travel_color(path);
            else
                color = { 0.25f, 0.25f, 0.25f };

            break;
        }
#if ENABLE_SHOW_WIPE_MOVES
        case EMoveType::Wipe: { color = Wipe_Color; break; }
#endif // ENABLE_SHOW_WIPE_MOVES
        default: { color = { 0.0f, 0.0f, 0.0f }; break; }
        }

        unsigned int ibuffer_id = index_buffer_id;
        auto it = std::find_if(buffer->render_paths.begin(), buffer->render_paths.end(),
            [color, ibuffer_id](const RenderPath& path) { return path.index_buffer_id == ibuffer_id && path.color == color; });
        if (it == buffer->render_paths.end()) {
            it = buffer->render_paths.insert(buffer->render_paths.end(), RenderPath());
            it->color = color;
            it->path_id = path_id;
            it->index_buffer_id = index_buffer_id;
        }

        unsigned int segments_count = std::min(m_sequential_view.current.last, path.last.s_id) - std::max(m_sequential_view.current.first, path.first.s_id) + 1;
        unsigned int size_in_indices = 0;
        switch (buffer->render_primitive_type)
        {
        case TBuffer::ERenderPrimitiveType::Point: { size_in_indices = segments_count; break; }
        case TBuffer::ERenderPrimitiveType::Line:
        case TBuffer::ERenderPrimitiveType::Triangle: { size_in_indices = buffer->indices_per_segment() * (segments_count - 1); break; }
        }
        it->sizes.push_back(size_in_indices);

        unsigned int delta_1st = 0;
        if (path.first.s_id < m_sequential_view.current.first && m_sequential_view.current.first <= path.last.s_id)
            delta_1st = m_sequential_view.current.first - path.first.s_id;

        if (buffer->render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle)
            delta_1st *= buffer->indices_per_segment();

        it->offsets.push_back(static_cast<size_t>((path.first.i_id + delta_1st) * sizeof(unsigned int)));
    }

    // set sequential data to their final value
    m_sequential_view.endpoints = top_layer_only ? top_layer_endpoints : global_endpoints;
    m_sequential_view.current.first = !top_layer_only && keep_sequential_current_first ? std::clamp(m_sequential_view.current.first, m_sequential_view.endpoints.first, m_sequential_view.endpoints.last) : m_sequential_view.endpoints.first;

    wxGetApp().plater()->enable_preview_moves_slider(!paths.empty());

#if ENABLE_GCODE_VIEWER_STATISTICS
    for (const TBuffer& buffer : m_buffers) {
        m_statistics.render_paths_size += SLIC3R_STDVEC_MEMSIZE(buffer.render_paths, RenderPath);
        for (const RenderPath& path : buffer.render_paths) {
            m_statistics.render_paths_size += SLIC3R_STDVEC_MEMSIZE(path.sizes, unsigned int);
            m_statistics.render_paths_size += SLIC3R_STDVEC_MEMSIZE(path.offsets, size_t);
        }
    }
    m_statistics.refresh_paths_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
}

void GCodeViewer::render_toolpaths() const
{
#if ENABLE_FIXED_SCREEN_SIZE_POINT_MARKERS
    float point_size = 20.0f;
#else
    float point_size = 0.8f;
#endif // ENABLE_FIXED_SCREEN_SIZE_POINT_MARKERS
    std::array<float, 4> light_intensity = { 0.25f, 0.70f, 0.75f, 0.75f };
    const Camera& camera = wxGetApp().plater()->get_camera();
    double zoom = camera.get_zoom();
    const std::array<int, 4>& viewport = camera.get_viewport();
    float near_plane_height = camera.get_type() == Camera::Perspective ? static_cast<float>(viewport[3]) / (2.0f * static_cast<float>(2.0 * std::tan(0.5 * Geometry::deg2rad(camera.get_fov())))) :
        static_cast<float>(viewport[3]) * 0.0005;

    auto set_uniform_color = [](const std::array<float, 3>& color, GLShaderProgram& shader) {
        std::array<float, 4> color4 = { color[0], color[1], color[2], 1.0f };
        shader.set_uniform("uniform_color", color4);
    };

    auto render_as_points = [this, zoom, point_size, near_plane_height, set_uniform_color]
    (const TBuffer& buffer, unsigned int index_buffer_id, EOptionsColors color_id, GLShaderProgram& shader) {
        set_uniform_color(Options_Colors[static_cast<unsigned int>(color_id)], shader);
#if ENABLE_FIXED_SCREEN_SIZE_POINT_MARKERS
        shader.set_uniform("use_fixed_screen_size", 1);
#else
        shader.set_uniform("use_fixed_screen_size", 0);
#endif // ENABLE_FIXED_SCREEN_SIZE_POINT_MARKERS
        shader.set_uniform("zoom", zoom);
        shader.set_uniform("percent_outline_radius", 0.0f);
        shader.set_uniform("percent_center_radius", 0.33f);
        shader.set_uniform("point_size", point_size);
        shader.set_uniform("near_plane_height", near_plane_height);

        glsafe(::glEnable(GL_VERTEX_PROGRAM_POINT_SIZE));
        glsafe(::glEnable(GL_POINT_SPRITE));

        for (const RenderPath& path : buffer.render_paths) {
            if (path.index_buffer_id == index_buffer_id) {
                glsafe(::glMultiDrawElements(GL_POINTS, (const GLsizei*)path.sizes.data(), GL_UNSIGNED_INT, (const void* const*)path.offsets.data(), (GLsizei)path.sizes.size()));
#if ENABLE_GCODE_VIEWER_STATISTICS
                ++m_statistics.gl_multi_points_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
            }
        }

        glsafe(::glDisable(GL_POINT_SPRITE));
        glsafe(::glDisable(GL_VERTEX_PROGRAM_POINT_SIZE));
    };

    auto render_as_lines = [this, light_intensity, set_uniform_color](const TBuffer& buffer, unsigned int index_buffer_id, GLShaderProgram& shader) {
        shader.set_uniform("light_intensity", light_intensity);
        for (const RenderPath& path : buffer.render_paths) {
            if (path.index_buffer_id == index_buffer_id) {
                set_uniform_color(path.color, shader);
                glsafe(::glMultiDrawElements(GL_LINES, (const GLsizei*)path.sizes.data(), GL_UNSIGNED_INT, (const void* const*)path.offsets.data(), (GLsizei)path.sizes.size()));
#if ENABLE_GCODE_VIEWER_STATISTICS
                ++m_statistics.gl_multi_lines_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
            }
        }
    };

    auto render_as_triangles = [this, set_uniform_color](const TBuffer& buffer, unsigned int index_buffer_id, GLShaderProgram& shader) {
        for (const RenderPath& path : buffer.render_paths) {
            if (path.index_buffer_id == index_buffer_id) {
                set_uniform_color(path.color, shader);
                glsafe(::glMultiDrawElements(GL_TRIANGLES, (const GLsizei*)path.sizes.data(), GL_UNSIGNED_INT, (const void* const*)path.offsets.data(), (GLsizei)path.sizes.size()));
#if ENABLE_GCODE_VIEWER_STATISTICS
                ++m_statistics.gl_multi_triangles_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
            }
        }
    };

    auto line_width = [](double zoom) {
        return (zoom < 5.0) ? 1.0 : (1.0 + 5.0 * (zoom - 5.0) / (100.0 - 5.0));
    };

    glsafe(::glLineWidth(static_cast<GLfloat>(line_width(zoom))));

    unsigned char begin_id = buffer_id(EMoveType::Retract);
    unsigned char end_id = buffer_id(EMoveType::Count);

    for (unsigned char i = begin_id; i < end_id; ++i) {
        const TBuffer& buffer = m_buffers[i];
        if (!buffer.visible || !buffer.has_data())
            continue;

        GLShaderProgram* shader = wxGetApp().get_shader(buffer.shader.c_str());
        if (shader != nullptr) {
            shader->start_using();

            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, buffer.vertices.id));
            glsafe(::glVertexPointer(buffer.vertices.position_size_floats(), GL_FLOAT, buffer.vertices.vertex_size_bytes(), (const void*)buffer.vertices.position_offset_size()));
            glsafe(::glEnableClientState(GL_VERTEX_ARRAY));
            bool has_normals = buffer.vertices.normal_size_floats() > 0;
            if (has_normals) {
                glsafe(::glNormalPointer(GL_FLOAT, buffer.vertices.vertex_size_bytes(), (const void*)buffer.vertices.normal_offset_size()));
                glsafe(::glEnableClientState(GL_NORMAL_ARRAY));
            }

            for (size_t j = 0; j < buffer.indices.size(); ++j) {
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.indices[j].id));

                switch (buffer.render_primitive_type)
                {
                case TBuffer::ERenderPrimitiveType::Point:
                {
                    EOptionsColors color;
                    switch (buffer_type(i))
                    {
                    case EMoveType::Tool_change:  { color = EOptionsColors::ToolChanges; break; }
                    case EMoveType::Color_change: { color = EOptionsColors::ColorChanges; break; }
                    case EMoveType::Pause_Print:  { color = EOptionsColors::PausePrints; break; }
                    case EMoveType::Custom_GCode: { color = EOptionsColors::CustomGCodes; break; }
                    case EMoveType::Retract:      { color = EOptionsColors::Retractions; break; }
                    case EMoveType::Unretract:    { color = EOptionsColors::Unretractions; break; }
                    }
                    render_as_points(buffer, static_cast<unsigned int>(j), color, *shader);
                    break;
                }
                case TBuffer::ERenderPrimitiveType::Line:
                {
                    render_as_lines(buffer, static_cast<unsigned int>(j), *shader);
                    break;
                }
                case TBuffer::ERenderPrimitiveType::Triangle:
                {
                    render_as_triangles(buffer, static_cast<unsigned int>(j), *shader);
                    break;
                }
                }

                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
            }

            if (has_normals)
                glsafe(::glDisableClientState(GL_NORMAL_ARRAY));

            glsafe(::glDisableClientState(GL_VERTEX_ARRAY));
            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));

            shader->stop_using();
        }
    }
}

void GCodeViewer::render_shells() const
{
    if (!m_shells.visible || m_shells.volumes.empty())
        return;

    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;

//    glsafe(::glDepthMask(GL_FALSE));

    shader->start_using();
    m_shells.volumes.render(GLVolumeCollection::Transparent, true, wxGetApp().plater()->get_camera().get_view_matrix());
    shader->stop_using();

//    glsafe(::glDepthMask(GL_TRUE));
}

void GCodeViewer::render_legend() const
{
    if (!m_legend_enabled)
        return;

    ImGuiWrapper& imgui = *wxGetApp().imgui();

    imgui.set_next_window_pos(0.0f, 0.0f, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::SetNextWindowBgAlpha(0.6f);
    imgui.begin(std::string("Legend"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    enum class EItemType : unsigned char
    {
        Rect,
        Circle,
        Hexagon,
        Line
    };

    const PrintEstimatedTimeStatistics::Mode& time_mode = m_time_statistics.modes[static_cast<size_t>(m_time_estimate_mode)];

    float icon_size = ImGui::GetTextLineHeight();
    float percent_bar_size = 2.0f * ImGui::GetTextLineHeight();

    auto append_item = [this, draw_list, icon_size, percent_bar_size, &imgui](EItemType type, const Color& color, const std::string& label,
        bool visible = true, const std::string& time = "", float percent = 0.0f, float max_percent = 0.0f, const std::array<float, 2>& offsets = { 0.0f, 0.0f },
        std::function<void()> callback = nullptr) {
            if (!visible)
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.3333f);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            switch (type) {
            default:
            case EItemType::Rect: {
                draw_list->AddRectFilled({ pos.x + 1.0f, pos.y + 1.0f }, { pos.x + icon_size - 1.0f, pos.y + icon_size - 1.0f },
                    ImGui::GetColorU32({ color[0], color[1], color[2], 1.0f }));
                break;
            }
            case EItemType::Circle: {
                ImVec2 center(0.5f * (pos.x + pos.x + icon_size), 0.5f * (pos.y + pos.y + icon_size));
                if (m_buffers[buffer_id(EMoveType::Retract)].shader == "options_120") {
                    draw_list->AddCircleFilled(center, 0.5f * icon_size,
                        ImGui::GetColorU32({ 0.5f * color[0], 0.5f * color[1], 0.5f * color[2], 1.0f }), 16);
                    float radius = 0.5f * icon_size;
                    draw_list->AddCircleFilled(center, radius, ImGui::GetColorU32({ color[0], color[1], color[2], 1.0f }), 16);
                    radius = 0.5f * icon_size * 0.01f * 33.0f;
                    draw_list->AddCircleFilled(center, radius, ImGui::GetColorU32({ 0.5f * color[0], 0.5f * color[1], 0.5f * color[2], 1.0f }), 16);
                }
                else
                    draw_list->AddCircleFilled(center, 0.5f * icon_size, ImGui::GetColorU32({ color[0], color[1], color[2], 1.0f }), 16);

                break;
            }
            case EItemType::Hexagon: {
                ImVec2 center(0.5f * (pos.x + pos.x + icon_size), 0.5f * (pos.y + pos.y + icon_size));
                draw_list->AddNgonFilled(center, 0.5f * icon_size, ImGui::GetColorU32({ color[0], color[1], color[2], 1.0f }), 6);
                break;
            }
            case EItemType::Line: {
                draw_list->AddLine({ pos.x + 1, pos.y + icon_size - 1 }, { pos.x + icon_size - 1, pos.y + 1 }, ImGui::GetColorU32({ color[0], color[1], color[2], 1.0f }), 3.0f);
                break;
            }
            }

            // draw text
            ImGui::Dummy({ icon_size, icon_size });
            ImGui::SameLine();
            if (callback != nullptr) {
                if (ImGui::MenuItem(label.c_str()))
                    callback();
                else {
                    // show tooltip
                    if (ImGui::IsItemHovered()) {
                        if (!visible)
                            ImGui::PopStyleVar();
                        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGuiWrapper::COL_WINDOW_BACKGROUND);
                        ImGui::BeginTooltip();
                        imgui.text(visible ? _u8L("Click to hide") : _u8L("Click to show"));
                        ImGui::EndTooltip();
                        ImGui::PopStyleColor();
                        if (!visible)
                            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.3333f);

                        // to avoid the tooltip to change size when moving the mouse
                        wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
                        wxGetApp().plater()->get_current_canvas3D()->request_extra_frame();
                    }
                }

                if (!time.empty()) {
                    ImGui::SameLine(offsets[0]);
                    imgui.text(time);
                    ImGui::SameLine(offsets[1]);
                    pos = ImGui::GetCursorScreenPos();
                    float width = std::max(1.0f, percent_bar_size * percent / max_percent);
                    draw_list->AddRectFilled({ pos.x, pos.y + 2.0f }, { pos.x + width, pos.y + icon_size - 2.0f },
                        ImGui::GetColorU32(ImGuiWrapper::COL_ORANGE_LIGHT));
                    ImGui::Dummy({ percent_bar_size, icon_size });
                    ImGui::SameLine();
                    char buf[64];
                    ::sprintf(buf, "%.1f%%", 100.0f * percent);
                    ImGui::TextUnformatted((percent > 0.0f) ? buf : "");
                }
            }
            else
                imgui.text(label);

            if (!visible)
                ImGui::PopStyleVar();
    };

    auto append_range = [this, draw_list, &imgui, append_item](const Extrusions::Range& range, unsigned int decimals) {
        auto append_range_item = [this, draw_list, &imgui, append_item](int i, float value, unsigned int decimals) {
            char buf[1024];
            ::sprintf(buf, "%.*f", decimals, value);
            append_item(EItemType::Rect, Range_Colors[i], buf);
        };

        if (range.count == 1)
            // single item use case
            append_range_item(0, range.min, decimals);
        else if (range.count == 2) {
            append_range_item(static_cast<int>(Range_Colors.size()) - 1, range.max, decimals);
            append_range_item(0, range.min, decimals);
        }
        else {
            float step_size = range.step_size();
            for (int i = static_cast<int>(Range_Colors.size()) - 1; i >= 0; --i) {
                append_range_item(i, range.min + static_cast<float>(i) * step_size, decimals);
            }
        }
    };

    auto append_headers = [&imgui](const std::array<std::string, 3>& texts, const std::array<float, 2>& offsets) {
        imgui.text(texts[0]);
        ImGui::SameLine(offsets[0]);
        imgui.text(texts[1]);
        ImGui::SameLine(offsets[1]);
        imgui.text(texts[2]);
        ImGui::Separator();
    };

    auto max_width = [](const std::vector<std::string>& items, const std::string& title, float extra_size = 0.0f) {
        float ret = ImGui::CalcTextSize(title.c_str()).x;
        for (const std::string& item : items) {
            ret = std::max(ret, extra_size + ImGui::CalcTextSize(item.c_str()).x);
        }
        return ret;
    };

    auto calculate_offsets = [max_width](const std::vector<std::string>& labels, const std::vector<std::string>& times,
        const std::array<std::string, 2>& titles, float extra_size = 0.0f) {
            const ImGuiStyle& style = ImGui::GetStyle();
            std::array<float, 2> ret = { 0.0f, 0.0f };
            ret[0] = max_width(labels, titles[0], extra_size) + 3.0f * style.ItemSpacing.x;
            ret[1] = ret[0] + max_width(times, titles[1]) + style.ItemSpacing.x;
            return ret;
    };

    auto color_print_ranges = [this](unsigned char extruder_id, const std::vector<CustomGCode::Item>& custom_gcode_per_print_z) {
        std::vector<std::pair<Color, std::pair<double, double>>> ret;
        ret.reserve(custom_gcode_per_print_z.size());

        for (const auto& item : custom_gcode_per_print_z) {
            if (extruder_id + 1 != static_cast<unsigned char>(item.extruder))
                continue;

            if (item.type != ColorChange)
                continue;

            const std::vector<double> zs = m_layers.get_zs();
            auto lower_b = std::lower_bound(zs.begin(), zs.end(), item.print_z - Slic3r::DoubleSlider::epsilon());
            if (lower_b == zs.end())
                continue;

            double current_z = *lower_b;
            double previous_z = (lower_b == zs.begin()) ? 0.0 : *(--lower_b);

            // to avoid duplicate values, check adding values
            if (ret.empty() || !(ret.back().second.first == previous_z && ret.back().second.second == current_z))
                ret.push_back({ decode_color(item.color), { previous_z, current_z } });
        }

        return ret;
    };

    auto upto_label = [](double z) {
        char buf[64];
        ::sprintf(buf, "%.2f", z);
        return _u8L("up to") + " " + std::string(buf) + " " + _u8L("mm");
    };

    auto above_label = [](double z) {
        char buf[64];
        ::sprintf(buf, "%.2f", z);
        return _u8L("above") + " " + std::string(buf) + " " + _u8L("mm");
    };

    auto fromto_label = [](double z1, double z2) {
        char buf1[64];
        ::sprintf(buf1, "%.2f", z1);
        char buf2[64];
        ::sprintf(buf2, "%.2f", z2);
        return _u8L("from") + " " + std::string(buf1) + " " + _u8L("to") + " " + std::string(buf2) + " " + _u8L("mm");
    };

    auto role_time_and_percent = [this, time_mode](ExtrusionRole role) {
        auto it = std::find_if(time_mode.roles_times.begin(), time_mode.roles_times.end(), [role](const std::pair<ExtrusionRole, float>& item) { return role == item.first; });
        return (it != time_mode.roles_times.end()) ? std::make_pair(it->second, it->second / time_mode.time) : std::make_pair(0.0f, 0.0f);
    };

    // data used to properly align items in columns when showing time
    std::array<float, 2> offsets = { 0.0f, 0.0f };
    std::vector<std::string> labels;
    std::vector<std::string> times;
    std::vector<float> percents;
    float max_percent = 0.0f;

    if (m_view_type == EViewType::FeatureType) {
        // calculate offsets to align time/percentage data
        for (size_t i = 0; i < m_roles.size(); ++i) {
            ExtrusionRole role = m_roles[i];
            if (role < erCount) {
                labels.push_back(_u8L(ExtrusionEntity::role_to_string(role)));
                auto [time, percent] = role_time_and_percent(role);
                times.push_back((time > 0.0f) ? short_time(get_time_dhms(time)) : "");
                percents.push_back(percent);
                max_percent = std::max(max_percent, percent);
            }
        }

        offsets = calculate_offsets(labels, times, { _u8L("Feature type"), _u8L("Time") }, icon_size);
    }

    // extrusion paths section -> title
    switch (m_view_type)
    {
    case EViewType::FeatureType:
    {
        append_headers({ _u8L("Feature type"), _u8L("Time"), _u8L("Percentage") }, offsets);
        break;
    }
    case EViewType::Height:         { imgui.title(_u8L("Height (mm)")); break; }
    case EViewType::Width:          { imgui.title(_u8L("Width (mm)")); break; }
    case EViewType::Feedrate:       { imgui.title(_u8L("Speed (mm/s)")); break; }
    case EViewType::FanSpeed:       { imgui.title(_u8L("Fan Speed (%)")); break; }
    case EViewType::VolumetricRate: { imgui.title(_u8L("Volumetric flow rate (mm³/s)")); break; }
    case EViewType::Tool:           { imgui.title(_u8L("Tool")); break; }
    case EViewType::ColorPrint:     { imgui.title(_u8L("Color Print")); break; }
    default: { break; }
    }

    // extrusion paths section -> items
    switch (m_view_type)
    {
    case EViewType::FeatureType:
    {
        for (size_t i = 0; i < m_roles.size(); ++i) {
            ExtrusionRole role = m_roles[i];
            if (role >= erCount)
                continue;
            bool visible = is_visible(role);
            append_item(EItemType::Rect, Extrusion_Role_Colors[static_cast<unsigned int>(role)], labels[i],
                visible, times[i], percents[i], max_percent, offsets, [this, role, visible]() {
                    m_extrusions.role_visibility_flags = visible ? m_extrusions.role_visibility_flags & ~(1 << role) : m_extrusions.role_visibility_flags | (1 << role);
                    // update buffers' render paths
                    refresh_render_paths(false, false);
                    wxGetApp().plater()->update_preview_moves_slider();
                    wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
                    wxGetApp().plater()->update_preview_bottom_toolbar();
                }
            );
        }
        break;
    }
    case EViewType::Height:         { append_range(m_extrusions.ranges.height, 3); break; }
    case EViewType::Width:          { append_range(m_extrusions.ranges.width, 3); break; }
    case EViewType::Feedrate:       { append_range(m_extrusions.ranges.feedrate, 1); break; }
    case EViewType::FanSpeed:       { append_range(m_extrusions.ranges.fan_speed, 0); break; }
    case EViewType::VolumetricRate: { append_range(m_extrusions.ranges.volumetric_rate, 3); break; }
    case EViewType::Tool:
    {
        // shows only extruders actually used
        for (unsigned char i : m_extruder_ids) {
            append_item(EItemType::Rect, m_tool_colors[i], _u8L("Extruder") + " " + std::to_string(i + 1));
        }
        break;
    }
    case EViewType::ColorPrint:
    {
        const std::vector<CustomGCode::Item>& custom_gcode_per_print_z = wxGetApp().plater()->model().custom_gcode_per_print_z.gcodes;
        if (m_extruders_count == 1) { // single extruder use case
            std::vector<std::pair<Color, std::pair<double, double>>> cp_values = color_print_ranges(0, custom_gcode_per_print_z);
            const int items_cnt = static_cast<int>(cp_values.size());
            if (items_cnt == 0) { // There are no color changes, but there are some pause print or custom Gcode
                append_item(EItemType::Rect, m_tool_colors.front(), _u8L("Default color"));
            }
            else {
                for (int i = items_cnt; i >= 0; --i) {
                    // create label for color change item
                    if (i == 0) {
                        append_item(EItemType::Rect, m_tool_colors[0], upto_label(cp_values.front().second.first));
                        break;
                    }
                    else if (i == items_cnt) {
                        append_item(EItemType::Rect, cp_values[i - 1].first, above_label(cp_values[i - 1].second.second));
                        continue;
                    }
                    append_item(EItemType::Rect, cp_values[i - 1].first, fromto_label(cp_values[i - 1].second.second, cp_values[i].second.first));
                }
            }
        }
        else // multi extruder use case
        {
            // shows only extruders actually used
            for (unsigned char i : m_extruder_ids) {
                std::vector<std::pair<Color, std::pair<double, double>>> cp_values = color_print_ranges(i, custom_gcode_per_print_z);
                const int items_cnt = static_cast<int>(cp_values.size());
                if (items_cnt == 0) { // There are no color changes, but there are some pause print or custom Gcode
                    append_item(EItemType::Rect, m_tool_colors[i], _u8L("Extruder") + " " + std::to_string(i + 1) + " " + _u8L("default color"));
                }
                else {
                    for (int j = items_cnt; j >= 0; --j) {
                        // create label for color change item
                        std::string label = _u8L("Extruder") + " " + std::to_string(i + 1);
                        if (j == 0) {
                            label += " " + upto_label(cp_values.front().second.first);
                            append_item(EItemType::Rect, m_tool_colors[i], label);
                            break;
                        }
                        else if (j == items_cnt) {
                            label += " " + above_label(cp_values[j - 1].second.second);
                            append_item(EItemType::Rect, cp_values[j - 1].first, label);
                            continue;
                        }

                        label += " " + fromto_label(cp_values[j - 1].second.second, cp_values[j].second.first);
                        append_item(EItemType::Rect, cp_values[j - 1].first, label);
                    }
                }
            }
        }

        break;
    }
    default: { break; }
    }

    // partial estimated printing time section
    if (m_view_type == EViewType::ColorPrint) {
        using Times = std::pair<float, float>;
        using TimesList = std::vector<std::pair<CustomGCode::Type, Times>>;

        // helper structure containig the data needed to render the time items
        struct PartialTime
        {
            enum class EType : unsigned char
            {
                Print,
                ColorChange,
                Pause
            };
            EType type;
            int extruder_id;
            Color color1;
            Color color2;
            Times times;
        };
        using PartialTimes = std::vector<PartialTime>;

        auto generate_partial_times = [this](const TimesList& times) {
            PartialTimes items;

            std::vector<CustomGCode::Item> custom_gcode_per_print_z = wxGetApp().plater()->model().custom_gcode_per_print_z.gcodes;
            int extruders_count = wxGetApp().extruders_edited_cnt();
            std::vector<Color> last_color(extruders_count);
            for (int i = 0; i < extruders_count; ++i) {
                last_color[i] = m_tool_colors[i];
            }
            int last_extruder_id = 1;
            for (const auto& time_rec : times) {
                switch (time_rec.first)
                {
                case CustomGCode::PausePrint: {
                    auto it = std::find_if(custom_gcode_per_print_z.begin(), custom_gcode_per_print_z.end(), [time_rec](const CustomGCode::Item& item) { return item.type == time_rec.first; });
                    if (it != custom_gcode_per_print_z.end()) {
                        items.push_back({ PartialTime::EType::Print, it->extruder, last_color[it->extruder - 1], Color(), time_rec.second });
                        items.push_back({ PartialTime::EType::Pause, it->extruder, Color(), Color(), time_rec.second });
                        custom_gcode_per_print_z.erase(it);
                    }
                    break;
                }
                case CustomGCode::ColorChange: {
                    auto it = std::find_if(custom_gcode_per_print_z.begin(), custom_gcode_per_print_z.end(), [time_rec](const CustomGCode::Item& item) { return item.type == time_rec.first; });
                    if (it != custom_gcode_per_print_z.end()) {
                        items.push_back({ PartialTime::EType::Print, it->extruder, last_color[it->extruder - 1], Color(), time_rec.second });
                        items.push_back({ PartialTime::EType::ColorChange, it->extruder, last_color[it->extruder - 1], decode_color(it->color), time_rec.second });
                        last_color[it->extruder - 1] = decode_color(it->color);
                        last_extruder_id = it->extruder;
                        custom_gcode_per_print_z.erase(it);
                    }
                    else
                        items.push_back({ PartialTime::EType::Print, last_extruder_id, last_color[last_extruder_id - 1], Color(), time_rec.second });

                    break;
                }
                default: { break; }
                }
            }

            return items;
        };

        auto append_color_change = [this, &imgui](const Color& color1, const Color& color2, const std::array<float, 2>& offsets, const Times& times) {
            imgui.text(_u8L("Color change"));
            ImGui::SameLine();

            float icon_size = ImGui::GetTextLineHeight();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            pos.x -= 0.5f * ImGui::GetStyle().ItemSpacing.x;

            draw_list->AddRectFilled({ pos.x + 1.0f, pos.y + 1.0f }, { pos.x + icon_size - 1.0f, pos.y + icon_size - 1.0f },
                ImGui::GetColorU32({ color1[0], color1[1], color1[2], 1.0f }));
            pos.x += icon_size;
            draw_list->AddRectFilled({ pos.x + 1.0f, pos.y + 1.0f }, { pos.x + icon_size - 1.0f, pos.y + icon_size - 1.0f },
                ImGui::GetColorU32({ color2[0], color2[1], color2[2], 1.0f }));

            ImGui::SameLine(offsets[0]);
            imgui.text(short_time(get_time_dhms(times.second - times.first)));
        };

        auto append_print = [this, &imgui](const Color& color, const std::array<float, 2>& offsets, const Times& times) {
            imgui.text(_u8L("Print"));
            ImGui::SameLine();

            float icon_size = ImGui::GetTextLineHeight();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            pos.x -= 0.5f * ImGui::GetStyle().ItemSpacing.x;

            draw_list->AddRectFilled({ pos.x + 1.0f, pos.y + 1.0f }, { pos.x + icon_size - 1.0f, pos.y + icon_size - 1.0f },
                ImGui::GetColorU32({ color[0], color[1], color[2], 1.0f }));

            ImGui::SameLine(offsets[0]);
            imgui.text(short_time(get_time_dhms(times.second)));
            ImGui::SameLine(offsets[1]);
            imgui.text(short_time(get_time_dhms(times.first)));
        };

        PartialTimes partial_times = generate_partial_times(time_mode.custom_gcode_times);
        if (!partial_times.empty()) {
            labels.clear();
            times.clear();

            for (const PartialTime& item : partial_times) {
                switch (item.type)
                {
                case PartialTime::EType::Print:       { labels.push_back(_u8L("Print")); break; }
                case PartialTime::EType::Pause:       { labels.push_back(_u8L("Pause")); break; }
                case PartialTime::EType::ColorChange: { labels.push_back(_u8L("Color change")); break; }
                }
                times.push_back(short_time(get_time_dhms(item.times.second)));
            }
            offsets = calculate_offsets(labels, times, { _u8L("Event"), _u8L("Remaining time") }, 2.0f * icon_size);

            ImGui::Spacing();
            append_headers({ _u8L("Event"), _u8L("Remaining time"), _u8L("Duration") }, offsets);
            for (const PartialTime& item : partial_times) {
                switch (item.type)
                {
                case PartialTime::EType::Print: {
                    append_print(item.color1, offsets, item.times);
                    break;
                }
                case PartialTime::EType::Pause: {
                    imgui.text(_u8L("Pause"));
                    ImGui::SameLine(offsets[0]);
                    imgui.text(short_time(get_time_dhms(item.times.second - item.times.first)));
                    break;
                }
                case PartialTime::EType::ColorChange: {
                    append_color_change(item.color1, item.color2, offsets, item.times);
                    break;
                }
                }
            }
        }
    }

    // travel paths section
    if (m_buffers[buffer_id(EMoveType::Travel)].visible) {
        switch (m_view_type)
        {
        case EViewType::Feedrate:
        case EViewType::Tool:
        case EViewType::ColorPrint: {
            break;
        }
        default: {
            // title
            ImGui::Spacing();
            imgui.title(_u8L("Travel"));

            // items
            append_item(EItemType::Line, Travel_Colors[0], _u8L("Movement"));
            append_item(EItemType::Line, Travel_Colors[1], _u8L("Extrusion"));
            append_item(EItemType::Line, Travel_Colors[2], _u8L("Retraction"));

            break;
        }
        }
    }

#if ENABLE_SHOW_WIPE_MOVES
    // wipe paths section
    if (m_buffers[buffer_id(EMoveType::Wipe)].visible) {
        switch (m_view_type)
        {
        case EViewType::Feedrate:
        case EViewType::Tool:
        case EViewType::ColorPrint: { break; }
        default: {
            // title
            ImGui::Spacing();
            imgui.title(_u8L("Wipe"));

            // items
            append_item(EItemType::Line, Wipe_Color, _u8L("Wipe"));

            break;
        }
        }
    }
#endif // ENABLE_SHOW_WIPE_MOVES

    auto any_option_available = [this]() {
        auto available = [this](EMoveType type) {
            const TBuffer& buffer = m_buffers[buffer_id(type)];
            return buffer.visible && buffer.has_data();
        };

        return available(EMoveType::Color_change) ||
            available(EMoveType::Custom_GCode) ||
            available(EMoveType::Pause_Print) ||
            available(EMoveType::Retract) ||
            available(EMoveType::Tool_change) ||
            available(EMoveType::Unretract);
    };

    auto add_option = [this, append_item](EMoveType move_type, EOptionsColors color, const std::string& text) {
        const TBuffer& buffer = m_buffers[buffer_id(move_type)];
        if (buffer.visible && buffer.has_data())
            append_item((buffer.shader == "options_110") ? EItemType::Rect : EItemType::Circle, Options_Colors[static_cast<unsigned int>(color)], text);
    };

    // options section
    if (any_option_available()) {
        // title
        ImGui::Spacing();
        imgui.title(_u8L("Options"));

        // items
        add_option(EMoveType::Retract, EOptionsColors::Retractions, _u8L("Retractions"));
        add_option(EMoveType::Unretract, EOptionsColors::Unretractions, _u8L("Deretractions"));
        add_option(EMoveType::Tool_change, EOptionsColors::ToolChanges, _u8L("Tool changes"));
        add_option(EMoveType::Color_change, EOptionsColors::ColorChanges, _u8L("Color changes"));
        add_option(EMoveType::Pause_Print, EOptionsColors::PausePrints, _u8L("Print pauses"));
        add_option(EMoveType::Custom_GCode, EOptionsColors::CustomGCodes, _u8L("Custom G-codes"));
    }

    // settings section
    if (wxGetApp().is_gcode_viewer() && 
        (m_view_type == EViewType::FeatureType || m_view_type == EViewType::Tool) &&
        (!m_settings_ids.print.empty() || !m_settings_ids.filament.empty() || !m_settings_ids.printer.empty())) {

        auto calc_offset = [this]() {
            float ret = 0.0f;
            if (!m_settings_ids.printer.empty())
                ret = std::max(ret, ImGui::CalcTextSize((_u8L("Printer") + std::string(":")).c_str()).x);
            if (!m_settings_ids.print.empty())
                ret = std::max(ret, ImGui::CalcTextSize((_u8L("Print settings") + std::string(":")).c_str()).x);
            if (!m_settings_ids.filament.empty()) {
                for (unsigned char i : m_extruder_ids) {
                    ret = std::max(ret, ImGui::CalcTextSize((_u8L("Filament") + " " + std::to_string(i + 1) + ":").c_str()).x);
                }
            }
            if (ret > 0.0f)
                ret += 2.0f * ImGui::GetStyle().ItemSpacing.x;
            return ret;
        };


        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Separator, { 1.0f, 1.0f, 1.0f, 1.0f });
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Spacing();

        float offset = calc_offset();

        if (!m_settings_ids.printer.empty()) {
            imgui.text(_u8L("Printer") + ":");
            ImGui::SameLine(offset);
            imgui.text(m_settings_ids.printer);
        }
        if (!m_settings_ids.print.empty()) {
            imgui.text(_u8L("Print settings") + ":");
            ImGui::SameLine(offset);
            imgui.text(m_settings_ids.print);
        }
        if (!m_settings_ids.filament.empty()) {
            for (unsigned char i : m_extruder_ids) {
                std::string txt = _u8L("Filament");
                txt += (m_extruder_ids.size() == 1) ? ":" : " " + std::to_string(i + 1);
                imgui.text(txt);
                ImGui::SameLine(offset);
                imgui.text(m_settings_ids.filament[i]);
            }
        }
    }

    // total estimated printing time section
    if (time_mode.time > 0.0f && (m_view_type == EViewType::FeatureType ||
        (m_view_type == EViewType::ColorPrint && !time_mode.custom_gcode_times.empty()))) {

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Separator, { 1.0f, 1.0f, 1.0f, 1.0f });
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Spacing();

        ImGui::AlignTextToFramePadding();
        switch (m_time_estimate_mode)
        {
        case PrintEstimatedTimeStatistics::ETimeMode::Normal:
        {
            imgui.text(_u8L("Estimated printing time") + " [" + _u8L("Normal mode") + "]:");
            break;
        }
        case PrintEstimatedTimeStatistics::ETimeMode::Stealth:
        {
            imgui.text(_u8L("Estimated printing time") + " [" + _u8L("Stealth mode") + "]:");
            break;
        }
        }
        ImGui::SameLine();
        imgui.text(short_time(get_time_dhms(time_mode.time)));

        auto show_mode_button = [this, &imgui](const std::string& label, PrintEstimatedTimeStatistics::ETimeMode mode) {
            bool show = false;
            for (size_t i = 0; i < m_time_statistics.modes.size(); ++i) {
                if (i != static_cast<size_t>(mode) &&
                    short_time(get_time_dhms(m_time_statistics.modes[static_cast<size_t>(mode)].time)) != short_time(get_time_dhms(m_time_statistics.modes[i].time))) {
                    show = true;
                    break;
                }
            }
            if (show && m_time_statistics.modes[static_cast<size_t>(mode)].roles_times.size() > 0) {
                if (imgui.button(label)) {
                    m_time_estimate_mode = mode;
                    wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
                    wxGetApp().plater()->get_current_canvas3D()->request_extra_frame();
                }
            }
        };

        switch (m_time_estimate_mode) {
        case PrintEstimatedTimeStatistics::ETimeMode::Normal: {
            show_mode_button(_u8L("Show stealth mode"), PrintEstimatedTimeStatistics::ETimeMode::Stealth);
            break;
        }
        case PrintEstimatedTimeStatistics::ETimeMode::Stealth: {
            show_mode_button(_u8L("Show normal mode"), PrintEstimatedTimeStatistics::ETimeMode::Normal);
            break;
        }
        }
    }

    imgui.end();
    ImGui::PopStyleVar();
}

#if ENABLE_GCODE_VIEWER_STATISTICS
void GCodeViewer::render_statistics() const
{
    static const float offset = 250.0f;

    ImGuiWrapper& imgui = *wxGetApp().imgui();

    auto add_time = [this, &imgui](const std::string& label, long long time) {
        char buf[1024];
        sprintf(buf, "%lld ms (%s)", time, get_time_dhms(static_cast<float>(time) * 0.001f).c_str());
        imgui.text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, label);
        ImGui::SameLine(offset);
        imgui.text(buf);
    };

    auto add_memory = [this, &imgui](const std::string& label, long long memory) {
        static const float mb = 1024.0f * 1024.0f;
        static const float inv_mb = 1.0f / mb;
        static const float gb = 1024.0f * mb;
        static const float inv_gb = 1.0f / gb;
        char buf[1024];
        if (static_cast<float>(memory) < gb)
            sprintf(buf, "%lld bytes (%.3f MB)", memory, static_cast<float>(memory) * inv_mb);
        else
            sprintf(buf, "%lld bytes (%.3f GB)", memory, static_cast<float>(memory) * inv_gb);
        imgui.text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, label);
        ImGui::SameLine(offset);
        imgui.text(buf);
    };

    auto add_counter = [this, &imgui](const std::string& label, long long counter) {
        char buf[1024];
        sprintf(buf, "%lld", counter);
        imgui.text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, label);
        ImGui::SameLine(offset);
        imgui.text(buf);
    };

    imgui.set_next_window_pos(0.5f * wxGetApp().plater()->get_current_canvas3D()->get_canvas_size().get_width(), 0.0f, ImGuiCond_Once, 0.5f, 0.0f);
    ImGui::SetNextWindowSizeConstraints({ 300, -1 }, { 600, -1 });
    imgui.begin(std::string("GCodeViewer Statistics"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    if (ImGui::CollapsingHeader("Time")) {
        add_time(std::string("GCodeProcessor:"), m_statistics.results_time);

        ImGui::Separator();
        add_time(std::string("Load:"), m_statistics.load_time);
        add_time(std::string("Refresh:"), m_statistics.refresh_time);
        add_time(std::string("Refresh paths:"), m_statistics.refresh_paths_time);
        wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
        wxGetApp().plater()->get_current_canvas3D()->request_extra_frame();
    }

    if (ImGui::CollapsingHeader("OpenGL calls")) {
        add_counter(std::string("Multi GL_POINTS:"), m_statistics.gl_multi_points_calls_count);
        add_counter(std::string("Multi GL_LINES:"), m_statistics.gl_multi_lines_calls_count);
        add_counter(std::string("Multi GL_TRIANGLES:"), m_statistics.gl_multi_triangles_calls_count);
        wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
        wxGetApp().plater()->get_current_canvas3D()->request_extra_frame();
    }

    if (ImGui::CollapsingHeader("CPU memory")) {
        add_memory(std::string("GCodeProcessor results:"), m_statistics.results_size);

        ImGui::Separator();
        add_memory(std::string("Paths:"), m_statistics.paths_size);
        add_memory(std::string("Render paths:"), m_statistics.render_paths_size);
        wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
        wxGetApp().plater()->get_current_canvas3D()->request_extra_frame();
    }

    if (ImGui::CollapsingHeader("GPU memory")) {
        add_memory(std::string("Vertices:"), m_statistics.vertices_gpu_size);
        add_memory(std::string("Indices:"), m_statistics.indices_gpu_size);
        wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
        wxGetApp().plater()->get_current_canvas3D()->request_extra_frame();
    }

    if (ImGui::CollapsingHeader("Other")) {
        add_counter(std::string("Travel segments count:"), m_statistics.travel_segments_count);
#if ENABLE_SHOW_WIPE_MOVES
        add_counter(std::string("Wipe segments count:"), m_statistics.wipe_segments_count);
#endif // ENABLE_SHOW_WIPE_MOVES
        add_counter(std::string("Extrude segments count:"), m_statistics.extrude_segments_count);
        add_counter(std::string("Max vertices in vertex buffer:"), m_statistics.max_vertices_in_vertex_buffer);
        add_counter(std::string("Max indices in index buffer:"), m_statistics.max_indices_in_index_buffer);

        wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
        wxGetApp().plater()->get_current_canvas3D()->request_extra_frame();
    }

    imgui.end();
}
#endif // ENABLE_GCODE_VIEWER_STATISTICS

void GCodeViewer::log_memory_used(const std::string& label, long long additional) const
{
    if (Slic3r::get_logging_level() >= 5) {
        long long paths_size = 0;
        long long render_paths_size = 0;
        for (const TBuffer& buffer : m_buffers) {
            paths_size += SLIC3R_STDVEC_MEMSIZE(buffer.paths, Path);
            render_paths_size += SLIC3R_STDVEC_MEMSIZE(buffer.render_paths, RenderPath);
            for (const RenderPath& path : buffer.render_paths) {
                render_paths_size += SLIC3R_STDVEC_MEMSIZE(path.sizes, unsigned int);
                render_paths_size += SLIC3R_STDVEC_MEMSIZE(path.offsets, size_t);
            }
        }
        long long layers_size = SLIC3R_STDVEC_MEMSIZE(m_layers.get_zs(), double);
        layers_size += SLIC3R_STDVEC_MEMSIZE(m_layers.get_endpoints(), Layers::Endpoints);
        BOOST_LOG_TRIVIAL(trace) << label
            << format_memsize_MB(additional + paths_size + render_paths_size + layers_size)
            << log_memory_info();
    }
}

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_GCODE_VIEWER
