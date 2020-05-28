#include "libslic3r/libslic3r.h"
#include "GCodeViewer.hpp"

#if ENABLE_GCODE_VIEWER
#include "libslic3r/Print.hpp"
#include "libslic3r/Geometry.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "PresetBundle.hpp"
#include "Camera.hpp"
#include "I18N.hpp"
#include "GUI_Utils.hpp"
#include "DoubleSlider.hpp"
#include "GLCanvas3D.hpp"
#include "GLToolbar.hpp"
#include "GUI_Preview.hpp"
#include "libslic3r/Model.hpp"
#if ENABLE_GCODE_VIEWER_STATISTICS
#include <imgui/imgui_internal.h>
#endif // ENABLE_GCODE_VIEWER_STATISTICS

#include <GL/glew.h>
#include <boost/log/trivial.hpp>

#include <array>
#include <algorithm>
#include <chrono>

namespace Slic3r {
namespace GUI {

static unsigned char buffer_id(GCodeProcessor::EMoveType type) {
    return static_cast<unsigned char>(type) - static_cast<unsigned char>(GCodeProcessor::EMoveType::Retract);
}

static GCodeProcessor::EMoveType buffer_type(unsigned char id) {
    return static_cast<GCodeProcessor::EMoveType>(static_cast<unsigned char>(GCodeProcessor::EMoveType::Retract) + id);
}

std::vector<std::array<float, 3>> decode_colors(const std::vector<std::string> & colors) {
    static const float INV_255 = 1.0f / 255.0f;

    std::vector<std::array<float, 3>> output(colors.size(), { 0.0f, 0.0f, 0.0f });
    for (size_t i = 0; i < colors.size(); ++i)
    {
        const std::string& color = colors[i];
        const char* c = color.data() + 1;
        if ((color.size() == 7) && (color.front() == '#')) {
            for (size_t j = 0; j < 3; ++j) {
                int digit1 = hex_digit_to_int(*c++);
                int digit2 = hex_digit_to_int(*c++);
                if ((digit1 == -1) || (digit2 == -1))
                    break;

                output[i][j] = float(digit1 * 16 + digit2) * INV_255;
            }
        }
    }
    return output;
}

void GCodeViewer::VBuffer::reset()
{
    // release gpu memory
    if (vbo_id > 0) {
        glsafe(::glDeleteBuffers(1, &vbo_id));
        vbo_id = 0;
    }

    vertices_count = 0;
}

bool GCodeViewer::Path::matches(const GCodeProcessor::MoveVertex& move) const
{
    switch (move.type)
    {
    case GCodeProcessor::EMoveType::Tool_change:
    case GCodeProcessor::EMoveType::Color_change:
    case GCodeProcessor::EMoveType::Pause_Print:
    case GCodeProcessor::EMoveType::Custom_GCode:
    case GCodeProcessor::EMoveType::Retract:
    case GCodeProcessor::EMoveType::Unretract:
    case GCodeProcessor::EMoveType::Extrude:
    {
        return type == move.type && role == move.extrusion_role && height == move.height && width == move.width &&
            feedrate == move.feedrate && fan_speed == move.fan_speed && volumetric_rate == move.volumetric_rate() &&
            extruder_id == move.extruder_id && cp_color_id == move.cp_color_id;
    }
    case GCodeProcessor::EMoveType::Travel:
    {
        return type == move.type && feedrate == move.feedrate && extruder_id == move.extruder_id && cp_color_id == move.cp_color_id;
    }
    default: { return false; }
    }
}

void GCodeViewer::IBuffer::reset()
{
    // release gpu memory
    if (ibo_id > 0) {
        glsafe(::glDeleteBuffers(1, &ibo_id));
        ibo_id = 0;
    }

    // release cpu memory
    indices_count = 0;
    paths = std::vector<Path>();
    render_paths = std::vector<RenderPath>();
}

void GCodeViewer::IBuffer::add_path(const GCodeProcessor::MoveVertex& move, unsigned int i_id, unsigned int s_id)
{
    Path::Endpoint endpoint = { i_id, s_id, move.position };
    paths.push_back({ move.type, move.extrusion_role, endpoint, endpoint, move.delta_extruder, move.height, move.width, move.feedrate, move.fan_speed, move.volumetric_rate(), move.extruder_id, move.cp_color_id });
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
    m_world_transform = (Geometry::assemble_transform(position.cast<double>()) * Geometry::assemble_transform(m_model.get_bounding_box().size()[2] * Vec3d::UnitZ(), { M_PI, 0.0, 0.0 })).cast<float>();
    m_world_bounding_box = m_model.get_bounding_box().transformed(m_world_transform.cast<double>());
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
}

const std::vector<GCodeViewer::Color> GCodeViewer::Extrusion_Role_Colors {{
    { 0.50f, 0.50f, 0.50f },   // erNone
    { 1.00f, 1.00f, 0.40f },   // erPerimeter
    { 1.00f, 0.65f, 0.00f },   // erExternalPerimeter
    { 0.00f, 0.00f, 1.00f },   // erOverhangPerimeter
    { 0.69f, 0.19f, 0.16f },   // erInternalInfill
    { 0.84f, 0.20f, 0.84f },   // erSolidInfill
    { 1.00f, 0.10f, 0.10f },   // erTopSolidInfill
    { 1.00f, 0.55f, 0.41f },   // erIroning    
    { 0.60f, 0.60f, 1.00f },   // erBridgeInfill
    { 1.00f, 1.00f, 1.00f },   // erGapFill
    { 0.52f, 0.48f, 0.13f },   // erSkirt
    { 0.00f, 1.00f, 0.00f },   // erSupportMaterial
    { 0.00f, 0.50f, 0.00f },   // erSupportMaterialInterface
    { 0.70f, 0.89f, 0.67f },   // erWipeTower
    { 0.16f, 0.80f, 0.58f },   // erCustom
    { 0.00f, 0.00f, 0.00f }    // erMixed
}};

const std::vector<GCodeViewer::Color> GCodeViewer::Options_Colors {{
    { 1.00f, 0.00f, 1.00f },   // Retractions
    { 0.00f, 1.00f, 1.00f },   // Unretractions
    { 1.00f, 1.00f, 1.00f },   // ToolChanges
    { 1.00f, 0.00f, 0.00f },   // ColorChanges
    { 0.00f, 1.00f, 0.00f },   // PausePrints
    { 0.00f, 0.00f, 1.00f }    // CustomGCodes
}};

const std::vector<GCodeViewer::Color> GCodeViewer::Travel_Colors {{
    { 0.0f, 0.0f, 0.5f }, // Move
    { 0.0f, 0.5f, 0.0f }, // Extrude
    { 0.5f, 0.0f, 0.0f }  // Retract
}};

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
    { 0.761f, 0.322f, 0.235f }  // reddish
}};

bool GCodeViewer::init()
{
    set_toolpath_move_type_visible(GCodeProcessor::EMoveType::Extrude, true);
    m_sequential_view.marker.init();
    init_shaders();

    std::array<int, 2> point_sizes;
    ::glGetIntegerv(GL_ALIASED_POINT_SIZE_RANGE, point_sizes.data());
    m_detected_point_sizes = { static_cast<float>(point_sizes[0]), static_cast<float>(point_sizes[1]) };

    return true;
}

