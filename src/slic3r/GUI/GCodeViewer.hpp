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
    static const std::vector<std::array<float, 3>> Extrusion_Role_Colors;
    static const std::vector<std::array<float, 3>> Travel_Colors;
    static const std::vector<std::array<float, 3>> Range_Colors;

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
        struct Endpoint
        {
            unsigned int id{ 0u };
            double z{ 0.0 };
        };

        GCodeProcessor::EMoveType type{ GCodeProcessor::EMoveType::Noop };
        ExtrusionRole role{ erNone };
        Endpoint first;
        Endpoint last;
        float delta_extruder{ 0.0f };
        float height{ 0.0f };
        float width{ 0.0f };
        float feedrate{ 0.0f };
        float fan_speed{ 0.0f };
        float volumetric_rate{ 0.0f };
        unsigned char extruder_id{ 0 };
        unsigned char cp_color_id{ 0 };

        bool matches(const GCodeProcessor::MoveVertex& move) const {
            return type == move.type && role == move.extrusion_role && height == move.height && width == move.width &&
                feedrate == move.feedrate && fan_speed == move.fan_speed && volumetric_rate == move.volumetric_rate() &&
                extruder_id == move.extruder_id && cp_color_id == move.cp_color_id;
        }
    };

    // Used to batch the indices needed to render paths
    struct RenderPath
    {
        std::array<float, 3> color;
        std::vector<unsigned int> sizes;
        std::vector<size_t> offsets; // use size_t because we need the pointer's size (used in the call glMultiDrawElements())
    };

    // buffer containing indices data and shader for a specific toolpath type
    struct IBuffer
    {
        unsigned int ibo_id{ 0 };
        Shader shader;
        std::vector<unsigned int> data;
        size_t data_size{ 0 };
        std::vector<Path> paths;
        std::vector<RenderPath> render_paths;
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

            void update_from(const float value) { min = std::min(min, value); max = std::max(max, value); }
            void reset() { min = FLT_MAX; max = -FLT_MAX; }

            float step_size() const { return (max - min) / (static_cast<float>(Range_Colors.size()) - 1.0f); }
            std::array<float, 3> get_color_at(float value) const;
        };

        struct Ranges
        {
            // Color mapping by layer height.
            Range height;
            // Color mapping by extrusion width.
            Range width;
            // Color mapping by feedrate.
            Range feedrate;
            // Color mapping by fan speed.
            Range fan_speed;
            // Color mapping by volumetric extrusion rate.
            Range volumetric_rate;

            void reset() {
                height.reset();
                width.reset();
                feedrate.reset();
                fan_speed.reset();
                volumetric_rate.reset();
            }
        };

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
    };

#if ENABLE_GCODE_VIEWER_STATISTICS
    struct Statistics
    {
        long long load_time{ 0 };
        long long refresh_time{ 0 };
        long long gl_multi_points_calls_count{ 0 };
        long long gl_multi_line_strip_calls_count{ 0 };
        long long results_size{ 0 };
        long long vertices_size{ 0 };
        long long vertices_gpu_size{ 0 };
        long long indices_size{ 0 };
        long long indices_gpu_size{ 0 };

        void reset_all() {
            reset_times();
            reset_opengl();
            reset_sizes();
        }

        void reset_times() {
            load_time = 0;
            refresh_time = 0;
        }

        void reset_opengl() {
            gl_multi_points_calls_count = 0;
            gl_multi_line_strip_calls_count = 0;
        }

        void reset_sizes() {
            results_size = 0;
            vertices_size = 0;
            vertices_gpu_size = 0;
            indices_size = 0;
            indices_gpu_size = 0;
        }
    };
#endif // ENABLE_GCODE_VIEWER_STATISTICS

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
    unsigned int m_last_result_id{ 0 };
    VBuffer m_vertices;
    mutable std::vector<IBuffer> m_buffers{ static_cast<size_t>(GCodeProcessor::EMoveType::Extrude) };
    BoundingBoxf3 m_bounding_box;
    std::vector<std::array<float, 3>> m_tool_colors;
    std::vector<double> m_layers_zs;
    std::array<double, 2> m_layers_z_range;
    std::vector<ExtrusionRole> m_roles;
    std::vector<unsigned char> m_extruder_ids;
    mutable Extrusions m_extrusions;
    Shells m_shells;
    EViewType m_view_type{ EViewType::FeatureType };
    bool m_legend_enabled{ true };
#if ENABLE_GCODE_VIEWER_STATISTICS
    mutable Statistics m_statistics;
#endif // ENABLE_GCODE_VIEWER_STATISTICS

public:
    GCodeViewer() = default;
    ~GCodeViewer() { reset(); }

    bool init() {
        set_toolpath_move_type_visible(GCodeProcessor::EMoveType::Extrude, true);
        return init_shaders();
    }

    // extract rendering data from the given parameters
    void load(const GCodeProcessor::Result& gcode_result, const Print& print, bool initialized);
    // recalculate ranges in dependence of what is visible and sets tool/print colors
    void refresh(const GCodeProcessor::Result& gcode_result, const std::vector<std::string>& str_tool_colors);

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

    bool is_toolpath_move_type_visible(GCodeProcessor::EMoveType type) const;
    void set_toolpath_move_type_visible(GCodeProcessor::EMoveType type, bool visible);
    unsigned int get_toolpath_role_visibility_flags() const { return m_extrusions.role_visibility_flags; }
    void set_toolpath_role_visibility_flags(unsigned int flags) { m_extrusions.role_visibility_flags = flags; }
    unsigned int get_options_visibility_flags() const;
    void set_options_visibility_from_flags(unsigned int flags);
    void set_layers_z_range(const std::array<double, 2>& layers_z_range)
    {
        m_layers_z_range = layers_z_range;
        refresh_render_paths();
    }

    bool is_legend_enabled() const { return m_legend_enabled; }
    void enable_legend(bool enable) { m_legend_enabled = enable; }

private:
    bool init_shaders();
    void load_toolpaths(const GCodeProcessor::Result& gcode_result);
    void load_shells(const Print& print, bool initialized);
    void refresh_render_paths() const;
    void render_toolpaths() const;
    void render_shells() const;
    void render_legend() const;
#if ENABLE_GCODE_VIEWER_STATISTICS
    void render_statistics() const;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
    bool is_visible(ExtrusionRole role) const {
        return role < erCount && (m_extrusions.role_visibility_flags & (1 << role)) != 0;
    }
    bool is_visible(const Path& path) const { return is_visible(path.role); }
    bool is_in_z_range(const Path& path) const {
        auto in_z_range = [this](double z) {
            return z > m_layers_z_range[0] - EPSILON && z < m_layers_z_range[1] + EPSILON;
        };

        return in_z_range(path.first.z) || in_z_range(path.last.z);
    }
};

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_GCODE_VIEWER

#endif // slic3r_GCodeViewer_hpp_

