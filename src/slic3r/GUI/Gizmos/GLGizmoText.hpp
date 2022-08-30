#ifndef slic3r_GLGizmoText_hpp_
#define slic3r_GLGizmoText_hpp_

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/3DScene.hpp"


namespace Slic3r {

enum class ModelVolumeType : int;

namespace GUI {

class GLGizmoText : public GLGizmoBase
{
private:
    //std::vector<std::string> m_avail_font_names;
    char m_text[1024] = { 0 };
    std::string m_font_name;
    float m_font_size = 16.f;
    int m_curr_font_idx = 0;
    bool m_bold = true;
    bool m_italic = false;
    float m_thickness = 2.f;

public:
    GLGizmoText(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

protected:
    virtual bool on_init() override;
    virtual std::string on_get_name() const override;
    virtual bool on_is_activable() const override;
    virtual void on_render() override;
    virtual void on_render_for_picking() override;
    void push_combo_style(const float scale);
    void pop_combo_style();
    virtual void on_set_state() override;
    virtual CommonGizmosDataID on_get_requirements() const override;
    virtual void on_render_input_window(float x, float y, float bottom_limit);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoText_hpp_