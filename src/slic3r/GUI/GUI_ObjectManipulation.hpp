#ifndef slic3r_GUI_ObjectManipulation_hpp_
#define slic3r_GUI_ObjectManipulation_hpp_

#include <memory>

#include <wx/panel.h>

#include "Preset.hpp"

class wxBoxSizer;

namespace Slic3r {
namespace GUI {
class ConfigOptionsGroup;


class OG_Settings
{
protected:
    std::shared_ptr<ConfigOptionsGroup> m_og;
public:
    OG_Settings(wxWindow* parent, const bool staticbox);
    ~OG_Settings() {}

    wxSizer*        get_sizer();
};


class ObjectManipulation : public OG_Settings
{
    bool        m_is_percent_scale = false;         // true  -> percentage scale unit  
                                                    // false -> uniform scale unit  
    wxBoxSizer* m_extra_settings_sizer{ nullptr };  // sizer for extra Object/Part's settings

public:
    ObjectManipulation(wxWindow* parent);
    ~ObjectManipulation() {}

    int ol_selection();

    void update_values();
    // update position values displacements or "gizmos"
    void update_position_values();
    void update_position_values(const Vec3d& position);
    // update scale values after scale unit changing or "gizmos"
    void update_scale_values();
#if ENABLE_MODELINSTANCE_3D_FULL_TRANSFORM
    void update_scale_values(const Vec3d& scaling_factor);
#else
    void update_scale_values(double scaling_factor);
#endif // ENABLE_MODELINSTANCE_3D_FULL_TRANSFORM
    // update rotation values object selection changing
    void update_rotation_values();
    // update rotation value after "gizmos"
    void update_rotation_value(double angle, Axis axis);
#if ENABLE_MODELINSTANCE_3D_FULL_TRANSFORM
    void update_rotation_value(const Vec3d& rotation);
#endif // ENABLE_MODELINSTANCE_3D_FULL_TRANSFORM

};

}}

#endif // slic3r_GUI_ObjectManipulation_hpp_
