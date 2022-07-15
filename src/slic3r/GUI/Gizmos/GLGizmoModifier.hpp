#ifndef slic3r_GLGizmoModifier_hpp_
#define slic3r_GLGizmoModifier_hpp_

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/3DScene.hpp"


namespace Slic3r {

enum class ModelVolumeType : int;


namespace GUI {

class GLGizmoModifier : public GLGizmoBase
{
// This gizmo does not use grabbers. The m_hover_id relates to polygon managed by the class itself.

private:
    std::vector<void*> texture_ids;
public:
    static const std::vector<std::pair<std::string, std::string>> MODIFIER_SHAPES;
    GLGizmoModifier(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id);

protected:
    virtual bool on_init() override;
    virtual std::string on_get_name() const override;
    virtual bool on_is_activable() const override;
    virtual void on_start_dragging() override;
    virtual void on_render() override;
    virtual void on_render_for_picking() override;
    virtual void on_set_state() override;
    virtual void on_render_input_window(float x, float y, float bottom_limit);
    virtual CommonGizmosDataID on_get_requirements() const override;

    bool init_shape_texture(std::string image_name);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoFlatten_hpp_
