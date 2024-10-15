#ifndef slic3r_GLGizmoBase_hpp_
#define slic3r_GLGizmoBase_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/Color.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/3DScene.hpp"

#include <cereal/archives/binary.hpp>

#include <wx/event.h>
#include <wx/timer.h>

#include <chrono>

#define ENABLE_FIXED_GRABBER 1

class wxWindow;

namespace Slic3r {

class BoundingBoxf3;
class Linef3;
class ModelObject;

namespace GUI {
#define MAX_NUM 9999.99
#define MAX_SIZE "9999.99"

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

    static float INV_ZOOM;

    //BBS colors
    static std::array<float, 4> DEFAULT_BASE_COLOR;
    static std::array<float, 4> DEFAULT_DRAG_COLOR;
    static std::array<float, 4> DEFAULT_HIGHLIGHT_COLOR;
    static std::array<std::array<float, 4>, 3> AXES_COLOR;
    static std::array<std::array<float, 4>, 3> AXES_HOVER_COLOR;
    static std::array<float, 4> CONSTRAINED_COLOR;
    static std::array<float, 4> FLATTEN_COLOR;
    static std::array<float, 4> FLATTEN_HOVER_COLOR;
    static std::array<float, 4> GRABBER_NORMAL_COL;
    static std::array<float, 4> GRABBER_HOVER_COL;
    static std::array<float, 4> GRABBER_UNIFORM_COL;
    static std::array<float, 4> GRABBER_UNIFORM_HOVER_COL;

    static void update_render_colors();
    static void load_render_colors();


    struct Grabber
    {
        static const float SizeFactor;
        static const float MinHalfSize;
        static const float DraggingScaleFactor;
        static const float FixedGrabberSize;
        static float       GrabberSizeFactor;
        static const float FixedRadiusSize;

        Vec3d center;
        Vec3d angles;
        std::array<float, 4> color;
        std::array<float, 4> hover_color;
        bool enabled;
        bool dragging;

        Grabber();

        void render(bool hover, float size) const;
        void render_for_picking(float size) const { render(size, color, true); }

        float get_half_size(float size) const;
        float get_dragging_half_size(float size) const;
        GLModel& get_cube();

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
    enum GripperType {
        UNDEFINE,
        POINT,
        EDGE,
        CIRCLE,
        CIRCLE_1,
        CIRCLE_2,
        PLANE,
        PLANE_1,
        PLANE_2,
        SPHERE_1,
        SPHERE_2,
    };
    std::map<GripperType, std::shared_ptr<PickRaycaster>> m_gripper_id_raycast_map;

    bool m_dragging;
    std::array<float, 4> m_base_color;
    std::array<float, 4> m_drag_color;
    std::array<float, 4> m_highlight_color;
    mutable std::vector<Grabber> m_grabbers;
    ImGuiWrapper* m_imgui;
    bool m_first_input_window_render;
    mutable std::string m_tooltip;
    CommonGizmosDataPool* m_c{nullptr};
    GLModel m_cone;
    GLModel m_cylinder;
    GLModel m_sphere;

    bool m_is_dark_mode = false;

    std::chrono::system_clock::time_point start;
    enum DoubleShowType {
        Normal, // origin data
        PERCENTAGE,
        DEGREE,//input must is radian
    };
    struct SliderInputLayout
    {
        float sliders_left_width;
        float sliders_width;
        float input_left_width;
        float input_width;
    };
    bool render_slider_double_input_by_format(const SliderInputLayout &    layout,
                                              const std::string &          label,
                                              float &                      value_in,
                                              float                        value_min,
                                              float                        value_max,
                                              int                          keep_digit ,
                                              DoubleShowType               show_type = DoubleShowType::Normal);
    bool render_combo(const std::string &label, const std::vector<std::string> &lines,
        size_t &selection_idx, float label_width, float item_width);
    void render_cross_mark(const Vec3f& target,bool is_single =false);
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
    void set_state(EState state);
    int get_shortcut_key() const { return m_shortcut_key; }

    const std::string& get_icon_filename() const { return m_icon_filename; }

    void set_icon_filename(const std::string& filename);

    bool is_activable() const { return on_is_activable(); }
    bool is_selectable() const { return on_is_selectable(); }
    CommonGizmosDataID get_requirements() const { return on_get_requirements(); }
    virtual bool wants_enter_leave_snapshots() const { return false; }
    virtual std::string get_gizmo_entering_text() const { assert(false); return ""; }
    virtual std::string get_gizmo_leaving_text() const { assert(false); return ""; }
    virtual std::string get_action_snapshot_name() { return "Gizmo action"; }
    void set_common_data_pool(CommonGizmosDataPool* ptr) { m_c = ptr; }

    virtual bool apply_clipping_plane() { return true; }
    virtual bool on_mouse(const wxMouseEvent &mouse_event) { return false; }
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
    virtual void on_change_color_mode(bool is_dark) {  m_is_dark_mode = is_dark; }

    virtual std::string get_tooltip() const { return ""; }
    /// <summary>
    /// Is called when data (Selection) is changed
    /// </summary>
    virtual void data_changed(bool is_serializing){};
    int get_count() { return ++count; }
    static void  render_glmodel(GLModel &model, const std::array<float, 4> &color, Transform3d view_model_matrix, bool for_picking = false, float emission_factor = 0.0f);
protected:
    float last_input_window_width = 0;
    virtual bool on_init() = 0;
    virtual void on_load(cereal::BinaryInputArchive& ar) {}
    virtual void on_save(cereal::BinaryOutputArchive& ar) const {}
    virtual std::string on_get_name() const = 0;
    virtual std::string on_get_name_str() { return ""; }
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

    bool GizmoImguiBegin(const std::string& name, int flags);
    void GizmoImguiEnd();
    void GizmoImguiSetNextWIndowPos(float &x, float y, int flag, float pivot_x = 0.0f, float pivot_y = 0.0f);
    // Returns the picking color for the given id, based on the BASE_ID constant
    // No check is made for clashing with other picking color (i.e. GLVolumes)
    std::array<float, 4> picking_color_component(unsigned int id) const;
    void render_grabbers(const BoundingBoxf3& box) const;
    void render_grabbers(float size) const;
    void render_grabbers_for_picking(const BoundingBoxf3& box) const;

    std::string format(float value, unsigned int decimals) const;

    // Mark gizmo as dirty to Re-Render when idle()
    void set_dirty();

    /// <summary>
    /// function which
    /// Set up m_dragging and call functions
    /// on_start_dragging / on_dragging / on_stop_dragging
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>same as on_mouse</returns>
    bool use_grabbers(const wxMouseEvent &mouse_event);
    void do_stop_dragging(bool perform_mouse_cleanup);
    template<typename T> void limit_value(T &value, T _min, T _max)
    {
        if (value >= _max) { value = _max;}
        if (value <= _min) { value = _min; }
    }

private:
    // Flag for dirty visible state of Gizmo
    // When True then need new rendering
    bool m_dirty;
    int count = 0;
};


// Produce an alpha channel checksum for the red green blue components. The alpha channel may then be used to verify, whether the rgb components
// were not interpolated by alpha blending or multi sampling.
extern unsigned char picking_checksum_alpha_channel(unsigned char red, unsigned char green, unsigned char blue);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoBase_hpp_