void GCodeViewer::load(const GCodeProcessor::Result& gcode_result, const Print& print, bool initialized)
{
    // avoid processing if called with the same gcode_result
    if (m_last_result_id == gcode_result.id)
        return;

    m_last_result_id = gcode_result.id;

    // release gpu memory, if used
    reset();

    load_toolpaths(gcode_result);
    load_shells(print, initialized);
}

void GCodeViewer::refresh(const GCodeProcessor::Result& gcode_result, const std::vector<std::string>& str_tool_colors)
{
#if ENABLE_GCODE_VIEWER_STATISTICS
    auto start_time = std::chrono::high_resolution_clock::now();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    if (m_vertices.vertices_count == 0)
        return;

    // update tool colors
    m_tool_colors = decode_colors(str_tool_colors);

    // update ranges for coloring / legend
    m_extrusions.reset_ranges();
    for (size_t i = 0; i < m_vertices.vertices_count; ++i)
    {
        // skip first vertex
        if (i == 0)
            continue;

        const GCodeProcessor::MoveVertex& curr = gcode_result.moves[i];

        switch (curr.type)
        {
        case GCodeProcessor::EMoveType::Extrude:
        {
            m_extrusions.ranges.height.update_from(curr.height);
            m_extrusions.ranges.width.update_from(curr.width);
            m_extrusions.ranges.fan_speed.update_from(curr.fan_speed);
            m_extrusions.ranges.volumetric_rate.update_from(curr.volumetric_rate());
            [[fallthrough]];
        }
        case GCodeProcessor::EMoveType::Travel:
        {
            if (m_buffers[buffer_id(curr.type)].visible)
                m_extrusions.ranges.feedrate.update_from(curr.feedrate);

            break;
        }
        default: { break; }
        }
    }

    // update buffers' render paths
    refresh_render_paths(false, false);

#if ENABLE_GCODE_VIEWER_STATISTICS
    m_statistics.refresh_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
}

void GCodeViewer::reset()
{
    m_vertices.reset();

    for (IBuffer& buffer : m_buffers) {
        buffer.reset();
    }

    m_bounding_box = BoundingBoxf3();
    m_tool_colors = std::vector<Color>();
    m_extruder_ids = std::vector<unsigned char>();
    m_extrusions.reset_role_visibility_flags();
    m_extrusions.reset_ranges();
    m_shells.volumes.clear();
    m_layers_zs = std::vector<double>();
    m_layers_z_range = { 0.0, 0.0 };
    m_roles = std::vector<ExtrusionRole>();

#if ENABLE_GCODE_VIEWER_STATISTICS
    m_statistics.reset_all();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
}

void GCodeViewer::render() const
{
#if ENABLE_GCODE_VIEWER_STATISTICS
    m_statistics.reset_opengl();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    if (m_roles.empty())
        return;

    glsafe(::glEnable(GL_DEPTH_TEST));
    render_toolpaths();
    m_sequential_view.marker.set_world_position(m_sequential_view.current_position + m_sequential_view_marker_z_offset * Vec3f::UnitZ());
    m_sequential_view.marker.render();
    render_shells();
    render_legend();
#if ENABLE_GCODE_VIEWER_STATISTICS
    render_statistics();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
#if ENABLE_GCODE_VIEWER_SHADERS_EDITOR
    render_shaders_editor();
#endif // ENABLE_GCODE_VIEWER_SHADERS_EDITOR
}

bool GCodeViewer::is_toolpath_move_type_visible(GCodeProcessor::EMoveType type) const
{
    size_t id = static_cast<size_t>(buffer_id(type));
    return (id < m_buffers.size()) ? m_buffers[id].visible : false;
}

void GCodeViewer::set_toolpath_move_type_visible(GCodeProcessor::EMoveType type, bool visible)
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
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Travel), is_toolpath_move_type_visible(GCodeProcessor::EMoveType::Travel));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Retractions), is_toolpath_move_type_visible(GCodeProcessor::EMoveType::Retract));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Unretractions), is_toolpath_move_type_visible(GCodeProcessor::EMoveType::Unretract));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::ToolChanges), is_toolpath_move_type_visible(GCodeProcessor::EMoveType::Tool_change));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::ColorChanges), is_toolpath_move_type_visible(GCodeProcessor::EMoveType::Color_change));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::PausePrints), is_toolpath_move_type_visible(GCodeProcessor::EMoveType::Pause_Print));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::CustomGCodes), is_toolpath_move_type_visible(GCodeProcessor::EMoveType::Custom_GCode));
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

    set_toolpath_move_type_visible(GCodeProcessor::EMoveType::Travel, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Travel)));
    set_toolpath_move_type_visible(GCodeProcessor::EMoveType::Retract, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Retractions)));
    set_toolpath_move_type_visible(GCodeProcessor::EMoveType::Unretract, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Unretractions)));
    set_toolpath_move_type_visible(GCodeProcessor::EMoveType::Tool_change, is_flag_set(static_cast<unsigned int>(Preview::OptionType::ToolChanges)));
    set_toolpath_move_type_visible(GCodeProcessor::EMoveType::Color_change, is_flag_set(static_cast<unsigned int>(Preview::OptionType::ColorChanges)));
    set_toolpath_move_type_visible(GCodeProcessor::EMoveType::Pause_Print, is_flag_set(static_cast<unsigned int>(Preview::OptionType::PausePrints)));
    set_toolpath_move_type_visible(GCodeProcessor::EMoveType::Custom_GCode, is_flag_set(static_cast<unsigned int>(Preview::OptionType::CustomGCodes)));
    m_shells.visible = is_flag_set(static_cast<unsigned int>(Preview::OptionType::Shells));
    m_sequential_view.marker.set_visible(is_flag_set(static_cast<unsigned int>(Preview::OptionType::ToolMarker)));
    enable_legend(is_flag_set(static_cast<unsigned int>(Preview::OptionType::Legend)));
}

void GCodeViewer::set_layers_z_range(const std::array<double, 2>& layers_z_range)
{
    bool keep_sequential_current_first = layers_z_range[0] >= m_layers_z_range[0];
    bool keep_sequential_current_last = layers_z_range[1] <= m_layers_z_range[1];
    m_layers_z_range = layers_z_range;
    refresh_render_paths(keep_sequential_current_first, keep_sequential_current_last);
    wxGetApp().plater()->update_preview_moves_slider();
}

