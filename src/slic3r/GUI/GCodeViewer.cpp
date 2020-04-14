#include "libslic3r/libslic3r.h"
#include "GCodeViewer.hpp"
#include "3DScene.hpp"

#if ENABLE_GCODE_VIEWER

#include <GL/glew.h>
#include <boost/log/trivial.hpp>

#include <array>

namespace Slic3r {
namespace GUI {

static unsigned char buffer_id(GCodeProcessor::EMoveType type) {
    return static_cast<unsigned char>(type) - static_cast<unsigned char>(GCodeProcessor::EMoveType::Retract);
}

static GCodeProcessor::EMoveType buffer_type(unsigned char id) {
    return static_cast<GCodeProcessor::EMoveType>(static_cast<unsigned char>(GCodeProcessor::EMoveType::Retract) + id);
}

void GCodeViewer::generate(const GCodeProcessor::Result& gcode_result)
{
    if (m_last_result_id == gcode_result.id)
        return;

    m_last_result_id = gcode_result.id;

    // release gpu memory, if used
    reset_buffers();

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
    }

    // send data to gpu
    for (Buffer& buffer : m_buffers)
    {
        glsafe(::glGenBuffers(1, &buffer.vbo_id));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, buffer.vbo_id));
        glsafe(::glBufferData(GL_ARRAY_BUFFER, buffer.data.size() * sizeof(float), buffer.data.data(), GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
    }
}

void GCodeViewer::render() const
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

    unsigned char begin_id = buffer_id(GCodeProcessor::EMoveType::Retract);
    unsigned char end_id = buffer_id(GCodeProcessor::EMoveType::Count);

    glsafe(::glEnable(GL_DEPTH_TEST));

    for (unsigned char i = begin_id; i < end_id; ++i)
    {
        const Buffer& buffer = m_buffers[i];
        if (buffer.vbo_id == 0)
            continue;

        const Shader& shader = m_shaders[i];
        if (shader.is_initialized())
        {
            shader.start_using();

            GLint current_program_id;
            glsafe(::glGetIntegerv(GL_CURRENT_PROGRAM, &current_program_id));

            GCodeProcessor::EMoveType type = buffer_type(i);

            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, buffer.vbo_id));
            glsafe(::glVertexPointer(3, GL_FLOAT, Buffer::stride(type), (const void*)0));
            glsafe(::glEnableClientState(GL_VERTEX_ARRAY));

            switch (type)
            {
            case GCodeProcessor::EMoveType::Tool_change:
            case GCodeProcessor::EMoveType::Retract:
            case GCodeProcessor::EMoveType::Unretract:
            {
                std::array<float, 4> color = { 0.0f, 1.0f, 0.0f, 1.0f };
                set_color(current_program_id, color);
                glsafe(::glDrawArrays(GL_POINTS, 0, (GLsizei)buffer.data.size() / Buffer::record_size(type)));
                break;
            }
            case GCodeProcessor::EMoveType::Extrude:
            {
                std::array<float, 4> color = { 1.0f, 0.0f, 0.0f, 1.0f };
                set_color(current_program_id, color);
                glsafe(::glDrawArrays(GL_LINES, 0, (GLsizei)buffer.data.size() / Buffer::record_size(type)));
                break;
            }
            case GCodeProcessor::EMoveType::Travel:
            {
                std::array<float, 4> color = { 1.0f, 1.0f, 0.0f, 1.0f };
                set_color(current_program_id, color);
                glsafe(::glDrawArrays(GL_LINES, 0, (GLsizei)buffer.data.size() / Buffer::record_size(type)));
                break;
            }
            default:
            {
                break;
            }
            }

            glsafe(::glDisableClientState(GL_VERTEX_ARRAY));

            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
            shader.stop_using();
        }
    }
}

bool GCodeViewer::init_shaders()
{
    unsigned char begin_id = buffer_id(GCodeProcessor::EMoveType::Retract);
    unsigned char end_id = buffer_id(GCodeProcessor::EMoveType::Count);

    for (unsigned char i = begin_id; i < end_id; ++i)
    {
        Shader& shader = m_shaders[i];
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

    return true;
}

void GCodeViewer::reset_buffers()
{
    for (Buffer& buffer : m_buffers)
    {
        // release gpu memory
        if (buffer.vbo_id > 0)
            glsafe(::glDeleteBuffers(1, &buffer.vbo_id));

        // release cpu memory
        buffer.data = std::vector<float>();
    }
}

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_GCODE_VIEWER
