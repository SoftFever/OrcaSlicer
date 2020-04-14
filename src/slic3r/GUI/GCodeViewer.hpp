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
        std::vector<float> data;

        static size_t stride(GCodeProcessor::EMoveType type)
        {
            return 3 * sizeof(float);
        }

        static size_t record_size(GCodeProcessor::EMoveType type)
        {
            switch (type)
            {
            case GCodeProcessor::EMoveType::Tool_change:
            case GCodeProcessor::EMoveType::Retract:
            case GCodeProcessor::EMoveType::Unretract: { return 3; }
            case GCodeProcessor::EMoveType::Extrude:
            case GCodeProcessor::EMoveType::Travel: { return 6; }
            default: { return 0; }
            }
        }
    };

    std::vector<Buffer> m_buffers{ static_cast<size_t>(GCodeProcessor::EMoveType::Extrude) };
    std::vector<Shader> m_shaders{ static_cast<size_t>(GCodeProcessor::EMoveType::Extrude) };
    unsigned int m_last_result_id{ 0 };

public:
    GCodeViewer() = default;
    ~GCodeViewer() { reset_buffers(); }

    bool init() { return init_shaders(); }
    void generate(const GCodeProcessor::Result& gcode_result);
    void render() const;

private:
    bool init_shaders();
    void reset_buffers();
};

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_GCODE_VIEWER

#endif // slic3r_GCodeViewer_hpp_

