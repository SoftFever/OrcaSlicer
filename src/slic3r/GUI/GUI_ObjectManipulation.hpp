#ifndef slic3r_GUI_ObjectManipulation_hpp_
#define slic3r_GUI_ObjectManipulation_hpp_

#include <memory>

#include "GUI_ObjectSettings.hpp"
#include "GLCanvas3D.hpp"

class wxBitmapComboBox;
class wxStaticText;
class LockButton;
class wxStaticBitmap;

namespace Slic3r {
namespace GUI {

class Selection;

class ObjectManipulation : public OG_Settings
{
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

    wxStaticText*   m_move_Label = nullptr;
    wxStaticText*   m_scale_Label = nullptr;
    wxStaticText*   m_rotate_Label = nullptr;

    // Non-owning pointers to the reset buttons, so we can hide and show them.
    ScalableButton* m_reset_scale_button = nullptr;
    ScalableButton* m_reset_rotation_button = nullptr;
    ScalableButton* m_drop_to_bed_button = nullptr;

    // Mirroring buttons and their current state
    enum MirrorButtonState {
        mbHidden,
        mbShown,
        mbActive
    };
    std::array<std::pair<ScalableButton*, MirrorButtonState>, 3> m_mirror_buttons;

    // Bitmaps for the mirroring buttons.
    ScalableBitmap m_mirror_bitmap_on;
    ScalableBitmap m_mirror_bitmap_off;
    ScalableBitmap m_mirror_bitmap_hidden;

    // Needs to be updated from OnIdle?
    bool            m_dirty = false;
    // Cached labels for the delayed update, not localized!
    std::string     m_new_move_label_string;
	std::string     m_new_rotate_label_string;
	std::string     m_new_scale_label_string;
    Vec3d           m_new_position;
    Vec3d           m_new_rotation;
    Vec3d           m_new_scale;
    Vec3d           m_new_size;
    bool            m_new_enabled;
    bool            m_uniform_scale {true};
    // Does the object manipulation panel work in World or Local coordinates?
    bool            m_world_coordinates = true;
    LockButton*     m_lock_bnt{ nullptr };
    wxBitmapComboBox* m_word_local_combo = nullptr;

    ScalableBitmap  m_manifold_warning_bmp;
    wxStaticBitmap* m_fix_throught_netfab_bitmap;

#ifndef __APPLE__
    // Currently focused option name (empty if none)
    std::string     m_focused_option;
#endif // __APPLE__

public:
    ObjectManipulation(wxWindow* parent);
    ~ObjectManipulation() {}

    void        Show(const bool show) override;
    bool        IsShown() override;
    void        UpdateAndShow(const bool show) override;

    void        set_dirty() { m_dirty = true; }
	// Called from the App to update the UI if dirty.
	void		update_if_dirty();

    void        set_uniform_scaling(const bool uniform_scale);
    bool        get_uniform_scaling() const { return m_uniform_scale; }
    // Does the object manipulation panel work in World or Local coordinates?
    void        set_world_coordinates(const bool world_coordinates) { m_world_coordinates = world_coordinates; this->UpdateAndShow(true); }
    bool        get_world_coordinates() const { return m_world_coordinates; }

    void reset_cache() { m_cache.reset(); }
#ifndef __APPLE__
    // On Windows and Linux, emulates a kill focus event on the currently focused option (if any)
    // Used only in ObjectList wxEVT_DATAVIEW_SELECTION_CHANGED handler which is called before the regular kill focus event
    // bound to this class when changing selection in the objects list
    void emulate_kill_focus();
#endif // __APPLE__

    void update_warning_icon_state(const wxString& tooltip);
    void msw_rescale();

private:
    void reset_settings_value();
    void update_settings_value(const Selection& selection);

    // Show or hide scale/rotation reset buttons if needed
    void update_reset_buttons_visibility();
    //Show or hide mirror buttons
    void update_mirror_buttons_visibility();

    // change values 
    void change_position_value(int axis, double value, const std::string& snapshot_name);
    void change_rotation_value(int axis, double value, const std::string& snapshot_name);
    void change_scale_value(int axis, double value,  const std::string& snapshot_name);
    void change_size_value(int axis, double value, const std::string& snapshot_name);
    void do_scale(int axis, const Vec3d &scale, const std::string& snapshot_name) const;

    void on_change(t_config_option_key opt_key, const boost::any& value);
    void on_fill_empty_value(const std::string& opt_key);
};

}}

#endif // slic3r_GUI_ObjectManipulation_hpp_
