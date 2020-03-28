#include "ImGuiWrapper.hpp"

#include <cstdio>
#include <vector>
#include <cmath>
#include <stdexcept>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include <wx/string.h>
#include <wx/event.h>
#include <wx/clipbrd.h>
#include <wx/debug.h>

#include <GL/glew.h>

#include <imgui/imgui_internal.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"
#include "3DScene.hpp"
#include "GUI.hpp"

namespace Slic3r {
namespace GUI {


ImGuiWrapper::ImGuiWrapper()
    : m_glyph_ranges(nullptr)
    , m_font_cjk(false)
    , m_font_size(18.0)
    , m_font_texture(0)
    , m_style_scaling(1.0)
    , m_mouse_buttons(0)
    , m_disabled(false)
    , m_new_frame_open(false)
{
    ImGui::CreateContext();

    init_input();
    init_style();

    ImGui::GetIO().IniFilename = nullptr;
}

ImGuiWrapper::~ImGuiWrapper()
{
    destroy_font();
    ImGui::DestroyContext();
}

void ImGuiWrapper::set_language(const std::string &language)
{
    if (m_new_frame_open) {
        // ImGUI internally locks the font between NewFrame() and EndFrame()
        // NewFrame() might've been called here because of input from the 3D scene;
        // call EndFrame()
        ImGui::EndFrame();
        m_new_frame_open = false;
    }

    const ImWchar *ranges = nullptr;
    size_t idx = language.find('_');
    std::string lang = (idx == std::string::npos) ? language : language.substr(0, idx);
    static const ImWchar ranges_latin2[] =
    {
        0x0020, 0x00FF, // Basic Latin + Latin Supplement
        0x0100, 0x017F, // Latin Extended-A
        0,
    };
	static const ImWchar ranges_turkish[] = {
		0x0020, 0x01FF, // Basic Latin + Latin Supplement
		0x0100, 0x017F, // Latin Extended-A
		0x0180, 0x01FF, // Turkish
		0,
	};
    static const ImWchar ranges_vietnamese[] =
    {
        0x0020, 0x00FF, // Basic Latin
        0x0102, 0x0103,
        0x0110, 0x0111,
        0x0128, 0x0129,
        0x0168, 0x0169,
        0x01A0, 0x01A1,
        0x01AF, 0x01B0,
        0x1EA0, 0x1EF9,
        0,
    };
    m_font_cjk = false;
    if (lang == "cs" || lang == "pl") {
        ranges = ranges_latin2;
    } else if (lang == "ru" || lang == "uk") {
        ranges = ImGui::GetIO().Fonts->GetGlyphRangesCyrillic(); // Default + about 400 Cyrillic characters
    } else if (lang == "tr") {
        ranges = ranges_turkish;
    } else if (lang == "vi") {
        ranges = ranges_vietnamese;
    } else if (lang == "ja") {
        ranges = ImGui::GetIO().Fonts->GetGlyphRangesJapanese(); // Default + Hiragana, Katakana, Half-Width, Selection of 1946 Ideographs
        m_font_cjk = true;
    } else if (lang == "ko") {
        ranges = ImGui::GetIO().Fonts->GetGlyphRangesKorean(); // Default + Korean characters
        m_font_cjk = true;
    } else if (lang == "zh") {
        ranges = (language == "zh_TW") ?
            // Traditional Chinese
            // Default + Half-Width + Japanese Hiragana/Katakana + full set of about 21000 CJK Unified Ideographs
            ImGui::GetIO().Fonts->GetGlyphRangesChineseFull() :
            // Simplified Chinese
            // Default + Half-Width + Japanese Hiragana/Katakana + set of 2500 CJK Unified Ideographs for common simplified Chinese
            ImGui::GetIO().Fonts->GetGlyphRangesChineseSimplifiedCommon();
        m_font_cjk = true;
    } else if (lang == "th") {
        ranges = ImGui::GetIO().Fonts->GetGlyphRangesThai(); // Default + Thai characters
    } else {
        ranges = ImGui::GetIO().Fonts->GetGlyphRangesDefault(); // Basic Latin, Extended Latin
    }

    if (ranges != m_glyph_ranges) {
        m_glyph_ranges = ranges;
        destroy_font();
    }
}

void ImGuiWrapper::set_display_size(float w, float h)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(w, h);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
}

void ImGuiWrapper::set_scaling(float font_size, float scale_style, float scale_both)
{
    font_size *= scale_both;
    scale_style *= scale_both;

    if (m_font_size == font_size && m_style_scaling == scale_style) {
        return;
    }

    m_font_size = font_size;

    ImGui::GetStyle().ScaleAllSizes(scale_style / m_style_scaling);
    m_style_scaling = scale_style;

    destroy_font();
}

bool ImGuiWrapper::update_mouse_data(wxMouseEvent& evt)
{
    if (! display_initialized()) {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.MousePos = ImVec2((float)evt.GetX(), (float)evt.GetY());
    io.MouseDown[0] = evt.LeftIsDown();
    io.MouseDown[1] = evt.RightIsDown();
    io.MouseDown[2] = evt.MiddleIsDown();

    unsigned buttons = (evt.LeftIsDown() ? 1 : 0) | (evt.RightIsDown() ? 2 : 0) | (evt.MiddleIsDown() ? 4 : 0);
    m_mouse_buttons = buttons;

    new_frame();
    return want_mouse();
}

bool ImGuiWrapper::update_key_data(wxKeyEvent &evt)
{
    if (! display_initialized()) {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();

    if (evt.GetEventType() == wxEVT_CHAR) {
        // Char event
        const auto key = evt.GetUnicodeKey();
        if (key != 0) {
            io.AddInputCharacter(key);
        }

        new_frame();
        return want_keyboard() || want_text_input();
    } else {
        // Key up/down event
        int key = evt.GetKeyCode();
        wxCHECK_MSG(key >= 0 && key < IM_ARRAYSIZE(io.KeysDown), false, "Received invalid key code");

        io.KeysDown[key] = evt.GetEventType() == wxEVT_KEY_DOWN;
        io.KeyShift = evt.ShiftDown();
        io.KeyCtrl = evt.ControlDown();
        io.KeyAlt = evt.AltDown();
        io.KeySuper = evt.MetaDown();

        new_frame();
        return want_keyboard() || want_text_input();
    }
}

void ImGuiWrapper::new_frame()
{
    if (m_new_frame_open) {
        return;
    }

    if (m_font_texture == 0) {
        init_font(true);
    }

    ImGui::NewFrame();
    m_new_frame_open = true;
}

void ImGuiWrapper::render()
{
    ImGui::Render();
    render_draw_data(ImGui::GetDrawData());
    m_new_frame_open = false;
}

ImVec2 ImGuiWrapper::calc_text_size(const wxString &text)
{
    auto text_utf8 = into_u8(text);
    ImVec2 size = ImGui::CalcTextSize(text_utf8.c_str());

/*#ifdef __linux__
    size.x *= m_style_scaling;
    size.y *= m_style_scaling;
#endif*/

    return size;
}

void ImGuiWrapper::set_next_window_pos(float x, float y, int flag, float pivot_x, float pivot_y)
{
    ImGui::SetNextWindowPos(ImVec2(x, y), (ImGuiCond)flag, ImVec2(pivot_x, pivot_y));
    ImGui::SetNextWindowSize(ImVec2(0.0, 0.0));
}

void ImGuiWrapper::set_next_window_bg_alpha(float alpha)
{
    ImGui::SetNextWindowBgAlpha(alpha);
}

bool ImGuiWrapper::begin(const std::string &name, int flags)
{
    return ImGui::Begin(name.c_str(), nullptr, (ImGuiWindowFlags)flags);
}

bool ImGuiWrapper::begin(const wxString &name, int flags)
{
    return begin(into_u8(name), flags);
}

bool ImGuiWrapper::begin(const std::string& name, bool* close, int flags)
{
    return ImGui::Begin(name.c_str(), close, (ImGuiWindowFlags)flags);
}

bool ImGuiWrapper::begin(const wxString& name, bool* close, int flags)
{
    return begin(into_u8(name), close, flags);
}

void ImGuiWrapper::end()
{
    ImGui::End();
}

bool ImGuiWrapper::button(const wxString &label)
{
    auto label_utf8 = into_u8(label);
    return ImGui::Button(label_utf8.c_str());
}

bool ImGuiWrapper::radio_button(const wxString &label, bool active)
{
    auto label_utf8 = into_u8(label);
    return ImGui::RadioButton(label_utf8.c_str(), active);
}

bool ImGuiWrapper::input_double(const std::string &label, const double &value, const std::string &format)
{
    return ImGui::InputDouble(label.c_str(), const_cast<double*>(&value), 0.0f, 0.0f, format.c_str());
}

bool ImGuiWrapper::input_double(const wxString &label, const double &value, const std::string &format)
{
    auto label_utf8 = into_u8(label);
    return input_double(label_utf8, value, format);
}

bool ImGuiWrapper::input_vec3(const std::string &label, const Vec3d &value, float width, const std::string &format)
{
    bool value_changed = false;

    ImGui::BeginGroup();

    for (int i = 0; i < 3; ++i)
    {
        std::string item_label = (i == 0) ? "X" : ((i == 1) ? "Y" : "Z");
        ImGui::PushID(i);
        ImGui::PushItemWidth(width);
        value_changed |= ImGui::InputDouble(item_label.c_str(), const_cast<double*>(&value(i)), 0.0f, 0.0f, format.c_str());
        ImGui::PopID();
    }
    ImGui::EndGroup();

    return value_changed;
}

bool ImGuiWrapper::checkbox(const wxString &label, bool &value)
{
    auto label_utf8 = into_u8(label);
    return ImGui::Checkbox(label_utf8.c_str(), &value);
}

void ImGuiWrapper::text(const char *label)
{
    ImGui::Text(label, NULL);
}

void ImGuiWrapper::text(const std::string &label)
{
    this->text(label.c_str());
}

void ImGuiWrapper::text(const wxString &label)
{
    auto label_utf8 = into_u8(label);
    this->text(label_utf8.c_str());
}

bool ImGuiWrapper::slider_float(const char* label, float* v, float v_min, float v_max, const char* format/* = "%.3f"*/, float power/* = 1.0f*/)
{
    return ImGui::SliderFloat(label, v, v_min, v_max, format, power);
}

bool ImGuiWrapper::slider_float(const std::string& label, float* v, float v_min, float v_max, const char* format/* = "%.3f"*/, float power/* = 1.0f*/)
{
    return this->slider_float(label.c_str(), v, v_min, v_max, format, power);
}

bool ImGuiWrapper::slider_float(const wxString& label, float* v, float v_min, float v_max, const char* format/* = "%.3f"*/, float power/* = 1.0f*/)
{
    auto label_utf8 = into_u8(label);
    return this->slider_float(label_utf8.c_str(), v, v_min, v_max, format, power);
}

bool ImGuiWrapper::combo(const wxString& label, const std::vector<std::string>& options, int& selection)
{
    // this is to force the label to the left of the widget:
    text(label);
    ImGui::SameLine();

    int selection_out = -1;
    bool res = false;

    const char *selection_str = selection < (int)options.size() ? options[selection].c_str() : "";
    if (ImGui::BeginCombo("", selection_str)) {
        for (int i = 0; i < (int)options.size(); i++) {
            if (ImGui::Selectable(options[i].c_str(), i == selection)) {
                selection_out = i;
            }
        }

        ImGui::EndCombo();
        res = true;
    }

    selection = selection_out;
    return res;
}

bool ImGuiWrapper::undo_redo_list(const ImVec2& size, const bool is_undo, bool (*items_getter)(const bool , int , const char**), int& hovered, int& selected)
{
    bool is_hovered = false;
    ImGui::ListBoxHeader("", size);

    int i=0;
    const char* item_text;
    while (items_getter(is_undo, i, &item_text))
    {
        ImGui::Selectable(item_text, i < hovered);

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", item_text);
            hovered = i;
            is_hovered = true;
        }

        if (ImGui::IsItemClicked())
            selected = i;
        i++;
    }

    ImGui::ListBoxFooter();
    return is_hovered;
}

void ImGuiWrapper::search_list(const ImVec2& size_, bool (*items_getter)(int, const char**), char* search_str, size_t& selected, bool& edited)
{
    // ImGui::ListBoxHeader("", size);
    {   
        // rewrote part of function to add a TextInput instead of label Text
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return ;

        const ImGuiStyle& style = g.Style;

        // Size default to hold ~7 items. Fractional number of items helps seeing that we can scroll down/up without looking at scrollbar.
        ImVec2 size = ImGui::CalcItemSize(size_, ImGui::CalcItemWidth(), ImGui::GetTextLineHeightWithSpacing() * 7.4f + style.ItemSpacing.y);
        ImRect frame_bb(window->DC.CursorPos, ImVec2(window->DC.CursorPos.x + size.x, window->DC.CursorPos.y + size.y));

        ImRect bb(frame_bb.Min, frame_bb.Max);
        window->DC.LastItemRect = bb; // Forward storage for ListBoxFooter.. dodgy.
        g.NextItemData.ClearFlags();

        if (!ImGui::IsRectVisible(bb.Min, bb.Max))
        {
            ImGui::ItemSize(bb.GetSize(), style.FramePadding.y);
            ImGui::ItemAdd(bb, 0, &frame_bb);
            return ;
        }

        ImGui::BeginGroup();

        const ImGuiID id = ImGui::GetID(search_str);
        ImVec2 search_size = ImVec2(size.x, ImGui::GetTextLineHeightWithSpacing() + style.ItemSpacing.y);

        ImGui::InputTextEx("", NULL, search_str, 20, search_size, 0, NULL, NULL);
        edited = ImGui::IsItemEdited();

        ImGui::BeginChildFrame(id, frame_bb.GetSize());
    }

    size_t i = 0;
    const char* item_text;
    while (items_getter(i, &item_text))
    {
        ImGui::Selectable(item_text, false);

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", item_text);

        if (ImGui::IsItemClicked())
            selected = i;
        i++;
    }

    ImGui::ListBoxFooter();
}

void ImGuiWrapper::disabled_begin(bool disabled)
{
    wxCHECK_RET(!m_disabled, "ImGUI: Unbalanced disabled_begin() call");

    if (disabled) {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        m_disabled = true;
    }
}

void ImGuiWrapper::disabled_end()
{
    if (m_disabled) {
        ImGui::PopItemFlag();
        ImGui::PopStyleVar();
        m_disabled = false;
    }
}

bool ImGuiWrapper::want_mouse() const
{
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiWrapper::want_keyboard() const
{
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool ImGuiWrapper::want_text_input() const
{
    return ImGui::GetIO().WantTextInput;
}

bool ImGuiWrapper::want_any_input() const
{
    const auto io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard || io.WantTextInput;
}

#ifdef __APPLE__
static const ImWchar ranges_keyboard_shortcuts[] =
{
    0x21E7, 0x21E7, // OSX Shift Key symbol
    0x2318, 0x2318, // OSX Command Key symbol
    0x2325, 0x2325, // OSX Option Key symbol
    0,
};
#endif // __APPLE__

void ImGuiWrapper::init_font(bool compress)
{
    destroy_font();

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    // Create ranges of characters from m_glyph_ranges, possibly adding some OS specific special characters.
	ImVector<ImWchar> ranges;
	ImFontAtlas::GlyphRangesBuilder builder;
	builder.AddRanges(m_glyph_ranges);
#ifdef __APPLE__
	if (m_font_cjk)
		// Apple keyboard shortcuts are only contained in the CJK fonts.
		builder.AddRanges(ranges_keyboard_shortcuts);
#endif
	builder.BuildRanges(&ranges); // Build the final result (ordered ranges with all the unique characters submitted)

    //FIXME replace with io.Fonts->AddFontFromMemoryTTF(buf_decompressed_data, (int)buf_decompressed_size, m_font_size, nullptr, ranges.Data);
    //https://github.com/ocornut/imgui/issues/220
	ImFont* font = io.Fonts->AddFontFromFileTTF((Slic3r::resources_dir() + "/fonts/" + (m_font_cjk ? "NotoSansCJK-Regular.ttc" : "NotoSans-Regular.ttf")).c_str(), m_font_size, nullptr, ranges.Data);
    if (font == nullptr) {
        font = io.Fonts->AddFontDefault();
        if (font == nullptr) {
            throw std::runtime_error("ImGui: Could not load deafult font");
        }
    }

#ifdef __APPLE__
    ImFontConfig config;
    config.MergeMode = true;
    if (! m_font_cjk) {
		// Apple keyboard shortcuts are only contained in the CJK fonts.
		ImFont *font_cjk = io.Fonts->AddFontFromFileTTF((Slic3r::resources_dir() + "/fonts/NotoSansCJK-Regular.ttc").c_str(), m_font_size, &config, ranges_keyboard_shortcuts);
        assert(font_cjk != nullptr);
    }
#endif

    // Build texture atlas
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);   // Load as RGBA 32-bits (75% of the memory is wasted, but default font is so small) because it is more likely to be compatible with user's existing shaders. If your ImTextureId represent a higher-level concept than just a GL texture id, consider calling GetTexDataAsAlpha8() instead to save on GPU memory.

    // Upload texture to graphics system
    GLint last_texture;
    glsafe(::glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture));
    glsafe(::glGenTextures(1, &m_font_texture));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_font_texture));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    glsafe(::glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
    if (compress && GLEW_EXT_texture_compression_s3tc)
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));
    else
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));

    // Store our identifier
    io.Fonts->TexID = (ImTextureID)(intptr_t)m_font_texture;

    // Restore state
    glsafe(::glBindTexture(GL_TEXTURE_2D, last_texture));
}

