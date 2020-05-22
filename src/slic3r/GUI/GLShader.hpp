#ifndef slic3r_GLShader_hpp_
#define slic3r_GLShader_hpp_

#if ENABLE_SHADERS_MANAGER
#include <array>
#include <string>

namespace Slic3r {

class GLShaderProgram
{
public:
    enum class EShaderType
    {
        Vertex,
        Fragment,
        Geometry,
        TessEvaluation,
        TessControl,
        Compute,
        Count
    };

    typedef std::array<std::string, static_cast<size_t>(EShaderType::Count)> ShaderFilenames;
    typedef std::array<std::string, static_cast<size_t>(EShaderType::Count)> ShaderSources;

private:
    std::string m_name;
    unsigned int m_id{ 0 };

public:
    ~GLShaderProgram();

    bool init_from_files(const std::string& name, const ShaderFilenames& filenames);
    bool init_from_texts(const std::string& name, const ShaderSources& sources);

    const std::string& get_name() const { return m_name; }
    unsigned int get_id() const { return m_id; }

    void start_using() const;
    void stop_using() const;

    bool set_uniform(const char* name, int value) const;
    bool set_uniform(const char* name, bool value) const;
    bool set_uniform(const char* name, float value) const;
    bool set_uniform(const char* name, double value) const;
    bool set_uniform(const char* name, const std::array<float, 2>& value) const;
    bool set_uniform(const char* name, const std::array<float, 3>& value) const;
    bool set_uniform(const char* name, const std::array<float, 4>& value) const;
    bool set_uniform(const char* name, const float* value, size_t size) const;
    bool set_uniform(const char* name, const Transform3f& value) const;
    bool set_uniform(const char* name, const Transform3d& value) const;
    bool set_uniform(const char* name, const Matrix3f& value) const;

    // returns -1 if not found
    int get_attrib_location(const char* name) const;
    // returns -1 if not found
    int get_uniform_location(const char* name) const;
};

} // namespace Slic3r
#else
#include "libslic3r/libslic3r.h"
#include "libslic3r/Point.hpp"

namespace Slic3r {

class GLShader
{
public:
    GLShader() :
        fragment_program_id(0),
        vertex_program_id(0),
        shader_program_id(0)
        {}
    ~GLShader();

    bool load_from_text(const char *fragment_shader, const char *vertex_shader);
    bool load_from_file(const char* fragment_shader_filename, const char* vertex_shader_filename);

    void release();

    int  get_attrib_location(const char *name) const;
    int  get_uniform_location(const char *name) const;

    bool set_uniform(const char *name, float value) const;
    bool set_uniform(const char* name, const float* matrix) const;
    bool set_uniform(const char* name, int value) const;

    void enable() const;
    void disable() const;

    unsigned int    fragment_program_id;
    unsigned int    vertex_program_id;
    unsigned int    shader_program_id;
    std::string     last_error;
};

class Shader
{
    GLShader* m_shader;

public:
    Shader();
    ~Shader();

    bool init(const std::string& vertex_shader_filename, const std::string& fragment_shader_filename);

    bool is_initialized() const;

    bool start_using() const;
    void stop_using() const;

    int get_attrib_location(const std::string& name) const;
    int get_uniform_location(const std::string& name) const;

    void set_uniform(const std::string& name, float value) const;
    void set_uniform(const std::string& name, const float* matrix) const;
    void set_uniform(const std::string& name, bool value) const;

    const GLShader* get_shader() const { return m_shader; }
    unsigned int get_shader_program_id() const;

private:
    void reset();
};

}


#endif // ENABLE_SHADERS_MANAGER

#endif /* slic3r_GLShader_hpp_ */
