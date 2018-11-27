#ifndef slic3r_ImGuiWrapper_hpp_
#define slic3r_ImGuiWrapper_hpp_

#include <string>
#include <map>

#include <imgui/imgui.h>

#include "libslic3r/Point.hpp"

class wxString;
class wxMouseEvent;


namespace Slic3r {
namespace GUI {

class ImGuiWrapper
{
    std::string m_glsl_version_string;
    unsigned m_glsl_version;
    unsigned m_shader_handle;
    unsigned m_vert_handle;
    unsigned m_frag_handle;
    unsigned m_vbo_handle;
    unsigned m_elements_handle;
    int m_attrib_location_tex;
    int m_attrib_location_proj_mtx;
    int m_attrib_location_position;
    int m_attrib_location_uv;
    int m_attrib_location_color;

    typedef std::map<std::string, ImFont*> FontsMap;

    FontsMap m_fonts;
    unsigned m_font_texture;

    unsigned m_mouse_buttons;

public:
    ImGuiWrapper();
    ~ImGuiWrapper();

    bool init();
    void read_glsl_version();

    void set_display_size(float w, float h);
    bool update_mouse_data(wxMouseEvent &evt);

    void new_frame();
    void render();

    void set_next_window_pos(float x, float y, int flag);
    void set_next_window_bg_alpha(float alpha);

    bool begin(const std::string &name, int flags = 0);
    bool begin(const wxString &name, int flags = 0);
    void end();

    bool button(const wxString &label);
    bool input_double(const std::string &label, const double &value, const std::string &format = "%.3f");
    bool input_vec3(const std::string &label, const Vec3d &value, float width, const std::string &format = "%.3f");
    bool checkbox(const wxString &label, bool &value);
    void text(const wxString &label);

    bool want_mouse() const;
    bool want_keyboard() const;
    bool want_text_input() const;
    bool want_any_input() const;
private:
    void create_device_objects();
    void create_fonts_texture();
    bool check_program(unsigned int handle, const char *desc);
    bool check_shader(unsigned int handle, const char *desc);
    void render_draw_data(ImDrawData *draw_data);
    void destroy_device_objects();
    void destroy_fonts_texture();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_ImGuiWrapper_hpp_

