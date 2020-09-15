#ifndef slic3r_GLGizmoFdmSupports_hpp_
#define slic3r_GLGizmoFdmSupports_hpp_

#include "GLGizmoPainterBase.hpp"

namespace Slic3r {

namespace GUI {

class GLGizmoFdmSupports : public GLGizmoPainterBase
{
public:
    GLGizmoFdmSupports(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
        : GLGizmoPainterBase(parent, icon_filename, sprite_id) {}

protected:
    void on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;

private:
    bool on_init() override;
    void on_render() const override;
    void on_render_for_picking() const override {}

    void update_model_object() const override;
    void update_from_model_object() override;

    void on_opening() override {}
    void on_shutdown() override;
    PainterGizmoType get_painter_type() const override;

    void select_facets_by_angle(float threshold, bool block);
    float m_angle_threshold_deg = 45.f;
    bool m_setting_angle = false;

    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    std::map<std::string, wxString> m_desc;
};



} // namespace GUI
} // namespace Slic3r


#endif // slic3r_GLGizmoFdmSupports_hpp_
