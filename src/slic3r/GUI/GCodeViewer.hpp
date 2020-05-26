#ifndef slic3r_GCodeViewer_hpp_
#define slic3r_GCodeViewer_hpp_

#if ENABLE_GCODE_VIEWER
#include "3DScene.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "GLModel.hpp"

#include <float.h>

namespace Slic3r {

class Print;
class TriangleMesh;

namespace GUI {

class GCodeViewer
{
    using Color = std::array<float, 3>;
    static const std::vector<Color> Extrusion_Role_Colors;
    static const std::vector<Color> Options_Colors;
    static const std::vector<Color> Travel_Colors;
    static const std::vector<Color> Range_Colors;

    enum class EOptionsColors : unsigned char
    {
        Retractions,
        Unretractions,
        ToolChanges,
        ColorChanges,
        PausePrints,
        CustomGCodes
    };

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
            // index into the buffer indices ibo
            unsigned int i_id{ 0u };
            // sequential id (same as index into the vertices vbo)
            unsigned int s_id{ 0u };
            Vec3f position{ Vec3f::Zero() };
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

        bool matches(const GCodeProcessor::MoveVertex& move) const;
    };

    // Used to batch the indices needed to render paths
    struct RenderPath
    {
        Color color;
        std::vector<unsigned int> sizes;
        std::vector<size_t> offsets; // use size_t because we need the pointer's size (used in the call glMultiDrawElements())
    };

    // buffer containing indices data and shader for a specific toolpath type
    struct IBuffer
    {
        unsigned int ibo_id{ 0 };
        size_t indices_count{ 0 };
        std::string shader;
        std::vector<Path> paths;
        std::vector<RenderPath> render_paths;
        bool visible{ false };

        void reset();
        void add_path(const GCodeProcessor::MoveVertex& move, unsigned int i_id, unsigned int s_id);
    };

    // helper to render shells
    struct Shells
    {
        GLVolumeCollection volumes;
        bool visible{ false };
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
            Color get_color_at(float value) const;
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
        long long refresh_paths_time{ 0 };
        long long gl_multi_points_calls_count{ 0 };
        long long gl_multi_line_strip_calls_count{ 0 };
        long long results_size{ 0 };
        long long vertices_size{ 0 };
        long long vertices_gpu_size{ 0 };
        long long indices_size{ 0 };
        long long indices_gpu_size{ 0 };
        long long paths_size{ 0 };
        long long render_paths_size{ 0 };

        void reset_all() {
            reset_times();
            reset_opengl();
            reset_sizes();
        }

        void reset_times() {
            load_time = 0;
            refresh_time = 0;
            refresh_paths_time = 0;
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
            paths_size =  0;
            render_paths_size = 0;
        }
    };
#endif // ENABLE_GCODE_VIEWER_STATISTICS

#if ENABLE_GCODE_VIEWER_SHADERS_EDITOR
    struct ShadersEditor
    {
        int glsl_version{ 1 };
        bool size_dependent_on_zoom{ true };
        int fixed_size{ 16 };
        std::array<int, 2> sizes{ 8, 64 };
        int percent_outline{ 15 };
        int percent_center{ 15 };
    };
#endif // ENABLE_GCODE_VIEWER_SHADERS_EDITOR

public:
    struct SequentialView
    {
        class Marker
        {
            GL_Model m_model;
            Transform3f m_world_transform;
            BoundingBoxf3 m_world_bounding_box;
            std::array<float, 4> m_color{ 1.0f, 1.0f, 1.0f, 1.0f };
            bool m_visible{ false };

        public:
            void init();

            const BoundingBoxf3& get_bounding_box() const { return m_world_bounding_box; }

            void set_world_position(const Vec3f& position);
            void set_color(const std::array<float, 4>& color) { m_color = color; }

            bool is_visible() const { return m_visible; }
            void set_visible(bool visible) { m_visible = visible; }

            void render() const;
        };

        struct Endpoints
        {
            unsigned int first{ 0 };
            unsigned int last{ 0 };
        };

        Endpoints endpoints;
        Endpoints current;
        Vec3f current_position{ Vec3f::Zero() };
        Marker marker;
    };

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
    std::vector<Color> m_tool_colors;
    std::vector<double> m_layers_zs;
    std::array<double, 2> m_layers_z_range;
    std::vector<ExtrusionRole> m_roles;
    std::vector<unsigned char> m_extruder_ids;
    mutable Extrusions m_extrusions;
    mutable SequentialView m_sequential_view;
    float m_sequential_view_marker_z_offset{ 0.5f };
    Shells m_shells;
    EViewType m_view_type{ EViewType::FeatureType };
    bool m_legend_enabled{ true };
#if ENABLE_GCODE_VIEWER_STATISTICS
    mutable Statistics m_statistics;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
#if ENABLE_GCODE_VIEWER_SHADERS_EDITOR
    mutable ShadersEditor m_shaders_editor;
#endif // ENABLE_GCODE_VIEWER_SHADERS_EDITOR
    std::array<float, 2> m_detected_point_sizes = { 0.0f, 0.0f };

public:
    GCodeViewer() = default;
    ~GCodeViewer() { reset(); }

    bool init();

    // extract rendering data from the given parameters
    void load(const GCodeProcessor::Result& gcode_result, const Print& print, bool initialized);
    // recalculate ranges in dependence of what is visible and sets tool/print colors
    void refresh(const GCodeProcessor::Result& gcode_result, const std::vector<std::string>& str_tool_colors);

    void reset();
    void render() const;

    bool has_data() const { return !m_roles.empty(); }

    const BoundingBoxf3& get_bounding_box() const { return m_bounding_box; }
    const std::vector<double>& get_layers_zs() const { return m_layers_zs; };

    const SequentialView& get_sequential_view() const { return m_sequential_view; }
    void update_sequential_view_current(unsigned int first, unsigned int last)
    {
        m_sequential_view.current.first = first;
        m_sequential_view.current.last = last;
        refresh_render_paths(true, true);
    }

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
    void set_layers_z_range(const std::array<double, 2>& layers_z_range);

    bool is_legend_enabled() const { return m_legend_enabled; }
    void enable_legend(bool enable) { m_legend_enabled = enable; }

private:
    void init_shaders();
    void load_toolpaths(const GCodeProcessor::Result& gcode_result);
    void load_shells(const Print& print, bool initialized);
    void refresh_render_paths(bool keep_sequential_current_first, bool keep_sequential_current_last) const;
    void render_toolpaths() const;
    void render_shells() const;
    void render_legend() const;
#if ENABLE_GCODE_VIEWER_STATISTICS
    void render_statistics() const;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
#if ENABLE_GCODE_VIEWER_SHADERS_EDITOR
    void render_shaders_editor() const;
#endif // ENABLE_GCODE_VIEWER_SHADERS_EDITOR
    bool is_visible(ExtrusionRole role) const {
        return role < erCount && (m_extrusions.role_visibility_flags & (1 << role)) != 0;
    }
    bool is_visible(const Path& path) const { return is_visible(path.role); }
    bool is_in_z_range(const Path& path) const {
        auto in_z_range = [this](double z) {
            return z > m_layers_z_range[0] - EPSILON && z < m_layers_z_range[1] + EPSILON;
        };

        return in_z_range(path.first.position[2]) || in_z_range(path.last.position[2]);
    }
    bool is_travel_in_z_range(size_t id) const;
};

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_GCODE_VIEWER

#endif // slic3r_GCodeViewer_hpp_

