#ifndef slic3r_GLGizmoAdvancedCut_hpp_
#define slic3r_GLGizmoAdvancedCut_hpp_

#include "GLGizmoBase.hpp"
#include "GLGizmoRotate.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/CutUtils.hpp"

namespace Slic3r {
enum class CutConnectorType : int;
class ModelVolume;
struct CutConnectorAttributes;

namespace GUI {
enum class SLAGizmoEventType : unsigned char;

namespace CommonGizmosDataObjects {
class ObjectClipper;
}
class PartSelection
{
public:
    PartSelection() = default;
    PartSelection(
        const ModelObject *mo, const Transform3d &cut_matrix, int instance_idx, const Vec3d &center, const Vec3d &normal, const CommonGizmosDataObjects::ObjectClipper &oc);
    PartSelection(const ModelObject *mo, int instance_idx_in);
    ~PartSelection()
    {
        m_model.clear_objects();
        for (size_t i = 0; i < m_cut_parts.size(); i++) {
            if (m_cut_parts[i].raycaster) { delete m_cut_parts[i].raycaster; }
        }
    }

    struct PartPara
    {
        GLModel        glmodel;
        MeshRaycaster* raycaster;
        bool           is_up_part;
        Transform3d    trans;
        bool           is_modifier;
    };
    void         part_render(const Vec3d *cut_center, const Vec3d *normal);
    void         toggle_selection(const Vec2d &mouse_pos);
    void         toggle_selection(int id);
    void         turn_over_selection();
    ModelObject* model_object() { return m_model.objects.front(); }
    bool         valid() const { return m_valid; }
    bool         is_one_object() const;

    const std::vector<size_t> *get_ignored_contours_ptr() const { return (valid() ? &m_ignored_contours : nullptr); }

    std::vector<Cut::Part> get_cut_parts();
    std::vector<PartPara> &get_parts() { return m_cut_parts; }
    bool                   has_modified_cut_parts();

private:
    Model                                                            m_model;
    int                                                              m_instance_idx;
    std::vector<PartPara>                                            m_cut_parts;
    std::vector<bool>                                                m_back_cut_parts_state;
    bool                                                             m_valid = false;
    std::vector<std::pair<std::vector<size_t>, std::vector<size_t>>> m_contour_to_parts; // for each contour, there is a vector of parts above and a vector of parts below
    std::vector<size_t> m_ignored_contours; // contour that should not be rendered (the parts on both sides will both be parts of the same object)

    std::vector<Vec3d>              m_contour_points; // Debugging
    std::vector<std::vector<Vec3d>> m_debug_pts;      // Debugging
    void                            add_object(const ModelObject *object);
};

class GLGizmoAdvancedCut : public GLGizmoRotate3D
{
private:
    unsigned int m_last_active_item_imgui{0};
    double m_snap_step{1.0};
    // archived values
    Vec3d m_ar_plane_center{Vec3d::Zero()};

    // plane_center and so on
    Vec3d m_plane_center{Vec3d::Zero()};//old name:m_cut_plane_center
    Vec3d m_plane_center_drag_start{Vec3d::Zero()};
    Vec3d m_plane_drag_start{Vec3d::Zero()};
    Vec3d m_bb_center{Vec3d::Zero()};//box center
    Vec3d m_center_offset{Vec3d::Zero()};

    Vec3d m_plane_normal{Vec3d::UnitZ()}; //old namce:Vec3d m_cut_normal//m_cut_plane_normal
    Vec3d m_plane_x_direction{Vec3d::UnitY()};
    Vec3d m_clp_normal{Vec3d::Ones()};
    // data to check position of the cut palne center on gizmo activation
    Vec3d m_min_pos{Vec3d::Zero()};
    Vec3d m_max_pos{Vec3d::Zero()};

    static const double Offset;
    static const double Margin;
    static const std::array<float, 4> GrabberColor;
    static const std::array<float, 4> GrabberHoverColor;

    mutable double m_movement;
    double m_start_movement;
    double m_start_height;

    Vec3d m_rotation;

    Vec3d m_buffered_rotation;
    double m_buffered_movement;
    double m_buffered_height;

    Vec3d m_drag_pos_start;

    bool m_keep_upper;
    bool m_keep_lower;
    bool m_cut_to_parts{false};
    bool m_place_on_cut_upper{true};
    bool m_place_on_cut_lower{false};
    bool m_rotate_upper{false};
    bool m_rotate_lower{false};

    bool m_do_segment;
    double m_segment_smoothing_alpha;
    int m_segment_number;

    mutable Grabber m_move_z_grabber;
    mutable Grabber m_move_x_grabber;

