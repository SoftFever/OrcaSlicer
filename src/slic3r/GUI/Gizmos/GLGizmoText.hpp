#ifndef slic3r_GLGizmoText_hpp_
#define slic3r_GLGizmoText_hpp_

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "../GLTexture.hpp"
#include "../Camera.hpp"
#include "libslic3r/Model.hpp"

namespace Slic3r {

enum class ModelVolumeType : int;
class ModelVolume;

namespace GUI {
//#define DEBUG_TEXT
//#define DEBUG_TEXT_VALUE
enum class SLAGizmoEventType : unsigned char;
class GLGizmoText : public GLGizmoBase
{
private:
    std::vector<std::string> m_avail_font_names;
    char m_text[1024] = { 0 };
    std::string m_font_name;
    float m_font_size = 16.f;
    const float m_font_size_min = 3.f;
    const float m_font_size_max     = 1000.f;
    int m_curr_font_idx = 0;
    bool m_bold = true;
    bool m_italic = false;
    float m_thickness = 2.f;
    const float  m_thickness_min     = 0.01f;
    const float  m_thickness_max     = 1000.f;
    float m_embeded_depth = 0.f;
    const float  m_embeded_depth_max = 1000.f;
    float m_rotate_angle = 0;
    float m_text_gap = 0.f;
    bool m_is_surface_text = false;
    mutable RaycastResult    m_rr;

    float m_combo_height = 0.0f;
    float m_combo_width = 0.0f;
    float m_scale;

    Vec2d m_mouse_position = Vec2d::Zero();
    Vec2d m_origin_mouse_position = Vec2d::Zero();
    bool  m_shift_down     = false;

    class TextureInfo {
    public:
        GLTexture* texture { nullptr };
        int h;
        int w;
        int hl;

        std::string font_name;
    };

    std::vector<TextureInfo> m_textures;

    std::vector<std::string> m_font_names;

    bool m_init_texture = false;
    std::vector<bool> m_font_status;
    std::mutex m_mutex;
    std::thread m_thread;

    bool m_edit_text_again  = false;
    bool m_is_modify = false;
    bool m_need_update_text = false;
    bool m_show_warning = false;

    int  m_object_idx = -1;
    int  m_volume_idx = -1;

    int m_preview_text_volume_id = -1;
    Vec3d                    m_fix_text_position_in_world = Vec3d::Zero();
    Vec3f                    m_fix_text_normal_in_world   = Vec3f::Zero();
    bool                     m_need_fix;
    Vec3d                    m_text_position_in_world = Vec3d::Zero();
    Vec3f                    m_text_normal_in_world  = Vec3f::Zero();
    Geometry::Transformation m_text_tran_in_object;
    Geometry::Transformation m_text_tran_in_world;
    Geometry::Transformation m_model_object_in_world_tran;
    Transform3d              m_text_cs_to_world_tran;

    Vec3d m_cut_plane_dir_in_world = Vec3d::UnitZ();

    std::vector<Vec3d> m_position_points;
    std::vector<Vec3d> m_normal_points;
    std::vector<Vec3d> m_cut_points_in_world;
    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    std::map<std::string, wxString> m_desc;
    Transform3d                     m_text_volume_tran;
    ModelVolume *                   m_last_text_mv;
    // move gizmo
    Grabber m_move_grabber;
    const int m_move_cube_id = 1;

    // TRN - Title in Undo/Redo stack after move with SVG along emboss axe - From surface
    const std::string move_snapshot_name = "Text move";

public:
    GLGizmoText(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    ~GLGizmoText();

    void update_font_texture();

    bool gizmo_event(SLAGizmoEventType action, const Vec2d &mouse_position, bool shift_down, bool alt_down, bool control_down);

    bool is_mesh_point_clipped(const Vec3d &point, const Transform3d &trafo) const;
    BoundingBoxf3 bounding_box() const;

protected:
    virtual bool on_init() override;
    virtual std::string on_get_name() const override;
    virtual std::string on_get_name_str() override { return "Text shape"; }
    virtual bool on_is_activable() const override;
    virtual void on_render() override;
    virtual void on_render_for_picking() override;
    virtual void on_start_dragging() override;
    virtual void on_stop_dragging() override;
    virtual void on_update(const UpdateData &data) override;
    void push_combo_style(const float scale);
    void pop_combo_style();
    void push_button_style(bool pressed);
    void pop_button_style();
    virtual void on_set_state() override;
    virtual void data_changed(bool is_serializing) override;
    virtual CommonGizmosDataID on_get_requirements() const override;
    virtual void on_render_input_window(float x, float y, float bottom_limit);

    void show_tooltip_information(float x, float y);

private:
    void load_init_text();
    void update_text_pos_normal();
    void update_font_status();
    void reset_text_info();
    bool update_text_positions(const std::vector<std::string>& texts);
    TriangleMesh get_text_mesh(const char* text_str, const Vec3d &position, const Vec3d &normal, const Vec3d &text_up_dir);

    bool update_raycast_cache(const Vec2d &mouse_position, const Camera &camera, const std::vector<Transform3d> &trafo_matrices);
    void generate_text_volume(bool is_temp = true);
    void delete_temp_preview_text_volume();

    TextInfo get_text_info();
    void     load_from_text_info(const TextInfo &text_info);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoText_hpp_
