#include "libslic3r/libslic3r.h"
#include "GCodeViewer.hpp"

#if ENABLE_GCODE_VIEWER
#include "libslic3r/Print.hpp"
#include "GUI_App.hpp"
#include "PresetBundle.hpp"
#include "Camera.hpp"
#include "I18N.hpp"
#include "libslic3r/I18N.hpp"

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

void GCodeViewer::VBuffer::reset()
{
    // release gpu memory
    if (vbo_id > 0)
        glsafe(::glDeleteBuffers(1, &vbo_id));

    vertices_count = 0;
}

void GCodeViewer::IBuffer::reset()
{
    // release gpu memory
    if (ibo_id > 0)
        glsafe(::glDeleteBuffers(1, &ibo_id));

    // release cpu memory
    data = std::vector<unsigned int>();
    data_size = 0;
    paths = std::vector<Path>();
}

bool GCodeViewer::IBuffer::init_shader(const std::string& vertex_shader_src, const std::string& fragment_shader_src)
{
    if (!shader.init(vertex_shader_src, fragment_shader_src))
    {
        BOOST_LOG_TRIVIAL(error) << "Unable to initialize toolpaths shader: please, check that the files " << vertex_shader_src << " and " << fragment_shader_src << " are available";
        return false;
    }

    return true;
}

void GCodeViewer::IBuffer::add_path(const GCodeProcessor::MoveVertex& move)
{
    unsigned int id = static_cast<unsigned int>(data.size());
    paths.push_back({ move.type, move.extrusion_role, id, id, move.height, move.width, move.feedrate, move.fan_speed });
}

std::array<float, 3> GCodeViewer::Extrusions::Range::get_color_at(float value, const std::array<std::array<float, 3>, Default_Range_Colors_Count>& colors) const
{
    // Input value scaled to the colors range
    const float step = step_size();
    const float global_t = (step != 0.0f) ? std::max(0.0f, value - min) / step : 0.0f; // lower limit of 0.0f

    const size_t color_max_idx = colors.size() - 1;

    // Compute the two colors just below (low) and above (high) the input value
    const size_t color_low_idx = std::clamp<size_t>(static_cast<size_t>(global_t), 0, color_max_idx);
    const size_t color_high_idx = std::clamp<size_t>(color_low_idx + 1, 0, color_max_idx);

    // Compute how far the value is between the low and high colors so that they can be interpolated
    const float local_t = std::clamp(global_t - static_cast<float>(color_low_idx), 0.0f, 1.0f);

    // Interpolate between the low and high colors to find exactly which color the input value should get
    std::array<float, 3> ret;
    for (unsigned int i = 0; i < 3; ++i)
    {
        ret[i] = lerp(colors[color_low_idx][i], colors[color_high_idx][i], local_t);
    }
    return ret;
}

const std::array<std::array<float, 3>, erCount> GCodeViewer::Default_Extrusion_Role_Colors {{
    { 1.00f, 1.00f, 1.00f },   // erNone
    { 1.00f, 1.00f, 0.40f },   // erPerimeter
    { 1.00f, 0.65f, 0.00f },   // erExternalPerimeter
    { 0.00f, 0.00f, 1.00f },   // erOverhangPerimeter
    { 0.69f, 0.19f, 0.16f },   // erInternalInfill
    { 0.84f, 0.20f, 0.84f },   // erSolidInfill
    { 1.00f, 0.10f, 0.10f },   // erTopSolidInfill
    { 0.60f, 0.60f, 1.00f },   // erBridgeInfill
    { 1.00f, 1.00f, 1.00f },   // erGapFill
    { 0.52f, 0.48f, 0.13f },   // erSkirt
    { 0.00f, 1.00f, 0.00f },   // erSupportMaterial
    { 0.00f, 0.50f, 0.00f },   // erSupportMaterialInterface
    { 0.70f, 0.89f, 0.67f },   // erWipeTower
    { 0.16f, 0.80f, 0.58f },   // erCustom
    { 0.00f, 0.00f, 0.00f }    // erMixed
}};

