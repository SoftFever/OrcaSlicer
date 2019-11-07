#ifndef slic3r_GLGizmoHollow_hpp_
#define slic3r_GLGizmoHollow_hpp_

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/GLSelectionRectangle.hpp"

#include "libslic3r/SLA/SLACommon.hpp"
#include <wx/dialog.h>

#include <cereal/types/vector.hpp>


namespace Slic3r {
namespace GUI {

class ClippingPlane;
class MeshClipper;
class MeshRaycaster;
enum class SLAGizmoEventType : unsigned char;

class GLGizmoHollow : public GLGizmoBase
{
private:
    ModelObject* m_model_object = nullptr;
    ObjectID m_model_object_id = 0;
    int m_active_instance = -1;
    float m_active_instance_bb_radius; // to cache the bb
    mutable double m_z_shift = 0.f;
    bool unproject_on_mesh(const Vec2d& mouse_pos, std::pair<Vec3f, Vec3f>& pos_and_normal);

    const float RenderPointScale = 1.f;

    GLUquadricObj* m_quadric;

    std::unique_ptr<MeshRaycaster> m_mesh_raycaster;
    std::unique_ptr<TriangleMesh> m_cavity_mesh;
    std::unique_ptr<GLVolume> m_volume_with_cavity;
    const TriangleMesh* m_mesh;
    const indexed_triangle_set* m_its;
    mutable const TriangleMesh* m_supports_mesh;
    mutable std::vector<Vec2f> m_triangles;
    mutable std::vector<Vec2f> m_supports_triangles;
    mutable int m_old_timestamp = -1;
    mutable int m_print_object_idx = -1;
    mutable int m_print_objects_count = -1;

    class CacheEntry {
    public:
        CacheEntry() :
            support_point(sla::SupportPoint()), selected(false), normal(Vec3f::Zero()) {}

        CacheEntry(const sla::SupportPoint& point, bool sel = false, const Vec3f& norm = Vec3f::Zero()) :
            support_point(point), selected(sel), normal(norm) {}

        bool operator==(const CacheEntry& rhs) const {
            return (support_point == rhs.support_point);
        }

        bool operator!=(const CacheEntry& rhs) const {
            return ! ((*this) == rhs);
        }

        sla::SupportPoint support_point;
        bool selected; // whether the point is selected
        Vec3f normal;

        template<class Archive>
        void serialize(Archive & ar)
        {
            ar(support_point, selected, normal);
        }
    };

public:
    GLGizmoHollow(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    ~GLGizmoHollow() override;
    void set_sla_support_data(ModelObject* model_object, const Selection& selection);
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);
    void delete_selected_points(bool force = false);
    ClippingPlane get_sla_clipping_plane() const;

    bool is_selection_rectangle_dragging() const { return m_selection_rectangle.is_dragging(); }

private:
    bool on_init() override;
    void on_update(const UpdateData& data) override;
    void on_render() const override;
    void on_render_for_picking() const override;

    //void render_selection_rectangle() const;
    void render_points(const Selection& selection, bool picking = false) const;
    void render_clipping_plane(const Selection& selection) const;
    bool is_mesh_update_necessary() const;
    void update_mesh();
    void hollow_mesh();
    bool unsaved_changes() const;
    const TriangleMesh* mesh() const;

    bool m_editing_mode = true;            // Is editing mode active?
    bool m_old_editing_state = false;       // To keep track of whether the user toggled between the modes (needed for imgui refreshes).
    float m_new_point_head_diameter;        // Size of a new point.
    float m_new_cone_angle = 0.f;
    float m_new_cone_height = 5.f;
    CacheEntry m_point_before_drag;         // undo/redo - so we know what state was edited
    float m_old_point_head_diameter = 0.;   // the same
    float m_minimal_point_distance_stash = 0.f; // and again
    float m_density_stash = 0.f;                // and again
    mutable std::vector<CacheEntry> m_editing_cache; // a support point and whether it is currently selected
    std::vector<sla::SupportPoint> m_normal_cache; // to restore after discarding changes or undo/redo

    float m_offset = 2.0f;

    float m_clipping_plane_distance = 0.f;
    std::unique_ptr<ClippingPlane> m_clipping_plane;
    
    float m_accuracy = 0.5f;

    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    std::map<std::string, wxString> m_desc;
    
    
    float m_smoothness = 0.5f;

    GLSelectionRectangle m_selection_rectangle;

    bool m_wait_for_up_event = false;
    bool m_selection_empty = true;
    EState m_old_state = Off; // to be able to see that the gizmo has just been closed (see on_set_state)

    mutable std::unique_ptr<MeshClipper> m_object_clipper;
    mutable std::unique_ptr<MeshClipper> m_supports_clipper;

    std::vector<const ConfigOption*> get_config_options(const std::vector<std::string>& keys) const;
    bool is_mesh_point_clipped(const Vec3d& point) const;
    //void find_intersecting_facets(const igl::AABB<Eigen::MatrixXf, 3>* aabb, const Vec3f& normal, double offset, std::vector<unsigned int>& out) const;

    // Methods that do the model_object and editing cache synchronization,
    // editing mode selection, etc:
    enum {
        AllPoints = -2,
        NoPoints,
    };
    void select_point(int i);
    void unselect_point(int i);
    void reload_cache();
    void update_clipping_plane(bool keep_normal = false) const;

protected:
    void on_set_state() override;
    void on_set_hover_id() override

    {
        if (! m_editing_mode || (int)m_editing_cache.size() <= m_hover_id)
            m_hover_id = -1;
    }
    void on_start_dragging() override;
    void on_stop_dragging() override;
    void on_render_input_window(float x, float y, float bottom_limit) override;

    std::string on_get_name() const override;
    bool on_is_activable() const override;
    bool on_is_selectable() const override;
    void on_load(cereal::BinaryInputArchive& ar) override;
    void on_save(cereal::BinaryOutputArchive& ar) const override;
};



} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoHollow_hpp_
