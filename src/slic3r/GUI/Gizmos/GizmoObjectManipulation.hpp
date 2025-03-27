#ifndef slic3r_GizmoObjectManipulation_hpp_
#define slic3r_GizmoObjectManipulation_hpp_

#include <memory>

#include "libslic3r/Point.hpp"
#include <float.h>

#include "slic3r/GUI/GUI_Geometry.hpp"

//#include "slic3r/GUI/GLCanvas3D.hpp"

namespace Slic3r {
namespace GUI {

class Selection;
class GLCanvas3D;

class GizmoObjectManipulation
{
public:
    static const double in_to_mm;
    static const double mm_to_in;
    static const double g_to_oz;
    static const double oz_to_g;

    struct Cache
    {
        Vec3d position;
        Vec3d position_rounded;
        Vec3d rotation;
        Vec3d rotation_rounded;
        Vec3d scale;
        Vec3d scale_rounded;
        Vec3d size;
        Vec3d size_rounded;

        wxString move_label_string;
        wxString rotate_label_string;
        wxString scale_label_string;

        Cache() { reset(); }
        void reset()
        {
            position = position_rounded = Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);
            rotation = rotation_rounded = Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);
            scale = scale_rounded = Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);
            size = size_rounded = Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);
            move_label_string = wxString();
            rotate_label_string = wxString();
            scale_label_string = wxString();
        }
        bool is_valid() const { return position != Vec3d(DBL_MAX, DBL_MAX, DBL_MAX); }
    };

    Cache m_cache;

    bool            m_imperial_units { false };
    
    // Mirroring buttons and their current state
    //enum MirrorButtonState {
    //    mbHidden,
    //    mbShown,
    //    mbActive
    //};
    //std::array<std::pair<ScalableButton*, MirrorButtonState>, 3> m_mirror_buttons;

    // Needs to be updated from OnIdle?
    bool            m_dirty = false;
    // Cached labels for the delayed update, not localized!
    std::string     m_new_title_string;
    std::string     m_new_move_label_string;
	std::string     m_new_rotate_label_string;
	std::string     m_new_scale_label_string;
    std::string     m_new_unit_string;
    Vec3d           m_new_position;
    Vec3d           m_new_rotation;
    Vec3d           m_new_scale;
    Vec3d           m_new_size;
    Vec3d           m_buffered_position;
    Vec3d           m_buffered_rotation;
    Vec3d           m_buffered_scale;
    Vec3d           m_buffered_size;
    bool            m_new_enabled {true};
    bool            m_uniform_scale {true};
    // Does the object manipulation panel work in World or Local coordinates?
    ECoordinatesType m_coordinates_type{ECoordinatesType::World};

    bool            m_show_clear_rotation { false };
    bool            m_show_clear_scale { false };
    bool            m_show_drop_to_bed { false };

protected:
    float last_move_input_window_width = 0.0f;
    float last_rotate_input_window_width = 0.0f;
    float last_scale_input_window_width = 0.0f;

public:
    GizmoObjectManipulation(GLCanvas3D& glcanvas);
    ~GizmoObjectManipulation() {}

    bool        IsShown();
    void        UpdateAndShow(const bool show);
    void update_ui_from_settings();

    void        set_dirty() { m_dirty = true; }
	// Called from the App to update the UI if dirty.
	void		update_if_dirty();

    void        set_uniform_scaling(const bool uniform_scale);
    bool        get_uniform_scaling() const { return m_uniform_scale; }
    // Does the object manipulation panel work in World or Local coordinates?
    void        set_coordinates_type(ECoordinatesType type);
    ECoordinatesType get_coordinates_type() const { return m_coordinates_type; }
    bool        is_world_coordinates() const { return m_coordinates_type == ECoordinatesType::World; }
    bool        is_instance_coordinates() const { return m_coordinates_type == ECoordinatesType::Instance; }
    bool        is_local_coordinates() const { return m_coordinates_type == ECoordinatesType::Local; }

    void reset_cache() { m_cache.reset(); }

    void on_change(const std::string& opt_key, int axis, double new_value);
    void do_render_move_window(ImGuiWrapper *imgui_wrapper, std::string window_name, float x, float y, float bottom_limit);
    void do_render_rotate_window(ImGuiWrapper *imgui_wrapper, std::string window_name, float x, float y, float bottom_limit);
    void do_render_scale_input_window(ImGuiWrapper* imgui_wrapper, std::string window_name, float x, float y, float bottom_limit);
    float max_unit_size(int number, Vec3d &vec1, Vec3d &vec2,std::string str);
    bool reset_button(ImGuiWrapper *imgui_wrapper, float caption_max, float unit_size, float space_size, float end_text_size);

private:
    void reset_settings_value();
    void update_settings_value(const Selection& selection);
    void update_buffered_value();

    // Show or hide scale/rotation reset buttons if needed
    void update_reset_buttons_visibility();
    //Show or hide mirror buttons
    //void update_mirror_buttons_visibility();

    // change values 
    void change_position_value(int axis, double value);
    void change_rotation_value(int axis, double value);
    void change_scale_value(int axis, double value);
    void change_size_value(int axis, double value);
    void do_scale(int axis, const Vec3d &scale) const;
    void reset_position_value();
    void reset_rotation_value();
    void reset_scale_value();

    GLCanvas3D& m_glcanvas;
    unsigned int m_last_active_item { 0 };
};

}}

#endif // slic3r_GizmoObjectManipulation_hpp_