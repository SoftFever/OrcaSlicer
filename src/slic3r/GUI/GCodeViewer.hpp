#ifndef slic3r_GCodeViewer_hpp_
#define slic3r_GCodeViewer_hpp_

#if ENABLE_GCODE_VIEWER

#include "GLShader.hpp"
#include "3DScene.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"

#include <float.h>

namespace Slic3r {
class Print;
namespace GUI {

class GCodeViewer
{
    static const std::array<std::array<float, 3>, erCount> Default_Extrusion_Role_Colors;
    static const size_t Default_Range_Colors_Count = 10;
    static const std::array<std::array<float, 3>, Default_Range_Colors_Count> Default_Range_Colors;

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

    // Used to identify different toolpath sub-types inside a IBuffer
    struct Path
    {
        GCodeProcessor::EMoveType type{ GCodeProcessor::EMoveType::Noop };
        ExtrusionRole role{ erNone };
        unsigned int first{ 0 };
        unsigned int last{ 0 };
        float height{ 0.0f };
        float width{ 0.0f };
        float feedrate{ 0.0f };
        float fan_speed{ 0.0f };

        bool matches(const GCodeProcessor::MoveVertex& move) const {
            return type == move.type && role == move.extrusion_role && height == move.height && width == move.width && feedrate == move.feedrate && fan_speed == move.fan_speed;
        }
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
        void add_path(const GCodeProcessor::MoveVertex& move);
    };

    struct Shells
    {
        GLVolumeCollection volumes;
        bool visible{ false };
        Shader shader;
    };

    // helper to render extrusion paths
    struct Extrusions
    {
        struct Range
        {
            float min;
            float max;

            Range() { reset(); }

            void update_from(const float value)
            {
                min = std::min(min, value);
                max = std::max(max, value);
            }

            void reset()
            {
                min = FLT_MAX;
                max = -FLT_MAX;
            }

            float step_size() const { return (max - min) / (static_cast<float>(Default_Range_Colors_Count) - 1.0f); }
            std::array<float, 3> get_color_at(float value, const std::array<std::array<float, 3>, Default_Range_Colors_Count>& colors) const;
        };

        struct Ranges
        {
            std::array<std::array<float, 3>, Default_Range_Colors_Count> colors;

            // Color mapping by layer height.
            Range height;
            // Color mapping by extrusion width.
            Range width;
            // Color mapping by feedrate.
            Range feedrate;
            // Color mapping by fan speed.
            Range fan_speed;
//        // Color mapping by volumetric extrusion rate.
//        Range volumetric_rate;

            void reset() {
                height.reset();
                width.reset();
                feedrate.reset();
                fan_speed.reset();
            }
        };

        std::array<std::array<float, 3>, erCount> role_colors;
        unsigned int role_visibility_flags{ 0 };
        Ranges ranges;

        void reset_role_visibility_flags() {
            role_visibility_flags = 0;
            for (unsigned int i = 0; i < erCount; ++i)
            {
                role_visibility_flags |= 1 << i;
            }
        }

        void reset_ranges() { ranges.reset(); }

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
    std::vector<ExtrusionRole> m_roles;
    Extrusions m_extrusions;
    Shells m_shells;
    EViewType m_view_type{ EViewType::FeatureType };
    bool m_legend_enabled{ true };

public:
    GCodeViewer() = default;
    ~GCodeViewer() { reset(); }

    bool init() {
        m_extrusions.role_colors = Default_Extrusion_Role_Colors;
        m_extrusions.ranges.colors = Default_Range_Colors;
        set_toolpath_move_type_visible(GCodeProcessor::EMoveType::Extrude, true);
        return init_shaders();
    }

    void load(const GCodeProcessor::Result& gcode_result, const Print& print, bool initialized);
    void refresh_toolpaths_ranges(const GCodeProcessor::Result& gcode_result);

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

    bool is_legend_enabled() const { return m_legend_enabled; }
    void enable_legend(bool enable) { m_legend_enabled = enable; }

private:
    bool init_shaders();
    void load_toolpaths(const GCodeProcessor::Result& gcode_result);
    void load_shells(const Print& print, bool initialized);
    void render_toolpaths() const;
    void render_shells() const;
    void render_overlay() const;
};

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_GCODE_VIEWER

#endif // slic3r_GCodeViewer_hpp_