const std::array<std::array<float, 3>, GCodeViewer::Default_Range_Colors_Count> GCodeViewer::Default_Range_Colors {{
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

void GCodeViewer::load(const GCodeProcessor::Result& gcode_result, const Print& print, bool initialized)
{
    if (m_last_result_id == gcode_result.id)
        return;

    m_last_result_id = gcode_result.id;

    // release gpu memory, if used
    reset();

    load_toolpaths(gcode_result);
    load_shells(print, initialized);
}

void GCodeViewer::refresh_toolpaths_ranges(const GCodeProcessor::Result& gcode_result)
{
    if (m_vertices.vertices_count == 0)
        return;

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
        case GCodeProcessor::EMoveType::Travel:
        {
            if (m_buffers[buffer_id(curr.type)].visible)
            {
                m_extrusions.ranges.height.update_from(curr.height);
                m_extrusions.ranges.width.update_from(curr.width);
                m_extrusions.ranges.feedrate.update_from(curr.feedrate);
                m_extrusions.ranges.fan_speed.update_from(curr.fan_speed);
            }
            break;
        }
        default:
        {
            break;
        }
        }
    }
}

void GCodeViewer::reset()
{
    m_vertices.reset();

    for (IBuffer& buffer : m_buffers)
    {
        buffer.reset();
    }

    m_bounding_box = BoundingBoxf3();
    m_extrusions.reset_role_visibility_flags();
    m_extrusions.reset_ranges();
    m_shells.volumes.clear();
    m_layers_zs = std::vector<double>();
    m_roles = std::vector<ExtrusionRole>();
}

void GCodeViewer::render() const
{
    glsafe(::glEnable(GL_DEPTH_TEST));
    render_toolpaths();
    render_shells();
    render_overlay();
}

bool GCodeViewer::is_toolpath_visible(GCodeProcessor::EMoveType type) const
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

bool GCodeViewer::init_shaders()
{
    unsigned char begin_id = buffer_id(GCodeProcessor::EMoveType::Retract);
    unsigned char end_id = buffer_id(GCodeProcessor::EMoveType::Count);

    for (unsigned char i = begin_id; i < end_id; ++i)
    {
        switch (buffer_type(i))
        {
        case GCodeProcessor::EMoveType::Tool_change:
        {
            if (!m_buffers[i].init_shader("toolchanges.vs", "toolchanges.fs"))
                return false;

            break;
        }
        case GCodeProcessor::EMoveType::Retract:
        {
            if (!m_buffers[i].init_shader("retractions.vs", "retractions.fs"))
                return false;

            break;
        }
        case GCodeProcessor::EMoveType::Unretract:
        {
            if (!m_buffers[i].init_shader("unretractions.vs", "unretractions.fs"))
                return false;

            break;
        }
        case GCodeProcessor::EMoveType::Extrude:
        {
            if (!m_buffers[i].init_shader("extrusions.vs", "extrusions.fs"))
                return false;

            break;
        }
        case GCodeProcessor::EMoveType::Travel:
        {
            if (!m_buffers[i].init_shader("travels.vs", "travels.fs"))
                return false;

            break;
        }
        default:
        {
            break;
        }
        }
    }

    if (!m_shells.shader.init("shells.vs", "shells.fs"))
    {
        BOOST_LOG_TRIVIAL(error) << "Unable to initialize shells shader: please, check that the files shells.vs and shells.fs are available";
        return false;
    }

    return true;
}