void ImGuiWrapper::init_input()
{
    ImGuiIO& io = ImGui::GetIO();

    // Keyboard mapping. ImGui will use those indices to peek into the io.KeysDown[] array.
    io.KeyMap[ImGuiKey_Tab] = WXK_TAB;
    io.KeyMap[ImGuiKey_LeftArrow] = WXK_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = WXK_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = WXK_UP;
    io.KeyMap[ImGuiKey_DownArrow] = WXK_DOWN;
    io.KeyMap[ImGuiKey_PageUp] = WXK_PAGEUP;
    io.KeyMap[ImGuiKey_PageDown] = WXK_PAGEDOWN;
    io.KeyMap[ImGuiKey_Home] = WXK_HOME;
    io.KeyMap[ImGuiKey_End] = WXK_END;
    io.KeyMap[ImGuiKey_Insert] = WXK_INSERT;
    io.KeyMap[ImGuiKey_Delete] = WXK_DELETE;
    io.KeyMap[ImGuiKey_Backspace] = WXK_BACK;
    io.KeyMap[ImGuiKey_Space] = WXK_SPACE;
    io.KeyMap[ImGuiKey_Enter] = WXK_RETURN;
    io.KeyMap[ImGuiKey_Escape] = WXK_ESCAPE;
    io.KeyMap[ImGuiKey_A] = 'A';
    io.KeyMap[ImGuiKey_C] = 'C';
    io.KeyMap[ImGuiKey_V] = 'V';
    io.KeyMap[ImGuiKey_X] = 'X';
    io.KeyMap[ImGuiKey_Y] = 'Y';
    io.KeyMap[ImGuiKey_Z] = 'Z';

    // Don't let imgui special-case Mac, wxWidgets already do that
    io.ConfigMacOSXBehaviors = false;

    // Setup clipboard interaction callbacks
    io.SetClipboardTextFn = clipboard_set;
    io.GetClipboardTextFn = clipboard_get;
    io.ClipboardUserData = this;
}

