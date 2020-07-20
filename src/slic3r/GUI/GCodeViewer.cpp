#include "libslic3r/libslic3r.h"
#include "GCodeViewer.hpp"

#if ENABLE_GCODE_VIEWER
#include "libslic3r/Print.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Utils.hpp"
#include "GUI_App.hpp"
#if ENABLE_GCODE_VIEWER_AS_STATE
#include "MainFrame.hpp"
#endif // ENABLE_GCODE_VIEWER_AS_STATE
#include "Plater.hpp"
#include "PresetBundle.hpp"
#include "Camera.hpp"
#include "I18N.hpp"
#include "GUI_Utils.hpp"
#include "DoubleSlider.hpp"
#include "GLCanvas3D.hpp"
#include "GLToolbar.hpp"
#include "GUI_Preview.hpp"
#include <imgui/imgui_internal.h>

#include <GL/glew.h>
#include <boost/log/trivial.hpp>
#include <boost/nowide/cstdio.hpp>

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

std::array<float, 3> decode_color(const std::string& color) {
    static const float INV_255 = 1.0f / 255.0f;

    std::array<float, 3> ret;
    const char* c = color.data() + 1;
    if ((color.size() == 7) && (color.front() == '#')) {
        for (size_t j = 0; j < 3; ++j) {
            int digit1 = hex_digit_to_int(*c++);
            int digit2 = hex_digit_to_int(*c++);
            if ((digit1 == -1) || (digit2 == -1))
                break;

            ret[j] = float(digit1 * 16 + digit2) * INV_255;
        }
    }
    return ret;
}

std::vector<std::array<float, 3>> decode_colors(const std::vector<std::string>& colors) {
    std::vector<std::array<float, 3>> output(colors.size(), { 0.0f, 0.0f, 0.0f });
    for (size_t i = 0; i < colors.size(); ++i) {
        output[i] = decode_color(colors[i]);
    }
    return output;
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

void GCodeViewer::TBuffer::reset()
{
    // release gpu memory
    vertices.reset();
    indices.reset();

    // release cpu memory
    paths = std::vector<Path>();
    render_paths = std::vector<RenderPath>();
}

void GCodeViewer::TBuffer::add_path(const GCodeProcessor::MoveVertex& move, unsigned int i_id, unsigned int s_id)
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
    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_ORANGE_LIGHT);
    imgui.text(_u8L("Tool position") + ":");
    ImGui::PopStyleColor();
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
    for (size_t i = 0; i < m_buffers.size(); ++i)
    {
        switch (buffer_type(i))
        {
        default: { break; }
        case GCodeProcessor::EMoveType::Tool_change:
        case GCodeProcessor::EMoveType::Color_change:
        case GCodeProcessor::EMoveType::Pause_Print:
        case GCodeProcessor::EMoveType::Custom_GCode:
        case GCodeProcessor::EMoveType::Retract:
        case GCodeProcessor::EMoveType::Unretract:
        {
            m_buffers[i].vertices.format = VBuffer::EFormat::Position;
            break;
        }
        case GCodeProcessor::EMoveType::Extrude:
        case GCodeProcessor::EMoveType::Travel:
        {
            m_buffers[i].vertices.format = VBuffer::EFormat::PositionNormal;
            break;
        }
        }
    }

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
#if ENABLE_GCODE_VIEWER_AS_STATE
    if (wxGetApp().mainframe->get_mode() != MainFrame::EMode::GCodeViewer)
#endif // ENABLE_GCODE_VIEWER_AS_STATE
        load_shells(print, initialized);

#if ENABLE_GCODE_VIEWER_AS_STATE
    if (wxGetApp().mainframe->get_mode() == MainFrame::EMode::GCodeViewer) {
        // adjust printbed size in dependence of toolpaths bbox
        const double margin = 10.0;
        Vec2d min(m_paths_bounding_box.min(0) - margin, m_paths_bounding_box.min(1) - margin);
        Vec2d max(m_paths_bounding_box.max(0) + margin, m_paths_bounding_box.max(1) + margin);
        Pointfs bed_shape = { { min(0), min(1) },
                              { max(0), min(1) },
                              { max(0), max(1) },
                              { min(0), max(1) } };
        wxGetApp().plater()->set_bed_shape(bed_shape, "", "");
    }
#endif // ENABLE_GCODE_VIEWER_AS_STATE
}

void GCodeViewer::refresh(const GCodeProcessor::Result& gcode_result, const std::vector<std::string>& str_tool_colors)
{
#if ENABLE_GCODE_VIEWER_STATISTICS
    auto start_time = std::chrono::high_resolution_clock::now();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    if (m_vertices_count == 0)
        return;

    // update tool colors
    m_tool_colors = decode_colors(str_tool_colors);

    // update ranges for coloring / legend
    m_extrusions.reset_ranges();
    for (size_t i = 0; i < m_vertices_count; ++i) {
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

#if ENABLE_GCODE_VIEWER_STATISTICS
    m_statistics.refresh_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    // update buffers' render paths
    refresh_render_paths(false, false);
}

void GCodeViewer::reset()
{
    m_vertices_count = 0;
    for (TBuffer& buffer : m_buffers) {
        buffer.reset();
    }

    m_paths_bounding_box = BoundingBoxf3();
    m_max_bounding_box = BoundingBoxf3();
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
    m_sequential_view.marker.set_world_position(m_sequential_view.current_position);
    m_sequential_view.marker.render();
    render_shells();
    render_legend();
    render_time_estimate();
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
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::TimeEstimate), is_time_estimate_enabled());
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
    enable_time_estimate(is_flag_set(static_cast<unsigned int>(Preview::OptionType::TimeEstimate)));
}

void GCodeViewer::set_layers_z_range(const std::array<double, 2>& layers_z_range)
{
    bool keep_sequential_current_first = layers_z_range[0] >= m_layers_z_range[0];
    bool keep_sequential_current_last = layers_z_range[1] <= m_layers_z_range[1];
    m_layers_z_range = layers_z_range;
    refresh_render_paths(keep_sequential_current_first, keep_sequential_current_last);
    wxGetApp().plater()->update_preview_moves_slider();
}

void GCodeViewer::enable_time_estimate(bool enable)
{
    m_time_estimate_enabled = enable;
    wxGetApp().update_ui_from_settings();
    wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
    wxGetApp().plater()->get_current_canvas3D()->request_extra_frame();
}