void GCodeViewer::init_shaders()
{
    unsigned char begin_id = buffer_id(GCodeProcessor::EMoveType::Retract);
    unsigned char end_id = buffer_id(GCodeProcessor::EMoveType::Count);

    bool is_glsl_120 = wxGetApp().is_glsl_version_greater_or_equal_to(1, 20);
    for (unsigned char i = begin_id; i < end_id; ++i)
    {
        switch (buffer_type(i))
        {
        case GCodeProcessor::EMoveType::Tool_change:  { m_buffers[i].shader = is_glsl_120 ? "options_120_solid" : "options_110"; break; }
        case GCodeProcessor::EMoveType::Color_change: { m_buffers[i].shader = is_glsl_120 ? "options_120_solid" : "options_110"; break; }
        case GCodeProcessor::EMoveType::Pause_Print:  { m_buffers[i].shader = is_glsl_120 ? "options_120_solid" : "options_110"; break; }
        case GCodeProcessor::EMoveType::Custom_GCode: { m_buffers[i].shader = is_glsl_120 ? "options_120_solid" : "options_110"; break; }
        case GCodeProcessor::EMoveType::Retract:      { m_buffers[i].shader = is_glsl_120 ? "options_120_solid" : "options_110"; break; }
        case GCodeProcessor::EMoveType::Unretract:    { m_buffers[i].shader = is_glsl_120 ? "options_120_solid" : "options_110"; break; }
        case GCodeProcessor::EMoveType::Extrude:      { m_buffers[i].shader = "extrusions"; break; }
        case GCodeProcessor::EMoveType::Travel:       { m_buffers[i].shader = "travels"; break; }
        default: { break; }
        }
    }
}

void GCodeViewer::load_toolpaths(const GCodeProcessor::Result& gcode_result)
{
#if ENABLE_GCODE_VIEWER_STATISTICS
    auto start_time = std::chrono::high_resolution_clock::now();
    m_statistics.results_size = SLIC3R_STDVEC_MEMSIZE(gcode_result.moves, GCodeProcessor::MoveVertex);
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    // vertex data
    m_vertices.vertices_count = gcode_result.moves.size();
    if (m_vertices.vertices_count == 0)
        return;

    // vertex data / bounding box -> extract from result
    std::vector<float> vertices_data(m_vertices.vertices_count * 3);
    for (size_t i = 0; i < m_vertices.vertices_count; ++i) {
        const GCodeProcessor::MoveVertex& move = gcode_result.moves[i];
        if (move.type == GCodeProcessor::EMoveType::Extrude)
            m_bounding_box.merge(move.position.cast<double>());
        ::memcpy(static_cast<void*>(&vertices_data[i * 3]), static_cast<const void*>(move.position.data()), 3 * sizeof(float));
    }

    m_bounding_box.merge(m_bounding_box.max + m_sequential_view.marker.get_bounding_box().max[2] * Vec3d::UnitZ());

#if ENABLE_GCODE_VIEWER_STATISTICS
    m_statistics.vertices_size = SLIC3R_STDVEC_MEMSIZE(vertices_data, float);
    m_statistics.vertices_gpu_size = vertices_data.size() * sizeof(float);
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    // vertex data -> send to gpu
    glsafe(::glGenBuffers(1, &m_vertices.vbo_id));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, m_vertices.vbo_id));
    glsafe(::glBufferData(GL_ARRAY_BUFFER, vertices_data.size() * sizeof(float), vertices_data.data(), GL_STATIC_DRAW));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));

    // vertex data -> no more needed, free ram
    vertices_data = std::vector<float>();

    // indices data -> extract from result
    std::vector<std::vector<unsigned int>> indices(m_buffers.size());
    for (size_t i = 0; i < m_vertices.vertices_count; ++i)
    {
        // skip first vertex
        if (i == 0)
            continue;

        const GCodeProcessor::MoveVertex& prev = gcode_result.moves[i - 1];
        const GCodeProcessor::MoveVertex& curr = gcode_result.moves[i];

        unsigned char id = buffer_id(curr.type);
        IBuffer& buffer = m_buffers[id];
        std::vector<unsigned int>& buffer_indices = indices[id];

        switch (curr.type)
        {
        case GCodeProcessor::EMoveType::Tool_change:
        case GCodeProcessor::EMoveType::Color_change:
        case GCodeProcessor::EMoveType::Pause_Print:
        case GCodeProcessor::EMoveType::Custom_GCode:
        case GCodeProcessor::EMoveType::Retract:
        case GCodeProcessor::EMoveType::Unretract:
        {
            buffer.add_path(curr, static_cast<unsigned int>(buffer_indices.size()), static_cast<unsigned int>(i));
            buffer_indices.push_back(static_cast<unsigned int>(i));
            break;
        }
        case GCodeProcessor::EMoveType::Extrude:
        case GCodeProcessor::EMoveType::Travel:
        {
            if (prev.type != curr.type || !buffer.paths.back().matches(curr)) {
                buffer.add_path(curr, static_cast<unsigned int>(buffer_indices.size()), static_cast<unsigned int>(i));
                Path& last_path = buffer.paths.back();
                last_path.first.position = prev.position;
                last_path.first.s_id = static_cast<unsigned int>(i - 1);
                buffer_indices.push_back(static_cast<unsigned int>(i - 1));
            }
            
            buffer.paths.back().last = { static_cast<unsigned int>(buffer_indices.size()), static_cast<unsigned int>(i), curr.position };
            buffer_indices.push_back(static_cast<unsigned int>(i));
            break;
        }
        default:
        {
            break;
        }
        }
    }

#if ENABLE_GCODE_VIEWER_STATISTICS
    for (IBuffer& buffer : m_buffers)
    {
        m_statistics.paths_size += SLIC3R_STDVEC_MEMSIZE(buffer.paths, Path);
    }
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    // indices data -> send data to gpu
    for (size_t i = 0; i < m_buffers.size(); ++i)
    {
        IBuffer& buffer = m_buffers[i];
        std::vector<unsigned int>& buffer_indices = indices[i];
        buffer.indices_count = buffer_indices.size();
#if ENABLE_GCODE_VIEWER_STATISTICS
        m_statistics.indices_size += SLIC3R_STDVEC_MEMSIZE(buffer_indices, unsigned int);
        m_statistics.indices_gpu_size += buffer.indices_count * sizeof(unsigned int);
#endif // ENABLE_GCODE_VIEWER_STATISTICS

        if (buffer.indices_count > 0) {
            glsafe(::glGenBuffers(1, &buffer.ibo_id));
            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.ibo_id));
            glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, buffer.indices_count * sizeof(unsigned int), buffer_indices.data(), GL_STATIC_DRAW));
            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
        }
    }

    // layers zs / roles / extruder ids / cp color ids -> extract from result
    for (size_t i = 0; i < m_vertices.vertices_count; ++i)
    {
        const GCodeProcessor::MoveVertex& move = gcode_result.moves[i];
        if (move.type == GCodeProcessor::EMoveType::Extrude)
            m_layers_zs.emplace_back(static_cast<double>(move.position[2]));

        m_extruder_ids.emplace_back(move.extruder_id);

        if (i > 0)
            m_roles.emplace_back(move.extrusion_role);
    }

    // layers zs -> replace intervals of layers with similar top positions with their average value.
    std::sort(m_layers_zs.begin(), m_layers_zs.end());
    int n = int(m_layers_zs.size());
    int k = 0;
    for (int i = 0; i < n;) {
        int j = i + 1;
        double zmax = m_layers_zs[i] + EPSILON;
        for (; j < n && m_layers_zs[j] <= zmax; ++j);
        m_layers_zs[k++] = (j > i + 1) ? (0.5 * (m_layers_zs[i] + m_layers_zs[j - 1])) : m_layers_zs[i];
        i = j;
    }
    if (k < n)
        m_layers_zs.erase(m_layers_zs.begin() + k, m_layers_zs.end());

    // set layers z range
    m_layers_z_range = { m_layers_zs.front(), m_layers_zs.back() };

    // roles -> remove duplicates
    std::sort(m_roles.begin(), m_roles.end());
    m_roles.erase(std::unique(m_roles.begin(), m_roles.end()), m_roles.end());

    // extruder ids -> remove duplicates
    std::sort(m_extruder_ids.begin(), m_extruder_ids.end());
    m_extruder_ids.erase(std::unique(m_extruder_ids.begin(), m_extruder_ids.end()), m_extruder_ids.end());