    bool m_connectors_editing{false};
    bool m_add_connector_ok{false};
    std::vector<size_t> m_invalid_connectors_idxs;
    bool m_show_shortcuts{false};

    std::vector<std::pair<wxString, wxString>> m_connector_shortcuts;
    std::vector<std::pair<wxString, wxString>> m_cut_plane_shortcuts;
    std::vector<std::pair<wxString, wxString>> m_cut_groove_shortcuts;
    double m_label_width{150.0};
    double m_control_width{ 200.0 };
    double m_editing_window_width;

    CutMode                  m_cut_mode{CutMode::cutPlanar};
    CutConnectorType         m_connector_type;
    size_t                   m_connector_style;
    size_t                   m_connector_shape_id;

      // Dovetail para
    Groove             m_groove;
    bool               m_groove_editing{false};
    float              m_contour_width{0.4f};
    float              m_cut_plane_radius_koef{1.5f};
    float              m_shortcut_label_width{-1.f};
    bool               m_is_slider_editing_done{false};
    bool               m_hide_cut_plane{false};
    double             m_radius{0.0};
    double             m_grabber_radius{0.0};
    double             m_grabber_connection_len{0.0};
    Vec3d              m_cut_plane_start_move_pos{Vec3d::Zero()};
    bool               m_cut_plane_as_circle{false};
    std::vector<Vec3d> m_groove_vertices;
    bool               m_was_cut_plane_dragged{false};
    bool               m_was_contour_selected{false};
    bool               m_is_dragging{false};
    std::shared_ptr<PartSelection>    m_part_selection{nullptr};
    // dragging angel in hovered axes
    double             m_rotate_angle{0.0};
    bool               m_imperial_units{false};
    BoundingBoxf3      m_bounding_box;
    BoundingBoxf3      m_transformed_bounding_box;

    float m_connector_depth_ratio{3.f};
    float m_connector_depth_ratio_tolerance{CUT_TOLERANCE};

    float m_connector_size{2.5f};
    float m_connector_size_tolerance{CUT_TOLERANCE};
    // Input params for cut with snaps
    float        m_snap_space_proportion{0.3f};
    float        m_snap_bulge_proportion{0.15f};

    TriangleMesh m_connector_mesh;

    // remember the connectors which is selected
    mutable std::vector<bool> m_selected;
    int                       m_selected_count{0};

    GLModel m_plane; // old name:PickingModel

    Vec3d m_cut_line_begin{Vec3d::Zero()};
    Vec3d m_cut_line_end{Vec3d::Zero()};

    Transform3d m_rotate_matrix{Transform3d::Identity()};
    Transform3d m_start_dragging_m{Transform3d::Identity()};
    std::map<CutConnectorAttributes, GLModel> m_shapes;

    struct InvalidConnectorsStatistics
    {
        unsigned int outside_cut_contour;
        unsigned int outside_bb;
        bool         is_overlap;

        void invalidate()
        {
            outside_cut_contour = 0;
            outside_bb          = 0;
            is_overlap          = false;
        }
    } m_info_stats;

