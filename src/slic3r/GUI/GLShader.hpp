#ifndef slic3r_GLShader_hpp_
#define slic3r_GLShader_hpp_

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
    bool set_uniform(const char* name, const std::array<int, 2>& value) const;
    bool set_uniform(const char* name, const std::array<int, 3>& value) const;
    bool set_uniform(const char* name, const std::array<int, 4>& value) const;
    bool set_uniform(const char* name, const std::array<float, 2>& value) const;
    bool set_uniform(const char* name, const std::array<float, 3>& value) const;
    bool set_uniform(const char* name, const std::array<float, 4>& value) const;
    bool set_uniform(const char* name, const float* value, size_t size) const;
    bool set_uniform(const char* name, const Transform3f& value) const;
    bool set_uniform(const char* name, const Transform3d& value) const;
    bool set_uniform(const char* name, const Matrix3f& value) const;
    bool set_uniform(const char* name, const Vec3f& value) const;
    bool set_uniform(const char* name, const Vec3d& value) const;

    // returns -1 if not found
    int get_attrib_location(const char* name) const;
    // returns -1 if not found
    int get_uniform_location(const char* name) const;
};

} // namespace Slic3r

#endif /* slic3r_GLShader_hpp_ */
