#include "libslic3r/libslic3r.h"
#include "GCodeViewer.hpp"

#if ENABLE_GCODE_VIEWER
#include "libslic3r/Print.hpp"
#include "GUI_App.hpp"
#include "PresetBundle.hpp"
#include "Camera.hpp"

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

void GCodeViewer::IBuffer::add_path(GCodeProcessor::EMoveType type, ExtrusionRole role)
{
    unsigned int id = static_cast<unsigned int>(data.size());
    paths.push_back({ type, role, id, id });
}

const std::array<std::array<float, 4>, erCount> GCodeViewer::Default_Extrusion_Role_Colors {{
    { 0.00f, 0.00f, 0.00f, 1.0f },   // erNone
    { 1.00f, 1.00f, 0.40f, 1.0f },   // erPerimeter
    { 1.00f, 0.65f, 0.00f, 1.0f },   // erExternalPerimeter
    { 0.00f, 0.00f, 1.00f, 1.0f },   // erOverhangPerimeter
    { 0.69f, 0.19f, 0.16f, 1.0f },   // erInternalInfill
    { 0.84f, 0.20f, 0.84f, 1.0f },   // erSolidInfill
    { 1.00f, 0.10f, 0.10f, 1.0f },   // erTopSolidInfill
    { 0.60f, 0.60f, 1.00f, 1.0f },   // erBridgeInfill
    { 1.00f, 1.00f, 1.00f, 1.0f },   // erGapFill
    { 0.52f, 0.48f, 0.13f, 1.0f },   // erSkirt
    { 0.00f, 1.00f, 0.00f, 1.0f },   // erSupportMaterial
    { 0.00f, 0.50f, 0.00f, 1.0f },   // erSupportMaterialInterface
    { 0.70f, 0.89f, 0.67f, 1.0f },   // erWipeTower
    { 0.16f, 0.80f, 0.58f, 1.0f },   // erCustom
    { 0.00f, 0.00f, 0.00f, 1.0f }    // erMixed
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

void GCodeViewer::reset()
{
    m_vertices.reset();

    for (IBuffer& buffer : m_buffers)
    {
        buffer.reset();
    }

    m_extrusions.reset_role_visibility_flags();
    m_shells.volumes.clear();
    m_layers_zs = std::vector<double>();
}

void GCodeViewer::render() const
{
    glsafe(::glEnable(GL_DEPTH_TEST));
    render_toolpaths();
    render_shells();
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

    // vertex data -> extract from result
    std::vector<float> vertices_data;
    for (const GCodeProcessor::MoveVertex& move : gcode_result.moves)
    {
        for (int j = 0; j < 3; ++j)
        {
            vertices_data.insert(vertices_data.end(), move.position[j]);
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
            buffer.add_path(curr.type, curr.extrusion_role);
            buffer.data.push_back(static_cast<unsigned int>(i));
            break;
        }
        case GCodeProcessor::EMoveType::Extrude:
        case GCodeProcessor::EMoveType::Travel:
        {
            if (prev.type != curr.type)
            {
                buffer.add_path(curr.type, curr.extrusion_role);
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

    // layers zs -> extract from result
    for (const GCodeProcessor::MoveVertex& move : gcode_result.moves)
    {
        if (move.type == GCodeProcessor::EMoveType::Extrude)
            m_layers_zs.emplace_back(move.position[2]);
    }

    // layers zs -> sort
    std::sort(m_layers_zs.begin(), m_layers_zs.end());

    // layers zs -> replace intervals of layers with similar top positions with their average value.
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
        std::array<float, 4> color;
        switch (m_view_type)
        {
        case EViewType::FeatureType:
        {
            unsigned int color_id = static_cast<unsigned int>(path.role);
            if (color_id >= erCount)
                color_id = 0;

            color = m_extrusions.role_colors[color_id];
            break;
        }
        case EViewType::Height:
        case EViewType::Width:
        case EViewType::Feedrate:
        case EViewType::FanSpeed:
        case EViewType::VolumetricRate:
        case EViewType::Tool:
        case EViewType::ColorPrint:
        default:
        {
            color = { 1.0f, 1.0f, 1.0f, 1.0f };
            break;
        }
        }
        return color;
    };

    auto set_color = [](GLint current_program_id, const std::array<float, 4>& color) {
        if (current_program_id > 0) {
            GLint color_id = (current_program_id > 0) ? ::glGetUniformLocation(current_program_id, "uniform_color") : -1;
            if (color_id >= 0) {
                glsafe(::glUniform4fv(color_id, 1, (const GLfloat*)color.data()));
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
                std::array<float, 4> color = { 1.0f, 1.0f, 1.0f, 1.0f };
                set_color(current_program_id, color);
                glsafe(::glEnable(GL_PROGRAM_POINT_SIZE));
                glsafe(::glDrawElements(GL_POINTS, (GLsizei)buffer.data_size, GL_UNSIGNED_INT, nullptr));
                glsafe(::glDisable(GL_PROGRAM_POINT_SIZE));
                break;
            }
            case GCodeProcessor::EMoveType::Retract:
            {
                std::array<float, 4> color = { 1.0f, 0.0f, 1.0f, 1.0f };
                set_color(current_program_id, color);
                glsafe(::glEnable(GL_PROGRAM_POINT_SIZE));
                glsafe(::glDrawElements(GL_POINTS, (GLsizei)buffer.data_size, GL_UNSIGNED_INT, nullptr));
                glsafe(::glDisable(GL_PROGRAM_POINT_SIZE));
                break;
            }
            case GCodeProcessor::EMoveType::Unretract:
            {
                std::array<float, 4> color = { 0.0f, 1.0f, 0.0f, 1.0f };
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
                std::array<float, 4> color = { 1.0f, 1.0f, 0.0f, 1.0f };
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

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_GCODE_VIEWER
