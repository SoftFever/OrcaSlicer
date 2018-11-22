#ifndef slic3r_GUI_ObjectManipulation_hpp_
#define slic3r_GUI_ObjectManipulation_hpp_

#include <memory>

#include "GUI_ObjectSettings.hpp"
#include "GLCanvas3D.hpp"


namespace Slic3r {
namespace GUI {


class ObjectManipulation : public OG_Settings
{
    bool        m_is_percent_scale = false;         // true  -> percentage scale unit  
                                                    // false -> uniform scale unit  
    bool        m_is_uniform_scale = false;         // It indicates if scale is uniform

    Vec3d       cache_position   { 0., 0., 0. };
    wxStaticText*   m_move_Label = nullptr;

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

    void update_values();
    // update position values displacements or "gizmos"
    void update_position_values();
    void update_position_value(const Vec3d& position);
    // update scale values after scale unit changing or "gizmos"
    void update_scale_values();
    void update_scale_value(const Vec3d& scaling_factor);
    // update rotation values object selection changing
    void update_rotation_values();
    // update rotation value after "gizmos"
    void update_rotation_value(double angle, Axis axis);
    void update_rotation_value(const Vec3d& rotation);

    void set_uniform_scaling(const bool uniform_scale) { m_is_uniform_scale = uniform_scale; }


    // change values 
    void    change_position_value(const Vec3d& position);
    void    change_rotation_value(const Vec3d& rotation);
    void    change_scale_value(const Vec3d& scale);


private:
    void    print_cashe_value(const std::string& label, const Vec3d& value);
};

}}

#endif // slic3r_GUI_ObjectManipulation_hpp_
