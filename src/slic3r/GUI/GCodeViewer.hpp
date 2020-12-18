#ifndef slic3r_GCodeViewer_hpp_
#define slic3r_GCodeViewer_hpp_

#include "3DScene.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "GLModel.hpp"

#include <cstdint>
#include <float.h>

namespace Slic3r {

class Print;
class TriangleMesh;

namespace GUI {

class GCodeViewer
{
    using Color = std::array<float, 3>;
    using IndexBuffer = std::vector<unsigned int>;
    using MultiIndexBuffer = std::vector<IndexBuffer>;

    static const std::vector<Color> Extrusion_Role_Colors;
    static const std::vector<Color> Options_Colors;
    static const std::vector<Color> Travel_Colors;
    static const Color              Wipe_Color;
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

    // vbo buffer containing vertices data used to rendder a specific toolpath type
    struct VBuffer
    {
        enum class EFormat : unsigned char
        {
            // vertex format: 3 floats -> position.x|position.y|position.z
            Position,
            // vertex format: 4 floats -> position.x|position.y|position.z|normal.x
            PositionNormal1,
            // vertex format: 6 floats -> position.x|position.y|position.z|normal.x|normal.y|normal.z
            PositionNormal3
        };

        EFormat format{ EFormat::Position };
        // vbo id
        unsigned int id{ 0 };
        // count of vertices, updated after data are sent to gpu
        size_t count{ 0 };

        size_t data_size_bytes() const { return count * vertex_size_bytes(); }

        size_t vertex_size_floats() const { return position_size_floats() + normal_size_floats(); }
        size_t vertex_size_bytes() const { return vertex_size_floats() * sizeof(float); }

        size_t position_offset_floats() const { return 0; }
        size_t position_offset_size() const { return position_offset_floats() * sizeof(float); }
        size_t position_size_floats() const
        {
            switch (format)
            {
            case EFormat::Position:
            case EFormat::PositionNormal3: { return 3; }
            case EFormat::PositionNormal1: { return 4; }
            default: { return 0; }
            }
        }
        size_t position_size_bytes() const { return position_size_floats() * sizeof(float); }

        size_t normal_offset_floats() const
        {
            switch (format)
            {
            case EFormat::Position:
            case EFormat::PositionNormal1: { return 0; }
            case EFormat::PositionNormal3: { return 3; }
            default: { return 0; }
            }
        }
        size_t normal_offset_size() const { return normal_offset_floats() * sizeof(float); }
        size_t normal_size_floats() const {
            switch (format)
            {
            default:
            case EFormat::Position:
            case EFormat::PositionNormal1: { return 0; }
            case EFormat::PositionNormal3: { return 3; }
            }
        }
        size_t normal_size_bytes() const { return normal_size_floats() * sizeof(float); }

        void reset();
    };

    // ibo buffer containing indices data (lines/triangles) used to render a specific toolpath type
    struct IBuffer
    {
        // ibo id
        unsigned int id{ 0 };
        // count of indices, updated after data are sent to gpu
        size_t count{ 0 };

        void reset();
    };

    // Used to identify different toolpath sub-types inside a IBuffer
    struct Path
    {
        struct Endpoint
        {
            // index of the index buffer
            unsigned int b_id{ 0 };
            // index into the index buffer
            size_t i_id{ 0 };
            // sequential id (index into the vertex buffer)
            size_t s_id{ 0 };
            Vec3f position{ Vec3f::Zero() };
        };

        EMoveType type{ EMoveType::Noop };
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
        size_t vertices_count() const { return last.s_id - first.s_id + 1; }
        bool contains(size_t id) const { return first.s_id <= id && id <= last.s_id; }
    };

    // Used to batch the indices needed to render paths
    struct RenderPath
    {
        Color color;
        unsigned int path_id;
        unsigned int index_buffer_id;
        std::vector<unsigned int> sizes;
        std::vector<size_t> offsets; // use size_t because we need an unsigned int whose size matches pointer's size (used in the call glMultiDrawElements())
    };

    // buffer containing data for rendering a specific toolpath type
    struct TBuffer
    {
        enum class ERenderPrimitiveType : unsigned char
        {
            Point,
            Line,
            Triangle
        };

        ERenderPrimitiveType render_primitive_type;
        VBuffer vertices;
        std::vector<IBuffer> indices;

        std::string shader;
        std::vector<Path> paths;
        std::vector<RenderPath> render_paths;
        bool visible{ false };