void ImGuiWrapper::init_style()
{
    ImGuiStyle &style = ImGui::GetStyle();

    auto set_color = [&](ImGuiCol_ col, unsigned hex_color) {
        style.Colors[col] = ImVec4(
            ((hex_color >> 24) & 0xff) / 255.0f,
            ((hex_color >> 16) & 0xff) / 255.0f,
            ((hex_color >> 8) & 0xff) / 255.0f,
            (hex_color & 0xff) / 255.0f);
    };

    static const unsigned COL_WINDOW_BACKGROND = 0x222222cc;
    static const unsigned COL_GREY_DARK = 0x555555ff;
    static const unsigned COL_GREY_LIGHT = 0x666666ff;
    static const unsigned COL_ORANGE_DARK = 0xc16737ff;
    static const unsigned COL_ORANGE_LIGHT = 0xff7d38ff;

    // Window
    style.WindowRounding = 4.0f;
    set_color(ImGuiCol_WindowBg, COL_WINDOW_BACKGROND);
    set_color(ImGuiCol_TitleBgActive, COL_ORANGE_DARK);

    // Generics
    set_color(ImGuiCol_FrameBg, COL_GREY_DARK);
    set_color(ImGuiCol_FrameBgHovered, COL_GREY_LIGHT);
    set_color(ImGuiCol_FrameBgActive, COL_GREY_LIGHT);

    // Text selection
    set_color(ImGuiCol_TextSelectedBg, COL_ORANGE_DARK);

    // Buttons
    set_color(ImGuiCol_Button, COL_ORANGE_DARK);
    set_color(ImGuiCol_ButtonHovered, COL_ORANGE_LIGHT);
    set_color(ImGuiCol_ButtonActive, COL_ORANGE_LIGHT);

    // Checkbox
    set_color(ImGuiCol_CheckMark, COL_ORANGE_LIGHT);

    // ComboBox items
    set_color(ImGuiCol_Header, COL_ORANGE_DARK);
    set_color(ImGuiCol_HeaderHovered, COL_ORANGE_LIGHT);
    set_color(ImGuiCol_HeaderActive, COL_ORANGE_LIGHT);

    // Slider
    set_color(ImGuiCol_SliderGrab, COL_ORANGE_DARK);
    set_color(ImGuiCol_SliderGrabActive, COL_ORANGE_LIGHT);

    // Separator
    set_color(ImGuiCol_Separator, COL_ORANGE_LIGHT);
}

