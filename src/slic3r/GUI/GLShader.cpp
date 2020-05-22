#include "libslic3r/libslic3r.h"
#include "GLShader.hpp"

#include "3DScene.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/nowide/fstream.hpp>
#include <GL/glew.h>
#include <cassert>

#include <boost/log/trivial.hpp>

namespace Slic3r {

GLShaderProgram::~GLShaderProgram()
{
    if (m_id > 0)
        glsafe(::glDeleteProgram(m_id));
}

bool GLShaderProgram::init_from_files(const std::string& name, const ShaderFilenames& filenames)
{
    auto load_from_file = [](const std::string& filename) {
        std::string path = resources_dir() + "/shaders/" + filename;
        boost::nowide::ifstream s(path, boost::nowide::ifstream::binary);
        if (!s.good()) {
            BOOST_LOG_TRIVIAL(error) << "Couldn't open file: '" << path << "'";
            return std::string();
        }

        s.seekg(0, s.end);
        int file_length = static_cast<int>(s.tellg());
        s.seekg(0, s.beg);
        std::string source(file_length, '\0');
        s.read(source.data(), file_length);
        if (!s.good()) {
            BOOST_LOG_TRIVIAL(error) << "Error while loading file: '" << path << "'";
            return std::string();
        }

        s.close();
        return source;
    };

    ShaderSources sources = {};
    for (size_t i = 0; i < static_cast<size_t>(EShaderType::Count); ++i) {
        sources[i] = filenames[i].empty() ? std::string() : load_from_file(filenames[i]);
    }

    bool valid = !sources[static_cast<size_t>(EShaderType::Vertex)].empty() && !sources[static_cast<size_t>(EShaderType::Fragment)].empty() && sources[static_cast<size_t>(EShaderType::Compute)].empty();
    valid |= !sources[static_cast<size_t>(EShaderType::Compute)].empty() && sources[static_cast<size_t>(EShaderType::Vertex)].empty() && sources[static_cast<size_t>(EShaderType::Fragment)].empty() && 
              sources[static_cast<size_t>(EShaderType::Geometry)].empty() && sources[static_cast<size_t>(EShaderType::TessEvaluation)].empty() && sources[static_cast<size_t>(EShaderType::TessControl)].empty();

    return valid ? init_from_texts(name, sources) : false;
}

bool GLShaderProgram::init_from_texts(const std::string& name, const ShaderSources& sources)
{
    auto shader_type_as_string = [](EShaderType type) {
        switch (type)
        {
        case EShaderType::Vertex:         { return "vertex"; }
        case EShaderType::Fragment:       { return "fragment"; }
        case EShaderType::Geometry:       { return "geometry"; }
        case EShaderType::TessEvaluation: { return "tesselation evaluation"; }
        case EShaderType::TessControl:    { return "tesselation control"; }
        case EShaderType::Compute:        { return "compute"; }
        default:                          { return "unknown"; }
        }
    };

    auto create_shader = [](EShaderType type) {
        GLuint id = 0;
        switch (type)
        {
        case EShaderType::Vertex:         { id = ::glCreateShader(GL_VERTEX_SHADER); glcheck(); break; }
        case EShaderType::Fragment:       { id = ::glCreateShader(GL_FRAGMENT_SHADER); glcheck(); break; }
        case EShaderType::Geometry:       { id = ::glCreateShader(GL_GEOMETRY_SHADER); glcheck(); break; }
        case EShaderType::TessEvaluation: { id = ::glCreateShader(GL_TESS_EVALUATION_SHADER); glcheck(); break; }
        case EShaderType::TessControl:    { id = ::glCreateShader(GL_TESS_CONTROL_SHADER); glcheck(); break; }
        case EShaderType::Compute:        { id = ::glCreateShader(GL_COMPUTE_SHADER); glcheck(); break; }
        default:                          { break; }
        }
           
        return (id == 0) ? std::make_pair(false, GLuint(0)) : std::make_pair(true, id);
    };

    auto release_shaders = [](const std::array<GLuint, static_cast<size_t>(EShaderType::Count)>& shader_ids) {
        for (size_t i = 0; i < static_cast<size_t>(EShaderType::Count); ++i) {
            if (shader_ids[i] > 0)
                glsafe(::glDeleteShader(shader_ids[i]));
        }
    };

    assert(m_id == 0);

    m_name = name;

    std::array<GLuint, static_cast<size_t>(EShaderType::Count)> shader_ids = { 0 };

    for (size_t i = 0; i < static_cast<size_t>(EShaderType::Count); ++i) {
        const std::string& source = sources[i];
        if (!source.empty())
        {
            EShaderType type = static_cast<EShaderType>(i);
            auto [result, id] = create_shader(type);
            if (result)
                shader_ids[i] = id;
            else {
                BOOST_LOG_TRIVIAL(error) << "glCreateShader() failed for " << shader_type_as_string(type) << " shader of shader program '" << name << "'";

                // release shaders
                release_shaders(shader_ids);
                return false;
            }

            const char* source_ptr = source.c_str();
            glsafe(::glShaderSource(id, 1, &source_ptr, nullptr));
            glsafe(::glCompileShader(id));
            GLint params;
            glsafe(::glGetShaderiv(id, GL_COMPILE_STATUS, &params));
            if (params == GL_FALSE) {
                // Compilation failed. 
                glsafe(::glGetShaderiv(id, GL_INFO_LOG_LENGTH, &params));
                std::vector<char> msg(params);
                glsafe(::glGetShaderInfoLog(id, params, &params, msg.data()));
                BOOST_LOG_TRIVIAL(error) << "Unable to compile " << shader_type_as_string(type) << " shader of shader program '" << name << "':\n" << msg.data();

                // release shaders
                release_shaders(shader_ids);
                return false;
            }
        }
    }

    m_id = ::glCreateProgram();
    glcheck();
    if (m_id == 0) {
        BOOST_LOG_TRIVIAL(error) << "glCreateProgram() failed for shader program '" << name << "'";

        // release shaders
        release_shaders(shader_ids);
        return false;
    }

    for (size_t i = 0; i < static_cast<size_t>(EShaderType::Count); ++i) {
        if (shader_ids[i] > 0)
            glsafe(::glAttachShader(m_id, shader_ids[i]));
    }

    glsafe(::glLinkProgram(m_id));
    GLint params;
    glsafe(::glGetProgramiv(m_id, GL_LINK_STATUS, &params));
    if (params == GL_FALSE) {
        // Linking failed. 
        glsafe(::glGetProgramiv(m_id, GL_INFO_LOG_LENGTH, &params));
        std::vector<char> msg(params);
        glsafe(::glGetProgramInfoLog(m_id, params, &params, msg.data()));
        BOOST_LOG_TRIVIAL(error) << "Unable to link shader program '" << name << "':\n" << msg.data();

        // release shaders
        release_shaders(shader_ids);

        // release shader program
        glsafe(::glDeleteProgram(m_id));
        m_id = 0;

        return false;
    }

    // release shaders, they are no more needed
    release_shaders(shader_ids);

    return true;
}

void GLShaderProgram::start_using() const
{
    assert(m_id > 0);
    glsafe(::glUseProgram(m_id));
}

void GLShaderProgram::stop_using() const
{
    glsafe(::glUseProgram(0));
}

bool GLShaderProgram::set_uniform(const char* name, int value) const
{
    int id = get_uniform_location(name);
    if (id >= 0) {
        glsafe(::glUniform1i(id, static_cast<GLint>(value)));
        return true;
    }
    return false;
}

bool GLShaderProgram::set_uniform(const char* name, bool value) const
{
    return set_uniform(name, value ? 1 : 0);
}

bool GLShaderProgram::set_uniform(const char* name, float value) const
{
    int id = get_uniform_location(name);
    if (id >= 0) {
        glsafe(::glUniform1f(id, static_cast<GLfloat>(value)));
        return true;
    }
    return false;
}

bool GLShaderProgram::set_uniform(const char* name, double value) const
{
    return set_uniform(name, static_cast<float>(value));
}

bool GLShaderProgram::set_uniform(const char* name, const std::array<float, 2>& value) const
{
    int id = get_uniform_location(name);
    if (id >= 0) {
        glsafe(::glUniform2fv(id, 1, static_cast<const GLfloat*>(value.data())));
        return true;
    }
    return false;
}

bool GLShaderProgram::set_uniform(const char* name, const std::array<float, 3>& value) const
{
    int id = get_uniform_location(name);
    if (id >= 0) {
        glsafe(::glUniform3fv(id, 1, static_cast<const GLfloat*>(value.data())));
        return true;
    }
    return false;
}

bool GLShaderProgram::set_uniform(const char* name, const std::array<float, 4>& value) const
{
    int id = get_uniform_location(name);
    if (id >= 0) {
        glsafe(::glUniform4fv(id, 1, static_cast<const GLfloat*>(value.data())));
        return true;
    }
    return false;
}

bool GLShaderProgram::set_uniform(const char* name, const float* value, size_t size) const
{
    if (size == 1)
        return set_uniform(name, value[0]);
    else if (size < 5) {
        int id = get_uniform_location(name);
        if (id >= 0) {
            if (size == 2)
                glsafe(::glUniform2fv(id, 1, static_cast<const GLfloat*>(value)));
            else if (size == 3)
                glsafe(::glUniform3fv(id, 1, static_cast<const GLfloat*>(value)));
            else
                glsafe(::glUniform4fv(id, 1, static_cast<const GLfloat*>(value)));

            return true;
        }
    }
    return false;
}

bool GLShaderProgram::set_uniform(const char* name, const Transform3f& value) const
{
    int id = get_uniform_location(name);
    if (id >= 0) {
        glsafe(::glUniformMatrix4fv(id, 1, GL_FALSE, static_cast<const GLfloat*>(value.matrix().data())));
        return true;
    }
    return false;
}

bool GLShaderProgram::set_uniform(const char* name, const Transform3d& value) const
{
    return set_uniform(name, value.cast<float>());
}

bool GLShaderProgram::set_uniform(const char* name, const Matrix3f& value) const
{
    int id = get_uniform_location(name);
    if (id >= 0) {
        glsafe(::glUniformMatrix3fv(id, 1, GL_FALSE, static_cast<const GLfloat*>(value.data())));
        return true;
    }
    return false;
}

int GLShaderProgram::get_attrib_location(const char* name) const
{
    return (m_id > 0) ? ::glGetAttribLocation(m_id, name) : -1;
}

int GLShaderProgram::get_uniform_location(const char* name) const
{
    return (m_id > 0) ? ::glGetUniformLocation(m_id, name) : -1;
}

} // namespace Slic3r