        void reset();
        // b_id index of buffer contained in this->indices
        // i_id index of first index contained in this->indices[b_id]
        // s_id index of first vertex contained in this->vertices
        void add_path(const GCodeProcessor::MoveVertex& move, unsigned int b_id, size_t i_id, size_t s_id);
        unsigned int indices_per_segment() const {
            switch (render_primitive_type)
            {
            case ERenderPrimitiveType::Point:    { return 1; }
            case ERenderPrimitiveType::Line:     { return 2; }
            case ERenderPrimitiveType::Triangle: { return 42; } // 3 indices x 14 triangles
            default:                             { return 0; }
            }
        }
        unsigned int start_segment_vertex_offset() const {
            switch (render_primitive_type)
            {
            case ERenderPrimitiveType::Point:
            case ERenderPrimitiveType::Line: 
            case ERenderPrimitiveType::Triangle:
            default: { return 0; }
            }
        }
        unsigned int end_segment_vertex_offset() const {
            switch (render_primitive_type)
            {
            case ERenderPrimitiveType::Point:    { return 0; }
            case ERenderPrimitiveType::Line:     { return 1; }
            case ERenderPrimitiveType::Triangle: { return 36; } // 1 vertex of 13th triangle
            default:                             { return 0; }
            }
        }

        bool has_data() const { return vertices.id != 0 && !indices.empty() && indices.front().id != 0; }
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
            unsigned int count;

            Range() { reset(); }

            void update_from(const float value) {
                if (value != max && value != min)
                    ++count;
                min = std::min(min, value);
                max = std::max(max, value);
            }
            void reset() { min = FLT_MAX; max = -FLT_MAX; count = 0; }

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
            for (unsigned int i = 0; i < erCount; ++i) {
                role_visibility_flags |= 1 << i;
            }
        }

        void reset_ranges() { ranges.reset(); }
    };

    class Layers
    {
    public:
        struct Endpoints
        {
            size_t first{ 0 };
            size_t last{ 0 };
        };

    private:
        std::vector<double> m_zs;
        std::vector<Endpoints> m_endpoints;

    public:
        void append(double z, Endpoints endpoints)
        {
            m_zs.emplace_back(z);
            m_endpoints.emplace_back(endpoints);
        }

        void reset()
        {
            m_zs = std::vector<double>();
            m_endpoints = std::vector<Endpoints>();
        }

        size_t size() const { return m_zs.size(); }
        bool empty() const { return m_zs.empty(); }
        const std::vector<double>& get_zs() const { return m_zs; }
        const std::vector<Endpoints>& get_endpoints() const { return m_endpoints; }
        std::vector<Endpoints>& get_endpoints() { return m_endpoints; }
        double get_z_at(unsigned int id) const { return (id < m_zs.size()) ? m_zs[id] : 0.0; }
        Endpoints get_endpoints_at(unsigned int id) const { return (id < m_endpoints.size()) ? m_endpoints[id] : Endpoints(); }
    };

#if ENABLE_GCODE_VIEWER_STATISTICS
    struct Statistics
    {
        // time
        int64_t results_time{ 0 };
        int64_t load_time{ 0 };
        int64_t refresh_time{ 0 };
        int64_t refresh_paths_time{ 0 };
        // opengl calls
        int64_t gl_multi_points_calls_count{ 0 };
        int64_t gl_multi_lines_calls_count{ 0 };
        int64_t gl_multi_triangles_calls_count{ 0 };
        // memory
        int64_t results_size{ 0 };
        int64_t total_vertices_gpu_size{ 0 };
        int64_t total_indices_gpu_size{ 0 };
        int64_t max_vbuffer_gpu_size{ 0 };
        int64_t max_ibuffer_gpu_size{ 0 };
        int64_t paths_size{ 0 };
        int64_t render_paths_size{ 0 };
        // other
        int64_t travel_segments_count{ 0 };
        int64_t wipe_segments_count{ 0 };
        int64_t extrude_segments_count{ 0 };
        int64_t vbuffers_count{ 0 };
        int64_t ibuffers_count{ 0 };
        int64_t max_vertices_in_vertex_buffer{ 0 };
        int64_t max_indices_in_index_buffer{ 0 };

        void reset_all() {
            reset_times();
            reset_opengl();
            reset_sizes();
            reset_others();
        }

        void reset_times() {
            results_time = 0;
            load_time = 0;
            refresh_time = 0;
            refresh_paths_time = 0;
        }

        void reset_opengl() {
            gl_multi_points_calls_count = 0;
            gl_multi_lines_calls_count = 0;
            gl_multi_triangles_calls_count = 0;
        }

        void reset_sizes() {
            results_size = 0;
            total_vertices_gpu_size = 0;
            total_indices_gpu_size = 0;
            max_vbuffer_gpu_size = 0;
            max_ibuffer_gpu_size = 0;
            paths_size = 0;
            render_paths_size = 0;
        }

        void reset_others() {
            travel_segments_count = 0;
            wipe_segments_count = 0;
            extrude_segments_count =  0;
            vbuffers_count = 0;
            ibuffers_count = 0;
            max_vertices_in_vertex_buffer = 0;
            max_indices_in_index_buffer = 0;
        }
    };
#endif // ENABLE_GCODE_VIEWER_STATISTICS

public:
    struct SequentialView
    {
        class Marker
        {
            GLModel m_model;
            Vec3f m_world_position;
            Transform3f m_world_transform;
            float m_z_offset{ 0.5f };
            std::array<float, 4> m_color{ 1.0f, 1.0f, 1.0f, 0.5f };
            bool m_visible{ true };

