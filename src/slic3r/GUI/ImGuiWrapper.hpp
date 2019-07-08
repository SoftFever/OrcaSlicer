#ifndef slic3r_ImGuiWrapper_hpp_
#define slic3r_ImGuiWrapper_hpp_

#include <string>
#include <map>

#include <imgui/imgui.h>

#include "libslic3r/Point.hpp"

class wxString;
class wxMouseEvent;
class wxKeyEvent;


namespace Slic3r {
namespace GUI {

class ImGuiWrapper
{
    const ImWchar *m_glyph_ranges;
    // Chinese, Japanese, Korean
    bool m_font_cjk;
    float m_font_size;
    unsigned m_font_texture;
    float m_style_scaling;
    unsigned m_mouse_buttons;
    bool m_disabled;
    bool m_new_frame_open;
    std::string m_clipboard_text;

public:
    ImGuiWrapper();
    ~ImGuiWrapper();

    void read_glsl_version();

    void set_language(const std::string &language);
    void set_display_size(float w, float h);
    void set_scaling(float font_size, float scale_style, float scale_both);
    bool update_mouse_data(wxMouseEvent &evt);
    bool update_key_data(wxKeyEvent &evt);

    float get_font_size() const { return m_font_size; }
    float get_style_scaling() const { return m_style_scaling; }

    void new_frame();
    void render();

    float scaled(float x) const { return x * m_font_size; }
    ImVec2 scaled(float x, float y) const { return ImVec2(x * m_font_size, y * m_font_size); }
    ImVec2 calc_text_size(const wxString &text);

    void set_next_window_pos(float x, float y, int flag);
    void set_next_window_bg_alpha(float alpha);

    bool begin(const std::string &name, int flags = 0);
    bool begin(const wxString &name, int flags = 0);
    void end();

    bool button(const wxString &label);
    bool radio_button(const wxString &label, bool active);
    bool input_double(const std::string &label, const double &value, const std::string &format = "%.3f");
    bool input_vec3(const std::string &label, const Vec3d &value, float width, const std::string &format = "%.3f");
    bool checkbox(const wxString &label, bool &value);
    void text(const char *label);
    void text(const std::string &label);
    void text(const wxString &label);
    bool combo(const wxString& label, const std::vector<std::string>& options, int& selection);   // Use -1 to not mark any option as selected
    bool multi_sel_list(const wxString& label, const std::vector<std::string>& options, int& selection);   // Use -1 to not mark any option as selected

    void disabled_begin(bool disabled);
    void disabled_end();

    bool want_mouse() const;
    bool want_keyboard() const;
    bool want_text_input() const;
    bool want_any_input() const;

private:
    void init_font(bool compress);
    void init_input();
    void init_style();
    void render_draw_data(ImDrawData *draw_data);
    bool display_initialized() const;
    void destroy_font();

    static const char* clipboard_get(void* user_data);
    static void clipboard_set(void* user_data, const char* text);
};


} // namespace GUI
} // namespace Slic3r

#endif // slic3r_ImGuiWrapper_hpp_

