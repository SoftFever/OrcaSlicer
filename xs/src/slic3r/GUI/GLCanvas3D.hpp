#ifndef slic3r_GLCanvas3D_hpp_
#define slic3r_GLCanvas3D_hpp_

#include "../../libslic3r/BoundingBox.hpp"
#include "../../libslic3r/Utils.hpp"
#include "../../libslic3r/ExPolygon.hpp"

class wxGLCanvas;
class wxGLContext;
class wxSizeEvent;
class wxIdleEvent;
class wxKeyEvent;

namespace Slic3r {

class GLVolumeCollection;
class DynamicPrintConfig;
class GLShader;
class ExPolygon;

namespace GUI {

class GeometryBuffer
{
    std::vector<float> m_data;

public:
    bool set_from_triangles(const Polygons& triangles, float z);
    bool set_from_lines(const Lines& lines, float z);

    const float* get_data() const;
    unsigned int get_data_size() const;
};

class GLCanvas3D
{
public:
    class Camera
    {
    public:
        enum EType : unsigned char
        {
            CT_Unknown,
            CT_Perspective,
            CT_Ortho,
            CT_Count
        };

    private:
        EType m_type;
        float m_zoom;
        float m_phi;
        float m_theta;
        float m_distance;
        Pointf3 m_target;

    public:
        Camera();

        Camera::EType get_type() const;
        void set_type(Camera::EType type);
        std::string get_type_as_string() const;

        float get_zoom() const;
        void set_zoom(float zoom);

        float get_phi() const;
        void set_phi(float phi);

        float get_theta() const;
        void set_theta(float theta);

        float get_distance() const;
        void set_distance(float distance);

        const Pointf3& get_target() const;
        void set_target(const Pointf3& target);
    };

    class Bed
    {
        Pointfs m_shape;
        BoundingBoxf3 m_bounding_box;
        Polygon m_polygon;
        GeometryBuffer m_triangles;
        GeometryBuffer m_gridlines;

    public:
        const Pointfs& get_shape() const;
        void set_shape(const Pointfs& shape);

        const BoundingBoxf3& get_bounding_box() const;

        void render() const;

    private:
        void _calc_bounding_box();
        void _calc_triangles(const ExPolygon& poly);
        void _calc_gridlines(const ExPolygon& poly, const BoundingBox& bed_bbox);
    };

    class Axes
    {
        Pointf3 m_origin;
        float m_length;

    public:
        Axes();

        const Pointf3& get_origin() const;
        void set_origin(const Pointf3& origin);

        float get_length() const;
        void set_length(float length);

        void render() const;
    };

    class CuttingPlane
    {
        float m_z;
        GeometryBuffer m_lines;

    public:
        CuttingPlane();

        bool set(float z, const ExPolygons& polygons);

        void render(const BoundingBoxf3& bb) const;

    private:
        void _render_plane(const BoundingBoxf3& bb) const;
        void _render_contour() const;
    };

    class LayersEditing
    {
        bool m_enabled;

    public:
        LayersEditing();

        bool is_enabled() const;
    };

    class Shader
    {
        bool m_enabled;
        GLShader* m_shader;

    public:
        Shader();

        bool init(const std::string& vertex_shader_filename, const std::string& fragment_shader_filename);
        void reset();

        bool is_enabled() const;
        void set_enabled(bool enabled);

        bool start() const;
        void stop() const;
    };

    class Mouse
    {
        bool m_dragging;
        Pointf m_position;

    public:
        Mouse();

        bool is_dragging() const;
        void set_dragging(bool dragging);

        const Pointf& get_position() const;
        void set_position(const Pointf& position);
    };

private:
    wxGLCanvas* m_canvas;
    wxGLContext* m_context;
    Camera m_camera;
    Bed m_bed;
    Axes m_axes;
    CuttingPlane m_cutting_plane;
    LayersEditing m_layers_editing;
    Shader m_shader;
    Mouse m_mouse;

    GLVolumeCollection* m_volumes;
    DynamicPrintConfig* m_config;

