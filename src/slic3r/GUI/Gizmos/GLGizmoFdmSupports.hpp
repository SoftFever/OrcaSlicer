#ifndef slic3r_GLGizmoFdmSupports_hpp_
#define slic3r_GLGizmoFdmSupports_hpp_

#include "GLGizmoBase.hpp"

#include "slic3r/GUI/3DScene.hpp"

#include "libslic3r/ObjectID.hpp"
#include "libslic3r/TriangleSelector.hpp"

#include <cereal/types/vector.hpp>




namespace Slic3r {

enum class FacetSupportType : int8_t;

namespace GUI {

enum class SLAGizmoEventType : unsigned char;
class ClippingPlane;



class TriangleSelectorGUI : public TriangleSelector {
public:
    explicit TriangleSelectorGUI(const TriangleMesh& mesh)
        : TriangleSelector(mesh) {}

    // Render current selection. Transformation matrices are supposed
    // to be already set.
    void render(ImGuiWrapper* imgui = nullptr);

#ifdef PRUSASLICER_TRIANGLE_SELECTOR_DEBUG
    void render_debug(ImGuiWrapper* imgui);
    bool m_show_triangles{false};
    bool m_show_invalid{false};
#endif

private:
    GLIndexedVertexArray m_iva_enforcers;
    GLIndexedVertexArray m_iva_blockers;
    std::array<GLIndexedVertexArray, 3> m_varrays;
};



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

    // For each model-part volume, store status and division of the triangles.
    std::vector<std::unique_ptr<TriangleSelectorGUI>> m_triangle_selectors;

public:
    GLGizmoFdmSupports(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    ~GLGizmoFdmSupports() override;
    void set_fdm_support_data(ModelObject* model_object, const Selection& selection);
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);


private:
    bool on_init() override;
    void on_render() const override;
    void on_render_for_picking() const override {}

    void render_triangles(const Selection& selection) const;
    void render_cursor_circle() const;

    void update_model_object() const;
    void update_from_model_object();
    void activate_internal_undo_redo_stack(bool activate);

    void select_facets_by_angle(float threshold, bool overwrite, bool block);
    bool m_overwrite_selected = false;
    float m_angle_threshold_deg = 45.f;

    bool is_mesh_point_clipped(const Vec3d& point) const;

    float m_clipping_plane_distance = 0.f;
    std::unique_ptr<ClippingPlane> m_clipping_plane;
    bool m_setting_angle = false;
    bool m_internal_stack_active = false;
    bool m_schedule_update = false;

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
