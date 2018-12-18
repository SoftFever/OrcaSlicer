#ifndef slic3r_GUI_ObjectManipulation_hpp_
#define slic3r_GUI_ObjectManipulation_hpp_

#include <memory>

#include "GUI_ObjectSettings.hpp"
#include "GLCanvas3D.hpp"

class wxStaticText;

namespace Slic3r {
namespace GUI {


class ObjectManipulation : public OG_Settings
{
    Vec3d       cache_position   { 0., 0., 0. };
    Vec3d       cache_rotation   { 0., 0., 0. };
    Vec3d       cache_scale      { 100., 100., 100. };
    Vec3d       cache_size       { 0., 0., 0. };

    wxStaticText*   m_move_Label = nullptr;
    wxStaticText*   m_scale_Label = nullptr;
    wxStaticText*   m_rotate_Label = nullptr;

public:
    ObjectManipulation(wxWindow* parent);
    ~ObjectManipulation() {}

    void        Show(const bool show) override;
    bool        IsShown() override;
    void        UpdateAndShow(const bool show) override;

    int ol_selection();

    void update_settings_value(const GLCanvas3D::Selection& selection);
    void reset_settings_value();
    void reset_position_value();
    void reset_rotation_value();
    void reset_scale_value();
    void reset_size_value();

    // update position values displacements or "gizmos"
    void update_position_value(const Vec3d& position);
    // update scale values after scale unit changing or "gizmos"
    void update_scale_value(const Vec3d& scaling_factor);
    // update size values after scale unit changing or "gizmos"
    void update_size_value(const Vec3d& size);
    // update rotation value after "gizmos"
    void update_rotation_value(const Vec3d& rotation);

    // change values 
    void    change_position_value(const Vec3d& position);
    void    change_rotation_value(const Vec3d& rotation);
    void    change_scale_value(const Vec3d& scale);
    void    change_size_value(const Vec3d& size);
};

}}

#endif // slic3r_GUI_ObjectManipulation_hpp_
