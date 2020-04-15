#ifndef slic3r_GCodeViewer_hpp_
#define slic3r_GCodeViewer_hpp_

#if ENABLE_GCODE_VIEWER

#include "GLShader.hpp"
#include "3DScene.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"

#include <vector>

namespace Slic3r {
class Print;
namespace GUI {

class GCodeViewer
{
    // buffer containing vertices data
    struct VBuffer
    {
        unsigned int vbo_id{ 0 };
        size_t vertices_count{ 0 };

        size_t data_size_bytes() { return vertices_count * vertex_size_bytes(); }

        void reset();

        static size_t vertex_size() { return 3; }
        static size_t vertex_size_bytes() { return vertex_size() * sizeof(float); }
    };

    // buffer containing indices data
    struct IBuffer
    {
        unsigned int ibo_id{ 0 };
        Shader shader;
        std::vector<unsigned int> data;
        size_t data_size{ 0 };
        bool visible{ false };

        void reset();
    };

    struct Shells
    {
        GLVolumeCollection volumes;
        bool visible{ false };
        Shader shader;
    };

    VBuffer m_vertices;
    std::vector<IBuffer> m_buffers{ static_cast<size_t>(GCodeProcessor::EMoveType::Extrude) };

    unsigned int m_last_result_id{ 0 };
    std::vector<double> m_layers_zs;
    Shells m_shells;

public:
    GCodeViewer() = default;
    ~GCodeViewer() { reset(); }

    bool init() { set_toolpath_visible(GCodeProcessor::EMoveType::Extrude, true);  return init_shaders(); }
    void load(const GCodeProcessor::Result& gcode_result, const Print& print, bool initialized);
    void reset();
    void render() const;

    const std::vector<double>& get_layers_zs() const { return m_layers_zs; };

    bool is_toolpath_visible(GCodeProcessor::EMoveType type) const;
    void set_toolpath_visible(GCodeProcessor::EMoveType type, bool visible);

    bool are_shells_visible() const { return m_shells.visible; }
    void set_shells_visible(bool visible) { m_shells.visible = visible; }

private:
    bool init_shaders();
    void load_toolpaths(const GCodeProcessor::Result& gcode_result);
    void load_shells(const Print& print, bool initialized);
    void render_toolpaths() const;
    void render_shells() const;
};

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_GCODE_VIEWER

#endif // slic3r_GCodeViewer_hpp_

