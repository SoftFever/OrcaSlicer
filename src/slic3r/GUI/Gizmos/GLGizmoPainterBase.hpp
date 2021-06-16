#ifndef slic3r_GLGizmoPainterBase_hpp_
#define slic3r_GLGizmoPainterBase_hpp_

#include "GLGizmoBase.hpp"

#include "slic3r/GUI/3DScene.hpp"

#include "libslic3r/ObjectID.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/Model.hpp"

#include <cereal/types/vector.hpp>



namespace Slic3r::GUI {

enum class SLAGizmoEventType : unsigned char;
class ClippingPlane;
struct Camera;
class GLGizmoMmuSegmentation;

enum class PainterGizmoType {
    FDM_SUPPORTS,
    SEAM,
    MMU_SEGMENTATION
};


class TriangleSelectorGUI : public TriangleSelector {
public:
    explicit TriangleSelectorGUI(const TriangleMesh& mesh)
        : TriangleSelector(mesh) {}
    virtual ~TriangleSelectorGUI() = default;

    // Render current selection. Transformation matrices are supposed
    // to be already set.
    virtual void render(ImGuiWrapper *imgui);
    void         render() { this->render(nullptr); }

#ifdef PRUSASLICER_TRIANGLE_SELECTOR_DEBUG
    void render_debug(ImGuiWrapper* imgui);
    bool m_show_triangles{false};
    bool m_show_invalid{false};
#endif

private:
    GLIndexedVertexArray                m_iva_enforcers;
    GLIndexedVertexArray                m_iva_blockers;
    std::array<GLIndexedVertexArray, 3> m_varrays;
};


// Following class is a base class for a gizmo with ability to paint on mesh
// using circular blush (such as FDM supports gizmo and seam painting gizmo).
// The purpose is not to duplicate code related to mesh painting.
class GLGizmoPainterBase : public GLGizmoBase
{
private:
    ObjectID m_old_mo_id;
    size_t m_old_volumes_size = 0;
    void on_render() const override {}
    void on_render_for_picking() const override {}

public:
    GLGizmoPainterBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    ~GLGizmoPainterBase() override = default;
    virtual void set_painter_gizmo_data(const Selection& selection);
    virtual bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);

    // Following function renders the triangles and cursor. Having this separated
    // from usual on_render method allows to render them before transparent objects,
    // so they can be seen inside them. The usual on_render is called after all
    // volumes (including transparent ones) are rendered.
    virtual void render_painter_gizmo() const = 0;

protected:
    void render_triangles(const Selection& selection, const bool use_polygon_offset_fill = true) const;
    void render_cursor() const;
    void render_cursor_circle() const;
    void render_cursor_sphere(const Transform3d& trafo) const;
    virtual void update_model_object() const = 0;
    virtual void update_from_model_object() = 0;
    void activate_internal_undo_redo_stack(bool activate);

    virtual std::array<float, 4> get_cursor_sphere_left_button_color() const { return {0.f, 0.f, 1.f, 0.25f}; }
    virtual std::array<float, 4> get_cursor_sphere_right_button_color() const { return {1.f, 0.f, 0.f, 0.25f}; }

    virtual EnforcerBlockerType get_left_button_state_type() const { return EnforcerBlockerType::ENFORCER; }
    virtual EnforcerBlockerType get_right_button_state_type() const { return EnforcerBlockerType::BLOCKER; }

    float m_cursor_radius = 2.f;
    static constexpr float CursorRadiusMin  = 0.4f; // cannot be zero
    static constexpr float CursorRadiusMax  = 8.f;
    static constexpr float CursorRadiusStep = 0.2f;

    // For each model-part volume, store status and division of the triangles.
    std::vector<std::unique_ptr<TriangleSelectorGUI>> m_triangle_selectors;

    TriangleSelector::CursorType m_cursor_type = TriangleSelector::SPHERE;

    bool  m_triangle_splitting_enabled = true;
    bool  m_seed_fill_enabled          = false;
    float m_seed_fill_angle            = 0.f;

    // It stores the value of the previous mesh_id to which the seed fill was applied.
    // It is used to detect when the mouse has moved from one volume to another one.
    int   m_seed_fill_last_mesh_id     = -1;

    enum class Button {
        None,
        Left,
        Right
    };

private:
    bool is_mesh_point_clipped(const Vec3d& point, const Transform3d& trafo) const;
    void update_raycast_cache(const Vec2d& mouse_position,
                              const Camera& camera,
                              const std::vector<Transform3d>& trafo_matrices) const;

    GLIndexedVertexArray m_vbo_sphere;

    bool m_internal_stack_active = false;
    bool m_schedule_update = false;
    Vec2d m_last_mouse_click = Vec2d::Zero();

    Button m_button_down = Button::None;
    EState m_old_state = Off; // to be able to see that the gizmo has just been closed (see on_set_state)

    // Following cache holds result of a raycast query. The queries are asked
    // during rendering the sphere cursor and painting, this saves repeated
    // raycasts when the mouse position is the same as before.
    struct RaycastResult {
        Vec2d mouse_position;
        int mesh_id;
        Vec3f hit;
        size_t facet;
    };
    mutable RaycastResult m_rr;

protected:
    void on_set_state() override;
    void on_start_dragging() override {}
    void on_stop_dragging() override {}

    virtual void on_opening() = 0;
    virtual void on_shutdown() = 0;
    virtual PainterGizmoType get_painter_type() const = 0;

    bool on_is_activable() const override;
    bool on_is_selectable() const override;
    void on_load(cereal::BinaryInputArchive& ar) override;
    void on_save(cereal::BinaryOutputArchive& ar) const override {}
    CommonGizmosDataID on_get_requirements() const override;

    virtual wxString handle_snapshot_action_name(bool shift_down, Button button_down) const = 0;

    friend class ::Slic3r::GUI::GLGizmoMmuSegmentation;
};


} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoPainterBase_hpp_
