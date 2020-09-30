#ifndef slic3r_GLGizmoPainterBase_hpp_
#define slic3r_GLGizmoPainterBase_hpp_

#include "GLGizmoBase.hpp"

#include "slic3r/GUI/3DScene.hpp"

#include "libslic3r/ObjectID.hpp"
#include "libslic3r/TriangleSelector.hpp"

#include <cereal/types/vector.hpp>




namespace Slic3r {

enum class EnforcerBlockerType : int8_t;

namespace GUI {

enum class SLAGizmoEventType : unsigned char;
class ClippingPlane;
class Camera;

enum class PainterGizmoType {
    FDM_SUPPORTS,
    SEAM
};


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


// Following class is a base class for a gizmo with ability to paint on mesh
// using circular blush (such as FDM supports gizmo and seam painting gizmo).
// The purpose is not to duplicate code related to mesh painting.
class GLGizmoPainterBase : public GLGizmoBase
{
private:
    ObjectID m_old_mo_id;
    size_t m_old_volumes_size = 0;

public:
    GLGizmoPainterBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    ~GLGizmoPainterBase() override {}
    void set_painter_gizmo_data(const Selection& selection);
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);

protected:
    void render_triangles(const Selection& selection) const;
    void render_cursor() const;
    void render_cursor_circle() const;
    void render_cursor_sphere() const;
    virtual void update_model_object() const = 0;
    virtual void update_from_model_object() = 0;
    void activate_internal_undo_redo_stack(bool activate);
    void set_cursor_type(TriangleSelector::CursorType);

    float m_cursor_radius = 2.f;
    static constexpr float CursorRadiusMin  = 0.4f; // cannot be zero
    static constexpr float CursorRadiusMax  = 8.f;
    static constexpr float CursorRadiusStep = 0.2f;

    // For each model-part volume, store status and division of the triangles.
    std::vector<std::unique_ptr<TriangleSelectorGUI>> m_triangle_selectors;

    TriangleSelector::CursorType m_cursor_type = TriangleSelector::SPHERE;


private:
    bool is_mesh_point_clipped(const Vec3d& point) const;
    void get_mesh_hit(const Vec2d& mouse_position,
                      const Camera& camera,
                      const std::vector<Transform3d>& trafo_matrices,
                      int& mesh_id, Vec3f& hit, size_t& facet,
                      bool& clipped_mesh_was_hit) const;

    float m_clipping_plane_distance = 0.f;
    std::unique_ptr<ClippingPlane> m_clipping_plane;
    GLIndexedVertexArray m_vbo_sphere;

    bool m_internal_stack_active = false;
    bool m_schedule_update = false;
    Vec2d m_last_mouse_position = Vec2d::Zero();
    std::pair<int, Vec3f> m_last_mesh_idx_and_hit = {-1, Vec3f::Zero()};

    enum class Button {
        None,
        Left,
        Right
    };

    Button m_button_down = Button::None;
    EState m_old_state = Off; // to be able to see that the gizmo has just been closed (see on_set_state)

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
};


} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoPainterBase_hpp_