    bool m_dirty;
    bool m_apply_zoom_to_volumes_filter;
    int m_hover_volume_id;
    bool m_warning_texture_enabled;
    bool m_legend_texture_enabled;
    bool m_picking_enabled;
    bool m_multisample_allowed;

    PerlCallback m_on_viewport_changed_callback;
    PerlCallback m_on_mark_volumes_for_layer_height_callback;

public:
    GLCanvas3D(wxGLCanvas* canvas, wxGLContext* context);
    ~GLCanvas3D();

    bool init(bool useVBOs);

    bool set_current();

    bool is_dirty() const;
    void set_dirty(bool dirty);

    bool is_shown_on_screen() const;

    void resize(unsigned int w, unsigned int h);

    GLVolumeCollection* get_volumes();
    void set_volumes(GLVolumeCollection* volumes);
    void reset_volumes();

    DynamicPrintConfig* get_config();
    void set_config(DynamicPrintConfig* config);

    // Set the bed shape to a single closed 2D polygon(array of two element arrays),
    // triangulate the bed and store the triangles into m_bed.m_triangles,
    // fills the m_bed.m_grid_lines and sets m_bed.m_origin.
    // Sets m_bed.m_polygon to limit the object placement.
    void set_bed_shape(const Pointfs& shape);
    // Used by ObjectCutDialog and ObjectPartsPanel to generate a rectangular ground plane to support the scene objects.
    void set_auto_bed_shape();

    const Pointf3& get_axes_origin() const;
    void set_axes_origin(const Pointf3& origin);

    float get_axes_length() const;
    void set_axes_length(float length);

    void set_cutting_plane(float z, const ExPolygons& polygons);

    Camera::EType get_camera_type() const;
    void set_camera_type(Camera::EType type);
    std::string get_camera_type_as_string() const;

    float get_camera_zoom() const;
    void set_camera_zoom(float zoom);

    float get_camera_phi() const;
    void set_camera_phi(float phi);

    float get_camera_theta() const;
    void set_camera_theta(float theta);

    float get_camera_distance() const;
    void set_camera_distance(float distance);

    const Pointf3& get_camera_target() const;
    void set_camera_target(const Pointf3& target);

    BoundingBoxf3 bed_bounding_box() const;
    BoundingBoxf3 volumes_bounding_box() const;
    BoundingBoxf3 max_bounding_box() const;

    bool is_layers_editing_enabled() const;
    bool is_picking_enabled() const;
    bool is_shader_enabled() const;
    bool is_multisample_allowed() const;

    void enable_warning_texture(bool enable);
    void enable_legend_texture(bool enable);
    void enable_picking(bool enable);
    void enable_shader(bool enable);
    void allow_multisample(bool allow);

    bool is_mouse_dragging() const;
    void set_mouse_dragging(bool dragging);

    const Pointf& get_mouse_position() const;
    void set_mouse_position(const Pointf& position);

    int get_hover_volume_id() const;
    void set_hover_volume_id(int id);

    void zoom_to_bed();
    void zoom_to_volumes();
    void select_view(const std::string& direction);

    bool start_using_shader() const;
    void stop_using_shader() const;

    void picking_pass();

    void render_background() const;
    void render_bed() const;
    void render_axes() const;
    void render_volumes(bool fake_colors) const;
    void render_objects(bool useVBOs);
    void render_cutting_plane() const;
    void render_warning_texture() const;
    void render_legend_texture() const;

    void render_texture(unsigned int tex_id, float left, float right, float bottom, float top) const;

    void register_on_viewport_changed_callback(void* callback);
    void register_on_mark_volumes_for_layer_height_callback(void* callback);

    void on_size(wxSizeEvent& evt);
    void on_idle(wxIdleEvent& evt);
    void on_char(wxKeyEvent& evt);

private:
    void _zoom_to_bounding_box(const BoundingBoxf3& bbox);
    std::pair<int, int> _get_canvas_size() const;
    float _get_zoom_to_bounding_box_factor(const BoundingBoxf3& bbox) const;

    void _deregister_callbacks();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLCanvas3D_hpp_