void ImGuiWrapper::render_draw_data(ImDrawData *draw_data)
{
    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    ImGuiIO& io = ImGui::GetIO();
    int fb_width = (int)(draw_data->DisplaySize.x * io.DisplayFramebufferScale.x);
    int fb_height = (int)(draw_data->DisplaySize.y * io.DisplayFramebufferScale.y);
    if (fb_width == 0 || fb_height == 0)
        return;
    draw_data->ScaleClipRects(io.DisplayFramebufferScale);

    // We are using the OpenGL fixed pipeline to make the example code simpler to read!
    // Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled, vertex/texcoord/color pointers, polygon fill.
    GLint last_texture; glsafe(::glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture));
    GLint last_polygon_mode[2]; glsafe(::glGetIntegerv(GL_POLYGON_MODE, last_polygon_mode));
    GLint last_viewport[4]; glsafe(::glGetIntegerv(GL_VIEWPORT, last_viewport));
    GLint last_scissor_box[4]; glsafe(::glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box));
    glsafe(::glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TRANSFORM_BIT));
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_LIGHTING));
    glsafe(::glDisable(GL_COLOR_MATERIAL));
    glsafe(::glEnable(GL_SCISSOR_TEST));
    glsafe(::glEnableClientState(GL_VERTEX_ARRAY));
    glsafe(::glEnableClientState(GL_TEXTURE_COORD_ARRAY));
    glsafe(::glEnableClientState(GL_COLOR_ARRAY));
    glsafe(::glEnable(GL_TEXTURE_2D));
    glsafe(::glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));
    glsafe(::glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE));
    GLint texture_env_mode = GL_MODULATE;
    glsafe(::glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &texture_env_mode));
    glsafe(::glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE));
    //glUseProgram(0); // You may want this if using this code in an OpenGL 3+ context where shaders may be bound

    // Setup viewport, orthographic projection matrix
    // Our visible imgui space lies from draw_data->DisplayPps (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayMin is typically (0,0) for single viewport apps.
    glsafe(::glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height));
    glsafe(::glMatrixMode(GL_PROJECTION));
    glsafe(::glPushMatrix());
    glsafe(::glLoadIdentity());
    glsafe(::glOrtho(draw_data->DisplayPos.x, draw_data->DisplayPos.x + draw_data->DisplaySize.x, draw_data->DisplayPos.y + draw_data->DisplaySize.y, draw_data->DisplayPos.y, -1.0f, +1.0f));
    glsafe(::glMatrixMode(GL_MODELVIEW));
    glsafe(::glPushMatrix());
    glsafe(::glLoadIdentity());

    // Render command lists
    ImVec2 pos = draw_data->DisplayPos;
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        const ImDrawVert* vtx_buffer = cmd_list->VtxBuffer.Data;
        const ImDrawIdx* idx_buffer = cmd_list->IdxBuffer.Data;
        glsafe(::glVertexPointer(2, GL_FLOAT, sizeof(ImDrawVert), (const GLvoid*)((const char*)vtx_buffer + IM_OFFSETOF(ImDrawVert, pos))));
        glsafe(::glTexCoordPointer(2, GL_FLOAT, sizeof(ImDrawVert), (const GLvoid*)((const char*)vtx_buffer + IM_OFFSETOF(ImDrawVert, uv))));
        glsafe(::glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(ImDrawVert), (const GLvoid*)((const char*)vtx_buffer + IM_OFFSETOF(ImDrawVert, col))));

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback)
            {
                // User callback (registered via ImDrawList::AddCallback)
                pcmd->UserCallback(cmd_list, pcmd);
            }
            else
            {
                ImVec4 clip_rect = ImVec4(pcmd->ClipRect.x - pos.x, pcmd->ClipRect.y - pos.y, pcmd->ClipRect.z - pos.x, pcmd->ClipRect.w - pos.y);
                if (clip_rect.x < fb_width && clip_rect.y < fb_height && clip_rect.z >= 0.0f && clip_rect.w >= 0.0f)
                {
                    // Apply scissor/clipping rectangle
                    glsafe(::glScissor((int)clip_rect.x, (int)(fb_height - clip_rect.w), (int)(clip_rect.z - clip_rect.x), (int)(clip_rect.w - clip_rect.y)));

                    // Bind texture, Draw
                    glsafe(::glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->TextureId));
                    glsafe(::glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, idx_buffer));
                }
            }
            idx_buffer += pcmd->ElemCount;
        }
    }

    // Restore modified state
    glsafe(::glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, texture_env_mode));
    glsafe(::glDisableClientState(GL_COLOR_ARRAY));
    glsafe(::glDisableClientState(GL_TEXTURE_COORD_ARRAY));
    glsafe(::glDisableClientState(GL_VERTEX_ARRAY));
    glsafe(::glBindTexture(GL_TEXTURE_2D, (GLuint)last_texture));
    glsafe(::glMatrixMode(GL_MODELVIEW));
    glsafe(::glPopMatrix());
    glsafe(::glMatrixMode(GL_PROJECTION));
    glsafe(::glPopMatrix());
    glsafe(::glPopAttrib());
    glsafe(::glPolygonMode(GL_FRONT, (GLenum)last_polygon_mode[0]); glPolygonMode(GL_BACK, (GLenum)last_polygon_mode[1]));
    glsafe(::glViewport(last_viewport[0], last_viewport[1], (GLsizei)last_viewport[2], (GLsizei)last_viewport[3]));
    glsafe(::glScissor(last_scissor_box[0], last_scissor_box[1], (GLsizei)last_scissor_box[2], (GLsizei)last_scissor_box[3]));
}

