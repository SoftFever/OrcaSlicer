#ifndef slic3r_GCodeViewer_hpp_
#define slic3r_GCodeViewer_hpp_

#if ENABLE_GCODE_VIEWER

#include "GLShader.hpp"
#include "3DScene.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"

namespace Slic3r {
class Print;
namespace GUI {

class GCodeViewer
{
    static const std::array<std::array<float, 4>, erCount> Default_Extrusion_Role_Colors;

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

    struct Path
    {
        GCodeProcessor::EMoveType type{ GCodeProcessor::EMoveType::Noop };
        ExtrusionRole role{ erNone };
        unsigned int first{ 0 };
        unsigned int last{ 0 };

        bool matches(GCodeProcessor::EMoveType type, ExtrusionRole role) const { return this->type == type && this->role == role; }
    };

    // buffer containing indices data and shader for a specific toolpath type
    struct IBuffer
    {
        unsigned int ibo_id{ 0 };
        Shader shader;
        std::vector<unsigned int> data;
        size_t data_size{ 0 };
        std::vector<Path> paths;
        bool visible{ false };

        void reset();
        bool init_shader(const std::string& vertex_shader_src, const std::string& fragment_shader_src);

        void add_path(GCodeProcessor::EMoveType type, ExtrusionRole role);
    };

    struct Shells
    {
        GLVolumeCollection volumes;
        bool visible{ false };
        Shader shader;
    };

    struct Extrusions
    {
        std::array<std::array<float, 4>, erCount> role_colors;
        unsigned int role_visibility_flags{ 0 };

        void reset_role_visibility_flags() {
            role_visibility_flags = 0;
            for (unsigned int i = 0; i < erCount; ++i)
            {
                role_visibility_flags |= 1 << i;
            }
        }

        static bool is_role_visible(unsigned int flags, ExtrusionRole role) {
            return role < erCount && (flags & (1 << role)) != 0;
        }
    };

public:
    enum class EViewType : unsigned char
    {
        FeatureType,
        Height,
        Width,
        Feedrate,
        FanSpeed,
        VolumetricRate,
        Tool,
        ColorPrint,
        Count
    };

private:
    VBuffer m_vertices;
    std::vector<IBuffer> m_buffers{ static_cast<size_t>(GCodeProcessor::EMoveType::Extrude) };
    BoundingBoxf3 m_bounding_box;

    unsigned int m_last_result_id{ 0 };
    std::vector<double> m_layers_zs;
    Extrusions m_extrusions;
    Shells m_shells;
    EViewType m_view_type{ EViewType::FeatureType };

public:
    GCodeViewer() = default;
    ~GCodeViewer() { reset(); }

    bool init() {
        m_extrusions.role_colors = Default_Extrusion_Role_Colors;
        set_toolpath_move_type_visible(GCodeProcessor::EMoveType::Extrude, true);
        return init_shaders();
    }
    void load(const GCodeProcessor::Result& gcode_result, const Print& print, bool initialized);
    void reset();
    void render() const;

    const BoundingBoxf3& get_bounding_box() const { return m_bounding_box; }
    const std::vector<double>& get_layers_zs() const { return m_layers_zs; };

    EViewType get_view_type() const { return m_view_type; }
    void set_view_type(EViewType type) {
        if (type == EViewType::Count)
            type = EViewType::FeatureType;

        m_view_type = type;
    }

    bool is_toolpath_visible(GCodeProcessor::EMoveType type) const;
    void set_toolpath_move_type_visible(GCodeProcessor::EMoveType type, bool visible);
    void set_toolpath_role_visibility_flags(unsigned int flags) { m_extrusions.role_visibility_flags = flags; }

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