    //GLSelectionRectangle m_selection_rectangle;

public:
    GLGizmoAdvancedCut(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    bool gizmo_event(SLAGizmoEventType action, const Vec2d &mouse_position, bool shift_down, bool alt_down, bool control_down);
    bool on_key(wxKeyEvent &evt);

    double get_movement() const { return m_movement; }
    void finish_rotation();
    std::string get_tooltip() const override;

    BoundingBoxf3 bounding_box() const;
    BoundingBoxf3 transformed_bounding_box(const Vec3d &plane_center, const Transform3d &rotation_m = Transform3d::Identity()) const;

    bool is_looking_forward() const;

    bool unproject_on_cut_plane(const Vec2d &mouse_pos, Vec3d &pos, Vec3d &pos_world, bool respect_contours = true);

    virtual bool apply_clipping_plane() { return m_connectors_editing; }

protected:
    virtual bool on_init();
    virtual void on_load(cereal::BinaryInputArchive &ar) override;
    virtual void on_save(cereal::BinaryOutputArchive &ar) const override;
    virtual void data_changed(bool is_serializing) override;
    virtual std::string on_get_name() const;
    virtual std::string on_get_name_str() override { return "Cut"; }
    virtual void on_set_state();
    void         close();
    virtual bool on_is_activable() const;
    virtual CommonGizmosDataID on_get_requirements() const override;
    virtual void on_start_dragging() override;
    virtual void on_stop_dragging() override;
    virtual void update_plate_center(Axis axis_type, double projection, bool is_abs_move); // old name:dragging_grabber_move
    virtual void update_plate_normal_boundingbox_clipper(const Transform3d &rotation_tmp); // old name:dragging_grabber_rotation
    virtual void on_update(const UpdateData& data);
    virtual void on_render();
    virtual void on_render_for_picking();
    virtual void on_render_input_window(float x, float y, float bottom_limit);

    void show_tooltip_information(float x, float y);

    virtual void on_enable_grabber(unsigned int id)
    {
        if (id < 3)
            m_gizmos[id].enable_grabber(0);
        else if (id == 3)
            this->enable_grabber(0);
    }

    virtual void on_disable_grabber(unsigned int id)
    {
        if (id < 3)
            m_gizmos[id].disable_grabber(0);
        else if (id == 3)
            this->disable_grabber(0);
    }

    virtual void on_set_hover_id()
    {
        for (int i = 0; i < 3; ++i)
            m_gizmos[i].set_hover_id((m_hover_id == i) ? 0 : -1);
    }

private:
    void perform_cut(const Selection& selection);
    bool can_perform_cut() const;
    void apply_connectors_in_model(ModelObject *mo, int &dowels_count);

    bool is_selection_changed(bool alt_down, bool shift_down);
    void select_connector(int idx, bool select);

    double calc_projection(const Vec3d &drag_pos, const Linef3 &mouse_ray, const Vec3d &project_dir) const;

    Vec3d get_plane_normal() const;
    Vec3d get_plane_center() const;

    void reset_cut_plane();
    void reset_all();

    // update the connectors position so that the connectors are on the cut plane
    void put_connectors_on_cut_plane(const Vec3d &cp_normal, double cp_offset);
    void update_plane_normal();
    void update_clipper();
    // on render
    void render_cut_plane_and_grabbers();
    void on_render_rotate_gizmos();
    void render_connectors();
    void render_clipper_cut();
    void render_cut_line();

    void clear_selection();
    void init_connector_shapes();
    void set_connectors_editing(bool connectors_editing);
    void reset_connectors();
    void update_connector_shape();
    void apply_selected_connectors(std::function<void(size_t idx)> apply_fn);
    void select_all_connectors();
    void unselect_all_connectors();
    void validate_connector_settings();
    bool add_connector(CutConnectors &connectors, const Vec2d &mouse_position);
    bool delete_selected_connectors();
    bool is_outside_of_cut_contour(size_t idx, const CutConnectors &connectors, const Vec3d cur_pos);
    bool is_conflict_for_connector(size_t idx, const CutConnectors &connectors, const Vec3d cur_pos);
    //deal groove
    void switch_to_mode(CutMode new_mode);
    void flip_cut_plane();
    void update_plane_model();
    void init_picking_models();
    bool has_valid_groove() const;
    bool has_valid_contour() const;
    void reset_cut_by_contours();
    void render_flip_plane_button(bool disable_pred = false);
    void process_contours();
    void toggle_model_objects_visibility(bool show_in_3d = false);
    void deal_connector_pos_by_type(Vec3d &pos, float &height, CutConnectorType, CutConnectorStyle, bool looking_forward, bool is_edit, const Vec3d &clp_normal);
    void update_bb();
    void check_and_update_connectors_state();
    void set_center(const Vec3d &center, bool update_tbb = false);
    bool set_center_pos(const Vec3d &center_pos, bool update_tbb = false);
    void invalidate_cut_plane();
    void rotate_vec3d_around_plane_center(Vec3d &vec, const Transform3d &rotate_matrix, const Vec3d &center);
    Transform3d get_cut_matrix(const Selection &selection);
    // render input window
    void update_buffer_data();
    bool render_cut_mode_combo(double label_width,float item_width);
    void render_color_marker(float size, const ColorRGBA &color);
    void render_cut_plane_input_window(float x, float y, float bottom_limit);
    void init_connectors_input_window_data();
    void render_connectors_input_window(float x, float y, float bottom_limit);
    void render_input_window_warning() const;
    bool render_reset_button(const std::string &label_id, const std::string &tooltip) const;
    bool render_connect_type_radio_button(CutConnectorType type);

    bool render_slider_double_input(const std::string &label, float &value_in, float &tolerance_in);
    bool render_slider_double_input_by_format(const std::string &label, float &value_in, float value_min, float value_max, DoubleShowType show_type = DoubleShowType::Normal);
    bool cut_line_processing() const;
    void discard_cut_line_processing();
    bool process_cut_line(SLAGizmoEventType action, const Vec2d &mouse_position);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoAdvancedCut_hpp_