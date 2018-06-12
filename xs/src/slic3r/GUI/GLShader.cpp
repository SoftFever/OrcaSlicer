#include <GL/glew.h>

#include "GLShader.hpp"

#include "../../libslic3r/Utils.hpp"
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
        this->fragment_program_id = glCreateShader(GL_FRAGMENT_SHADER);
        if (this->fragment_program_id == 0) {
            last_error = "glCreateShader(GL_FRAGMENT_SHADER) failed.";
            return false;
        }
        GLint len = (GLint)strlen(fragment_shader);
        glShaderSource(this->fragment_program_id, 1, &fragment_shader, &len);
        glCompileShader(this->fragment_program_id);
        GLint params;
        glGetShaderiv(this->fragment_program_id, GL_COMPILE_STATUS, &params);
        if (params == GL_FALSE) {
            // Compilation failed. Get the log.
            glGetShaderiv(this->fragment_program_id, GL_INFO_LOG_LENGTH, &params);
            std::vector<char> msg(params);
            glGetShaderInfoLog(this->fragment_program_id, params, &params, msg.data());
            this->last_error = std::string("Fragment shader compilation failed:\n") + msg.data();
            this->release();
            return false;
        }
    }

    if (vertex_shader != nullptr) {
        this->vertex_program_id = glCreateShader(GL_VERTEX_SHADER);
        if (this->vertex_program_id == 0) {
            last_error = "glCreateShader(GL_VERTEX_SHADER) failed.";
            this->release();
            return false;
        }
        GLint len = (GLint)strlen(vertex_shader);
        glShaderSource(this->vertex_program_id, 1, &vertex_shader, &len);
        glCompileShader(this->vertex_program_id);
        GLint params;
        glGetShaderiv(this->vertex_program_id, GL_COMPILE_STATUS, &params);
        if (params == GL_FALSE) {
            // Compilation failed. Get the log.
            glGetShaderiv(this->vertex_program_id, GL_INFO_LOG_LENGTH, &params);
            std::vector<char> msg(params);
            glGetShaderInfoLog(this->vertex_program_id, params, &params, msg.data());
            this->last_error = std::string("Vertex shader compilation failed:\n") + msg.data();
            this->release();
            return false;
        }
    }

    // Link shaders
    this->shader_program_id = glCreateProgram();
    if (this->shader_program_id == 0) {
        last_error = "glCreateProgram() failed.";
        this->release();
        return false;
    }

    if (this->fragment_program_id)
        glAttachShader(this->shader_program_id, this->fragment_program_id);
    if (this->vertex_program_id)
        glAttachShader(this->shader_program_id, this->vertex_program_id);
    glLinkProgram(this->shader_program_id);

    GLint params;
    glGetProgramiv(this->shader_program_id, GL_LINK_STATUS, &params);
    if (params == GL_FALSE) {
        // Linking failed. Get the log.
        glGetProgramiv(this->vertex_program_id, GL_INFO_LOG_LENGTH, &params);
        std::vector<char> msg(params);
        glGetProgramInfoLog(this->vertex_program_id, params, &params, msg.data());
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
    int file_length = vs.tellg();
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
    file_length = fs.tellg();
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
            glDetachShader(this->shader_program_id, this->vertex_program_id);
        if (this->fragment_program_id)
            glDetachShader(this->shader_program_id, this->fragment_program_id);
        glDeleteProgram(this->shader_program_id);
        this->shader_program_id = 0;
    }

    if (this->vertex_program_id) {
        glDeleteShader(this->vertex_program_id);
        this->vertex_program_id = 0;
    }
    if (this->fragment_program_id) {
        glDeleteShader(this->fragment_program_id);
        this->fragment_program_id = 0;
    }
}

void GLShader::enable() const
{
    glUseProgram(this->shader_program_id);
}

void GLShader::disable() const
{
    glUseProgram(0);
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
        glUniform1fARB(id, value);
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

} // namespace Slic3r
