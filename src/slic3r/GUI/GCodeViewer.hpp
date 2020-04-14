#ifndef slic3r_GCodeViewer_hpp_
#define slic3r_GCodeViewer_hpp_

#if ENABLE_GCODE_VIEWER

#include "GLShader.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"

#include <vector>

namespace Slic3r {
namespace GUI {

class GCodeViewer
{
    struct Buffer
    {
        unsigned int vbo_id{ 0 };
        Shader shader;
        std::vector<float> data;
        bool visible{ false };

        void reset();

        static size_t vertex_size() { return 3; }

        static size_t vertex_size_bytes() { return vertex_size() * sizeof(float); }
    };

    std::vector<Buffer> m_buffers{ static_cast<size_t>(GCodeProcessor::EMoveType::Extrude) };

    unsigned int m_last_result_id{ 0 };
    std::vector<double> m_layers_zs;

public:
    GCodeViewer() = default;
    ~GCodeViewer() { reset(); }

    bool init() { set_toolpath_visible(GCodeProcessor::EMoveType::Extrude, true);  return init_shaders(); }
    void generate(const GCodeProcessor::Result& gcode_result);
    void reset();
    void render() const;

    const std::vector<double>& get_layers_zs() const { return m_layers_zs; };

    bool is_toolpath_visible(GCodeProcessor::EMoveType type) const;
    void set_toolpath_visible(GCodeProcessor::EMoveType type, bool visible);

private:
    bool init_shaders();
};

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_GCODE_VIEWER

#endif // slic3r_GCodeViewer_hpp_