void GCodeViewer::load_toolpaths(const GCodeProcessor::Result& gcode_result)
{
    auto start_time = std::chrono::high_resolution_clock::now();

    // vertex data
    m_vertices.vertices_count = gcode_result.moves.size();
    if (m_vertices.vertices_count == 0)
        return;

    // vertex data / bounding box -> extract from result
    std::vector<float> vertices_data;
    for (const GCodeProcessor::MoveVertex& move : gcode_result.moves)
    {
        for (int j = 0; j < 3; ++j)
        {
            vertices_data.insert(vertices_data.end(), move.position[j]);
            m_bounding_box.merge(move.position.cast<double>());
        }
    }

    // vertex data -> send to gpu
    glsafe(::glGenBuffers(1, &m_vertices.vbo_id));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, m_vertices.vbo_id));
    glsafe(::glBufferData(GL_ARRAY_BUFFER, vertices_data.size() * sizeof(float), vertices_data.data(), GL_STATIC_DRAW));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));

    // vertex data -> free ram
    vertices_data = std::vector<float>();

    // indices data -> extract from result
    for (size_t i = 0; i < m_vertices.vertices_count; ++i)
    {
        // skip first vertex
        if (i == 0)
            continue;

        const GCodeProcessor::MoveVertex& prev = gcode_result.moves[i - 1];
        const GCodeProcessor::MoveVertex& curr = gcode_result.moves[i];

        IBuffer& buffer = m_buffers[buffer_id(curr.type)];

        switch (curr.type)
        {
        case GCodeProcessor::EMoveType::Tool_change:
        case GCodeProcessor::EMoveType::Retract:
        case GCodeProcessor::EMoveType::Unretract:
        {
            buffer.add_path(curr);
            buffer.data.push_back(static_cast<unsigned int>(i));
            break;
        }
        case GCodeProcessor::EMoveType::Extrude:
        case GCodeProcessor::EMoveType::Travel:
        {
            if (prev.type != curr.type || !buffer.paths.back().matches(curr))
            {
                buffer.add_path(curr);
                buffer.data.push_back(static_cast<unsigned int>(i - 1));
            }
            
            buffer.paths.back().last = static_cast<unsigned int>(buffer.data.size());
            buffer.data.push_back(static_cast<unsigned int>(i));
            break;
        }
        default:
        {
            continue;
        }
        }
    }

    // indices data -> send data to gpu
    for (IBuffer& buffer : m_buffers)
    {
        buffer.data_size = buffer.data.size();
        if (buffer.data_size > 0)
        {
            glsafe(::glGenBuffers(1, &buffer.ibo_id));
            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.ibo_id));
            glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, buffer.data_size * sizeof(unsigned int), buffer.data.data(), GL_STATIC_DRAW));
            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

            // indices data -> free ram
            buffer.data = std::vector<unsigned int>();
        }
    }

    // layers zs / roles -> extract from result
    for (const GCodeProcessor::MoveVertex& move : gcode_result.moves)
    {
        if (move.type == GCodeProcessor::EMoveType::Extrude)
            m_layers_zs.emplace_back(move.position[2]);

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

    // roles -> remove duplicates
    std::sort(m_roles.begin(), m_roles.end());
    m_roles.erase(std::unique(m_roles.begin(), m_roles.end()), m_roles.end());

    auto end_time = std::chrono::high_resolution_clock::now();
    std::cout << "toolpaths generation time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() << "ms \n";
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
        for (int i = 0; i < (int)model_obj->instances.size(); ++i)
        {
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

void GCodeViewer::render_toolpaths() const
{
    auto extrusion_color = [this](const Path& path) {
        std::array<float, 3> color;
        switch (m_view_type)
        {
        case EViewType::FeatureType:    { color = m_extrusions.role_colors[static_cast<unsigned int>(path.role)]; break; }
        case EViewType::Height:         { color = m_extrusions.ranges.height.get_color_at(path.height, m_extrusions.ranges.colors); break; }
        case EViewType::Width:          { color = m_extrusions.ranges.width.get_color_at(path.width, m_extrusions.ranges.colors); break; }
        case EViewType::Feedrate:       { color = m_extrusions.ranges.feedrate.get_color_at(path.feedrate, m_extrusions.ranges.colors); break; }
        case EViewType::FanSpeed:       { color = m_extrusions.ranges.fan_speed.get_color_at(path.fan_speed, m_extrusions.ranges.colors); break; }
        case EViewType::VolumetricRate:
        case EViewType::Tool:
        case EViewType::ColorPrint:
        default:                        { color = { 1.0f, 1.0f, 1.0f }; break; }
        }
        return color;
    };

    auto set_color = [](GLint current_program_id, const std::array<float, 3>& color) {
        if (current_program_id > 0) {
            GLint color_id = (current_program_id > 0) ? ::glGetUniformLocation(current_program_id, "uniform_color") : -1;
            if (color_id >= 0) {
                glsafe(::glUniform3fv(color_id, 1, (const GLfloat*)color.data()));
                return;
            }
        }
        BOOST_LOG_TRIVIAL(error) << "Unable to find uniform_color uniform";
    };

    auto is_path_visible = [](unsigned int flags, const Path& path) {
        return Extrusions::is_role_visible(flags, path.role);
    };

    glsafe(::glCullFace(GL_BACK));

    unsigned char begin_id = buffer_id(GCodeProcessor::EMoveType::Retract);
    unsigned char end_id = buffer_id(GCodeProcessor::EMoveType::Count);

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, m_vertices.vbo_id));
    glsafe(::glVertexPointer(3, GL_FLOAT, VBuffer::vertex_size_bytes(), (const void*)0));
    glsafe(::glEnableClientState(GL_VERTEX_ARRAY));

    for (unsigned char i = begin_id; i < end_id; ++i)
    {
        const IBuffer& buffer = m_buffers[i];
        if (buffer.ibo_id == 0)
            continue;
        
        if (!buffer.visible)
            continue;

        if (buffer.shader.is_initialized())
        {
            GCodeProcessor::EMoveType type = buffer_type(i);

            buffer.shader.start_using();
            
            GLint current_program_id;
            glsafe(::glGetIntegerv(GL_CURRENT_PROGRAM, &current_program_id));

            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.ibo_id));

            switch (type)
            {
            case GCodeProcessor::EMoveType::Tool_change:
            {
                std::array<float, 3> color = { 1.0f, 1.0f, 1.0f };
                set_color(current_program_id, color);
                glsafe(::glEnable(GL_PROGRAM_POINT_SIZE));
                glsafe(::glDrawElements(GL_POINTS, (GLsizei)buffer.data_size, GL_UNSIGNED_INT, nullptr));
                glsafe(::glDisable(GL_PROGRAM_POINT_SIZE));
                break;
            }
            case GCodeProcessor::EMoveType::Retract:
            {
                std::array<float, 3> color = { 1.0f, 0.0f, 1.0f };
                set_color(current_program_id, color);
                glsafe(::glEnable(GL_PROGRAM_POINT_SIZE));
                glsafe(::glDrawElements(GL_POINTS, (GLsizei)buffer.data_size, GL_UNSIGNED_INT, nullptr));
                glsafe(::glDisable(GL_PROGRAM_POINT_SIZE));
                break;
            }
            case GCodeProcessor::EMoveType::Unretract:
            {
                std::array<float, 3> color = { 0.0f, 1.0f, 0.0f };
                set_color(current_program_id, color);
                glsafe(::glEnable(GL_PROGRAM_POINT_SIZE));
                glsafe(::glDrawElements(GL_POINTS, (GLsizei)buffer.data_size, GL_UNSIGNED_INT, nullptr));
                glsafe(::glDisable(GL_PROGRAM_POINT_SIZE));
                break;
            }
            case GCodeProcessor::EMoveType::Extrude:
            {
                for (const Path& path : buffer.paths)
                {
                    if (!is_path_visible(m_extrusions.role_visibility_flags, path))
                        continue;

                    set_color(current_program_id, extrusion_color(path));
                    glsafe(::glDrawElements(GL_LINE_STRIP, GLsizei(path.last - path.first + 1), GL_UNSIGNED_INT, (const void*)(path.first * sizeof(GLuint))));
                }
                break;
            }
            case GCodeProcessor::EMoveType::Travel:
            {
                std::array<float, 3> color = { 1.0f, 1.0f, 0.0f };
                set_color(current_program_id, color);
                for (const Path& path : buffer.paths)
                {
                    glsafe(::glDrawElements(GL_LINE_STRIP, GLsizei(path.last - path.first + 1), GL_UNSIGNED_INT, (const void*)(path.first * sizeof(GLuint))));
                }
                break;
            }
            }

            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
            buffer.shader.stop_using();
        }
    }

    glsafe(::glDisableClientState(GL_VERTEX_ARRAY));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
}

