#ifndef slic3r_GLGizmoSVG_hpp_
#define slic3r_GLGizmoSVG_hpp_

#include "GLGizmoRotate.hpp"
#include "slic3r/GUI/SurfaceDrag.hpp"
#include "slic3r/GUI/GLTexture.hpp"

#include "slic3r/GUI/IconManager.hpp"
//BBS: add size adjust related
#include "libslic3r/Model.hpp"
#include "slic3r/GUI/Jobs/EmbossJob.hpp"
namespace Slic3r {
class ModelVolume;
enum class ModelVolumeType : int;
} // namespace Slic3r

namespace Slic3r {
namespace GUI {
//#define DEBUG_SVG
struct Camera;
class Worker;
enum class SLAGizmoEventType : unsigned char;
class GLGizmoSVG : public GLGizmoBase
{
    Emboss::DataBasePtr        create_emboss_data_base(std::shared_ptr<std::atomic<bool>> &cancel, ModelVolumeType volume_type, std::string_view filepath = "");
    Emboss::CreateVolumeParams create_input(GLCanvas3D &canvas, ModelVolumeType volume_type);


public:
    GLGizmoSVG(GLCanvas3D& parent,  unsigned int sprite_id);
    virtual ~GLGizmoSVG() = default;

    std::string get_tooltip() const override;
    void        data_changed(bool is_serializing) override;
    void        on_set_hover_id() override { m_rotate_gizmo.set_hover_id(m_hover_id); }
    void        on_enable_grabber(unsigned int id) override;
    void        on_disable_grabber(unsigned int id) override;

     /// <summary>
    /// Create new text without given position
    /// </summary>
    /// <param name="volume_type">Object part / Negative volume / Modifier</param>
    /// <returns>True on succesfull start creation otherwise False</returns>
    bool create_volume(ModelVolumeType volume_type); // first open file dialog //by rigth menu
    bool create_volume(ModelVolumeType volume_type, const Vec2d &mouse_pos); // first open file dialog //by rigth menu

    bool create_volume(std::string_view svg_file, const Vec2d &mouse_pos, ModelVolumeType volume_type = ModelVolumeType::MODEL_PART);
    bool create_volume(std::string_view svg_file, ModelVolumeType volume_type = ModelVolumeType::MODEL_PART);
    /// <summary>
    /// Check whether volume is object containing only emboss volume
    /// </summary>
    /// <param name="volume">Pointer to volume</param>
    /// <returns>True when object otherwise False</returns>
    static bool is_svg_object(const ModelVolume &volume);

    /// <summary>
    /// Check whether volume has emboss data
    /// </summary>
    /// <param name="volume">Pointer to volume</param>
    /// <returns>True when constain emboss data otherwise False</returns>
    static bool is_svg(const ModelVolume &volume);

    void register_single_mesh_pick();

    void update_single_mesh_pick(GLVolume *v);
    bool gizmo_event(SLAGizmoEventType action, const Vec2d &mouse_position, bool shift_down, bool alt_down, bool control_down);

protected:
    bool on_is_selectable() const override { return false; }
    virtual bool on_init() override;
    virtual std::string on_get_name() const override;
    std::string on_get_name_str() override { return "Move"; }
    virtual bool on_is_activable() const override;
    virtual void on_set_state() override;
    virtual void on_start_dragging() override;
    virtual void on_stop_dragging() override;
    /// <summary>
    /// Rotate by text on dragging rotate grabers
    /// </summary>
    /// <param name="mouse_event">Information about mouse</param>
    /// <returns>Propagete normaly return false.</returns>
    bool         on_mouse(const wxMouseEvent &mouse_event) override;
    virtual void on_update(const UpdateData& data) override;
    virtual void on_render() override;
    virtual void on_render_for_picking() override;
    virtual void on_render_input_window(float x, float y, float bottom_limit);

private:
    void   set_volume_by_selection();
    void   delete_texture(Emboss::Texture &texture);
    void   reset_volume();
    void   calculate_scale();
    float  get_scale_for_tolerance();
    std::vector<std::string> create_shape_warnings(const EmbossShape &shape, float scale);

    bool   process_job(bool make_snapshot = true); // process(bool make_snapshot = true)
    void   close();
    void   draw_window();
    bool   init_texture(Emboss::Texture &texture, const ExPolygonsWithIds &shapes_with_ids, unsigned max_size_px, const std::vector<std::string> &shape_warnings);
    void   draw_preview();
    void   draw_filename();
    void   draw_depth();
    void   draw_size();
    void   draw_use_surface();
    void   draw_distance();
    void   draw_rotation();
    void   draw_mirroring();
    void   draw_face_the_camera();
    void   draw_model_type();

    void volume_transformation_changed();

    // process mouse event
    bool   on_mouse_for_rotation(const wxMouseEvent &mouse_event);
    bool   on_mouse_for_translate(const wxMouseEvent &mouse_event);

private:
    struct GuiCfg;
    std::unique_ptr<const GuiCfg> m_gui_cfg;
    // actual selected only one volume - with emboss data
    ModelVolume *m_volume = nullptr;
    const GLVolume *   m_svg_volume{nullptr};
    // Is used to edit eboss and send changes to job
    // Inside volume is current state of shape WRT Volume
    EmbossShape m_volume_shape; // copy from m_volume for edit
                                // same index as volumes in
    std::vector<std::string> m_shape_warnings;
    // cancel for previous update of volume to cancel finalize part
    std::shared_ptr<std::atomic<bool>> m_job_cancel = nullptr;
    // When work with undo redo stack there could be situation that
    // m_volume point to unexisting volume so One need also objectID
    ObjectID m_volume_id;
    // move gizmo
    Grabber m_move_grabber;
    // Rotation gizmo
    GLGizmoRotate        m_rotate_gizmo;
    std::optional<float> m_angle;
    std::optional<float> m_distance;

    bool m_can_use_surface;
    // Value is set only when dragging rotation to calculate actual angle
    std::optional<float> m_rotate_start_angle;
    // TODO: it should be accessible by other gizmo too.
    // May be move to plater?
    RaycastManager m_raycast_manager;
    RaycastManager::AllowVolumes                         m_raycast_condition;
    std::map<GLVolume *, std::shared_ptr<PickRaycaster>> m_mesh_raycaster_map;//for text
    // When true keep up vector otherwise relative rotation
    bool m_keep_up = true;
    // Keep size aspect ratio when True.
    bool m_keep_ratio = true;
    // Keep data about dragging only during drag&drop
    std::optional<SurfaceDrag> m_surface_drag;
    // For volume on scaled objects
    std::optional<float> m_scale_width;
    std::optional<float> m_scale_height;
    std::optional<float> m_scale_depth;

    // keep SVG data rendered on GPU
    Emboss::Texture m_texture;
    // bounding box of shape
    // Note: Scaled mm to int value by m_volume_shape.scale
    BoundingBox         m_shape_bb;
    BoundingBox         m_origin_shape_bb;
    std::string         m_filename_preview;
    IconManager         m_icon_manager;
    IconManager::VIcons m_icons;
};



} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoMove_hpp_
