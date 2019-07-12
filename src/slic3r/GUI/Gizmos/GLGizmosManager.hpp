#ifndef slic3r_GUI_GLGizmosManager_hpp_
#define slic3r_GUI_GLGizmosManager_hpp_

#include "slic3r/GUI/GLTexture.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/Gizmos/GLGizmos.hpp"

#include <map>

namespace Slic3r {
namespace GUI {

class Selection;
class GLGizmoBase;
class GLCanvas3D;
class ClippingPlane;

class Rect
{
    float m_left;
    float m_top;
    float m_right;
    float m_bottom;

public:
    Rect() : m_left(0.0f) , m_top(0.0f) , m_right(0.0f) , m_bottom(0.0f) {}

    Rect(float left, float top, float right, float bottom) : m_left(left) , m_top(top) , m_right(right) , m_bottom(bottom) {}

    float get_left() const { return m_left; }
    void set_left(float left) { m_left = left; }

    float get_top() const { return m_top; }
    void set_top(float top) { m_top = top; }

    float get_right() const { return m_right; }
    void set_right(float right) { m_right = right; }

    float get_bottom() const { return m_bottom; }
    void set_bottom(float bottom) { m_bottom = bottom; }

    float get_width() const { return m_right - m_left; }
    float get_height() const { return m_top - m_bottom; }
};

class GLGizmosManager
{
public:
    static const float Default_Icons_Size;

    enum EType : unsigned char
    {
        Undefined,
        Move,
        Scale,
        Rotate,
        Flatten,
        Cut,
        SlaSupports,
        Num_Types
    };

private:
    bool m_enabled;
    typedef std::map<EType, GLGizmoBase*> GizmosMap;
    GizmosMap m_gizmos;
    mutable GLTexture m_icons_texture;
    mutable bool m_icons_texture_dirty;
    BackgroundTexture m_background_texture;
    EType m_current;

    float m_overlay_icons_size;
    float m_overlay_scale;
    float m_overlay_border;
    float m_overlay_gap_y;

    struct MouseCapture
    {
        bool left;
        bool middle;
        bool right;
        GLCanvas3D* parent;

        MouseCapture() { reset(); }

        bool any() const { return left || middle || right; }
        void reset() { left = middle = right = false; parent = nullptr; }
    };

    MouseCapture m_mouse_capture;
    std::string m_tooltip;

public:
    GLGizmosManager();
    ~GLGizmosManager();

    bool init(GLCanvas3D& parent);

    bool is_enabled() const { return m_enabled; }
    void set_enabled(bool enable) { m_enabled = enable; }

    void set_overlay_icon_size(float size);
    void set_overlay_scale(float scale);

    void refresh_on_off_state(const Selection& selection);
    void reset_all_states();

    void set_hover_id(int id);
    void enable_grabber(EType type, unsigned int id, bool enable);

    void update(const Linef3& mouse_ray, const Selection& selection, const Point* mouse_pos = nullptr);
    void update_data(GLCanvas3D& canvas);

    Rect get_reset_rect_viewport(const GLCanvas3D& canvas) const;
    EType get_current_type() const { return m_current; }

    bool is_running() const;
    bool handle_shortcut(int key, const Selection& selection);

    bool is_dragging() const;
    void start_dragging(const Selection& selection);
    void stop_dragging();

    Vec3d get_displacement() const;

    Vec3d get_scale() const;
    void set_scale(const Vec3d& scale);

    Vec3d get_scale_offset() const;

    Vec3d get_rotation() const;
    void set_rotation(const Vec3d& rotation);

    Vec3d get_flattening_normal() const;

    void set_flattening_data(const ModelObject* model_object);

    void set_sla_support_data(ModelObject* model_object, const Selection& selection);
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position = Vec2d::Zero(), bool shift_down = false, bool alt_down = false, bool control_down = false);
    ClippingPlane get_sla_clipping_plane() const;

    void render_current_gizmo(const Selection& selection) const;
    void render_current_gizmo_for_picking_pass(const Selection& selection) const;

    void render_overlay(const GLCanvas3D& canvas, const Selection& selection) const;

    const std::string& get_tooltip() const { return m_tooltip; }

    bool on_mouse(wxMouseEvent& evt, GLCanvas3D& canvas);
    bool on_mouse_wheel(wxMouseEvent& evt, GLCanvas3D& canvas);
    bool on_char(wxKeyEvent& evt, GLCanvas3D& canvas);
    bool on_key(wxKeyEvent& evt, GLCanvas3D& canvas);

private:
    void reset();

    void do_render_overlay(const GLCanvas3D& canvas, const Selection& selection) const;

    float get_total_overlay_height() const;
    float get_total_overlay_width() const;

    GLGizmoBase* get_current() const;

    bool generate_icons_texture() const;

    void update_on_off_state(const GLCanvas3D& canvas, const Vec2d& mouse_pos, const Selection& selection);
    std::string update_hover_state(const GLCanvas3D& canvas, const Vec2d& mouse_pos);
    bool overlay_contains_mouse(const GLCanvas3D& canvas, const Vec2d& mouse_pos) const;
    bool grabber_contains_mouse() const;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_GLGizmosManager_hpp_
