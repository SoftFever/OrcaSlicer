#ifndef slic3r_GUI_ObjectManipulation_hpp_
#define slic3r_GUI_ObjectManipulation_hpp_

#include <memory>

#include "GUI_ObjectSettings.hpp"
#include "GLCanvas3D.hpp"

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
        Vec3d rotation;
        Vec3d scale;
        Vec3d size;

        std::string move_label_string;
        std::string rotate_label_string;
        std::string scale_label_string;

        struct Instance
        {
            int object_idx;
            int instance_idx;
            Vec3d box_size;

            Instance() { reset(); }
            void reset() { this->object_idx = -1; this->instance_idx = -1; this->box_size = Vec3d::Zero(); }
            void set(int object_idx, int instance_idx, const Vec3d& box_size) { this->object_idx = object_idx; this->instance_idx = instance_idx; this->box_size = box_size; }
            bool matches(int object_idx, int instance_idx) const { return (this->object_idx == object_idx) && (this->instance_idx == instance_idx); }
            bool matches_object(int object_idx) const { return (this->object_idx == object_idx); }
            bool matches_instance(int instance_idx) const { return (this->instance_idx == instance_idx); }
        };

        Instance instance;

        Cache() { reset(); }
        void reset()
        {
            position = Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);
            rotation = Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);
            scale = Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);
            size = Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);
            move_label_string = "";
            rotate_label_string = "";
            scale_label_string = "";
            instance.reset();
        }
        bool is_valid() const { return position != Vec3d(DBL_MAX, DBL_MAX, DBL_MAX); }
    };

    Cache m_cache;

    wxStaticText*   m_move_Label = nullptr;
    wxStaticText*   m_scale_Label = nullptr;
    wxStaticText*   m_rotate_Label = nullptr;

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
    LockButton*     m_lock_bnt{ nullptr };

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

    void        update_settings_value(const Selection& selection);

	// Called from the App to update the UI if dirty.
	void		update_if_dirty();

    void        set_uniform_scaling(const bool uniform_scale) { m_uniform_scale = uniform_scale;}
    bool        get_uniform_scaling() const { return m_uniform_scale; }

    void reset_cache() { m_cache.reset(); }
#ifndef __APPLE__
    // On Windows and Linux, emulates a kill focus event on the currently focused option (if any)
    // Used only in ObjectList wxEVT_DATAVIEW_SELECTION_CHANGED handler which is called before the regular kill focus event
    // bound to this class when changing selection in the objects list
    void emulate_kill_focus();
#endif // __APPLE__

    void update_warning_icon_state(/*const wxString& tooltip*/);
    void msw_rescale();

private:
    void reset_settings_value();

    // update size values after scale unit changing or "gizmos"
    void update_size_value(const Vec3d& size);
    // update rotation value after "gizmos"
    void update_rotation_value(const Vec3d& rotation);

    // change values 
    void    change_position_value(const Vec3d& position);
    void    change_rotation_value(const Vec3d& rotation);
    void    change_scale_value(const Vec3d& scale);
    void    change_size_value(const Vec3d& size);

    void on_change(const t_config_option_key& opt_key, const boost::any& value);
    void on_fill_empty_value(const std::string& opt_key);
};

}}

#endif // slic3r_GUI_ObjectManipulation_hpp_