void GCodeViewer::export_toolpaths_to_obj(const char* filename) const
{
    if (filename == nullptr)
        return;

    if (!has_data())
        return;

    wxBusyCursor busy;

    // the data needed is contained into the Extrude TBuffer
    const TBuffer& buffer = m_buffers[buffer_id(GCodeProcessor::EMoveType::Extrude)];
    if (buffer.vertices.id == 0 || buffer.indices.id == 0)
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
    for (const Color& color : colors)
    {
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

    auto get_vertex = [&vertices, floats_per_vertex](size_t id) {
        // extract vertex from vector of floats
        size_t base_id = id * floats_per_vertex;
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

    auto generate_segment = [get_vertex](size_t start_id, float half_width, float half_height) {
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
        Vec3f v2 = get_vertex(start_id + 1) - half_height * Vec3f::UnitZ();
        float length = (v2 - v1).norm();
        const auto&& [dir, right, up] = local_basis(v2 - v1);
        return Segment({ v1, v2, dir, right, up, half_width * right, half_height * up, length });
    };

    size_t out_vertices_count = 0;

    for (size_t i = 0; i < buffer.render_paths.size(); ++i) {
        // get paths segments from buffer paths
        const RenderPath& render_path = buffer.render_paths[i];
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

            for (size_t k = start; k < end; k += 2) {
                Segment curr = generate_segment(k, half_width, half_height);

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

                    Segment prev = generate_segment(k - 2, half_width, half_height);
                    Vec3f med_dir = (prev.dir + curr.dir).normalized();
                    float disp = half_width * ::tan(::acos(std::clamp(curr.dir.dot(med_dir), -1.0f, 1.0f)));
                    Vec3f disp_vec = disp * prev.dir;

                    bool is_right_turn = prev.up.dot(prev.dir.cross(curr.dir)) <= 0.0f;
                    if (prev.dir.dot(curr.dir) < 0.7071068f) {
                        // if the angle between two consecutive segments is greater than 45 degrees
                        // we add a cap in the outside corner 
                        // and displace the vertices in the inside corner to the same position, if possible
                        if (is_right_turn) {
                            // corner cap triangles (left)
                            size_t base_id = out_vertices_count - 6 + 1;
                            out_triangles.push_back({ base_id + 5, base_id + 2, base_id + 1 });
                            out_triangles.push_back({ base_id + 5, base_id + 3, base_id + 2 });

                            // update right vertices
                            if (disp < prev.length && disp < curr.length) {
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
                            if (disp < prev.length && disp < curr.length) {
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

void GCodeViewer::init_shaders()
{
    unsigned char begin_id = buffer_id(GCodeProcessor::EMoveType::Retract);
    unsigned char end_id = buffer_id(GCodeProcessor::EMoveType::Count);

    bool is_glsl_120 = wxGetApp().is_glsl_version_greater_or_equal_to(1, 20);
    for (unsigned char i = begin_id; i < end_id; ++i) {
        switch (buffer_type(i))
        {
        case GCodeProcessor::EMoveType::Tool_change:  { m_buffers[i].shader = is_glsl_120 ? "options_120_flat" : "options_110"; break; }
        case GCodeProcessor::EMoveType::Color_change: { m_buffers[i].shader = is_glsl_120 ? "options_120_flat" : "options_110"; break; }
        case GCodeProcessor::EMoveType::Pause_Print:  { m_buffers[i].shader = is_glsl_120 ? "options_120_flat" : "options_110"; break; }
        case GCodeProcessor::EMoveType::Custom_GCode: { m_buffers[i].shader = is_glsl_120 ? "options_120_flat" : "options_110"; break; }
        case GCodeProcessor::EMoveType::Retract:      { m_buffers[i].shader = is_glsl_120 ? "options_120_flat" : "options_110"; break; }
        case GCodeProcessor::EMoveType::Unretract:    { m_buffers[i].shader = is_glsl_120 ? "options_120_flat" : "options_110"; break; }
        case GCodeProcessor::EMoveType::Extrude:      { m_buffers[i].shader = "toolpaths"; break; }
        case GCodeProcessor::EMoveType::Travel:       { m_buffers[i].shader = "toolpaths"; break; }
        default: { break; }
        }
    }
}

void GCodeViewer::load_toolpaths(const GCodeProcessor::Result& gcode_result)
{
#if ENABLE_GCODE_VIEWER_STATISTICS
    auto start_time = std::chrono::high_resolution_clock::now();
    m_statistics.results_size = SLIC3R_STDVEC_MEMSIZE(gcode_result.moves, GCodeProcessor::MoveVertex);
    m_statistics.results_time = gcode_result.time;
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    // vertices data
    m_vertices_count = gcode_result.moves.size();
    if (m_vertices_count == 0)
        return;

    for (size_t i = 0; i < m_vertices_count; ++i) {
        const GCodeProcessor::MoveVertex& move = gcode_result.moves[i];
#if ENABLE_GCODE_VIEWER_AS_STATE
        if (wxGetApp().mainframe->get_mode() == MainFrame::EMode::GCodeViewer)
            // for the gcode viewer we need all moves to correctly size the printbed
            m_paths_bounding_box.merge(move.position.cast<double>());
        else {
#endif // ENABLE_GCODE_VIEWER_AS_STATE
            if (move.type == GCodeProcessor::EMoveType::Extrude && move.width != 0.0f && move.height != 0.0f)
                m_paths_bounding_box.merge(move.position.cast<double>());
#if ENABLE_GCODE_VIEWER_AS_STATE
        }
#endif // ENABLE_GCODE_VIEWER_AS_STATE
    }

    // max bounding box
    m_max_bounding_box = m_paths_bounding_box;
    m_max_bounding_box.merge(m_paths_bounding_box.max + m_sequential_view.marker.get_bounding_box().size()[2] * Vec3d::UnitZ());

    // toolpaths data -> extract from result
    std::vector<std::vector<float>> vertices(m_buffers.size());
    std::vector<std::vector<unsigned int>> indices(m_buffers.size());
    for (size_t i = 0; i < m_vertices_count; ++i) {
        // skip first vertex
        if (i == 0)
            continue;

        const GCodeProcessor::MoveVertex& prev = gcode_result.moves[i - 1];
        const GCodeProcessor::MoveVertex& curr = gcode_result.moves[i];

        unsigned char id = buffer_id(curr.type);
        TBuffer& buffer = m_buffers[id];
        std::vector<float>& buffer_vertices = vertices[id];
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
            for (int j = 0; j < 3; ++j) {
                buffer_vertices.push_back(curr.position[j]);
            }
            buffer.add_path(curr, static_cast<unsigned int>(buffer_indices.size()), static_cast<unsigned int>(i));
            buffer_indices.push_back(static_cast<unsigned int>(buffer_indices.size()));
            break;
        }
        case GCodeProcessor::EMoveType::Extrude:
        case GCodeProcessor::EMoveType::Travel:
        {
            // x component of the normal to the current segment (the normal is parallel to the XY plane)
            float normal_x = (curr.position - prev.position).normalized()[1];

            if (prev.type != curr.type || !buffer.paths.back().matches(curr)) {
                // add starting vertex position
                for (int j = 0; j < 3; ++j) {
                    buffer_vertices.push_back(prev.position[j]);
                }
                // add starting vertex normal x component
                buffer_vertices.push_back(normal_x);
                // add starting index
                buffer_indices.push_back(buffer_indices.size());
                buffer.add_path(curr, static_cast<unsigned int>(buffer_indices.size() - 1), static_cast<unsigned int>(i - 1));
                Path& last_path = buffer.paths.back();
                last_path.first.position = prev.position;
            }

            Path& last_path = buffer.paths.back();
            if (last_path.first.i_id != last_path.last.i_id)
            {
                // add previous vertex position
                for (int j = 0; j < 3; ++j) {
                    buffer_vertices.push_back(prev.position[j]);
                }
                // add previous vertex normal x component
                buffer_vertices.push_back(normal_x);
                // add previous index
                buffer_indices.push_back(buffer_indices.size());
            }

            // add current vertex position
            for (int j = 0; j < 3; ++j) {
                buffer_vertices.push_back(curr.position[j]);
            }
            // add current vertex normal x component
            buffer_vertices.push_back(normal_x);
            // add current index
            buffer_indices.push_back(buffer_indices.size());
            last_path.last = { static_cast<unsigned int>(buffer_indices.size() - 1), static_cast<unsigned int>(i), curr.position };
            break;
        }
        default: { break; }
        }
    }

    // toolpaths data -> send data to gpu
    for (size_t i = 0; i < m_buffers.size(); ++i) {
        TBuffer& buffer = m_buffers[i];

        // vertices
        const std::vector<float>& buffer_vertices = vertices[i];
        buffer.vertices.count = buffer_vertices.size() / buffer.vertices.vertex_size_floats();
#if ENABLE_GCODE_VIEWER_STATISTICS
        m_statistics.vertices_gpu_size = buffer_vertices.size() * sizeof(float);
#endif // ENABLE_GCODE_VIEWER_STATISTICS

        glsafe(::glGenBuffers(1, &buffer.vertices.id));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, buffer.vertices.id));
        glsafe(::glBufferData(GL_ARRAY_BUFFER, buffer_vertices.size() * sizeof(float), buffer_vertices.data(), GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));

        // indices
        const std::vector<unsigned int>& buffer_indices = indices[i];
        buffer.indices.count = buffer_indices.size();
#if ENABLE_GCODE_VIEWER_STATISTICS
        m_statistics.indices_gpu_size += buffer.indices.count * sizeof(unsigned int);
#endif // ENABLE_GCODE_VIEWER_STATISTICS

        if (buffer.indices.count > 0) {
            glsafe(::glGenBuffers(1, &buffer.indices.id));
            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.indices.id));
            glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, buffer.indices.count * sizeof(unsigned int), buffer_indices.data(), GL_STATIC_DRAW));
            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
        }
    }

#if ENABLE_GCODE_VIEWER_STATISTICS
    for (const TBuffer& buffer : m_buffers) {
        m_statistics.paths_size += SLIC3R_STDVEC_MEMSIZE(buffer.paths, Path);
    }
    m_statistics.travel_segments_count = indices[buffer_id(GCodeProcessor::EMoveType::Travel)].size() / 2;
    m_statistics.extrude_segments_count = indices[buffer_id(GCodeProcessor::EMoveType::Extrude)].size() / 2;
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    // layers zs / roles / extruder ids / cp color ids -> extract from result
    for (size_t i = 0; i < m_vertices_count; ++i) {
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

    m_sequential_view.endpoints.first = m_vertices_count;
    m_sequential_view.endpoints.last = 0;
    if (!keep_sequential_current_first)
        m_sequential_view.current.first = 0;
    if (!keep_sequential_current_last)
        m_sequential_view.current.last = m_vertices_count;

    // first pass: collect visible paths and update sequential view data
    std::vector<std::pair<TBuffer*, size_t>> paths;
    for (TBuffer& buffer : m_buffers) {
        // reset render paths
        buffer.render_paths.clear();

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

    // get the world position from gpu
    bool found = false;
    for (const TBuffer& buffer : m_buffers) {
        // searches the path containing the current position
        for (const Path& path : buffer.paths) {
            if (path.first.s_id <= m_sequential_view.current.last && m_sequential_view.current.last <= path.last.s_id) {
                size_t offset = m_sequential_view.current.last - path.first.s_id;
                if (offset > 0 && (path.type == GCodeProcessor::EMoveType::Travel || path.type == GCodeProcessor::EMoveType::Extrude))
                    offset = 1 + 2 * (offset - 1);

                offset += path.first.i_id;

                // gets the position from the vertices buffer on gpu
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, buffer.vertices.id));
                glsafe(::glGetBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(offset * buffer.vertices.vertex_size_bytes()), static_cast<GLsizeiptr>(3 * sizeof(float)), static_cast<void*>(m_sequential_view.current_position.data())));
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
                found = true;
                break;
            }
        }
        if (found)
            break;
    }

    // second pass: filter paths by sequential data and collect them by color
    for (const auto& [buffer, id] : paths) {
        const Path& path = buffer->paths[id];
        if (m_sequential_view.current.last <= path.first.s_id || path.last.s_id <= m_sequential_view.current.first)
            continue;

        Color color;
        switch (path.type)
        {
        case GCodeProcessor::EMoveType::Extrude: { color = extrusion_color(path); break; }
        case GCodeProcessor::EMoveType::Travel: { color = (m_view_type == EViewType::Feedrate || m_view_type == EViewType::Tool || m_view_type == EViewType::ColorPrint) ? extrusion_color(path) : travel_color(path); break; }
        default: { color = { 0.0f, 0.0f, 0.0f }; break; }
        }

        auto it = std::find_if(buffer->render_paths.begin(), buffer->render_paths.end(), [color](const RenderPath& path) { return path.color == color; });
        if (it == buffer->render_paths.end()) {
            it = buffer->render_paths.insert(buffer->render_paths.end(), RenderPath());
            it->color = color;
            it->path_id = id;
        }

        unsigned int size = std::min(m_sequential_view.current.last, path.last.s_id) - std::max(m_sequential_view.current.first, path.first.s_id) + 1;
        if (path.type == GCodeProcessor::EMoveType::Extrude || path.type == GCodeProcessor::EMoveType::Travel)
            size = 2 * (size - 1);

        it->sizes.push_back(size);
        unsigned int delta_1st = 0;
        if ((path.first.s_id < m_sequential_view.current.first) && (m_sequential_view.current.first <= path.last.s_id))
            delta_1st = m_sequential_view.current.first - path.first.s_id;

        it->offsets.push_back(static_cast<size_t>((path.first.i_id + delta_1st) * sizeof(unsigned int)));
    }

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
#if ENABLE_GCODE_VIEWER_SHADERS_EDITOR
    float point_size = m_shaders_editor.points.point_size;
#else
    float point_size = 0.8f;
#endif // ENABLE_GCODE_VIEWER_SHADERS_EDITOR
    const Camera& camera = wxGetApp().plater()->get_camera();
    double zoom = camera.get_zoom();
    const std::array<int, 4>& viewport = camera.get_viewport();
    float near_plane_height = camera.get_type() == Camera::Perspective ? static_cast<float>(viewport[3]) / (2.0f * static_cast<float>(2.0 * std::tan(0.5 * Geometry::deg2rad(camera.get_fov())))) :
        static_cast<float>(viewport[3]) * 0.0005;

    Transform3d inv_proj = camera.get_projection_matrix().inverse();

    auto render_as_points = [this, zoom, inv_proj, viewport, point_size, near_plane_height](const TBuffer& buffer, EOptionsColors color_id, GLShaderProgram& shader) {
        shader.set_uniform("uniform_color", Options_Colors[static_cast<unsigned int>(color_id)]);
        shader.set_uniform("zoom", zoom);
#if ENABLE_GCODE_VIEWER_SHADERS_EDITOR
        shader.set_uniform("percent_outline_radius", 0.01f * static_cast<float>(m_shaders_editor.points.percent_outline));
        shader.set_uniform("percent_center_radius", 0.01f * static_cast<float>(m_shaders_editor.points.percent_center));
#else
        shader.set_uniform("percent_outline_radius", 0.0f);
        shader.set_uniform("percent_center_radius", 0.33f);
#endif // ENABLE_GCODE_VIEWER_SHADERS_EDITOR
        shader.set_uniform("viewport", viewport);
        shader.set_uniform("inv_proj_matrix", inv_proj);
        shader.set_uniform("point_size", point_size);
        shader.set_uniform("near_plane_height", near_plane_height);

        glsafe(::glEnable(GL_VERTEX_PROGRAM_POINT_SIZE));
        glsafe(::glEnable(GL_POINT_SPRITE));

        for (const RenderPath& path : buffer.render_paths) {
            glsafe(::glMultiDrawElements(GL_POINTS, (const GLsizei*)path.sizes.data(), GL_UNSIGNED_INT, (const void* const*)path.offsets.data(), (GLsizei)path.sizes.size()));
#if ENABLE_GCODE_VIEWER_STATISTICS
            ++m_statistics.gl_multi_points_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
        }

        glsafe(::glDisable(GL_POINT_SPRITE));
        glsafe(::glDisable(GL_VERTEX_PROGRAM_POINT_SIZE));
    };

    auto render_as_lines = [this](const TBuffer& buffer, GLShaderProgram& shader) {
        for (const RenderPath& path : buffer.render_paths) {
            shader.set_uniform("uniform_color", path.color);
            glsafe(::glMultiDrawElements(GL_LINES, (const GLsizei*)path.sizes.data(), GL_UNSIGNED_INT, (const void* const*)path.offsets.data(), (GLsizei)path.sizes.size()));
#if ENABLE_GCODE_VIEWER_STATISTICS
            ++m_statistics.gl_multi_line_strip_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
        }
    };

    auto line_width = [](double zoom) {
        return (zoom < 5.0) ? 1.0 : (1.0 + 5.0 * (zoom - 5.0) / (100.0 - 5.0));
    };

    glsafe(::glLineWidth(static_cast<GLfloat>(line_width(zoom))));

    unsigned char begin_id = buffer_id(GCodeProcessor::EMoveType::Retract);
    unsigned char end_id = buffer_id(GCodeProcessor::EMoveType::Count);

    for (unsigned char i = begin_id; i < end_id; ++i) {
        const TBuffer& buffer = m_buffers[i];
        if (!buffer.visible)
            continue;

        if (buffer.vertices.id == 0 || buffer.indices.id == 0)
            continue;

        GLShaderProgram* shader = wxGetApp().get_shader(buffer.shader.c_str());
        if (shader != nullptr) {
            shader->start_using();

            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, buffer.vertices.id));
            glsafe(::glVertexPointer(buffer.vertices.vertex_size_floats(), GL_FLOAT, buffer.vertices.vertex_size_bytes(), (const void*)0));
            glsafe(::glEnableClientState(GL_VERTEX_ARRAY));

            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.indices.id));

            switch (buffer_type(i))
            {
            default: { break; }
            case GCodeProcessor::EMoveType::Tool_change:  { render_as_points(buffer, EOptionsColors::ToolChanges, *shader); break; }
            case GCodeProcessor::EMoveType::Color_change: { render_as_points(buffer, EOptionsColors::ColorChanges, *shader); break; }
            case GCodeProcessor::EMoveType::Pause_Print:  { render_as_points(buffer, EOptionsColors::PausePrints, *shader); break; }
            case GCodeProcessor::EMoveType::Custom_GCode: { render_as_points(buffer, EOptionsColors::CustomGCodes, *shader); break; }
            case GCodeProcessor::EMoveType::Retract:      { render_as_points(buffer, EOptionsColors::Retractions, *shader); break; }
            case GCodeProcessor::EMoveType::Unretract:    { render_as_points(buffer, EOptionsColors::Unretractions, *shader); break; }
            case GCodeProcessor::EMoveType::Extrude:
            case GCodeProcessor::EMoveType::Travel:
            {
#if ENABLE_GCODE_VIEWER_SHADERS_EDITOR
                std::array<float, 4> light_intensity = {
                    m_shaders_editor.lines.lights.ambient,
                    m_shaders_editor.lines.lights.top_diffuse,
                    m_shaders_editor.lines.lights.front_diffuse,
                    m_shaders_editor.lines.lights.global };
#else
                std::array<float, 4> light_intensity = { 0.25f, 0.7f, 0.75f, 0.75f };
#endif // ENABLE_GCODE_VIEWER_SHADERS_EDITOR
                shader->set_uniform("light_intensity", light_intensity);
                render_as_lines(buffer, *shader);
                break;
            }
            }

            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

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

