#ifndef slic3r_GLGizmoBase_hpp_
#define slic3r_GLGizmoBase_hpp_

#include "libslic3r/Point.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Selection.hpp"

#include <cereal/archives/binary.hpp>

class wxWindow;
class GLUquadric;
typedef class GLUquadric GLUquadricObj;


namespace Slic3r {

class BoundingBoxf3;
class Linef3;
class ModelObject;

namespace GUI {

static const float DEFAULT_BASE_COLOR[4] = { 0.625f, 0.625f, 0.625f, 1.0f };
static const float DEFAULT_DRAG_COLOR[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float DEFAULT_HIGHLIGHT_COLOR[4] = { 1.0f, 0.38f, 0.0f, 1.0f };
static const float AXES_COLOR[][4] = { { 0.75f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.75f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.75f, 1.0f } };
static const float CONSTRAINED_COLOR[4] = { 0.5f, 0.5f, 0.5f, 1.0f };



class ImGuiWrapper;
class CommonGizmosData;
class GLCanvas3D;
class ClippingPlane;

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
        float color[4];
        bool enabled;
        bool dragging;

        Grabber();

        void render(bool hover, float size) const;
        void render_for_picking(float size) const { render(size, color, false); }

        float get_half_size(float size) const;
        float get_dragging_half_size(float size) const;

    private:
        void render(float size, const float* render_color, bool use_lighting) const;
        void render_face(float half_size) const;
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
    float m_base_color[4];
    float m_drag_color[4];
    float m_highlight_color[4];
    mutable std::vector<Grabber> m_grabbers;
    ImGuiWrapper* m_imgui;
    bool m_first_input_window_render;
    CommonGizmosData* m_c = nullptr;

public:
    GLGizmoBase(GLCanvas3D& parent,
                const std::string& icon_filename,
                unsigned int sprite_id,
                CommonGizmosData* common_data = nullptr);
    virtual ~GLGizmoBase() {}

    bool init() { return on_init(); }

    void load(cereal::BinaryInputArchive& ar) { m_state = On; on_load(ar); }
    void save(cereal::BinaryOutputArchive& ar) const { on_save(ar); }

    std::string get_name() const { return on_get_name(); }

    int get_group_id() const { return m_group_id; }
    void set_group_id(int id) { m_group_id = id; }

    EState get_state() const { return m_state; }
    void set_state(EState state) { m_state = state; on_set_state(); }

    int get_shortcut_key() const { return m_shortcut_key; }
    void set_shortcut_key(int key) { m_shortcut_key = key; }

    const std::string& get_icon_filename() const { return m_icon_filename; }

    bool is_activable() const { return on_is_activable(); }
    bool is_selectable() const { return on_is_selectable(); }

    unsigned int get_sprite_id() const { return m_sprite_id; }

    int get_hover_id() const { return m_hover_id; }
    void set_hover_id(int id);
    
    void set_highlight_color(const float* color);

    void enable_grabber(unsigned int id);
    void disable_grabber(unsigned int id);

    void start_dragging();
    void stop_dragging();

    bool is_dragging() const { return m_dragging; }

    void update(const UpdateData& data);

    void render() const { on_render(); }
    void render_for_picking() const { on_render_for_picking(); }
    void render_input_window(float x, float y, float bottom_limit);

protected:
    virtual bool on_init() = 0;
    virtual void on_load(cereal::BinaryInputArchive& ar) {}
    virtual void on_save(cereal::BinaryOutputArchive& ar) const {}
    virtual std::string on_get_name() const = 0;
    virtual void on_set_state() {}
    virtual void on_set_hover_id() {}
    virtual bool on_is_activable() const { return true; }
    virtual bool on_is_selectable() const { return true; }
    virtual void on_enable_grabber(unsigned int id) {}
    virtual void on_disable_grabber(unsigned int id) {}
    virtual void on_start_dragging() {}
    virtual void on_stop_dragging() {}
    virtual void on_update(const UpdateData& data) {}
    virtual void on_render() const = 0;
    virtual void on_render_for_picking() const = 0;
    virtual void on_render_input_window(float x, float y, float bottom_limit) {}

    // Returns the picking color for the given id, based on the BASE_ID constant
    // No check is made for clashing with other picking color (i.e. GLVolumes)
    std::array<float, 4> picking_color_component(unsigned int id) const;
    void render_grabbers(const BoundingBoxf3& box) const;
    void render_grabbers(float size) const;
    void render_grabbers_for_picking(const BoundingBoxf3& box) const;

    void set_tooltip(const std::string& tooltip) const;
    std::string format(float value, unsigned int decimals) const;
};

// Produce an alpha channel checksum for the red green blue components. The alpha channel may then be used to verify, whether the rgb components
// were not interpolated by alpha blending or multi sampling.
extern unsigned char picking_checksum_alpha_channel(unsigned char red, unsigned char green, unsigned char blue);

class MeshRaycaster;
class MeshClipper;

class CommonGizmosData {
public:
    const TriangleMesh* mesh() const {
        return (! m_mesh ? nullptr : m_mesh); //(m_cavity_mesh ? m_cavity_mesh.get() : m_mesh));
    }

    bool update_from_backend(GLCanvas3D& canvas, ModelObject* model_object);

    bool recent_update = false;



    ModelObject* m_model_object = nullptr;
    const TriangleMesh* m_mesh;
    std::unique_ptr<MeshRaycaster> m_mesh_raycaster;
    std::unique_ptr<MeshClipper> m_object_clipper;
    std::unique_ptr<MeshClipper> m_supports_clipper;

    //std::unique_ptr<TriangleMesh> m_cavity_mesh;
    //std::unique_ptr<GLVolume> m_volume_with_cavity;

    int m_active_instance = -1;
    float m_active_instance_bb_radius = 0;
    ObjectID m_model_object_id = 0;
    int m_print_object_idx = -1;
    int m_print_objects_count = -1;
    int m_old_timestamp = -1;

    float m_clipping_plane_distance = 0.f;
    std::unique_ptr<ClippingPlane> m_clipping_plane;

    void stash_clipping_plane() {
        m_clipping_plane_distance_stash = m_clipping_plane_distance;
    }

    void unstash_clipping_plane() {
        m_clipping_plane_distance = m_clipping_plane_distance_stash;
    }

    bool has_drilled_mesh() const { return m_has_drilled_mesh; }

private:
    const TriangleMesh* m_old_mesh;
    TriangleMesh m_backend_mesh_transformed;
    float m_clipping_plane_distance_stash = 0.f;
    bool m_has_drilled_mesh = false;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoBase_hpp_