bool ImGuiWrapper::display_initialized() const
{
    const ImGuiIO& io = ImGui::GetIO();
    return io.DisplaySize.x >= 0.0f && io.DisplaySize.y >= 0.0f;
}

void ImGuiWrapper::destroy_font()
{
    if (m_font_texture != 0) {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->TexID = 0;
        glsafe(::glDeleteTextures(1, &m_font_texture));
        m_font_texture = 0;
    }
}

const char* ImGuiWrapper::clipboard_get(void* user_data)
{
    ImGuiWrapper *self = reinterpret_cast<ImGuiWrapper*>(user_data);

    const char* res = "";

    if (wxTheClipboard->Open()) {
        if (wxTheClipboard->IsSupported(wxDF_TEXT)) {
            wxTextDataObject data;
            wxTheClipboard->GetData(data);

            if (data.GetTextLength() > 0) {
                self->m_clipboard_text = into_u8(data.GetText());
                res = self->m_clipboard_text.c_str();
            }
        }

        wxTheClipboard->Close();
    }

    return res;
}

void ImGuiWrapper::clipboard_set(void* /* user_data */, const char* text)
{
    if (wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(text)));   // object owned by the clipboard
        wxTheClipboard->Close();
    }
}


} // namespace GUI
} // namespace Slic3r