        public:
            void init();

            const BoundingBoxf3& get_bounding_box() const { return m_model.get_bounding_box(); }

            void set_world_position(const Vec3f& position);
            void set_color(const std::array<float, 4>& color) { m_color = color; }

            bool is_visible() const { return m_visible; }
            void set_visible(bool visible) { m_visible = visible; }

            void render() const;
        };

        struct Endpoints
        {
            size_t first{ 0 };
            size_t last{ 0 };
        };

        bool skip_invisible_moves{ false };
        Endpoints endpoints;
        Endpoints current;
        Endpoints last_current;
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
    bool m_initialized{ false };
    mutable bool m_gl_data_initialized{ false };
    unsigned int m_last_result_id{ 0 };
    size_t m_moves_count{ 0 };
    mutable std::vector<TBuffer> m_buffers{ static_cast<size_t>(EMoveType::Extrude) };
    // bounding box of toolpaths
    BoundingBoxf3 m_paths_bounding_box;
    // bounding box of toolpaths + marker tools
    BoundingBoxf3 m_max_bounding_box;
    std::vector<Color> m_tool_colors;
    Layers m_layers;
    std::array<unsigned int, 2> m_layers_z_range;
    std::vector<ExtrusionRole> m_roles;
    size_t m_extruders_count;
    std::vector<unsigned char> m_extruder_ids;
    mutable Extrusions m_extrusions;
    mutable SequentialView m_sequential_view;
    Shells m_shells;
    EViewType m_view_type{ EViewType::FeatureType };
    bool m_legend_enabled{ true };
    PrintEstimatedTimeStatistics m_time_statistics;
    mutable PrintEstimatedTimeStatistics::ETimeMode m_time_estimate_mode{ PrintEstimatedTimeStatistics::ETimeMode::Normal };
#if ENABLE_GCODE_VIEWER_STATISTICS
    mutable Statistics m_statistics;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
    mutable std::array<float, 2> m_detected_point_sizes = { 0.0f, 0.0f };
    GCodeProcessor::Result::SettingsIds m_settings_ids;

public:
    GCodeViewer() = default;
    ~GCodeViewer() { reset(); }

    // extract rendering data from the given parameters
    void load(const GCodeProcessor::Result& gcode_result, const Print& print, bool initialized);
    // recalculate ranges in dependence of what is visible and sets tool/print colors
    void refresh(const GCodeProcessor::Result& gcode_result, const std::vector<std::string>& str_tool_colors);
#if ENABLE_RENDER_PATH_REFRESH_AFTER_OPTIONS_CHANGE
    void refresh_render_paths();
#endif // ENABLE_RENDER_PATH_REFRESH_AFTER_OPTIONS_CHANGE
    void update_shells_color_by_extruder(const DynamicPrintConfig* config);

    void reset();
    void render() const;

    bool has_data() const { return !m_roles.empty(); }

    const BoundingBoxf3& get_paths_bounding_box() const { return m_paths_bounding_box; }
    const BoundingBoxf3& get_max_bounding_box() const { return m_max_bounding_box; }
    const std::vector<double>& get_layers_zs() const { return m_layers.get_zs(); };

    const SequentialView& get_sequential_view() const { return m_sequential_view; }
    void update_sequential_view_current(unsigned int first, unsigned int last);

    EViewType get_view_type() const { return m_view_type; }
    void set_view_type(EViewType type) {
        if (type == EViewType::Count)
            type = EViewType::FeatureType;

        m_view_type = type;
    }

    bool is_toolpath_move_type_visible(EMoveType type) const;
    void set_toolpath_move_type_visible(EMoveType type, bool visible);
    unsigned int get_toolpath_role_visibility_flags() const { return m_extrusions.role_visibility_flags; }
    void set_toolpath_role_visibility_flags(unsigned int flags) { m_extrusions.role_visibility_flags = flags; }
    unsigned int get_options_visibility_flags() const;
    void set_options_visibility_from_flags(unsigned int flags);
    void set_layers_z_range(const std::array<unsigned int, 2>& layers_z_range);

    bool is_legend_enabled() const { return m_legend_enabled; }
    void enable_legend(bool enable) { m_legend_enabled = enable; }

    void export_toolpaths_to_obj(const char* filename) const;

private:
    void init();
    void load_toolpaths(const GCodeProcessor::Result& gcode_result);
    void load_shells(const Print& print, bool initialized);
    void refresh_render_paths(bool keep_sequential_current_first, bool keep_sequential_current_last) const;
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
    void log_memory_used(const std::string& label, int64_t additional = 0) const;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GCodeViewer_hpp_