#if ENABLE_GCODE_VIEWER_STATISTICS
    m_statistics.load_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
}

void GCodeViewer::load_shells(const Print& print, bool initialized)
{
    if (print.objects().empty())
        // no shells, return
        return;

    // adds objects' volumes 
    int object_id = 0;
    for (const PrintObject* obj : print.objects())
    {
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

    for (GLVolume* volume : m_shells.volumes.volumes)
    {
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
        case EViewType::ColorPrint:     { color = m_tool_colors[path.cp_color_id]; break; }
        default:                        { color = { 1.0f, 1.0f, 1.0f }; break; }
        }
        return color;
    };

    auto travel_color = [this](const Path& path) {
        return (path.delta_extruder < 0.0f) ? Travel_Colors[2] /* Retract */ :
              ((path.delta_extruder > 0.0f) ? Travel_Colors[1] /* Extrude */ :
                Travel_Colors[0] /* Move */);
    };

#if ENABLE_GCODE_VIEWER_STATISTICS
    m_statistics.render_paths_size = 0;
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    m_sequential_view.endpoints.first = m_vertices.vertices_count;
    m_sequential_view.endpoints.last = 0;
    if (!keep_sequential_current_first)
        m_sequential_view.current.first = 0;
    if (!keep_sequential_current_last)
        m_sequential_view.current.last = m_vertices.vertices_count;

    // first pass: collect visible paths and update sequential view data
    std::vector<std::pair<IBuffer*, size_t>> paths;
    for (IBuffer& buffer : m_buffers) {
        // reset render paths
        buffer.render_paths = std::vector<RenderPath>();

        if (!buffer.visible)
            continue;

        for (size_t i = 0; i < buffer.paths.size(); ++i) {
            const Path& path = buffer.paths[i];
            if (path.type == GCodeProcessor::EMoveType::Travel) {
                if (!is_travel_in_z_range(i))
                    continue;
            }
            else if (!is_in_z_range(path))
                continue;

            if (path.type == GCodeProcessor::EMoveType::Extrude && !is_visible(path))
                continue;

            // store valid path
            paths.push_back({ &buffer, i });

            m_sequential_view.endpoints.first = std::min(m_sequential_view.endpoints.first, path.first.s_id);
            m_sequential_view.endpoints.last = std::max(m_sequential_view.endpoints.last, path.last.s_id);
        }
    }

    // update current sequential position
    m_sequential_view.current.first = keep_sequential_current_first ? std::clamp(m_sequential_view.current.first, m_sequential_view.endpoints.first, m_sequential_view.endpoints.last) : m_sequential_view.endpoints.first;
    m_sequential_view.current.last = keep_sequential_current_last ? std::clamp(m_sequential_view.current.last, m_sequential_view.endpoints.first, m_sequential_view.endpoints.last) : m_sequential_view.endpoints.last;
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, m_vertices.vbo_id));
    size_t v_size = VBuffer::vertex_size_bytes();
    glsafe(::glGetBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(m_sequential_view.current.last * v_size), static_cast<GLsizeiptr>(v_size), static_cast<void*>(m_sequential_view.current_position.data())));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));

    // second pass: filter paths by sequential data
    for (auto&& [buffer, id] : paths) {
        const Path& path = buffer->paths[id];
        if ((m_sequential_view.current.last <= path.first.s_id) || (path.last.s_id <= m_sequential_view.current.first))
            continue;

        Color color;
        switch (path.type)
        {
        case GCodeProcessor::EMoveType::Extrude: { color = extrusion_color(path); break; }
        case GCodeProcessor::EMoveType::Travel:  { color = (m_view_type == EViewType::Feedrate || m_view_type == EViewType::Tool || m_view_type == EViewType::ColorPrint) ? extrusion_color(path) : travel_color(path); break; }
        default:                                 { color = { 0.0f, 0.0f, 0.0f }; break; }
        }

        auto it = std::find_if(buffer->render_paths.begin(), buffer->render_paths.end(), [color](const RenderPath& path) { return path.color == color; });
        if (it == buffer->render_paths.end()) {
            it = buffer->render_paths.insert(buffer->render_paths.end(), RenderPath());
            it->color = color;
        }

        it->sizes.push_back(std::min(m_sequential_view.current.last, path.last.s_id) - std::max(m_sequential_view.current.first, path.first.s_id) + 1);
        unsigned int delta_1st = 0;
        if ((path.first.s_id < m_sequential_view.current.first) && (m_sequential_view.current.first <= path.last.s_id))
            delta_1st = m_sequential_view.current.first - path.first.s_id;

        it->offsets.push_back(static_cast<size_t>((path.first.i_id + delta_1st) * sizeof(unsigned int)));
    }

