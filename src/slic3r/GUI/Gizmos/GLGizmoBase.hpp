#ifndef slic3r_GLGizmoBase_hpp_
#define slic3r_GLGizmoBase_hpp_

#include "libslic3r/Point.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GLModel.hpp"

#include <cereal/archives/binary.hpp>

class wxWindow;

namespace Slic3r {

class BoundingBoxf3;
class Linef3;
class ModelObject;

namespace GUI {

static const std::array<float, 4> DEFAULT_BASE_COLOR = { 0.625f, 0.625f, 0.625f, 1.0f };
static const std::array<float, 4> DEFAULT_DRAG_COLOR = { 1.0f, 1.0f, 1.0f, 1.0f };
static const std::array<float, 4> DEFAULT_HIGHLIGHT_COLOR = { 1.0f, 0.38f, 0.0f, 1.0f };
static const std::array<std::array<float, 4>, 3> AXES_COLOR = {{
                                                                { 0.75f, 0.0f, 0.0f, 1.0f },
                                                                { 0.0f, 0.75f, 0.0f, 1.0f },
                                                                { 0.0f, 0.0f, 0.75f, 1.0f }
                                                              }};
static const std::array<float, 4> CONSTRAINED_COLOR = { 0.5f, 0.5f, 0.5f, 1.0f };

class ImGuiWrapper;
class GLCanvas3D;
enum class CommonGizmosDataID;
class CommonGizmosDataPool;
class Selection;

class GLGizmoBase
{
public:
    // Starting value for ids to avoid clashing with ids used by GLVolumes
    // (254 is choosen to leave some space for forward compatibility)
    static const unsigned int BASE_ID = 255 * 255 * 254;

protected:
    struct Grabber
    {
        static const float SizeFactor;
        static const float MinHalfSize;
        static const float DraggingScaleFactor;

        Vec3d center;
        Vec3d angles;
        std::array<float, 4> color;
        bool enabled;
        bool dragging;

        Grabber();

        void render(bool hover, float size) const;
        void render_for_picking(float size) const { render(size, color, true); }

        float get_half_size(float size) const;
        float get_dragging_half_size(float size) const;

    private:
        void render(float size, const std::array<float, 4>& render_color, bool picking) const;

        GLModel cube;
        bool cube_initialized = false;
    };

public:
    enum EState
    {
        Off,
        On,
        Num_States
    };

    struct UpdateData
    {
        const Linef3& mouse_ray;
        const Point& mouse_pos;

        UpdateData(const Linef3& mouse_ray, const Point& mouse_pos)
            : mouse_ray(mouse_ray), mouse_pos(mouse_pos)
        {}
    };

protected:
    GLCanvas3D& m_parent;

    int m_group_id;
    EState m_state;
    int m_shortcut_key;
    std::string m_icon_filename;
    unsigned int m_sprite_id;
    int m_hover_id;
    bool m_dragging;
    std::array<float, 4> m_base_color;
    std::array<float, 4> m_drag_color;
    std::array<float, 4> m_highlight_color;
    mutable std::vector<Grabber> m_grabbers;
    ImGuiWrapper* m_imgui;
    bool m_first_input_window_render;
    mutable std::string m_tooltip;
    CommonGizmosDataPool* m_c;
    GLModel m_cone;
    GLModel m_cylinder;
    GLModel m_sphere;

public:
    GLGizmoBase(GLCanvas3D& parent,
                const std::string& icon_filename,
                unsigned int sprite_id);
    virtual ~GLGizmoBase() {}

    bool init() { return on_init(); }

    void load(cereal::BinaryInputArchive& ar) { m_state = On; on_load(ar); }
    void save(cereal::BinaryOutputArchive& ar) const { on_save(ar); }

    std::string get_name(bool include_shortcut = true) const;

    int get_group_id() const { return m_group_id; }
    void set_group_id(int id) { m_group_id = id; }

    EState get_state() const { return m_state; }
    void set_state(EState state) { m_state = state; on_set_state(); }

    int get_shortcut_key() const { return m_shortcut_key; }

    const std::string& get_icon_filename() const { return m_icon_filename; }

    bool is_activable() const { return on_is_activable(); }
    bool is_selectable() const { return on_is_selectable(); }
    CommonGizmosDataID get_requirements() const { return on_get_requirements(); }
    virtual bool wants_enter_leave_snapshots() const { return false; }
    virtual std::string get_gizmo_entering_text() const { assert(false); return ""; }
    virtual std::string get_gizmo_leaving_text() const { assert(false); return ""; }
    virtual std::string get_action_snapshot_name() { return _u8L("Gizmo action"); }
    void set_common_data_pool(CommonGizmosDataPool* ptr) { m_c = ptr; }

    unsigned int get_sprite_id() const { return m_sprite_id; }

    int get_hover_id() const { return m_hover_id; }
    void set_hover_id(int id);
    
    void set_highlight_color(const std::array<float, 4>& color);

    void enable_grabber(unsigned int id);
    void disable_grabber(unsigned int id);

    void start_dragging();
    void stop_dragging();

    bool is_dragging() const { return m_dragging; }

    void update(const UpdateData& data);

    // returns True when Gizmo changed its state
    bool update_items_state();

    void render() { m_tooltip.clear(); on_render(); }
    void render_for_picking() { on_render_for_picking(); }
    void render_input_window(float x, float y, float bottom_limit);

    virtual std::string get_tooltip() const { return ""; }

protected:
    virtual bool on_init() = 0;
    virtual void on_load(cereal::BinaryInputArchive& ar) {}
    virtual void on_save(cereal::BinaryOutputArchive& ar) const {}
    virtual std::string on_get_name() const = 0;
    virtual void on_set_state() {}
    virtual void on_set_hover_id() {}
    virtual bool on_is_activable() const { return true; }
    virtual bool on_is_selectable() const { return true; }
    virtual CommonGizmosDataID on_get_requirements() const { return CommonGizmosDataID(0); }
    virtual void on_enable_grabber(unsigned int id) {}
    virtual void on_disable_grabber(unsigned int id) {}
    virtual void on_start_dragging() {}
    virtual void on_stop_dragging() {}
    virtual void on_update(const UpdateData& data) {}
    virtual void on_render() = 0;
    virtual void on_render_for_picking() = 0;
    virtual void on_render_input_window(float x, float y, float bottom_limit) {}

    // Returns the picking color for the given id, based on the BASE_ID constant
    // No check is made for clashing with other picking color (i.e. GLVolumes)
    std::array<float, 4> picking_color_component(unsigned int id) const;
    void render_grabbers(const BoundingBoxf3& box) const;
    void render_grabbers(float size) const;
    void render_grabbers_for_picking(const BoundingBoxf3& box) const;

    std::string format(float value, unsigned int decimals) const;

    // Mark gizmo as dirty to Re-Render when idle()
    void set_dirty();
private:
    // Flag for dirty visible state of Gizmo
    // When True then need new rendering
    bool m_dirty;
};

// Produce an alpha channel checksum for the red green blue components. The alpha channel may then be used to verify, whether the rgb components
// were not interpolated by alpha blending or multi sampling.
extern unsigned char picking_checksum_alpha_channel(unsigned char red, unsigned char green, unsigned char blue);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoBase_hpp_
