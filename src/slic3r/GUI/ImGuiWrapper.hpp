#ifndef slic3r_ImGuiWrapper_hpp_
#define slic3r_ImGuiWrapper_hpp_

#include <string>
#include <map>

#include <imgui/imgui.h>

#include "libslic3r/Point.hpp"

namespace Slic3r {namespace Search {
struct OptionViewParameters;
}}

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

    void set_next_window_pos(float x, float y, int flag, float pivot_x = 0.0f, float pivot_y = 0.0f);
    void set_next_window_bg_alpha(float alpha);
	void set_next_window_size(float x, float y, ImGuiCond cond);

    bool begin(const std::string &name, int flags = 0);
    bool begin(const wxString &name, int flags = 0);
    bool begin(const std::string& name, bool* close, int flags = 0);
    bool begin(const wxString& name, bool* close, int flags = 0);
    void end();

    bool button(const wxString &label);
	bool button(const wxString& label, float width, float height);
    bool radio_button(const wxString &label, bool active);
	bool image_button();
    bool input_double(const std::string &label, const double &value, const std::string &format = "%.3f");
    bool input_double(const wxString &label, const double &value, const std::string &format = "%.3f");
    bool input_vec3(const std::string &label, const Vec3d &value, float width, const std::string &format = "%.3f");
    bool checkbox(const wxString &label, bool &value);
    void text(const char *label);
    void text(const std::string &label);
    void text(const wxString &label);
    void text_colored(const ImVec4& color, const char* label);
    void text_colored(const ImVec4& color, const std::string& label);
    void text_colored(const ImVec4& color, const wxString& label);
    bool slider_float(const char* label, float* v, float v_min, float v_max, const char* format = "%.3f", float power = 1.0f);
    bool slider_float(const std::string& label, float* v, float v_min, float v_max, const char* format = "%.3f", float power = 1.0f);
    bool slider_float(const wxString& label, float* v, float v_min, float v_max, const char* format = "%.3f", float power = 1.0f);
    bool combo(const wxString& label, const std::vector<std::string>& options, int& selection);   // Use -1 to not mark any option as selected
    bool undo_redo_list(const ImVec2& size, const bool is_undo, bool (*items_getter)(const bool, int, const char**), int& hovered, int& selected, int& mouse_wheel);
    void search_list(const ImVec2& size, bool (*items_getter)(int, const char** label, const char** tooltip), char* search_str,
                     Search::OptionViewParameters& view_params, int& selected, bool& edited, int& mouse_wheel, bool is_localized);
    void title(const std::string& str);

    void disabled_begin(bool disabled);
    void disabled_end();

    bool want_mouse() const;
    bool want_keyboard() const;
    bool want_text_input() const;
    bool want_any_input() const;

    static const ImVec4 COL_WINDOW_BACKGROND;
    static const ImVec4 COL_GREY_DARK;
    static const ImVec4 COL_GREY_LIGHT;
    static const ImVec4 COL_ORANGE_DARK;
    static const ImVec4 COL_ORANGE_LIGHT;

private:
    void init_font(bool compress);
    void init_input();
    void init_style();
    void render_draw_data(ImDrawData *draw_data);
    bool display_initialized() const;
    void destroy_font();
    std::vector<unsigned char> load_svg(const std::string& bitmap_name, unsigned target_width, unsigned target_height);

    static const char* clipboard_get(void* user_data);
    static void clipboard_set(void* user_data, const char* text);
};


} // namespace GUI
} // namespace Slic3r

#endif // slic3r_ImGuiWrapper_hpp_

