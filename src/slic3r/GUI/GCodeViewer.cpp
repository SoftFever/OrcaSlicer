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

void GCodeViewer::Buffer::reset()
{
    // release gpu memory
    if (vbo_id > 0)
        glsafe(::glDeleteBuffers(1, &vbo_id));

    // release cpu memory
    data = std::vector<float>();
    data_size = 0;
}

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
    for (Buffer& buffer : m_buffers)
    {
        buffer.reset();
    }

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

void GCodeViewer::set_toolpath_visible(GCodeProcessor::EMoveType type, bool visible)
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
        Shader& shader = m_buffers[i].shader;
        std::string vertex_shader_src;
        std::string fragment_shader_src;
        GCodeProcessor::EMoveType type = buffer_type(i);
        switch (type)
        {
        case GCodeProcessor::EMoveType::Tool_change:
        {
            vertex_shader_src = "toolchanges.vs";
            fragment_shader_src = "toolchanges.fs";
            break;
        }
        case GCodeProcessor::EMoveType::Retract:
        {
            vertex_shader_src = "retractions.vs";
            fragment_shader_src = "retractions.fs";
            break;
        }
        case GCodeProcessor::EMoveType::Unretract:
        {
            vertex_shader_src = "unretractions.vs";
            fragment_shader_src = "unretractions.fs";
            break;
        }
        case GCodeProcessor::EMoveType::Extrude:
        {
            vertex_shader_src = "extrusions.vs";
            fragment_shader_src = "extrusions.fs";
            break;
        }
        case GCodeProcessor::EMoveType::Travel:
        {
            vertex_shader_src = "travels.vs";
            fragment_shader_src = "travels.fs";
            break;
        }
        default:
        {
            break;
        }
        }

        if (!shader.init(vertex_shader_src, fragment_shader_src))
        {
            BOOST_LOG_TRIVIAL(error) << "Unable to initialize toolpaths shader: please, check that the files " << vertex_shader_src << " and " << fragment_shader_src << " are available";
            return false;
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

    // convert data
    size_t vertices_count = gcode_result.moves.size();
    for (size_t i = 0; i < vertices_count; ++i)
    {
        // skip first vertex
        if (i == 0)
            continue;

        const GCodeProcessor::MoveVertex& prev = gcode_result.moves[i - 1];
        const GCodeProcessor::MoveVertex& curr = gcode_result.moves[i];

        Buffer& buffer = m_buffers[buffer_id(curr.type)];

        switch (curr.type)
        {
        case GCodeProcessor::EMoveType::Tool_change:
        case GCodeProcessor::EMoveType::Retract:
        case GCodeProcessor::EMoveType::Unretract:
        {
            for (int j = 0; j < 3; ++j)
            {
                buffer.data.insert(buffer.data.end(), curr.position[j]);
            }
            break;
        }
        case GCodeProcessor::EMoveType::Extrude:
        case GCodeProcessor::EMoveType::Travel:
        {
            for (int j = 0; j < 3; ++j)
            {
                buffer.data.insert(buffer.data.end(), prev.position[j]);
            }
            for (int j = 0; j < 3; ++j)
            {
                buffer.data.insert(buffer.data.end(), curr.position[j]);
            }
            break;
        }
        default:
        {
            continue;
        }
        }

        if (curr.type == GCodeProcessor::EMoveType::Extrude)
            m_layers_zs.emplace_back(curr.position[2]);
    }

    std::sort(m_layers_zs.begin(), m_layers_zs.end());

    // Replace intervals of layers with similar top positions with their average value.
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

    // send data to gpu
    for (Buffer& buffer : m_buffers)
    {
        buffer.data_size = buffer.data.size();
        if (buffer.data_size > 0)
        {
            glsafe(::glGenBuffers(1, &buffer.vbo_id));
            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, buffer.vbo_id));
            glsafe(::glBufferData(GL_ARRAY_BUFFER, buffer.data_size * sizeof(float), buffer.data.data(), GL_STATIC_DRAW));
            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
            buffer.data = std::vector<float>();
        }
    }

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
    auto set_color = [](GLint current_program_id, const std::array<float, 4>& color) {
        if (current_program_id > 0)
        {
            GLint color_id = (current_program_id > 0) ? ::glGetUniformLocation(current_program_id, "uniform_color") : -1;
            if (color_id >= 0)
            {
                glsafe(::glUniform4fv(color_id, 1, (const GLfloat*)color.data()));
                return;
            }
        }
        BOOST_LOG_TRIVIAL(error) << "Unable to find uniform_color uniform";
    };

    glsafe(::glCullFace(GL_BACK));

    unsigned char begin_id = buffer_id(GCodeProcessor::EMoveType::Retract);
    unsigned char end_id = buffer_id(GCodeProcessor::EMoveType::Count);

    for (unsigned char i = begin_id; i < end_id; ++i)
    {
        const Buffer& buffer = m_buffers[i];
        if (buffer.vbo_id == 0)
            continue;

        if (!buffer.visible)
            continue;

        if (buffer.shader.is_initialized())
        {
            buffer.shader.start_using();

            GLint current_program_id;
            glsafe(::glGetIntegerv(GL_CURRENT_PROGRAM, &current_program_id));

            GCodeProcessor::EMoveType type = buffer_type(i);

            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, buffer.vbo_id));
            glsafe(::glVertexPointer(3, GL_FLOAT, Buffer::vertex_size_bytes(), (const void*)0));
            glsafe(::glEnableClientState(GL_VERTEX_ARRAY));

            switch (type)
            {
            case GCodeProcessor::EMoveType::Tool_change:
            {
                std::array<float, 4> color = { 1.0f, 1.0f, 1.0f, 1.0f };
                set_color(current_program_id, color);
                glsafe(::glEnable(GL_PROGRAM_POINT_SIZE));
                glsafe(::glDrawArrays(GL_POINTS, 0, (GLsizei)(buffer.data_size / Buffer::vertex_size())));
                glsafe(::glDisable(GL_PROGRAM_POINT_SIZE));
                break;
            }
            case GCodeProcessor::EMoveType::Retract:
            {
                std::array<float, 4> color = { 1.0f, 0.0f, 1.0f, 1.0f };
                set_color(current_program_id, color);
                glsafe(::glEnable(GL_PROGRAM_POINT_SIZE));
                glsafe(::glDrawArrays(GL_POINTS, 0, (GLsizei)(buffer.data_size / Buffer::vertex_size())));
                glsafe(::glDisable(GL_PROGRAM_POINT_SIZE));
                break;
            }
            case GCodeProcessor::EMoveType::Unretract:
            {
                std::array<float, 4> color = { 0.0f, 1.0f, 0.0f, 1.0f };
                set_color(current_program_id, color);
                glsafe(::glEnable(GL_PROGRAM_POINT_SIZE));
                glsafe(::glDrawArrays(GL_POINTS, 0, (GLsizei)(buffer.data_size / Buffer::vertex_size())));
                glsafe(::glDisable(GL_PROGRAM_POINT_SIZE));
                break;
            }
            case GCodeProcessor::EMoveType::Extrude:
            {
                std::array<float, 4> color = { 1.0f, 0.0f, 0.0f, 1.0f };
                set_color(current_program_id, color);
                glsafe(::glDrawArrays(GL_LINES, 0, (GLsizei)(buffer.data_size / Buffer::vertex_size())));
                break;
            }
            case GCodeProcessor::EMoveType::Travel:
            {
                std::array<float, 4> color = { 1.0f, 1.0f, 0.0f, 1.0f };
                set_color(current_program_id, color);
                glsafe(::glDrawArrays(GL_LINES, 0, (GLsizei)(buffer.data_size / Buffer::vertex_size())));
                break;
            }
            default:
            {
                break;
            }
            }

            glsafe(::glDisableClientState(GL_VERTEX_ARRAY));

            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
            buffer.shader.stop_using();
        }
    }
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
