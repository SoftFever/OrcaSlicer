#ifndef slic3r_GLGizmoFdmSupports_hpp_
#define slic3r_GLGizmoFdmSupports_hpp_

#include "GLGizmoBase.hpp"

#include "slic3r/GUI/3DScene.hpp"

#include <cereal/types/vector.hpp>


namespace Slic3r {

enum class FacetSupportType : int8_t;

namespace GUI {

enum class SLAGizmoEventType : unsigned char;

class GLGizmoFdmSupports : public GLGizmoBase
{
private:
    ObjectID m_old_mo_id;
    size_t m_old_volumes_size = 0;

    GLUquadricObj* m_quadric;

    float m_cursor_radius = 2.f;
    static constexpr float CursorRadiusMin  = 0.f;
    static constexpr float CursorRadiusMax  = 8.f;
    static constexpr float CursorRadiusStep = 0.2f;

    // For each model-part volume, store a list of statuses of
    // individual facets (one of the enum values above).
    std::vector<std::vector<FacetSupportType>> m_selected_facets;

    // Store two vertex buffer arrays (for enforcers/blockers)
    // for each model-part volume.
    std::vector<std::array<GLIndexedVertexArray, 2>> m_ivas;

    void update_vertex_buffers(const ModelVolume* mv,
                               int mesh_id,
                               bool update_enforcers,
                               bool update_blockers);

public:
    GLGizmoFdmSupports(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    ~GLGizmoFdmSupports() override;
    void set_fdm_support_data(ModelObject* model_object, const Selection& selection);
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);
    using NeighborData = std::pair<size_t, size_t>;


private:
    bool on_init() override;
    void on_render() const override;
    void on_render_for_picking() const override {}

    void render_triangles(const Selection& selection) const;
    void render_cursor_circle() const;

    void update_model_object() const;
    void update_from_model_object();

    void select_facets_by_angle(float threshold, bool overwrite, bool block);
    bool m_overwrite_selected = false;
    float m_angle_threshold_deg = 45.f;

    bool is_mesh_point_clipped(const Vec3d& point) const;

    float m_clipping_plane_distance = 0.f;
    std::unique_ptr<ClippingPlane> m_clipping_plane;
    bool m_setting_angle = false;

    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    std::map<std::string, wxString> m_desc;

    enum class Button {
        None,
        Left,
        Right
    };

    Button m_button_down = Button::None;
    EState m_old_state = Off; // to be able to see that the gizmo has just been closed (see on_set_state)

    std::vector<std::vector<NeighborData>> m_neighbors; // pairs of vertex_index - facet_index for each mesh

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
    CommonGizmosDataID on_get_requirements() const override;
};


} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoFdmSupports_hpp_
