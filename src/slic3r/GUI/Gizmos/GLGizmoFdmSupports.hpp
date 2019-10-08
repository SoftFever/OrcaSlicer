#ifndef slic3r_GLGizmoFdmSupports_hpp_
#define slic3r_GLGizmoFdmSupports_hpp_

#include "GLGizmoBase.hpp"

#include <cereal/types/vector.hpp>


namespace Slic3r {
namespace GUI {

class ClippingPlane;
class MeshClipper;
class MeshRaycaster;
enum class SLAGizmoEventType : unsigned char;

class GLGizmoFdmSupports : public GLGizmoBase
{
private:
    ModelObject* m_model_object = nullptr;
    ObjectID m_model_object_id = 0;
    int m_active_instance = -1;
    float m_active_instance_bb_radius; // to cache the bb
    bool unproject_on_mesh(const Vec2d& mouse_pos,  size_t& facet_idx, Vec3f* position = nullptr);


    GLUquadricObj* m_quadric;

    std::unique_ptr<MeshRaycaster> m_mesh_raycaster;
    const TriangleMesh* m_mesh;
    const indexed_triangle_set* m_its;
    mutable std::vector<Vec2f> m_triangles;
    float m_cursor_radius = 2.f;

    std::vector<bool> m_selected_facets;

public:
    GLGizmoFdmSupports(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    ~GLGizmoFdmSupports() override;
    void set_fdm_support_data(ModelObject* model_object, const Selection& selection);
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);
    ClippingPlane get_fdm_clipping_plane() const;
    using NeighborData = std::pair<size_t, size_t>;


private:
    bool on_init() override;
    void on_render() const override;
    void on_render_for_picking() const override;

    void render_triangles(const Selection& selection) const;
    void render_clipping_plane(const Selection& selection) const;
    void render_cursor_circle() const;
    bool is_mesh_update_necessary() const;
    void update_mesh();

    float m_clipping_plane_distance = 0.f;
    std::unique_ptr<ClippingPlane> m_clipping_plane;

    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    std::map<std::string, wxString> m_desc;

    bool m_wait_for_up_event = false;
    EState m_old_state = Off; // to be able to see that the gizmo has just been closed (see on_set_state)

    mutable std::unique_ptr<MeshClipper> m_object_clipper;

    std::vector<NeighborData> m_neighbors; // pairs of vertex_index - facet_index


    bool is_point_clipped(const Vec3d& point) const;
    void update_clipping_plane(bool keep_normal = false) const;

protected:
    void on_set_state() override;
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

#endif // slic3r_GLGizmoFdmSupports_hpp_