#define USE_ICON_HEXAGON 1

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

    auto append_item = [this, draw_list, &imgui](EItemType type, const Color& color, const std::string& label, std::function<void()> callback = nullptr) {
        float icon_size = ImGui::GetTextLineHeight();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        switch (type)
        {
        default:
        case EItemType::Rect:
        {
            draw_list->AddRectFilled({ pos.x + 1.0f, pos.y + 1.0f }, { pos.x + icon_size - 1.0f, pos.y + icon_size - 1.0f },
                ImGui::GetColorU32({ color[0], color[1], color[2], 1.0f }));
            break;
        }
        case EItemType::Circle:
        {
#if ENABLE_GCODE_VIEWER_SHADERS_EDITOR
            ImVec2 center(0.5f * (pos.x + pos.x + icon_size), 0.5f * (pos.y + pos.y + icon_size));
            if (m_shaders_editor.points.shader_version == 1) {
                draw_list->AddCircleFilled(center, 0.5f * icon_size,
                    ImGui::GetColorU32({ 0.5f * color[0], 0.5f * color[1], 0.5f * color[2], 1.0f }), 16);
                float radius = 0.5f * icon_size * (1.0f - 0.01f * static_cast<float>(m_shaders_editor.points.percent_outline));
                draw_list->AddCircleFilled(center, radius, ImGui::GetColorU32({ color[0], color[1], color[2], 1.0f }), 16);
                if (m_shaders_editor.points.percent_center > 0) {
                    radius = 0.5f * icon_size * 0.01f * static_cast<float>(m_shaders_editor.points.percent_center);
                    draw_list->AddCircleFilled(center, radius, ImGui::GetColorU32({ 0.5f * color[0], 0.5f * color[1], 0.5f * color[2], 1.0f }), 16);
                }
            }
            else
                draw_list->AddCircleFilled(center, 0.5f * icon_size, ImGui::GetColorU32({ color[0], color[1], color[2], 1.0f }), 16);
#else
            ImVec2 center(0.5f * (pos.x + pos.x + icon_size), 0.5f * (pos.y + pos.y + icon_size));            
            if (m_buffers[buffer_id(GCodeProcessor::EMoveType::Retract)].shader == "options_120_flat") {
                draw_list->AddCircleFilled(center, 0.5f * icon_size,
                    ImGui::GetColorU32({ 0.5f * color[0], 0.5f * color[1], 0.5f * color[2], 1.0f }), 16);
                float radius = 0.5f * icon_size;
                draw_list->AddCircleFilled(center, radius, ImGui::GetColorU32({ color[0], color[1], color[2], 1.0f }), 16);
                radius = 0.5f * icon_size * 0.01f * 33.0f;
                draw_list->AddCircleFilled(center, radius, ImGui::GetColorU32({ 0.5f * color[0], 0.5f * color[1], 0.5f * color[2], 1.0f }), 16);
            }
            else
                draw_list->AddCircleFilled(center, 0.5f * icon_size, ImGui::GetColorU32({ color[0], color[1], color[2], 1.0f }), 16);
#endif // ENABLE_GCODE_VIEWER_SHADERS_EDITOR

            break;
        }
        case EItemType::Hexagon:
        {
            ImVec2 center(0.5f * (pos.x + pos.x + icon_size), 0.5f * (pos.y + pos.y + icon_size));
            draw_list->AddNgonFilled(center, 0.5f * icon_size, ImGui::GetColorU32({ color[0], color[1], color[2], 1.0f }), 6);
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
        if (callback != nullptr) {
            if (ImGui::MenuItem(label.c_str()))
                callback();
        }
        else
            imgui.text(label);
    };

    auto append_range = [this, draw_list, &imgui, append_item](const Extrusions::Range& range, unsigned int decimals) {
        auto append_range_item = [this, draw_list, &imgui, append_item](int i, float value, unsigned int decimals) {
            char buf[1024];
            ::sprintf(buf, "%.*f", decimals, value);
#if USE_ICON_HEXAGON
            append_item(EItemType::Hexagon, Range_Colors[i], buf);
#else
            append_item(EItemType::Rect, Range_Colors[i], buf);
#endif // USE_ICON_HEXAGON
        };

        float step_size = range.step_size();
        if (step_size == 0.0f)
            // single item use case
            append_range_item(0, range.min, decimals);
        else {
            for (int i = static_cast<int>(Range_Colors.size()) - 1; i >= 0; --i) {
                append_range_item(i, range.min + static_cast<float>(i) * step_size, decimals);
            }
        }
    };

    auto color_print_ranges = [this](unsigned char extruder_id, const std::vector<CustomGCode::Item>& custom_gcode_per_print_z) {
        std::vector<std::pair<Color, std::pair<double, double>>> ret;
        ret.reserve(custom_gcode_per_print_z.size());

        for (const auto& item : custom_gcode_per_print_z) {
            if (extruder_id + 1 != static_cast<unsigned char>(item.extruder))
                continue;

            if (item.type != ColorChange)
                continue;

            auto lower_b = std::lower_bound(m_layers_zs.begin(), m_layers_zs.end(), item.print_z - Slic3r::DoubleSlider::epsilon());

            if (lower_b == m_layers_zs.end())
                continue;

            double current_z = *lower_b;
            double previous_z = lower_b == m_layers_zs.begin() ? 0.0 : *(--lower_b);

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

    // extrusion paths -> title
    switch (m_view_type)
    {
    case EViewType::FeatureType:    { imgui.title(_u8L("Feature type")); break; }
    case EViewType::Height:         { imgui.title(_u8L("Height (mm)")); break; }
    case EViewType::Width:          { imgui.title(_u8L("Width (mm)")); break; }
    case EViewType::Feedrate:       { imgui.title(_u8L("Speed (mm/s)")); break; }
    case EViewType::FanSpeed:       { imgui.title(_u8L("Fan Speed (%%)")); break; }
    case EViewType::VolumetricRate: { imgui.title(_u8L("Volumetric flow rate (mm³/s)")); break; }
    case EViewType::Tool:           { imgui.title(_u8L("Tool")); break; }
    case EViewType::ColorPrint:     { imgui.title(_u8L("Color Print")); break; }
    default: { break; }
    }

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
            append_item(EItemType::Hexagon, Extrusion_Role_Colors[static_cast<unsigned int>(role)], _u8L(ExtrusionEntity::role_to_string(role)), [this, role]() {
#else
            append_item(EItemType::Rect, Extrusion_Role_Colors[static_cast<unsigned int>(role)], _u8L(ExtrusionEntity::role_to_string(role)), [this, role]() {
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
    case EViewType::Height:         { append_range(m_extrusions.ranges.height, 3); break; }
    case EViewType::Width:          { append_range(m_extrusions.ranges.width, 3); break; }
    case EViewType::Feedrate:       { append_range(m_extrusions.ranges.feedrate, 1); break; }
    case EViewType::FanSpeed:       { append_range(m_extrusions.ranges.fan_speed, 0); break; }
    case EViewType::VolumetricRate: { append_range(m_extrusions.ranges.volumetric_rate, 3); break; }
    case EViewType::Tool:
    {
        // shows only extruders actually used
        for (unsigned char i : m_extruder_ids) {
#if USE_ICON_HEXAGON
            append_item(EItemType::Hexagon, m_tool_colors[i], _u8L("Extruder") + " " + std::to_string(i + 1));
#else
            append_item(EItemType::Rect, m_tool_colors[i], _u8L("Extruder") + " " + std::to_string(i + 1));
#endif // USE_ICON_HEXAGON
        }
        break;
    }
    case EViewType::ColorPrint:
    {
        const std::vector<CustomGCode::Item>& custom_gcode_per_print_z = wxGetApp().plater()->model().custom_gcode_per_print_z.gcodes;
        const int extruders_count = wxGetApp().extruders_edited_cnt();
        if (extruders_count == 1) { // single extruder use case
            std::vector<std::pair<Color, std::pair<double, double>>> cp_values = color_print_ranges(0, custom_gcode_per_print_z);
            const int items_cnt = static_cast<int>(cp_values.size());
            if (items_cnt == 0) { // There are no color changes, but there are some pause print or custom Gcode
#if USE_ICON_HEXAGON
                append_item(EItemType::Hexagon, m_tool_colors.front(), _u8L("Default color"));
#else
                append_item(EItemType::Rect, m_tool_colors.front(), _u8L("Default color"));
#endif // USE_ICON_HEXAGON
            }
            else {
                for (int i = items_cnt; i >= 0; --i) {
                    // create label for color change item
                    if (i == 0) {
#if USE_ICON_HEXAGON
                        append_item(EItemType::Hexagon, m_tool_colors[0], upto_label(cp_values.front().second.first));
#else
                        append_item(EItemType::Rect, m_tool_colors[0], upto_label(cp_values.front().second.first);
#endif // USE_ICON_HEXAGON
                        break;
                    }
                    else if (i == items_cnt) {
#if USE_ICON_HEXAGON
                        append_item(EItemType::Hexagon, cp_values[i - 1].first, above_label(cp_values[i - 1].second.second));
#else
                        append_item(EItemType::Rect, cp_values[i - 1].first, above_label(cp_values[i - 1].second.second);
#endif // USE_ICON_HEXAGON
                        continue;
                    }
#if USE_ICON_HEXAGON
                    append_item(EItemType::Hexagon, cp_values[i - 1].first, fromto_label(cp_values[i - 1].second.second, cp_values[i].second.first));
#else
                    append_item(EItemType::Rect, cp_values[i - 1].first, fromto_label(cp_values[i - 1].second.second, cp_values[i].second.first));
#endif // USE_ICON_HEXAGON
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
#if USE_ICON_HEXAGON
                    append_item(EItemType::Hexagon, m_tool_colors[i], _u8L("Extruder") + " " + std::to_string(i + 1) + " " + _u8L("default color"));
#else
                    append_item(EItemType::Rect, m_tool_colors[i], _u8L("Extruder") + " " + std::to_string(i + 1) + " " + _u8L("default color"));
#endif // USE_ICON_HEXAGON
                }
                else {
                    for (int j = items_cnt; j >= 0; --j) {
                        // create label for color change item
                        std::string label = _u8L("Extruder") + " " + std::to_string(i + 1);
                        if (j == 0) {
                            label += " " + upto_label(cp_values.front().second.first);
#if USE_ICON_HEXAGON
                            append_item(EItemType::Hexagon, m_tool_colors[i], label);
#else
                            append_item(EItemType::Rect, m_tool_colors[i], label);
#endif // USE_ICON_HEXAGON
                            break;
                        }
                        else if (j == items_cnt) {
                            label += " " + above_label(cp_values[j - 1].second.second);
#if USE_ICON_HEXAGON
                            append_item(EItemType::Hexagon, cp_values[j - 1].first, label);
#else
                            append_item(EItemType::Rect, cp_values[j - 1].first, label);
#endif // USE_ICON_HEXAGON
                            continue;
                        }

                        label += " " + fromto_label(cp_values[j - 1].second.second, cp_values[j].second.first);
#if USE_ICON_HEXAGON
                        append_item(EItemType::Hexagon, cp_values[j - 1].first, label);
#else
                        append_item(EItemType::Rect, cp_values[j - 1].first, label);
#endif // USE_ICON_HEXAGON
                    }
                }
            }
        }

        break;
    }
    default: { break; }
    }

    // travel paths
    if (m_buffers[buffer_id(GCodeProcessor::EMoveType::Travel)].visible) {
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
            imgui.title(_u8L("Travel"));

            // items
            append_item(EItemType::Line, Travel_Colors[0], _u8L("Movement"));
            append_item(EItemType::Line, Travel_Colors[1], _u8L("Extrusion"));
            append_item(EItemType::Line, Travel_Colors[2], _u8L("Retraction"));

            break;
        }
        }
    }

    auto any_option_available = [this]() {
        auto available = [this](GCodeProcessor::EMoveType type) {
            const TBuffer& buffer = m_buffers[buffer_id(type)];
            return buffer.visible && buffer.indices.count > 0;
        };

        return available(GCodeProcessor::EMoveType::Color_change) ||
            available(GCodeProcessor::EMoveType::Custom_GCode) ||
            available(GCodeProcessor::EMoveType::Pause_Print) ||
            available(GCodeProcessor::EMoveType::Retract) ||
            available(GCodeProcessor::EMoveType::Tool_change) ||
            available(GCodeProcessor::EMoveType::Unretract);
    };

    auto add_option = [this, append_item](GCodeProcessor::EMoveType move_type, EOptionsColors color, const std::string& text) {
        const TBuffer& buffer = m_buffers[buffer_id(move_type)];
        if (buffer.visible && buffer.indices.count > 0)
#if ENABLE_GCODE_VIEWER_SHADERS_EDITOR
            append_item((m_shaders_editor.points.shader_version == 0) ? EItemType::Rect : EItemType::Circle, Options_Colors[static_cast<unsigned int>(color)], text);
#else
            append_item((buffer.shader == "options_110") ? EItemType::Rect : EItemType::Circle, Options_Colors[static_cast<unsigned int>(color)], text);
#endif // ENABLE_GCODE_VIEWER_SHADERS_EDITOR
    };

    // options
    if (any_option_available()) {
        // title
        ImGui::Spacing();
        imgui.title(_u8L("Options"));

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

void GCodeViewer::render_time_estimate() const
{
    if (!m_time_estimate_enabled)
        return;

    const PrintStatistics& ps = wxGetApp().plater()->fff_print().print_statistics();
    if (ps.estimated_normal_print_time <= 0.0f && ps.estimated_silent_print_time <= 0.0f)
        return;

    ImGuiWrapper& imgui = *wxGetApp().imgui();

    using Times = std::pair<float, float>;
    using TimesList = std::vector<std::pair<CustomGCode::Type, Times>>;
    using Headers = std::vector<std::string>;
    using ColumnOffsets = std::array<float, 2>;

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

    auto append_headers = [&imgui](const Headers& headers, const ColumnOffsets& offsets) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_ORANGE_LIGHT);
        imgui.text(headers[0]);
        ImGui::SameLine(offsets[0]);
        imgui.text(headers[1]);
        ImGui::SameLine(offsets[1]);
        imgui.text(headers[2]);
        ImGui::PopStyleColor();
        ImGui::Separator();
    };

    auto append_mode = [this, &imgui, append_headers](float total_time, const PartialTimes& items,
        const Headers& partial_times_headers,
        const std::vector<std::pair<GCodeProcessor::EMoveType, float>>& moves_time,
        const Headers& moves_headers,
        const std::vector<std::pair<ExtrusionRole, float>>& roles_time,
        const Headers& roles_headers) {
            auto append_partial_times = [this, &imgui, append_headers](const PartialTimes& items, const Headers& headers) {
                auto calc_offsets = [this, &headers](const PartialTimes& items) {
                ColumnOffsets ret = { ImGui::CalcTextSize(headers[0].c_str()).x, ImGui::CalcTextSize(headers[1].c_str()).x };
                for (const PartialTime& item : items) {
                    std::string label;
                    switch (item.type)
                    {
                    case PartialTime::EType::Print:       { label = _u8L("Print"); break; }
                    case PartialTime::EType::Pause:       { label = _u8L("Pause"); break; }
                    case PartialTime::EType::ColorChange: { label = _u8L("Color change"); break; }
                    }

                    ret[0] = std::max(ret[0], ImGui::CalcTextSize(label.c_str()).x);
                    ret[1] = std::max(ret[1], ImGui::CalcTextSize(short_time(get_time_dhms(item.times.second)).c_str()).x);
                }

                const ImGuiStyle& style = ImGui::GetStyle();
                ret[0] += 2.0f * (ImGui::GetTextLineHeight() + style.ItemSpacing.x);
                ret[1] += ret[0] + style.ItemSpacing.x;
                return ret;
            };
            auto append_color = [this, &imgui](const Color& color1, const Color& color2, ColumnOffsets& offsets, const Times& times) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_ORANGE_LIGHT);
                imgui.text(_u8L("Color change"));
                ImGui::PopStyleColor();
                ImGui::SameLine();

                float icon_size = ImGui::GetTextLineHeight();
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                ImVec2 pos = ImGui::GetCursorScreenPos();
                pos.x -= 0.5f * ImGui::GetStyle().ItemSpacing.x;
#if USE_ICON_HEXAGON
                ImVec2 center(0.5f * (pos.x + pos.x + icon_size), 0.5f * (pos.y + pos.y + icon_size));
                draw_list->AddNgonFilled(center, 0.5f * icon_size, ImGui::GetColorU32({ color1[0], color1[1], color1[2], 1.0f }), 6);
                center.x += icon_size;
                draw_list->AddNgonFilled(center, 0.5f * icon_size, ImGui::GetColorU32({ color2[0], color2[1], color2[2], 1.0f }), 6);
#else
                draw_list->AddRectFilled({ pos.x + 1.0f, pos.y + 1.0f }, { pos.x + icon_size - 1.0f, pos.y + icon_size - 1.0f },
                    ImGui::GetColorU32({ color1[0], color1[1], color1[2], 1.0f }));
                pos.x += icon_size;
                draw_list->AddRectFilled({ pos.x + 1.0f, pos.y + 1.0f }, { pos.x + icon_size - 1.0f, pos.y + icon_size - 1.0f },
                    ImGui::GetColorU32({ color2[0], color2[1], color2[2], 1.0f }));
#endif // USE_ICON_HEXAGON
                ImGui::SameLine(offsets[0]);
                imgui.text(short_time(get_time_dhms(times.second - times.first)));
            };

            if (items.empty())
                return;

            ColumnOffsets offsets = calc_offsets(items);

            ImGui::Spacing();
            append_headers(headers, offsets);

            for (const PartialTime& item : items) {
                switch (item.type)
                {
                case PartialTime::EType::Print:
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_ORANGE_LIGHT);
                    imgui.text(_u8L("Print"));
                    ImGui::PopStyleColor();
                    ImGui::SameLine(offsets[0]);
                    imgui.text(short_time(get_time_dhms(item.times.second)));
                    ImGui::SameLine(offsets[1]);
                    imgui.text(short_time(get_time_dhms(item.times.first)));
                    break;
                }
                case PartialTime::EType::Pause:
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_ORANGE_LIGHT);
                    imgui.text(_u8L("Pause"));
                    ImGui::PopStyleColor();
                    ImGui::SameLine(offsets[0]);
                    imgui.text(short_time(get_time_dhms(item.times.second - item.times.first)));
                    break;
                }
                case PartialTime::EType::ColorChange:
                {
                    append_color(item.color1, item.color2, offsets, item.times);
                    break;
                }
                }
            }
        };

        auto move_type_label = [](GCodeProcessor::EMoveType type) {
            switch (type)
            {
            case GCodeProcessor::EMoveType::Noop:         { return _u8L("Noop"); }
            case GCodeProcessor::EMoveType::Retract:      { return _u8L("Retraction"); }
            case GCodeProcessor::EMoveType::Unretract:    { return _u8L("Unretraction"); }
            case GCodeProcessor::EMoveType::Tool_change:  { return _u8L("Tool change"); }
            case GCodeProcessor::EMoveType::Color_change: { return _u8L("Color change"); }
            case GCodeProcessor::EMoveType::Pause_Print:  { return _u8L("Pause print"); }
            case GCodeProcessor::EMoveType::Custom_GCode: { return _u8L("Custom GCode"); }
            case GCodeProcessor::EMoveType::Travel:       { return _u8L("Travel"); }
            case GCodeProcessor::EMoveType::Extrude:      { return _u8L("Extrusion"); }
            default:                                      { return _u8L("Unknown"); }
            }
        };

        auto append_time_item = [&imgui] (const std::string& label, float time, float percentage, const ImVec4& color, const ColumnOffsets& offsets) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_ORANGE_LIGHT);
            imgui.text(label);
            ImGui::PopStyleColor();
            ImGui::SameLine(offsets[0]);
            imgui.text(short_time(get_time_dhms(time)));
            ImGui::SameLine(offsets[1]);
            char buf[64];
            ::sprintf(buf, "%.2f%%", 100.0f * percentage);
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            ImRect frame_bb;
            frame_bb.Min = { ImGui::GetCursorScreenPos().x, window->DC.CursorPos.y };
            frame_bb.Max = { frame_bb.Min.x + percentage * (window->WorkRect.Max.x - frame_bb.Min.x), window->DC.CursorPos.y + ImGui::CalcTextSize(buf, nullptr, false).y };
            frame_bb.Min.x -= IM_FLOOR(window->WindowPadding.x * 0.5f - 1.0f);
            frame_bb.Max.x += IM_FLOOR(window->WindowPadding.x * 0.5f);
            window->DrawList->AddRectFilled(frame_bb.Min, frame_bb.Max, ImGui::GetColorU32({ color.x, color.y, color.z, 1.0f }), 0.0f, 0);
            ImGui::TextUnformatted(buf);
        };

        auto append_move_times = [this, &imgui, move_type_label, append_headers, append_time_item](float total_time,
            const std::vector<std::pair<GCodeProcessor::EMoveType, float>>& moves_time,
            const Headers& headers, const ColumnOffsets& offsets) {

            if (moves_time.empty())
                return;

            if (!ImGui::CollapsingHeader(_u8L("Moves Time").c_str()))
                return;

            append_headers(headers, offsets);

            std::vector<std::pair<GCodeProcessor::EMoveType, float>> sorted_moves_time(moves_time);
            std::sort(sorted_moves_time.begin(), sorted_moves_time.end(), [](const auto& p1, const auto& p2) { return p2.second < p1.second; });

            for (const auto& [type, time] : sorted_moves_time) {
                append_time_item(move_type_label(type), time, time / total_time, ImGuiWrapper::COL_ORANGE_LIGHT, offsets);
            }
        };

        auto append_role_times = [this, &imgui, append_headers, append_time_item](float total_time,
            const std::vector<std::pair<ExtrusionRole, float>>& roles_time,
            const Headers& headers, const ColumnOffsets& offsets) {

            if (roles_time.empty())
                return;

            if (!ImGui::CollapsingHeader(_u8L("Features Time").c_str()))
                return;

            append_headers(headers, offsets);

            std::vector<std::pair<ExtrusionRole, float>> sorted_roles_time(roles_time);
            std::sort(sorted_roles_time.begin(), sorted_roles_time.end(), [](const auto& p1, const auto& p2) { return p2.second < p1.second; });

            for (const auto& [role, time] : sorted_roles_time) {
                Color color = Extrusion_Role_Colors[static_cast<unsigned int>(role)];
                append_time_item(_u8L(ExtrusionEntity::role_to_string(role)), time, time / total_time, { 0.666f * color[0], 0.666f * color[1], 0.666f * color[2], 1.0f}, offsets);
            }
        };

        auto calc_common_offsets = [move_type_label](
            const std::vector<std::pair<GCodeProcessor::EMoveType, float>>& moves_time, const Headers& moves_headers,
            const std::vector<std::pair<ExtrusionRole, float>>& roles_time, const Headers& roles_headers) {
                ColumnOffsets ret = { std::max(ImGui::CalcTextSize(moves_headers[0].c_str()).x, ImGui::CalcTextSize(roles_headers[0].c_str()).x),
                    std::max(ImGui::CalcTextSize(moves_headers[1].c_str()).x, ImGui::CalcTextSize(roles_headers[1].c_str()).x) };

                for (const auto& [type, time] : moves_time) {
                    ret[0] = std::max(ret[0], ImGui::CalcTextSize(move_type_label(type).c_str()).x);
                    ret[1] = std::max(ret[1], ImGui::CalcTextSize(short_time(get_time_dhms(time)).c_str()).x);
                }

                for (const auto& [role, time] : roles_time) {
                    ret[0] = std::max(ret[0], ImGui::CalcTextSize(_u8L(ExtrusionEntity::role_to_string(role)).c_str()).x);
                    ret[1] = std::max(ret[1], ImGui::CalcTextSize(short_time(get_time_dhms(time)).c_str()).x);
                }

                const ImGuiStyle& style = ImGui::GetStyle();
                ret[0] += 2.0f * style.ItemSpacing.x;
                ret[1] += ret[0] + style.ItemSpacing.x;
                return ret;
        };

        ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_ORANGE_LIGHT);
        imgui.text(_u8L("Time") + ":");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        imgui.text(short_time(get_time_dhms(total_time)));
        append_partial_times(items, partial_times_headers);
        ColumnOffsets common_offsets = calc_common_offsets(moves_time, moves_headers, roles_time, roles_headers);
        append_move_times(total_time, moves_time, moves_headers, common_offsets);
        append_role_times(total_time, roles_time, roles_headers, common_offsets);
    };

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
            case CustomGCode::PausePrint:
            {
                auto it = std::find_if(custom_gcode_per_print_z.begin(), custom_gcode_per_print_z.end(), [time_rec](const CustomGCode::Item& item) { return item.type == time_rec.first; });
                if (it != custom_gcode_per_print_z.end()) {
                    items.push_back({ PartialTime::EType::Print, it->extruder, Color(), Color(), time_rec.second });
                    items.push_back({ PartialTime::EType::Pause, it->extruder, Color(), Color(), time_rec.second });
                    custom_gcode_per_print_z.erase(it);
                }
                break;
            }
            case CustomGCode::ColorChange:
            {
                auto it = std::find_if(custom_gcode_per_print_z.begin(), custom_gcode_per_print_z.end(), [time_rec](const CustomGCode::Item& item) { return item.type == time_rec.first; });
                if (it != custom_gcode_per_print_z.end()) {
                    items.push_back({ PartialTime::EType::Print, it->extruder, Color(), Color(), time_rec.second });
                    items.push_back({ PartialTime::EType::ColorChange, it->extruder, last_color[it->extruder - 1], decode_color(it->color), time_rec.second });
                    last_color[it->extruder - 1] = decode_color(it->color);
                    last_extruder_id = it->extruder;
                    custom_gcode_per_print_z.erase(it);
                }
                else
                    items.push_back({ PartialTime::EType::Print, last_extruder_id, Color(), Color(), time_rec.second });

                break;
            }
            default: { break; }
            }
        }

        return items;
    };

    const Headers partial_times_headers = {
        _u8L("Event"),
        _u8L("Remaining"),
        _u8L("Duration")
    };
    const Headers moves_headers = {
        _u8L("Type"),
        _u8L("Time"),
        _u8L("Percentage")
    };
    const Headers roles_headers = {
        _u8L("Feature"),
        _u8L("Time"),
        _u8L("Percentage")
    };

    Size cnv_size = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size();
    imgui.set_next_window_pos(static_cast<float>(cnv_size.get_width()), static_cast<float>(cnv_size.get_height()), ImGuiCond_Always, 1.0f, 1.0f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(-1.0f, 0.5f * static_cast<float>(cnv_size.get_height())));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::SetNextWindowBgAlpha(0.6f);
    imgui.begin(std::string("Time_estimate"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);

    // title
    imgui.title(_u8L("Estimated printing time"));

    // mode tabs
    ImGui::BeginTabBar("mode_tabs");
    if (ps.estimated_normal_print_time > 0.0f) {
        if (ImGui::BeginTabItem(_u8L("Normal").c_str())) {
            append_mode(ps.estimated_normal_print_time, 
                generate_partial_times(ps.estimated_normal_custom_gcode_print_times), partial_times_headers,
                ps.estimated_normal_moves_times, moves_headers, 
                ps.estimated_normal_roles_times, roles_headers);
            ImGui::EndTabItem();
        }
    }
    if (ps.estimated_silent_print_time > 0.0f) {
        if (ImGui::BeginTabItem(_u8L("Stealth").c_str())) {
            append_mode(ps.estimated_silent_print_time,
                generate_partial_times(ps.estimated_silent_custom_gcode_print_times), partial_times_headers,
                ps.estimated_silent_moves_times, moves_headers,
                ps.estimated_silent_roles_times, roles_headers);
            ImGui::EndTabItem();
        }
    }
    ImGui::EndTabBar();

    imgui.end();
    ImGui::PopStyleVar();
}

