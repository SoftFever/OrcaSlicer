#include <GL/glew.h>

#include "GLShader.hpp"

#include "libslic3r/Utils.hpp"
#include "3DScene.hpp"
#include <boost/nowide/fstream.hpp>

#include <string>
#include <utility>
#include <assert.h>

namespace Slic3r {

GLShader::~GLShader()
{
    assert(fragment_program_id == 0);
    assert(vertex_program_id == 0);
    assert(shader_program_id == 0);
}

// A safe wrapper around glGetString to report a "N/A" string in case glGetString returns nullptr.
inline std::string gl_get_string_safe(GLenum param)
{
    const char *value = (const char*)glGetString(param);
    return std::string(value ? value : "N/A");
}

bool GLShader::load_from_text(const char *fragment_shader, const char *vertex_shader)
{
    std::string gl_version = gl_get_string_safe(GL_VERSION);
    int major = atoi(gl_version.c_str());
    //int minor = atoi(gl_version.c_str() + gl_version.find('.') + 1);
    if (major < 2) {
        // Cannot create a shader object on OpenGL 1.x.
        // Form an error message.
        std::string gl_vendor    = gl_get_string_safe(GL_VENDOR);
        std::string gl_renderer  = gl_get_string_safe(GL_RENDERER);
        std::string glsl_version = gl_get_string_safe(GL_SHADING_LANGUAGE_VERSION);
        last_error = "Your computer does not support OpenGL shaders.\n";
#ifdef _WIN32
        if (gl_vendor == "Microsoft Corporation" && gl_renderer == "GDI Generic") {
            last_error = "Windows is using a software OpenGL renderer.\n"
                         "You are either connected over remote desktop,\n"
                         "or a hardware acceleration is not available.\n";
        }
#endif
        last_error += "GL version:   " + gl_version   + "\n";
        last_error += "vendor:       " + gl_vendor    + "\n";
        last_error += "renderer:     " + gl_renderer  + "\n";
        last_error += "GLSL version: " + glsl_version + "\n";
        return false;
    }

    if (fragment_shader != nullptr) {
        this->fragment_program_id = ::glCreateShader(GL_FRAGMENT_SHADER);
        glcheck();
        if (this->fragment_program_id == 0) {
            last_error = "glCreateShader(GL_FRAGMENT_SHADER) failed.";
            return false;
        }
        GLint len = (GLint)strlen(fragment_shader);
        glsafe(::glShaderSource(this->fragment_program_id, 1, &fragment_shader, &len));
        glsafe(::glCompileShader(this->fragment_program_id));
        GLint params;
        glsafe(::glGetShaderiv(this->fragment_program_id, GL_COMPILE_STATUS, &params));
        if (params == GL_FALSE) {
            // Compilation failed. Get the log.
            glsafe(::glGetShaderiv(this->fragment_program_id, GL_INFO_LOG_LENGTH, &params));
            std::vector<char> msg(params);
            glsafe(::glGetShaderInfoLog(this->fragment_program_id, params, &params, msg.data()));
            this->last_error = std::string("Fragment shader compilation failed:\n") + msg.data();
            this->release();
            return false;
        }
    }

    if (vertex_shader != nullptr) {
        this->vertex_program_id = ::glCreateShader(GL_VERTEX_SHADER);
        glcheck();
        if (this->vertex_program_id == 0) {
            last_error = "glCreateShader(GL_VERTEX_SHADER) failed.";
            this->release();
            return false;
        }
        GLint len = (GLint)strlen(vertex_shader);
        glsafe(::glShaderSource(this->vertex_program_id, 1, &vertex_shader, &len));
        glsafe(::glCompileShader(this->vertex_program_id));
        GLint params;
        glsafe(::glGetShaderiv(this->vertex_program_id, GL_COMPILE_STATUS, &params));
        if (params == GL_FALSE) {
            // Compilation failed. Get the log.
            glsafe(::glGetShaderiv(this->vertex_program_id, GL_INFO_LOG_LENGTH, &params));
            std::vector<char> msg(params);
            glsafe(::glGetShaderInfoLog(this->vertex_program_id, params, &params, msg.data()));
            this->last_error = std::string("Vertex shader compilation failed:\n") + msg.data();
            this->release();
            return false;
        }
    }

    // Link shaders
    this->shader_program_id = ::glCreateProgram();
    glcheck();
    if (this->shader_program_id == 0) {
        last_error = "glCreateProgram() failed.";
        this->release();
        return false;
    }

    if (this->fragment_program_id)
        glsafe(::glAttachShader(this->shader_program_id, this->fragment_program_id));
    if (this->vertex_program_id)
        glsafe(::glAttachShader(this->shader_program_id, this->vertex_program_id));
    glsafe(::glLinkProgram(this->shader_program_id));

    GLint params;
    glsafe(::glGetProgramiv(this->shader_program_id, GL_LINK_STATUS, &params));
    if (params == GL_FALSE) {
        // Linking failed. Get the log.
        glsafe(::glGetProgramiv(this->vertex_program_id, GL_INFO_LOG_LENGTH, &params));
        std::vector<char> msg(params);
        glsafe(::glGetProgramInfoLog(this->vertex_program_id, params, &params, msg.data()));
        this->last_error = std::string("Shader linking failed:\n") + msg.data();
        this->release();
        return false;
    }

    last_error.clear();
    return true;
}

bool GLShader::load_from_file(const char* fragment_shader_filename, const char* vertex_shader_filename)
{
    const std::string& path = resources_dir() + "/shaders/";

    boost::nowide::ifstream vs(path + std::string(vertex_shader_filename), boost::nowide::ifstream::binary);
    if (!vs.good())
        return false;

    vs.seekg(0, vs.end);
    int file_length = (int)vs.tellg();
    vs.seekg(0, vs.beg);
    std::string vertex_shader(file_length, '\0');
    vs.read(const_cast<char*>(vertex_shader.data()), file_length);
    if (!vs.good())
        return false;

    vs.close();

    boost::nowide::ifstream fs(path + std::string(fragment_shader_filename), boost::nowide::ifstream::binary);
    if (!fs.good())
        return false;

    fs.seekg(0, fs.end);
    file_length = (int)fs.tellg();
    fs.seekg(0, fs.beg);
    std::string fragment_shader(file_length, '\0');
    fs.read(const_cast<char*>(fragment_shader.data()), file_length);
    if (!fs.good())
        return false;

    fs.close();

    return load_from_text(fragment_shader.c_str(), vertex_shader.c_str());
}

void GLShader::release()
{
    if (this->shader_program_id) {
        if (this->vertex_program_id)
            glsafe(::glDetachShader(this->shader_program_id, this->vertex_program_id));
        if (this->fragment_program_id)
            glsafe(::glDetachShader(this->shader_program_id, this->fragment_program_id));
        glsafe(::glDeleteProgram(this->shader_program_id));
        this->shader_program_id = 0;
    }

    if (this->vertex_program_id) {
        glsafe(::glDeleteShader(this->vertex_program_id));
        this->vertex_program_id = 0;
    }
    if (this->fragment_program_id) {
        glsafe(::glDeleteShader(this->fragment_program_id));
        this->fragment_program_id = 0;
    }
}

void GLShader::enable() const
{
    glsafe(::glUseProgram(this->shader_program_id));
}

void GLShader::disable() const
{
    glsafe(::glUseProgram(0));
}

// Return shader vertex attribute ID
int GLShader::get_attrib_location(const char *name) const
{
    return this->shader_program_id ? glGetAttribLocation(this->shader_program_id, name) : -1;
}

// Return shader uniform variable ID
int GLShader::get_uniform_location(const char *name) const
{
    return this->shader_program_id ? glGetUniformLocation(this->shader_program_id, name) : -1;
}

bool GLShader::set_uniform(const char *name, float value) const
{
    int id = this->get_uniform_location(name);
    if (id >= 0) { 
        glsafe(::glUniform1fARB(id, value));
        return true;
    }
    return false;
}

bool GLShader::set_uniform(const char* name, const float* matrix) const
{
    int id = get_uniform_location(name);
    if (id >= 0)
    {
        glsafe(::glUniformMatrix4fv(id, 1, GL_FALSE, (const GLfloat*)matrix));
        return true;
    }
    return false;
}

bool GLShader::set_uniform(const char* name, int value) const
{
    int id = get_uniform_location(name);
    if (id >= 0)
    {
        glsafe(::glUniform1i(id, value));
        return true;
    }
    return false;
}

/*
# Set shader vector
sub SetVector
{
    my($self,$var,@values) = @_;

    my $id = $self->Map($var);
    return 'Unable to map $var' if (!defined($id));

    my $count = scalar(@values);
    eval('glUniform'.$count.'fARB($id,@values)');

    return '';
}

# Set shader 4x4 matrix
sub SetMatrix
{
    my($self,$var,$oga) = @_;

    my $id = $self->Map($var);
    return 'Unable to map $var' if (!defined($id));

    glUniformMatrix4fvARB_c($id,1,0,$oga->ptr());
    return '';
}
*/

Shader::Shader()
    : m_shader(nullptr)
{
}

Shader::~Shader()
{
    reset();
}

bool Shader::init(const std::string& vertex_shader_filename, const std::string& fragment_shader_filename)
{
    if (is_initialized())
        return true;

    m_shader = new GLShader();
    if (m_shader != nullptr)
    {
        if (!m_shader->load_from_file(fragment_shader_filename.c_str(), vertex_shader_filename.c_str()))
        {
            std::cout << "Compilaton of shader failed:" << std::endl;
            std::cout << m_shader->last_error << std::endl;
            reset();
            return false;
        }
    }

    return true;
}

bool Shader::is_initialized() const
{
    return (m_shader != nullptr);
}

bool Shader::start_using() const
{
    if (is_initialized())
    {
        m_shader->enable();
        return true;
    }
    else
        return false;
}

void Shader::stop_using() const
{
    if (m_shader != nullptr)
        m_shader->disable();
}

int Shader::get_attrib_location(const std::string& name) const
{
    return (m_shader != nullptr) ? m_shader->get_attrib_location(name.c_str()) : -1;
}

int Shader::get_uniform_location(const std::string& name) const
{
    return (m_shader != nullptr) ? m_shader->get_uniform_location(name.c_str()) : -1;
}

void Shader::set_uniform(const std::string& name, float value) const
{
    if (m_shader != nullptr)
        m_shader->set_uniform(name.c_str(), value);
}

void Shader::set_uniform(const std::string& name, const float* matrix) const
{
    if (m_shader != nullptr)
        m_shader->set_uniform(name.c_str(), matrix);
}

void Shader::set_uniform(const std::string& name, bool value) const
{
    if (m_shader != nullptr)
        m_shader->set_uniform(name.c_str(), value ? 1 : 0);
}

unsigned int Shader::get_shader_program_id() const
{
    return (m_shader != nullptr) ? m_shader->shader_program_id : 0;
}

void Shader::reset()
{
    if (m_shader != nullptr)
    {
        m_shader->release();
        delete m_shader;
        m_shader = nullptr;
    }
}

} // namespace Slic3r
