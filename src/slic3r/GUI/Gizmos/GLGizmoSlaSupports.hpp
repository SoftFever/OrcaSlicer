#ifndef slic3r_GLGizmoSlaSupports_hpp_
#define slic3r_GLGizmoSlaSupports_hpp_

#include "GLGizmoBase.hpp"

// There is an L function in igl that would be overridden by our localization macro - let's undefine it...
#undef L
#include <igl/AABB.h>
#include "slic3r/GUI/I18N.hpp"  // ...and redefine again when we are done with the igl code

#include "libslic3r/SLA/SLACommon.hpp"
#include "libslic3r/SLAPrint.hpp"


namespace Slic3r {
namespace GUI {


class GLGizmoSlaSupports : public GLGizmoBase
{
private:
    ModelObject* m_model_object = nullptr;
    ModelObject* m_old_model_object = nullptr;
    int m_active_instance = -1;
    BoundingBoxf3 m_active_instance_bb; // to cache the bb
    std::pair<Vec3f, Vec3f> unproject_on_mesh(const Vec2d& mouse_pos);

    const float RenderPointScale = 1.f;

    GLUquadricObj* m_quadric;
    Eigen::MatrixXf m_V; // vertices
    Eigen::MatrixXi m_F; // facets indices
    igl::AABB<Eigen::MatrixXf,3> m_AABB;
    TriangleMesh m_mesh;
    mutable std::vector<Vec2f> m_triangles;

    class CacheEntry {
    public:
        CacheEntry(const sla::SupportPoint& point, bool sel, const Vec3f& norm = Vec3f::Zero()) :
            support_point(point), selected(sel), normal(norm) {}

        sla::SupportPoint support_point;
        bool selected; // whether the point is selected
        Vec3f normal;
    };

public:
#if ENABLE_SVG_ICONS
    GLGizmoSlaSupports(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
#else
    GLGizmoSlaSupports(GLCanvas3D& parent, unsigned int sprite_id);
#endif // ENABLE_SVG_ICONS
    virtual ~GLGizmoSlaSupports();
    void set_sla_support_data(ModelObject* model_object, const Selection& selection);
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down);
    void delete_selected_points(bool force = false);
    GLCanvas3D::ClippingPlane get_sla_clipping_plane() const;

private:
    bool on_init();
    void on_update(const UpdateData& data, const Selection& selection);
    virtual void on_render(const Selection& selection) const;
    virtual void on_render_for_picking(const Selection& selection) const;

    void render_selection_rectangle() const;
    void render_points(const Selection& selection, const Vec3d& direction_to_camera, bool picking = false) const;
    void render_clipping_plane(const Selection& selection, const Vec3d& direction_to_camera) const;
    bool is_mesh_update_necessary() const;
    void update_mesh();
    void update_cache_entry_normal(unsigned int i) const;

    bool m_lock_unique_islands = false;
    bool m_editing_mode = false;            // Is editing mode active?
    bool m_old_editing_state = false;       // To keep track of whether the user toggled between the modes (needed for imgui refreshes).
    float m_new_point_head_diameter;        // Size of a new point.
    float m_minimal_point_distance = 20.f;
    float m_density = 100.f;
    mutable std::vector<CacheEntry> m_editing_mode_cache; // a support point and whether it is currently selected
    float m_clipping_plane_distance = 0.f;
    mutable float m_old_clipping_plane_distance = 0.f;
    mutable Vec3d m_old_direction_to_camera;

    bool m_selection_rectangle_active = false;
    Vec2d m_selection_rectangle_start_corner;
    Vec2d m_selection_rectangle_end_corner;
    bool m_wait_for_up_event = false;
    bool m_unsaved_changes = false; // Are there unsaved changes in manual mode?
    bool m_selection_empty = true;
    EState m_old_state = Off; // to be able to see that the gizmo has just been closed (see on_set_state)
    int m_canvas_width;
    int m_canvas_height;

    mutable std::unique_ptr<TriangleMeshSlicer> m_tms;

    std::vector<const ConfigOption*> get_config_options(const std::vector<std::string>& keys) const;
    bool is_point_clipped(const Vec3d& point, const Vec3d& direction_to_camera, float z_shift) const;
    void find_intersecting_facets(const igl::AABB<Eigen::MatrixXf, 3>* aabb, const Vec3f& normal, double offset, std::vector<unsigned int>& out) const;

    // Methods that do the model_object and editing cache synchronization,
    // editing mode selection, etc:
    enum {
        AllPoints = -2,
        NoPoints,
    };
    void select_point(int i);
    void unselect_point(int i);
    void editing_mode_apply_changes();
    void editing_mode_discard_changes();
    void editing_mode_reload_cache();
    void get_data_from_backend();
    void auto_generate();
    void switch_to_editing_mode();

protected:
    void on_set_state() override;
    void on_start_dragging(const Selection& selection) override;
    virtual void on_render_input_window(float x, float y, float bottom_limit, const Selection& selection) override;

    virtual std::string on_get_name() const;
    virtual bool on_is_activable(const Selection& selection) const;
    virtual bool on_is_selectable() const;
};


} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoSlaSupports_hpp_