#if ENABLE_GCODE_VIEWER_STATISTICS
void GCodeViewer::render_statistics() const
{
    static const float offset = 230.0f;

    ImGuiWrapper& imgui = *wxGetApp().imgui();

    imgui.set_next_window_pos(0.5f * wxGetApp().plater()->get_current_canvas3D()->get_canvas_size().get_width(), 0.0f, ImGuiCond_Once, 0.5f, 0.0f);
    imgui.begin(std::string("GCodeViewer Statistics"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_ORANGE_LIGHT);
    imgui.text(std::string("GCodeProcessor time:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.results_time) + " ms");

    ImGui::Separator();

    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_ORANGE_LIGHT);
    imgui.text(std::string("Load time:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.load_time) + " ms");

    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_ORANGE_LIGHT);
    imgui.text(std::string("Resfresh time:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.refresh_time) + " ms");

    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_ORANGE_LIGHT);
    imgui.text(std::string("Resfresh paths time:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.refresh_paths_time) + " ms");

    ImGui::Separator();

    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_ORANGE_LIGHT);
    imgui.text(std::string("Multi GL_POINTS calls:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.gl_multi_points_calls_count));

    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_ORANGE_LIGHT);
    imgui.text(std::string("Multi GL_LINE_STRIP calls:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.gl_multi_line_strip_calls_count));

    ImGui::Separator();

    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_ORANGE_LIGHT);
    imgui.text(std::string("GCodeProcessor results:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.results_size) + " bytes");

    ImGui::Separator();

    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_ORANGE_LIGHT);
    imgui.text(std::string("Paths CPU:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.paths_size) + " bytes");

    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_ORANGE_LIGHT);
    imgui.text(std::string("Render paths CPU:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.render_paths_size) + " bytes");

    ImGui::Separator();

    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_ORANGE_LIGHT);
    imgui.text(std::string("Vertices GPU:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.vertices_gpu_size) + " bytes");

    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_ORANGE_LIGHT);
    imgui.text(std::string("Indices GPU:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.indices_gpu_size) + " bytes");

    ImGui::Separator();

    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_ORANGE_LIGHT);
    imgui.text(std::string("Travel segments:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.travel_segments_count));

    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_ORANGE_LIGHT);
    imgui.text(std::string("Extrude segments:"));
    ImGui::PopStyleColor();
    ImGui::SameLine(offset);
    imgui.text(std::to_string(m_statistics.extrude_segments_count));

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

    ImGuiWrapper& imgui = *wxGetApp().imgui();

    Size cnv_size = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size();
    imgui.set_next_window_pos(static_cast<float>(cnv_size.get_width()), 0.5f * static_cast<float>(cnv_size.get_height()), ImGuiCond_Once, 1.0f, 0.5f);

    imgui.begin(std::string("Shaders editor (DEV only)"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);

    if (ImGui::CollapsingHeader("Points", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::TreeNode("GLSL version")) {
            ImGui::RadioButton("1.10 (low end PCs)", &m_shaders_editor.points.shader_version, 0);
            ImGui::RadioButton("1.20 flat (billboards) [default]", &m_shaders_editor.points.shader_version, 1);
            ImGui::RadioButton("1.20 solid (spheres)", &m_shaders_editor.points.shader_version, 2);
            ImGui::TreePop();
        }

        switch (m_shaders_editor.points.shader_version)
        {
        case 0: { set_shader("options_110"); break; }
        case 1: { set_shader("options_120_flat"); break; }
        case 2: { set_shader("options_120_solid"); break; }
        }

        if (ImGui::TreeNode("Options")) {
            ImGui::SliderFloat("point size", &m_shaders_editor.points.point_size, 0.5f, 3.0f, "%.2f");
            if (m_shaders_editor.points.shader_version == 1) {
                ImGui::SliderInt("% outline", &m_shaders_editor.points.percent_outline, 0, 50);
                ImGui::SliderInt("% center", &m_shaders_editor.points.percent_center, 0, 50);
            }
            ImGui::TreePop();
        }
    }

    if (ImGui::CollapsingHeader("Lines", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::TreeNode("Lights")) {
            ImGui::SliderFloat("ambient", &m_shaders_editor.lines.lights.ambient, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("top diffuse", &m_shaders_editor.lines.lights.top_diffuse, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("front diffuse", &m_shaders_editor.lines.lights.front_diffuse, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("global", &m_shaders_editor.lines.lights.global, 0.0f, 1.0f, "%.2f");
            ImGui::TreePop();
        }
    }

    ImGui::SetWindowSize(ImVec2(std::max(ImGui::GetWindowWidth(), 600.0f), -1.0f), ImGuiCond_Always);
    if (ImGui::GetWindowPos().x + ImGui::GetWindowWidth() > static_cast<float>(cnv_size.get_width())) {
        ImGui::SetWindowPos(ImVec2(static_cast<float>(cnv_size.get_width()) - ImGui::GetWindowWidth(), ImGui::GetWindowPos().y), ImGuiCond_Always);
        wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
        wxGetApp().plater()->get_current_canvas3D()->request_extra_frame();
    }

    imgui.end();
}
#endif // ENABLE_GCODE_VIEWER_SHADERS_EDITOR

bool GCodeViewer::is_travel_in_z_range(size_t id) const
{
    const TBuffer& buffer = m_buffers[buffer_id(GCodeProcessor::EMoveType::Travel)];
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