#if ENABLE_GCODE_VIEWER_STATISTICS
    for (const IBuffer& buffer : m_buffers) {
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
#if ENABLE_GCODE_VIEWER_SHADERS_EDITOR
    bool is_glsl_120 = m_shaders_editor.shader_version >= 1 && wxGetApp().is_glsl_version_greater_or_equal_to(1, 20);
    std::array<float, 2> point_sizes;
    if (m_shaders_editor.size_dependent_on_zoom)
        point_sizes = { std::min(static_cast<float>(m_shaders_editor.sizes[0]), m_detected_point_sizes[1]), std::min(static_cast<float>(m_shaders_editor.sizes[1]), m_detected_point_sizes[1]) };
    else
        point_sizes = { static_cast<float>(m_shaders_editor.fixed_size), static_cast<float>(m_shaders_editor.fixed_size) };
#else
    bool is_glsl_120 = wxGetApp().is_glsl_version_greater_or_equal_to(1, 20);
    std::array<float, 2> point_sizes = { std::min(8.0f, m_detected_point_sizes[1]), std::min(48.0f, m_detected_point_sizes[1]) };
#endif // ENABLE_GCODE_VIEWER_SHADERS_EDITOR
    const Camera& camera = wxGetApp().plater()->get_camera();
    double zoom = camera.get_zoom();
    const std::array<int, 4>& viewport = camera.get_viewport();
    std::array<int, 2> viewport_sizes = { viewport[2], viewport[3] };

    Transform3d inv_proj = camera.get_projection_matrix().inverse();

    auto render_options = [this, is_glsl_120, zoom, viewport, inv_proj, viewport_sizes, point_sizes](const IBuffer& buffer, EOptionsColors colors_id, GLShaderProgram& shader) {
        shader.set_uniform("uniform_color", Options_Colors[static_cast<unsigned int>(colors_id)]);
#if ENABLE_GCODE_VIEWER_SHADERS_EDITOR
        shader.set_uniform("zoom", m_shaders_editor.size_dependent_on_zoom ? zoom : 1.0f);
        shader.set_uniform("percent_outline_radius", 0.01f * static_cast<float>(m_shaders_editor.percent_outline));
        shader.set_uniform("percent_center_radius", 0.01f * static_cast<float>(m_shaders_editor.percent_center));
#else
        shader.set_uniform("zoom", zoom);
        shader.set_uniform("percent_outline_radius", 0.15f);
        shader.set_uniform("percent_center_radius", 0.15f);
#endif // ENABLE_GCODE_VIEWER_SHADERS_EDITOR
        shader.set_uniform("viewport_sizes", viewport_sizes);
        shader.set_uniform("inv_proj_matrix", inv_proj);
        shader.set_uniform("point_sizes", point_sizes);
        glsafe(::glEnable(GL_VERTEX_PROGRAM_POINT_SIZE));
        if (is_glsl_120)
            glsafe(::glEnable(GL_POINT_SPRITE));

        for (const RenderPath& path : buffer.render_paths) {
            glsafe(::glMultiDrawElements(GL_POINTS, (const GLsizei*)path.sizes.data(), GL_UNSIGNED_INT, (const void* const*)path.offsets.data(), (GLsizei)path.sizes.size()));
#if ENABLE_GCODE_VIEWER_STATISTICS
            ++m_statistics.gl_multi_points_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
        }
        if (is_glsl_120)
            glsafe(::glDisable(GL_POINT_SPRITE));

        glsafe(::glDisable(GL_VERTEX_PROGRAM_POINT_SIZE));
    };

    glsafe(::glCullFace(GL_BACK));
    glsafe(::glLineWidth(3.0f));

    unsigned char begin_id = buffer_id(GCodeProcessor::EMoveType::Retract);
    unsigned char end_id = buffer_id(GCodeProcessor::EMoveType::Count);

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, m_vertices.vbo_id));
    glsafe(::glVertexPointer(3, GL_FLOAT, VBuffer::vertex_size_bytes(), (const void*)0));
    glsafe(::glEnableClientState(GL_VERTEX_ARRAY));

    for (unsigned char i = begin_id; i < end_id; ++i) {
        const IBuffer& buffer = m_buffers[i];
        if (buffer.ibo_id == 0)
            continue;
        
        if (!buffer.visible)
            continue;

        GLShaderProgram* shader = wxGetApp().get_shader(buffer.shader.c_str());
        if (shader != nullptr) {
            shader->start_using();

            GCodeProcessor::EMoveType type = buffer_type(i);

            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.ibo_id));

            switch (type)
            {
            case GCodeProcessor::EMoveType::Tool_change:
            {
                render_options(buffer, EOptionsColors::ToolChanges, *shader);
                break;
            }
            case GCodeProcessor::EMoveType::Color_change:
            {
                render_options(buffer, EOptionsColors::ColorChanges, *shader);
                break;
            }
            case GCodeProcessor::EMoveType::Pause_Print:
            {
                render_options(buffer, EOptionsColors::PausePrints, *shader);
                break;
            }
            case GCodeProcessor::EMoveType::Custom_GCode:
            {
                render_options(buffer, EOptionsColors::CustomGCodes, *shader);
                break;
            }
            case GCodeProcessor::EMoveType::Retract:
            {
                render_options(buffer, EOptionsColors::Retractions, *shader);
                break;
            }
            case GCodeProcessor::EMoveType::Unretract:
            {
                render_options(buffer, EOptionsColors::Unretractions, *shader);
                break;
            }
            case GCodeProcessor::EMoveType::Extrude:
            {
                for (const RenderPath& path : buffer.render_paths)
                {
                    shader->set_uniform("uniform_color", path.color);
                    glsafe(::glMultiDrawElements(GL_LINE_STRIP, (const GLsizei*)path.sizes.data(), GL_UNSIGNED_INT, (const void* const*)path.offsets.data(), (GLsizei)path.sizes.size()));
#if ENABLE_GCODE_VIEWER_STATISTICS
                    ++m_statistics.gl_multi_line_strip_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS

                }
                break;
            }
            case GCodeProcessor::EMoveType::Travel:
            {
                for (const RenderPath& path : buffer.render_paths)
                {
                    shader->set_uniform("uniform_color", path.color);
                    glsafe(::glMultiDrawElements(GL_LINE_STRIP, (const GLsizei*)path.sizes.data(), GL_UNSIGNED_INT, (const void* const*)path.offsets.data(), (GLsizei)path.sizes.size()));
#if ENABLE_GCODE_VIEWER_STATISTICS
                    ++m_statistics.gl_multi_line_strip_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS

                }
                break;
            }
            }

            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
            shader->stop_using();
        }
    }

    glsafe(::glDisableClientState(GL_VERTEX_ARRAY));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
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
    static const ImVec4 ORANGE(1.0f, 0.49f, 0.22f, 1.0f);
    static const ImU32 ICON_BORDER_COLOR = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

    if (!m_legend_enabled)
        return;

    ImGuiWrapper& imgui = *wxGetApp().imgui();

#define USE_ICON_HEXAGON 1

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