void GCodeViewer::render_shells() const
{
    if (!m_shells.visible || m_shells.volumes.empty() || !m_shells.shader.is_initialized())
        return;

//    glsafe(::glDepthMask(GL_FALSE));

    m_shells.shader.start_using();
    m_shells.volumes.render(GLVolumeCollection::Transparent, true, wxGetApp().plater()->get_camera().get_view_matrix());
    m_shells.shader.stop_using();

//    glsafe(::glDepthMask(GL_TRUE));
}

void GCodeViewer::render_overlay() const
{
    static const ImVec4 ORANGE(1.0f, 0.49f, 0.22f, 1.0f);
    static const float ICON_BORDER_SIZE = 25.0f;
    static const ImU32 ICON_BORDER_COLOR = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    static const float GAP_ICON_TEXT = 5.0f;

    if (!m_legend_enabled || m_roles.empty())
        return;

    ImGuiWrapper& imgui = *wxGetApp().imgui();

    imgui.set_next_window_pos(0, 0, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    imgui.begin(_L("Legend"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    auto add_range = [this, draw_list, &imgui](const Extrusions::Range& range, unsigned int decimals) {
        auto add_item = [this, draw_list, &imgui](int i, float value, unsigned int decimals) {
            ImVec2 pos(ImGui::GetCursorPosX() + 2.0f, ImGui::GetCursorPosY() + 2.0f);
            draw_list->AddRect(ImVec2(pos.x, pos.y), ImVec2(pos.x + ICON_BORDER_SIZE, pos.y + ICON_BORDER_SIZE), ICON_BORDER_COLOR, 0.0f, 0);
            const std::array<float, 3>& color = m_extrusions.ranges.colors[i];
            ImU32 fill_color = ImGui::GetColorU32(ImVec4(color[0], color[1], color[2], 1.0f));
            draw_list->AddRectFilled(ImVec2(pos.x + 1.0f, pos.y + 1.0f), ImVec2(pos.x + ICON_BORDER_SIZE - 1.0f, pos.y + ICON_BORDER_SIZE - 1.0f), fill_color);
            ImGui::SetCursorPosX(pos.x + ICON_BORDER_SIZE + GAP_ICON_TEXT);
            ImGui::AlignTextToFramePadding();
            char buf[1024];
            ::sprintf(buf, "%.*f", decimals, value);
            imgui.text(buf);
        };

        float step_size = range.step_size();
        if (step_size == 0.0f)
            add_item(0, range.min, decimals);
        else
        {
            for (int i = Default_Range_Colors_Count - 1; i >= 0; --i)
            {
                add_item(i, range.min + static_cast<float>(i) * step_size, decimals);
            }
        }
    };

    ImGui::PushStyleColor(ImGuiCol_Text, ORANGE);
    switch (m_view_type)
    {
    case EViewType::FeatureType:    { imgui.text(Slic3r::I18N::translate(L("Feature type"))); break; }
    case EViewType::Height:         { imgui.text(Slic3r::I18N::translate(L("Height (mm)"))); break; }
    case EViewType::Width:          { imgui.text(Slic3r::I18N::translate(L("Width (mm)"))); break; }
    case EViewType::Feedrate:       { imgui.text(Slic3r::I18N::translate(L("Speed (mm/s)"))); break; }
    case EViewType::FanSpeed:       { imgui.text(Slic3r::I18N::translate(L("Fan Speed (%)"))); break; }
    case EViewType::VolumetricRate: { imgui.text(Slic3r::I18N::translate(L("Volumetric flow rate (mm³/s)"))); break; }
    case EViewType::Tool:           { imgui.text(Slic3r::I18N::translate(L("Tool"))); break; }
    case EViewType::ColorPrint:     { imgui.text(Slic3r::I18N::translate(L("Color Print"))); break; }
    }
    ImGui::PopStyleColor();

    ImGui::Separator();

    switch (m_view_type)
    {
    case EViewType::FeatureType:
    {
        for (ExtrusionRole role : m_roles)
        {
            ImVec2 pos(ImGui::GetCursorPosX() + 2.0f, ImGui::GetCursorPosY() + 2.0f);
            draw_list->AddRect(ImVec2(pos.x, pos.y), ImVec2(pos.x + ICON_BORDER_SIZE, pos.y + ICON_BORDER_SIZE), ICON_BORDER_COLOR, 0.0f, 0);
            const std::array<float, 3>& color = m_extrusions.role_colors[static_cast<unsigned int>(role)];
            ImU32 fill_color = ImGui::GetColorU32(ImVec4(color[0], color[1], color[2], 1.0));
            draw_list->AddRectFilled(ImVec2(pos.x + 1.0f, pos.y + 1.0f), ImVec2(pos.x + ICON_BORDER_SIZE - 1.0f, pos.y + ICON_BORDER_SIZE - 1.0f), fill_color);
            ImGui::SetCursorPosX(pos.x + ICON_BORDER_SIZE + GAP_ICON_TEXT);
            ImGui::AlignTextToFramePadding();
            imgui.text(Slic3r::I18N::translate(ExtrusionEntity::role_to_string(role)));
        }
        break;
    }
    case EViewType::Height:   { add_range(m_extrusions.ranges.height, 3); break; }
    case EViewType::Width:    { add_range(m_extrusions.ranges.width, 3); break; }
    case EViewType::Feedrate: { add_range(m_extrusions.ranges.feedrate, 1); break; }
    case EViewType::FanSpeed: { add_range(m_extrusions.ranges.fan_speed, 0); break; }
    case EViewType::VolumetricRate: { break; }
    case EViewType::Tool: { break; }
    case EViewType::ColorPrint: { break; }
    }

    imgui.end();
    ImGui::PopStyleVar();
}

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_GCODE_VIEWER
