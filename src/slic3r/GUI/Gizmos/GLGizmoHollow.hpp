#ifndef slic3r_GLGizmoHollow_hpp_
#define slic3r_GLGizmoHollow_hpp_

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/GLSelectionRectangle.hpp"

#include <libslic3r/SLA/Hollowing.hpp>
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
    mutable double m_z_shift = 0.;
    bool unproject_on_mesh(const Vec2d& mouse_pos, std::pair<Vec3f, Vec3f>& pos_and_normal);

    const float HoleStickOutLength = 1.f;

    GLUquadricObj* m_quadric;

    std::unique_ptr<MeshRaycaster> m_mesh_raycaster;
    std::unique_ptr<TriangleMesh> m_cavity_mesh;
    std::unique_ptr<GLVolume> m_volume_with_cavity;
    const TriangleMesh* m_mesh;
    mutable const TriangleMesh* m_supports_mesh;
    mutable std::vector<Vec2f> m_triangles;
    mutable std::vector<Vec2f> m_supports_triangles;
    mutable int m_old_timestamp = -1;
    mutable int m_print_object_idx = -1;
    mutable int m_print_objects_count = -1;

    class CacheEntry {
    public:
        CacheEntry() :
            drain_hole(sla::DrainHole()), selected(false) {}

        CacheEntry(const sla::DrainHole& point, bool sel = false) :
            drain_hole(point), selected(sel) {}

        bool operator==(const CacheEntry& rhs) const {
            return (drain_hole == rhs.drain_hole);
        }

        bool operator!=(const CacheEntry& rhs) const {
            return ! ((*this) == rhs);
        }

        sla::DrainHole drain_hole;
        bool selected; // whether the point is selected

        template<class Archive>
        void serialize(Archive & ar)
        {
            ar(drain_hole, selected);
        }
    };

public:
    GLGizmoHollow(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    ~GLGizmoHollow() override;
    void set_sla_support_data(ModelObject* model_object, const Selection& selection);
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);
    void delete_selected_points(bool force = false);
    ClippingPlane get_sla_clipping_plane() const;
    
    
    std::pair<const TriangleMesh *, sla::HollowingConfig> get_hollowing_parameters() const;
    void update_mesh_raycaster(std::unique_ptr<MeshRaycaster> &&rc);
    void update_hollowed_mesh(std::unique_ptr<TriangleMesh> &&mesh);

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

    bool  m_show_supports = true;
    float m_new_hole_radius;        // Size of a new hole.
    float m_new_hole_height = 5.f;
    float m_old_hole_radius = 0.;   // undo/redo - so we know what state was edited
    Vec3f m_hole_before_drag = Vec3f::Zero();
    float m_minimal_point_distance_stash = 0.f; // and again
    float m_density_stash = 0.f;                // and again
    mutable std::vector<bool> m_selected; // which holes are currently selected

    float m_offset = 2.0f;
    float m_accuracy = 0.5f;
    float m_closing_d = 2.f;
    
    float m_clipping_plane_distance = 0.f;
    std::unique_ptr<ClippingPlane> m_clipping_plane;
    
    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    std::map<std::string, wxString> m_desc;

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
        if (int(m_model_object->sla_drain_holes.size()) <= m_hover_id)
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