#if ENABLE_GCODE_VIEWER_SHADERS_EDITOR
    auto add_item = [this, draw_list, &imgui](EItemType type, const Color& color, const std::string& label, std::function<void()> callback = nullptr) {
#else
    auto add_item = [draw_list, &imgui](EItemType type, const Color& color, const std::string& label, std::function<void()> callback = nullptr) {
#endif // ENABLE_GCODE_VIEWER_SHADERS_EDITOR
        float icon_size = ImGui::GetTextLineHeight();
        ImVec2 pos = ImGui::GetCursorPos();
        switch (type)
        {
        default:
        case EItemType::Rect:
        {
            draw_list->AddRect({ pos.x, pos.y }, { pos.x + icon_size, pos.y + icon_size }, ICON_BORDER_COLOR, 0.0f, 0);
            draw_list->AddRectFilled({ pos.x + 1.0f, pos.y + 1.0f }, { pos.x + icon_size - 1.0f, pos.y + icon_size - 1.0f },
                ImGui::GetColorU32({ color[0], color[1], color[2], 1.0f }));
            break;
        }
        case EItemType::Circle:
        {
#if ENABLE_GCODE_VIEWER_SHADERS_EDITOR
            ImVec2 center(0.5f * (pos.x + pos.x + icon_size), 0.5f * (pos.y + pos.y + icon_size));
            draw_list->AddCircle(center, 0.5f * icon_size, ICON_BORDER_COLOR, 16);
            if (m_shaders_editor.shader_version == 1) {
                draw_list->AddCircleFilled(center, (0.5f * icon_size) - 2.0f,
                    ImGui::GetColorU32({ 0.5f * color[0], 0.5f * color[1], 0.5f * color[2], 1.0f }), 16);
                float radius = ((0.5f * icon_size) - 2.0f) * (1.0f - 0.01f * static_cast<float>(m_shaders_editor.percent_outline));
                draw_list->AddCircleFilled(center, radius, ImGui::GetColorU32({ color[0], color[1], color[2], 1.0f }), 16);
                if (m_shaders_editor.percent_center > 0) {
                    radius = ((0.5f * icon_size) - 2.0f) * 0.01f * static_cast<float>(m_shaders_editor.percent_center);
                    draw_list->AddCircleFilled(center, radius, ImGui::GetColorU32({ 0.5f * color[0], 0.5f * color[1], 0.5f * color[2], 1.0f }), 16);
                }
            } else
                draw_list->AddCircleFilled(center, (0.5f * icon_size) - 2.0f, ImGui::GetColorU32({ color[0], color[1], color[2], 1.0f }), 16);
#else
            draw_list->AddCircle({ 0.5f * (pos.x + pos.x + icon_size), 0.5f * (pos.y + pos.y + icon_size) }, 0.5f * icon_size, ICON_BORDER_COLOR, 16);
            draw_list->AddCircleFilled({ 0.5f * (pos.x + pos.x + icon_size), 0.5f * (pos.y + pos.y + icon_size) }, (0.5f * icon_size) - 2.0f,
                ImGui::GetColorU32({ color[0], color[1], color[2], 1.0f }), 16);
#endif // ENABLE_GCODE_VIEWER_SHADERS_EDITOR
            break;
        }
        case EItemType::Hexagon:
        {
            ImVec2 center(0.5f * (pos.x + pos.x + icon_size), 0.5f * (pos.y + pos.y + icon_size));
            draw_list->AddNgon(center, 0.5f * icon_size, ICON_BORDER_COLOR, 6);
            draw_list->AddNgonFilled({ 0.5f * (pos.x + pos.x + icon_size), 0.5f * (pos.y + pos.y + icon_size) }, (0.5f * icon_size) - 2.0f,
                ImGui::GetColorU32({ color[0], color[1], color[2], 1.0f }), 6);
            break;
        }
        case EItemType::Line:
        {
            draw_list->AddLine({ pos.x + 1, pos.y + icon_size - 1}, { pos.x + icon_size - 1, pos.y + 1 }, ImGui::GetColorU32({ color[0], color[1], color[2], 1.0f }), 3.0f);
            break;
        }
        }

        // draw text
        ImGui::Dummy({ icon_size, icon_size });
        ImGui::SameLine();
        if (callback != nullptr)
        {
            if (ImGui::MenuItem(label.c_str()))
                callback();
        }
        else
            imgui.text(label);
    };

    auto add_range = [this, draw_list, &imgui, add_item](const Extrusions::Range& range, unsigned int decimals) {
        auto add_range_item = [this, draw_list, &imgui, add_item](int i, float value, unsigned int decimals) {
            char buf[1024];
            ::sprintf(buf, "%.*f", decimals, value);
#if USE_ICON_HEXAGON
            add_item(EItemType::Hexagon, Range_Colors[i], buf);
#else
            add_item(EItemType::Rect, Range_Colors[i], buf);
#endif // USE_ICON_HEXAGON
        };

        float step_size = range.step_size();
        if (step_size == 0.0f)
            // single item use case
            add_range_item(0, range.min, decimals);
        else
        {
            for (int i = static_cast<int>(Range_Colors.size()) - 1; i >= 0; --i) {
                add_range_item(i, range.min + static_cast<float>(i) * step_size, decimals);
            }
        }
    };

    // extrusion paths -> title
    ImGui::PushStyleColor(ImGuiCol_Text, ORANGE);
    switch (m_view_type)
    {
    case EViewType::FeatureType:    { imgui.text(_u8L("Feature type")); break; }
    case EViewType::Height:         { imgui.text(_u8L("Height (mm)")); break; }
    case EViewType::Width:          { imgui.text(_u8L("Width (mm)")); break; }
    case EViewType::Feedrate:       { imgui.text(_u8L("Speed (mm/s)")); break; }
    case EViewType::FanSpeed:       { imgui.text(_u8L("Fan Speed (%%)")); break; }
    case EViewType::VolumetricRate: { imgui.text(_u8L("Volumetric flow rate (mm³/s)")); break; }
    case EViewType::Tool:           { imgui.text(_u8L("Tool")); break; }
    case EViewType::ColorPrint:     { imgui.text(_u8L("Color Print")); break; }
    default:                        { break; }
    }
    ImGui::PopStyleColor();
    ImGui::Separator();

    // extrusion paths -> items
    switch (m_view_type)
    {
    case EViewType::FeatureType:
    {
        for (ExtrusionRole role : m_roles) {
            bool visible = is_visible(role);
            if (!visible)
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.3333f);

#if USE_ICON_HEXAGON
            add_item(EItemType::Hexagon, Extrusion_Role_Colors[static_cast<unsigned int>(role)], _u8L(ExtrusionEntity::role_to_string(role)), [this, role]() {
#else
            add_item(EItemType::Rect, Extrusion_Role_Colors[static_cast<unsigned int>(role)], _u8L(ExtrusionEntity::role_to_string(role)), [this, role]() {
#endif // USE_ICON_HEXAGON
                if (role < erCount)
                {
                    m_extrusions.role_visibility_flags = is_visible(role) ? m_extrusions.role_visibility_flags & ~(1 << role) : m_extrusions.role_visibility_flags | (1 << role);
                    // update buffers' render paths
                    refresh_render_paths(false, false);
                    wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
                    wxGetApp().plater()->update_preview_bottom_toolbar();
                }
                });

            if (!visible)
                ImGui::PopStyleVar();
        }
        break;
    }
    case EViewType::Height:         { add_range(m_extrusions.ranges.height, 3); break; }
    case EViewType::Width:          { add_range(m_extrusions.ranges.width, 3); break; }
    case EViewType::Feedrate:       { add_range(m_extrusions.ranges.feedrate, 1); break; }
    case EViewType::FanSpeed:       { add_range(m_extrusions.ranges.fan_speed, 0); break; }
    case EViewType::VolumetricRate: { add_range(m_extrusions.ranges.volumetric_rate, 3); break; }
    case EViewType::Tool:
    {
        size_t tools_count = m_tool_colors.size();
        for (size_t i = 0; i < tools_count; ++i) {
            // shows only extruders actually used
            auto it = std::find(m_extruder_ids.begin(), m_extruder_ids.end(), static_cast<unsigned char>(i));
            if (it == m_extruder_ids.end())
                continue;

#if USE_ICON_HEXAGON
            add_item(EItemType::Hexagon, m_tool_colors[i], (boost::format(_u8L("Extruder %d")) % (i + 1)).str());
#else
            add_item(EItemType::Rect, m_tool_colors[i], (boost::format(_u8L("Extruder %d")) % (i + 1)).str());
#endif // USE_ICON_HEXAGON
        }
        break;
    }
    case EViewType::ColorPrint:
    {
        const std::vector<CustomGCode::Item>& custom_gcode_per_print_z = wxGetApp().plater()->model().custom_gcode_per_print_z.gcodes;
        const int extruders_count = wxGetApp().extruders_edited_cnt();
        if (extruders_count == 1) { // single extruder use case
            if (custom_gcode_per_print_z.empty())
                // no data to show
#if USE_ICON_HEXAGON
                add_item(EItemType::Hexagon, m_tool_colors.front(), _u8L("Default print color"));
#else
                add_item(EItemType::Rect, m_tool_colors.front(), _u8L("Default print color"));
#endif // USE_ICON_HEXAGON
            else {
                std::vector<std::pair<double, double>> cp_values;
                cp_values.reserve(custom_gcode_per_print_z.size());

                for (auto custom_code : custom_gcode_per_print_z) {
                    if (custom_code.gcode != ColorChangeCode)
                        continue;

                    auto lower_b = std::lower_bound(m_layers_zs.begin(), m_layers_zs.end(), custom_code.print_z - Slic3r::DoubleSlider::epsilon());

                    if (lower_b == m_layers_zs.end())
                        continue;

                    double current_z = *lower_b;
                    double previous_z = lower_b == m_layers_zs.begin() ? 0.0 : *(--lower_b);

                    // to avoid duplicate values, check adding values
                    if (cp_values.empty() || !(cp_values.back().first == previous_z && cp_values.back().second == current_z))
                        cp_values.emplace_back(std::make_pair(previous_z, current_z));
                }

                const int items_cnt = static_cast<int>(cp_values.size());
                if (items_cnt == 0) { // There is no one color change, but there are some pause print or custom Gcode
#if USE_ICON_HEXAGON
                    add_item(EItemType::Hexagon, m_tool_colors.front(), _u8L("Default print color"));
#else
                    add_item(EItemType::Rect, m_tool_colors.front(), _u8L("Default print color"));
#endif // USE_ICON_HEXAGON
                }
                else {
                    for (int i = items_cnt; i >= 0; --i) {
                        // create label for color change item
                        std::string id_str = " (" + std::to_string(i + 1) + ")";

                        if (i == 0) {
#if USE_ICON_HEXAGON
                            add_item(EItemType::Hexagon, m_tool_colors[i], (boost::format(_u8L("up to %.2f mm")) % cp_values.front().first).str() + id_str);
#else
                            add_item(EItemType::Rect, m_tool_colors[i], (boost::format(_u8L("up to %.2f mm")) % cp_values.front().first).str() + id_str);
#endif // USE_ICON_HEXAGON
                            break;
                        }
                        else if (i == items_cnt) {
#if USE_ICON_HEXAGON
                            add_item(EItemType::Hexagon, m_tool_colors[i], (boost::format(_u8L("above %.2f mm")) % cp_values[i - 1].second).str() + id_str);
#else
                            add_item(EItemType::Rect, m_tool_colors[i], (boost::format(_u8L("above %.2f mm")) % cp_values[i - 1].second).str() + id_str);
#endif // USE_ICON_HEXAGON
                            continue;
                        }
#if USE_ICON_HEXAGON
                        add_item(EItemType::Hexagon, m_tool_colors[i], (boost::format(_u8L("%.2f - %.2f mm")) % cp_values[i - 1].second% cp_values[i].first).str() + id_str);
#else
                        add_item(EItemType::Rect, m_tool_colors[i], (boost::format(_u8L("%.2f - %.2f mm")) % cp_values[i - 1].second% cp_values[i].first).str() + id_str);
#endif // USE_ICON_HEXAGON
                    }
                }
            }
        }
        else // multi extruder use case
        {
            // extruders
            for (unsigned int i = 0; i < (unsigned int)extruders_count; ++i) {
#if USE_ICON_HEXAGON
                add_item(EItemType::Hexagon, m_tool_colors[i], (boost::format(_u8L("Extruder %d")) % (i + 1)).str());
#else
                add_item(EItemType::Rect, m_tool_colors[i], (boost::format(_u8L("Extruder %d")) % (i + 1)).str());
#endif // USE_ICON_HEXAGON
            }

            // color changes
            int color_change_idx = 1 + static_cast<int>(m_tool_colors.size()) - extruders_count;
            size_t last_color_id = m_tool_colors.size() - 1;
            for (int i = static_cast<int>(custom_gcode_per_print_z.size()) - 1; i >= 0; --i) {
                if (custom_gcode_per_print_z[i].gcode == ColorChangeCode) {
                    // create label for color change item
                    std::string id_str = " (" + std::to_string(color_change_idx--) + ")";

#if USE_ICON_HEXAGON
                    add_item(EItemType::Hexagon, m_tool_colors[last_color_id--],
#else
                    add_item(EItemType::Rect, m_tool_colors[last_color_id--],
#endif // USE_ICON_HEXAGON
                        (boost::format(_u8L("Color change for Extruder %d at %.2f mm")) % custom_gcode_per_print_z[i].extruder % custom_gcode_per_print_z[i].print_z).str() + id_str);
                }
            }
        }

        break;
    }
    default: { break; }
    }

    // travel paths
    if (m_buffers[buffer_id(GCodeProcessor::EMoveType::Travel)].visible)
    {
        switch (m_view_type)
        {
        case EViewType::Feedrate:
        case EViewType::Tool:
        case EViewType::ColorPrint:
        {
            break;
        }
        default:
        {
            // title
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ORANGE);
            imgui.text(_u8L("Travel"));
            ImGui::PopStyleColor();
            ImGui::Separator();

            // items
            add_item(EItemType::Line, Travel_Colors[0], _u8L("Movement"));
            add_item(EItemType::Line, Travel_Colors[1], _u8L("Extrusion"));
            add_item(EItemType::Line, Travel_Colors[2], _u8L("Retraction"));

            break;
        }
        }
    }

    auto any_option_visible = [this]() {
        return m_buffers[buffer_id(GCodeProcessor::EMoveType::Color_change)].visible ||
               m_buffers[buffer_id(GCodeProcessor::EMoveType::Custom_GCode)].visible ||
               m_buffers[buffer_id(GCodeProcessor::EMoveType::Pause_Print)].visible ||
               m_buffers[buffer_id(GCodeProcessor::EMoveType::Retract)].visible ||
               m_buffers[buffer_id(GCodeProcessor::EMoveType::Tool_change)].visible ||
               m_buffers[buffer_id(GCodeProcessor::EMoveType::Unretract)].visible;
    };

    auto add_option = [this, add_item](GCodeProcessor::EMoveType move_type, EOptionsColors color, const std::string& text) {
        const IBuffer& buffer = m_buffers[buffer_id(move_type)];
        if (buffer.visible && buffer.indices_count > 0)
#if ENABLE_GCODE_VIEWER_SHADERS_EDITOR
            add_item((m_shaders_editor.shader_version == 0) ? EItemType::Rect : EItemType::Circle, Options_Colors[static_cast<unsigned int>(color)], text);
#else
            add_item((buffer.shader == "options_110") ? EItemType::Rect : EItemType::Circle, Options_Colors[static_cast<unsigned int>(color)], text);
#endif // ENABLE_GCODE_VIEWER_SHADERS_EDITOR
    };

    // options
    if (any_option_visible())
    {
        // title
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ORANGE);
        imgui.text(_u8L("Options"));
        ImGui::PopStyleColor();
        ImGui::Separator();

        // items
        add_option(GCodeProcessor::EMoveType::Retract, EOptionsColors::Retractions, _u8L("Retractions"));
        add_option(GCodeProcessor::EMoveType::Unretract, EOptionsColors::Unretractions, _u8L("Unretractions"));
        add_option(GCodeProcessor::EMoveType::Tool_change, EOptionsColors::ToolChanges, _u8L("Tool changes"));
        add_option(GCodeProcessor::EMoveType::Color_change, EOptionsColors::ColorChanges, _u8L("Color changes"));
        add_option(GCodeProcessor::EMoveType::Pause_Print, EOptionsColors::PausePrints, _u8L("Pause prints"));
        add_option(GCodeProcessor::EMoveType::Custom_GCode, EOptionsColors::CustomGCodes, _u8L("Custom GCodes"));
    }

    imgui.end();
    ImGui::PopStyleVar();
}

#if ENABLE_GCODE_VIEWER_STATISTICS
void GCodeViewer::render_statistics() const
{
    static const ImVec4 ORANGE(1.0f, 0.49f, 0.22f, 1.0f);
    static const float offset = 250.0f;

    ImGuiWrapper& imgui = *wxGetApp().imgui();

    imgui.set_next_window_pos(0.5f * wxGetApp().plater()->get_current_canvas3D()->get_canvas_size().get_width(), 0.0f, ImGuiCond_Once, 0.5f, 0.0f);
    imgui.begin(std::string("Statistics"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    ImGui::PushStyleColor(ImGuiCol_Text, ORANGE);
    imgui.text(std::string("Load time:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.load_time) + "ms");

    ImGui::PushStyleColor(ImGuiCol_Text, ORANGE);
    imgui.text(std::string("Resfresh time:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.refresh_time) + "ms");

    ImGui::PushStyleColor(ImGuiCol_Text, ORANGE);
    imgui.text(std::string("Resfresh paths time:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.refresh_paths_time) + "ms");

    ImGui::Separator();

    ImGui::PushStyleColor(ImGuiCol_Text, ORANGE);
    imgui.text(std::string("Multi GL_POINTS calls:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.gl_multi_points_calls_count));

    ImGui::PushStyleColor(ImGuiCol_Text, ORANGE);
    imgui.text(std::string("Multi GL_LINE_STRIP calls:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.gl_multi_line_strip_calls_count));

    ImGui::Separator();

    ImGui::PushStyleColor(ImGuiCol_Text, ORANGE);
    imgui.text(std::string("GCodeProcessor results:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.results_size) + " bytes");

    ImGui::Separator();

    ImGui::PushStyleColor(ImGuiCol_Text, ORANGE);
    imgui.text(std::string("Vertices CPU:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.vertices_size) + " bytes");

    ImGui::PushStyleColor(ImGuiCol_Text, ORANGE);
    imgui.text(std::string("Indices CPU:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.indices_size) + " bytes");

    ImGui::PushStyleColor(ImGuiCol_Text, ORANGE);
    imgui.text(std::string("Paths CPU:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.paths_size) + " bytes");

    ImGui::PushStyleColor(ImGuiCol_Text, ORANGE);
    imgui.text(std::string("Render paths CPU:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.render_paths_size) + " bytes");

    ImGui::Separator();

    ImGui::PushStyleColor(ImGuiCol_Text, ORANGE);
    imgui.text(std::string("Vertices GPU:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.vertices_gpu_size) + " bytes");

    ImGui::PushStyleColor(ImGuiCol_Text, ORANGE);
    imgui.text(std::string("Indices GPU:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.indices_gpu_size) + " bytes");

    imgui.end();
}
#endif // ENABLE_GCODE_VIEWER_STATISTICS

#if ENABLE_GCODE_VIEWER_SHADERS_EDITOR
void GCodeViewer::render_shaders_editor() const
{
    auto set_shader = [this](const std::string& shader) {
        unsigned char begin_id = buffer_id(GCodeProcessor::EMoveType::Retract);
        unsigned char end_id = buffer_id(GCodeProcessor::EMoveType::Custom_GCode);
        for (unsigned char i = begin_id; i <= end_id; ++i) {
            m_buffers[i].shader = shader;
        }
    };

    static const ImVec4 ORANGE(1.0f, 0.49f, 0.22f, 1.0f);

    ImGuiWrapper& imgui = *wxGetApp().imgui();

    Size cnv_size = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size();
    imgui.set_next_window_pos(static_cast<float>(cnv_size.get_width()), 0.5f * static_cast<float>(cnv_size.get_height()), ImGuiCond_Once, 1.0f, 0.5f);
    imgui.begin(std::string("Shaders editor (DEV only)"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);

    ImGui::RadioButton("glsl version 1.10 (low end PCs)", &m_shaders_editor.shader_version, 0);
    ImGui::RadioButton("glsl version 1.20 flat (billboards)", &m_shaders_editor.shader_version, 1);
    ImGui::RadioButton("glsl version 1.20 solid (spheres default)", &m_shaders_editor.shader_version, 2);

    switch (m_shaders_editor.shader_version)
    {
    case 0: { set_shader("options_110"); break; }
    case 1: { set_shader("options_120_flat"); break; }
    case 2: { set_shader("options_120_solid"); break; }
    }

    if (ImGui::CollapsingHeader("Options", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("size dependent on zoom", &m_shaders_editor.size_dependent_on_zoom);
        if (m_shaders_editor.size_dependent_on_zoom)
        {
            if (ImGui::SliderInt("min size (min zoom)", &m_shaders_editor.sizes[0], 1, 100))
            {
                if (m_shaders_editor.sizes[1] < m_shaders_editor.sizes[0])
                    m_shaders_editor.sizes[1] = m_shaders_editor.sizes[0];
            }
            ImGui::SliderInt("max size (max zoom)", &m_shaders_editor.sizes[1], 1, 100);
            {
                if (m_shaders_editor.sizes[1] < m_shaders_editor.sizes[0])
                    m_shaders_editor.sizes[0] = m_shaders_editor.sizes[1];
            }
        }
        else
            ImGui::SliderInt("fixed size", &m_shaders_editor.fixed_size, 1, 100);

        if (m_shaders_editor.shader_version == 1)
        {
            ImGui::SliderInt("percent outline", &m_shaders_editor.percent_outline, 0, 50);
            ImGui::SliderInt("percent center", &m_shaders_editor.percent_center, 0, 50);
        }
    }

    imgui.end();
}
#endif // ENABLE_GCODE_VIEWER_SHADERS_EDITOR

bool GCodeViewer::is_travel_in_z_range(size_t id) const
{
    const IBuffer& buffer = m_buffers[buffer_id(GCodeProcessor::EMoveType::Travel)];
    if (id >= buffer.paths.size())
        return false;

    Path path = buffer.paths[id];
    int first = static_cast<int>(id);
    unsigned int last = static_cast<unsigned int>(id);

    // check adjacent paths
    while (first > 0 && path.first.position.isApprox(buffer.paths[first - 1].last.position)) {
        --first;
        path.first = buffer.paths[first].first;
    }
    while (last < static_cast<unsigned int>(buffer.paths.size() - 1) && path.last.position.isApprox(buffer.paths[last + 1].first.position)) {
        ++last;
        path.last = buffer.paths[last].last;
    }

    return is_in_z_range(path);
}

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_GCODE_VIEWER
