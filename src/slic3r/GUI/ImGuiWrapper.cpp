#include "ImGuiWrapper.hpp"

#include <cstdio>
#include <vector>
#include <cmath>
#include <stdexcept>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#if ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
#include <boost/algorithm/string/predicate.hpp>
#include <boost/nowide/convert.hpp>
#endif // ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT

#include <wx/string.h>
#include <wx/event.h>
#include <wx/clipbrd.h>
#include <wx/debug.h>

#include <GL/glew.h>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"
#include "3DScene.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include "Search.hpp"
#include "BitmapCache.hpp"

#include "../Utils/MacDarkMode.hpp"
#include "nanosvg/nanosvg.h"
#include "nanosvg/nanosvgrast.h"

namespace Slic3r {
namespace GUI {


static const std::map<const wchar_t, std::string> font_icons = {
    {ImGui::PrintIconMarker       , "cog"                           },
    {ImGui::PrinterIconMarker     , "printer"                       },
    {ImGui::PrinterSlaIconMarker  , "sla_printer"                   },
    {ImGui::FilamentIconMarker    , "spool"                         },
    {ImGui::MaterialIconMarker    , "resin"                         },
    {ImGui::MinimalizeButton      , "notification_minimalize"       },
    {ImGui::MinimalizeHoverButton , "notification_minimalize_hover" },
    {ImGui::RightArrowButton      , "notification_right"            },
    {ImGui::RightArrowHoverButton , "notification_right_hover"      },
    {ImGui::PreferencesButton      , "notification_preferences"      },
    {ImGui::PreferencesHoverButton , "notification_preferences_hover"},
#if ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    {ImGui::SliderFloatEditBtnIcon, "edit_button"                    },
#endif // ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
};
static const std::map<const wchar_t, std::string> font_icons_large = {
    {ImGui::CloseNotifButton        , "notification_close"              },
    {ImGui::CloseNotifHoverButton   , "notification_close_hover"        },
    {ImGui::EjectButton             , "notification_eject_sd"           },
    {ImGui::EjectHoverButton        , "notification_eject_sd_hover"     },
    {ImGui::WarningMarker           , "notification_warning"            },
    {ImGui::ErrorMarker             , "notification_error"              },
    {ImGui::CancelButton            , "notification_cancel"             },
    {ImGui::CancelHoverButton       , "notification_cancel_hover"       },
//    {ImGui::SinkingObjectMarker     , "move"                            },
//    {ImGui::CustomSupportsMarker    , "fdm_supports"                    },
//    {ImGui::CustomSeamMarker        , "seam"                            },
//    {ImGui::MmuSegmentationMarker   , "mmu_segmentation"                },
//    {ImGui::VarLayerHeightMarker    , "layers"                          },
    {ImGui::DocumentationButton     , "notification_documentation"      },
    {ImGui::DocumentationHoverButton, "notification_documentation_hover"},
    {ImGui::InfoMarker              , "notification_info"               },
    
};

static const std::map<const wchar_t, std::string> font_icons_extra_large = {
    {ImGui::ClippyMarker            , "notification_clippy"             },

};

const ImVec4 ImGuiWrapper::COL_GREY_DARK         = { 0.333f, 0.333f, 0.333f, 1.0f };
const ImVec4 ImGuiWrapper::COL_GREY_LIGHT        = { 0.4f, 0.4f, 0.4f, 1.0f };
const ImVec4 ImGuiWrapper::COL_ORANGE_DARK       = { 0.757f, 0.404f, 0.216f, 1.0f };
const ImVec4 ImGuiWrapper::COL_ORANGE_LIGHT      = { 1.0f, 0.49f, 0.216f, 1.0f };
const ImVec4 ImGuiWrapper::COL_WINDOW_BACKGROUND = { 0.133f, 0.133f, 0.133f, 0.8f };
const ImVec4 ImGuiWrapper::COL_BUTTON_BACKGROUND = COL_ORANGE_DARK;
const ImVec4 ImGuiWrapper::COL_BUTTON_HOVERED    = COL_ORANGE_LIGHT;
const ImVec4 ImGuiWrapper::COL_BUTTON_ACTIVE     = ImGuiWrapper::COL_BUTTON_HOVERED;

ImGuiWrapper::ImGuiWrapper()
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
    float wheel_delta = static_cast<float>(evt.GetWheelDelta());
    if (wheel_delta != 0.0f)
        io.MouseWheel = static_cast<float>(evt.GetWheelRotation()) / wheel_delta;

    unsigned buttons = (evt.LeftIsDown() ? 1 : 0) | (evt.RightIsDown() ? 2 : 0) | (evt.MiddleIsDown() ? 4 : 0);
    m_mouse_buttons = buttons;

    if (want_mouse())
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
    } else {
        // Key up/down event
        int key = evt.GetKeyCode();
        wxCHECK_MSG(key >= 0 && key < IM_ARRAYSIZE(io.KeysDown), false, "Received invalid key code");

        io.KeysDown[key] = evt.GetEventType() == wxEVT_KEY_DOWN;
        io.KeyShift = evt.ShiftDown();
        io.KeyCtrl = evt.ControlDown();
        io.KeyAlt = evt.AltDown();
        io.KeySuper = evt.MetaDown();
    }
    bool ret = want_keyboard() || want_text_input();
    if (ret)
        new_frame();
    return ret;
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

ImVec2 ImGuiWrapper::calc_text_size(const wxString &text, float wrap_width) const
{
    auto text_utf8 = into_u8(text);
    ImVec2 size = ImGui::CalcTextSize(text_utf8.c_str(), NULL, false, wrap_width);

/*#ifdef __linux__
    size.x *= m_style_scaling;
    size.y *= m_style_scaling;
#endif*/

    return size;
}

ImVec2 ImGuiWrapper::calc_button_size(const wxString &text, const ImVec2 &button_size) const
{
    const ImVec2        text_size = this->calc_text_size(text);
    const ImGuiContext &g         = *GImGui;
    const ImGuiStyle   &style     = g.Style;

    return ImGui::CalcItemSize(button_size, text_size.x + style.FramePadding.x * 2.0f, text_size.y + style.FramePadding.y * 2.0f);
}

ImVec2 ImGuiWrapper::get_item_spacing() const
{
    const ImGuiContext &g     = *GImGui;
    const ImGuiStyle   &style = g.Style;
    return style.ItemSpacing;
}

float ImGuiWrapper::get_slider_float_height() const
{
    const ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    return g.FontSize + style.FramePadding.y * 2.0f + style.ItemSpacing.y;
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

void ImGuiWrapper::set_next_window_size(float x, float y, ImGuiCond cond)
{
	ImGui::SetNextWindowSize(ImVec2(x, y), cond);
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

bool ImGuiWrapper::button(const wxString& label, float width, float height)
{
	auto label_utf8 = into_u8(label);
	return ImGui::Button(label_utf8.c_str(), ImVec2(width, height));
}

bool ImGuiWrapper::radio_button(const wxString &label, bool active)
{
    auto label_utf8 = into_u8(label);
    return ImGui::RadioButton(label_utf8.c_str(), active);
}

bool ImGuiWrapper::image_button()
{
	return false;
}

bool ImGuiWrapper::input_double(const std::string &label, const double &value, const std::string &format)
{
    return ImGui::InputDouble(label.c_str(), const_cast<double*>(&value), 0.0f, 0.0f, format.c_str(), ImGuiInputTextFlags_CharsDecimal);
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
    ImGui::Text("%s", label);
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

void ImGuiWrapper::text_colored(const ImVec4& color, const char* label)
{
    ImGui::TextColored(color, "%s", label);
}

void ImGuiWrapper::text_colored(const ImVec4& color, const std::string& label)
{
    this->text_colored(color, label.c_str());
}

void ImGuiWrapper::text_colored(const ImVec4& color, const wxString& label)
{
    auto label_utf8 = into_u8(label);
    this->text_colored(color, label_utf8.c_str());
}

void ImGuiWrapper::text_wrapped(const char *label, float wrap_width)
{
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + wrap_width);
    this->text(label);
    ImGui::PopTextWrapPos();
}

void ImGuiWrapper::text_wrapped(const std::string &label, float wrap_width)
{
    this->text_wrapped(label.c_str(), wrap_width);
}

void ImGuiWrapper::text_wrapped(const wxString &label, float wrap_width)
{
    auto label_utf8 = into_u8(label);
    this->text_wrapped(label_utf8.c_str(), wrap_width);
}

void ImGuiWrapper::tooltip(const char *label, float wrap_width)
{
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(wrap_width);
    ImGui::TextUnformatted(label);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

void ImGuiWrapper::tooltip(const wxString &label, float wrap_width)
{
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(wrap_width);
    ImGui::TextUnformatted(label.ToUTF8().data());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

#if ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
ImVec2 ImGuiWrapper::get_slider_icon_size() const
{
    return this->calc_button_size(std::wstring(&ImGui::SliderFloatEditBtnIcon, 1));
}

bool ImGuiWrapper::slider_float(const char* label, float* v, float v_min, float v_max, const char* format/* = "%.3f"*/, float power/* = 1.0f*/, bool clamp /*= true*/, const wxString& tooltip /*= ""*/, bool show_edit_btn /*= true*/)
{
    const float max_tooltip_width = ImGui::GetFontSize() * 20.0f;

    // let the label string start with "##" to hide the automatic label from ImGui::SliderFloat()
    bool label_visible = !boost::algorithm::istarts_with(label, "##");
    std::string str_label = label_visible ? std::string("##") + std::string(label) : std::string(label);

    // removes 2nd evenience of "##", if present
    std::string::size_type pos = str_label.find("##", 2);
    if (pos != std::string::npos)
        str_label = str_label.substr(0, pos) + str_label.substr(pos + 2);

    bool ret = ImGui::SliderFloat(str_label.c_str(), v, v_min, v_max, format, power);
    if (!tooltip.empty() && ImGui::IsItemHovered())
        this->tooltip(into_u8(tooltip).c_str(), max_tooltip_width);

    if (clamp)
        *v = std::clamp(*v, v_min, v_max);

    const ImGuiStyle& style = ImGui::GetStyle();
    if (show_edit_btn) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 1, style.ItemSpacing.y });
        ImGui::SameLine();
        std::wstring btn_name = ImGui::SliderFloatEditBtnIcon + boost::nowide::widen(str_label);
        ImGui::PushStyleColor(ImGuiCol_Button, { 0.25f, 0.25f, 0.25f, 0.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.5f, 0.5f, 0.5f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, { 0.5f, 0.5f, 0.5f, 1.0f });
        if (ImGui::Button(into_u8(btn_name).c_str())) {
            ImGui::SetKeyboardFocusHere(-1);
            this->set_requires_extra_frame();
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered())
            this->tooltip(into_u8(_L("Edit")).c_str(), max_tooltip_width);

        ImGui::PopStyleVar();
    }

    if (label_visible) {
        // if the label is visible, hide the part of it that should be hidden
        std::string out_label = std::string(label);
        std::string::size_type pos = out_label.find("##");
        if (pos != std::string::npos)
            out_label = out_label.substr(0, pos);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 1, style.ItemSpacing.y });
        ImGui::SameLine();
        this->text(out_label.c_str());
        ImGui::PopStyleVar();
    }

    return ret;
}

bool ImGuiWrapper::slider_float(const std::string& label, float* v, float v_min, float v_max, const char* format/* = "%.3f"*/, float power/* = 1.0f*/, bool clamp /*= true*/, const wxString& tooltip /*= ""*/, bool show_edit_btn /*= true*/)
{
    return this->slider_float(label.c_str(), v, v_min, v_max, format, power, clamp, tooltip, show_edit_btn);
}

bool ImGuiWrapper::slider_float(const wxString& label, float* v, float v_min, float v_max, const char* format/* = "%.3f"*/, float power/* = 1.0f*/, bool clamp /*= true*/, const wxString& tooltip /*= ""*/, bool show_edit_btn /*= true*/)
{
    auto label_utf8 = into_u8(label);
    return this->slider_float(label_utf8.c_str(), v, v_min, v_max, format, power, clamp, tooltip, show_edit_btn);
}
#else
bool ImGuiWrapper::slider_float(const char* label, float* v, float v_min, float v_max, const char* format/* = "%.3f"*/, float power/* = 1.0f*/, bool clamp /*= true*/)
{
    bool ret = ImGui::SliderFloat(label, v, v_min, v_max, format, power);
    if (clamp)
        *v = std::clamp(*v, v_min, v_max);
    return ret;
}

bool ImGuiWrapper::slider_float(const std::string& label, float* v, float v_min, float v_max, const char* format/* = "%.3f"*/, float power/* = 1.0f*/, bool clamp /*= true*/)
{
    return this->slider_float(label.c_str(), v, v_min, v_max, format, power, clamp);
}

bool ImGuiWrapper::slider_float(const wxString& label, float* v, float v_min, float v_max, const char* format/* = "%.3f"*/, float power/* = 1.0f*/, bool clamp /*= true*/)
{
    auto label_utf8 = into_u8(label);
    return this->slider_float(label_utf8.c_str(), v, v_min, v_max, format, power, clamp);
}
#endif // ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT

bool ImGuiWrapper::combo(const wxString& label, const std::vector<std::string>& options, int& selection)
{
    // this is to force the label to the left of the widget:
    text(label);
    ImGui::SameLine();

    int selection_out = selection;
    bool res = false;

    const char *selection_str = selection < int(options.size()) && selection >= 0 ? options[selection].c_str() : "";
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

// Scroll up for one item 
static void scroll_up()
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;

    float item_size_y = window->DC.PrevLineSize.y + g.Style.ItemSpacing.y;
    float win_top = window->Scroll.y;

    ImGui::SetScrollY(win_top - item_size_y);
}

// Scroll down for one item 
static void scroll_down()
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;

    float item_size_y = window->DC.PrevLineSize.y + g.Style.ItemSpacing.y;
    float win_top = window->Scroll.y;

    ImGui::SetScrollY(win_top + item_size_y);
}

static void process_mouse_wheel(int& mouse_wheel)
{
    if (mouse_wheel > 0)
        scroll_up();
    else if (mouse_wheel < 0)
        scroll_down();
    mouse_wheel = 0;
}

bool ImGuiWrapper::undo_redo_list(const ImVec2& size, const bool is_undo, bool (*items_getter)(const bool , int , const char**), int& hovered, int& selected, int& mouse_wheel)
{
    bool is_hovered = false;
    ImGui::ListBoxHeader("", size);

    int i=0;
    const char* item_text;
    while (items_getter(is_undo, i, &item_text)) {
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

    if (is_hovered)
        process_mouse_wheel(mouse_wheel);

    ImGui::ListBoxFooter();
    return is_hovered;
}

// It's a copy of IMGui::Selactable function.
// But a little beat modified to change a label text.
// If item is hovered we should use another color for highlighted letters.
// To do that we push a ColorMarkerHovered symbol at the very beginning of the label
// This symbol will be used to a color selection for the highlighted letters.
// see imgui_draw.cpp, void ImFont::RenderText()
static bool selectable(const char* label, bool selected, ImGuiSelectableFlags flags = 0, const ImVec2& size_arg = ImVec2(0, 0))
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    // Submit label or explicit size to ItemSize(), whereas ItemAdd() will submit a larger/spanning rectangle.
    ImGuiID id = window->GetID(label);
    ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    ImVec2 size(size_arg.x != 0.0f ? size_arg.x : label_size.x, size_arg.y != 0.0f ? size_arg.y : label_size.y);
    ImVec2 pos = window->DC.CursorPos;
    pos.y += window->DC.CurrLineTextBaseOffset;
    ImGui::ItemSize(size, 0.0f);

    // Fill horizontal space
    // We don't support (size < 0.0f) in Selectable() because the ItemSpacing extension would make explicitly right-aligned sizes not visibly match other widgets.
    const bool span_all_columns = (flags & ImGuiSelectableFlags_SpanAllColumns) != 0;
    const float min_x = span_all_columns ? window->ParentWorkRect.Min.x : pos.x;
    const float max_x = span_all_columns ? window->ParentWorkRect.Max.x : window->WorkRect.Max.x;
    if (size_arg.x == 0.0f || (flags & ImGuiSelectableFlags_SpanAvailWidth))
        size.x = ImMax(label_size.x, max_x - min_x);

    // Text stays at the submission position, but bounding box may be extended on both sides
    const ImVec2 text_min = pos;
    const ImVec2 text_max(min_x + size.x, pos.y + size.y);

    // Selectables are meant to be tightly packed together with no click-gap, so we extend their box to cover spacing between selectable.
    ImRect bb(min_x, pos.y, text_max.x, text_max.y);
    if ((flags & ImGuiSelectableFlags_NoPadWithHalfSpacing) == 0)
    {
        const float spacing_x = span_all_columns ? 0.0f : style.ItemSpacing.x;
        const float spacing_y = style.ItemSpacing.y;
        const float spacing_L = IM_FLOOR(spacing_x * 0.50f);
        const float spacing_U = IM_FLOOR(spacing_y * 0.50f);
        bb.Min.x -= spacing_L;
        bb.Min.y -= spacing_U;
        bb.Max.x += (spacing_x - spacing_L);
        bb.Max.y += (spacing_y - spacing_U);
    }
    //if (g.IO.KeyCtrl) { GetForegroundDrawList()->AddRect(bb.Min, bb.Max, IM_COL32(0, 255, 0, 255)); }

    // Modify ClipRect for the ItemAdd(), faster than doing a PushColumnsBackground/PushTableBackground for every Selectable..
    const float backup_clip_rect_min_x = window->ClipRect.Min.x;
    const float backup_clip_rect_max_x = window->ClipRect.Max.x;
    if (span_all_columns)
    {
        window->ClipRect.Min.x = window->ParentWorkRect.Min.x;
        window->ClipRect.Max.x = window->ParentWorkRect.Max.x;
    }

    bool item_add;
    if (flags & ImGuiSelectableFlags_Disabled)
    {
        ImGuiItemFlags backup_item_flags = g.CurrentItemFlags;
        g.CurrentItemFlags |= ImGuiItemFlags_Disabled | ImGuiItemFlags_NoNavDefaultFocus;
        item_add = ImGui::ItemAdd(bb, id);
        g.CurrentItemFlags = backup_item_flags;
    }
    else
    {
        item_add = ImGui::ItemAdd(bb, id);
    }

    if (span_all_columns)
    {
        window->ClipRect.Min.x = backup_clip_rect_min_x;
        window->ClipRect.Max.x = backup_clip_rect_max_x;
    }

    if (!item_add)
        return false;

    // FIXME: We can standardize the behavior of those two, we could also keep the fast path of override ClipRect + full push on render only,
    // which would be advantageous since most selectable are not selected.
    if (span_all_columns && window->DC.CurrentColumns)
        ImGui::PushColumnsBackground();
    else if (span_all_columns && g.CurrentTable)
        ImGui::TablePushBackgroundChannel();

    // We use NoHoldingActiveID on menus so user can click and _hold_ on a menu then drag to browse child entries
    ImGuiButtonFlags button_flags = 0;
    if (flags & ImGuiSelectableFlags_NoHoldingActiveID) { button_flags |= ImGuiButtonFlags_NoHoldingActiveId; }
    if (flags & ImGuiSelectableFlags_SelectOnClick)     { button_flags |= ImGuiButtonFlags_PressedOnClick; }
    if (flags & ImGuiSelectableFlags_SelectOnRelease)   { button_flags |= ImGuiButtonFlags_PressedOnRelease; }
    if (flags & ImGuiSelectableFlags_Disabled)          { button_flags |= ImGuiButtonFlags_Disabled; }
    if (flags & ImGuiSelectableFlags_AllowDoubleClick)  { button_flags |= ImGuiButtonFlags_PressedOnClickRelease | ImGuiButtonFlags_PressedOnDoubleClick; }
    if (flags & ImGuiSelectableFlags_AllowItemOverlap)  { button_flags |= ImGuiButtonFlags_AllowItemOverlap; }

    if (flags & ImGuiSelectableFlags_Disabled)
        selected = false;

    const bool was_selected = selected;
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, button_flags);

    // Update NavId when clicking or when Hovering (this doesn't happen on most widgets), so navigation can be resumed with gamepad/keyboard
    if (pressed || (hovered && (flags & ImGuiSelectableFlags_SetNavIdOnHover)))
    {
        if (!g.NavDisableMouseHover && g.NavWindow == window && g.NavLayer == window->DC.NavLayerCurrent)
        {
            ImGui::SetNavID(id, window->DC.NavLayerCurrent, window->DC.NavFocusScopeIdCurrent, ImRect(bb.Min - window->Pos, bb.Max - window->Pos));
            g.NavDisableHighlight = true;
        }
    }
    if (pressed)
        ImGui::MarkItemEdited(id);

    if (flags & ImGuiSelectableFlags_AllowItemOverlap)
        ImGui::SetItemAllowOverlap();

    // In this branch, Selectable() cannot toggle the selection so this will never trigger.
    if (selected != was_selected) //-V547
        window->DC.LastItemStatusFlags |= ImGuiItemStatusFlags_ToggledSelection;

    // Render
    if (held && (flags & ImGuiSelectableFlags_DrawHoveredWhenHeld))
        hovered = true;
    if (hovered || selected)
    {
        const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_HeaderActive : hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Header);
        ImGui::RenderFrame(bb.Min, bb.Max, col, false, 0.0f);
        ImGui::RenderNavHighlight(bb, id, ImGuiNavHighlightFlags_TypeThin | ImGuiNavHighlightFlags_NoRounding);
    }

    if (span_all_columns && window->DC.CurrentColumns)
        ImGui::PopColumnsBackground();
    else if (span_all_columns && g.CurrentTable)
        ImGui::TablePopBackgroundChannel();

    // mark a label with a ColorMarkerHovered, if item is hovered
    char marked_label[512]; //255 symbols is not enough for translated string (e.t. to Russian)
    if (hovered)
        sprintf(marked_label, "%c%s", ImGui::ColorMarkerHovered, label);
    else
        strcpy(marked_label, label);

    if (flags & ImGuiSelectableFlags_Disabled) ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
    ImGui::RenderTextClipped(text_min, text_max, marked_label, NULL, &label_size, style.SelectableTextAlign, &bb);
    if (flags & ImGuiSelectableFlags_Disabled) ImGui::PopStyleColor();

    // Automatically close popups
    if (pressed && (window->Flags & ImGuiWindowFlags_Popup) && !(flags & ImGuiSelectableFlags_DontClosePopups) && !(g.CurrentItemFlags & ImGuiItemFlags_SelectableDontClosePopup))
        ImGui::CloseCurrentPopup();

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, window->DC.LastItemStatusFlags);
    return pressed;
}

// Scroll so that the hovered item is at the top of the window
static void scroll_y(int hover_id)
{
    if (hover_id < 0)
        return;
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;

    float item_size_y = window->DC.PrevLineSize.y + g.Style.ItemSpacing.y;
    float item_delta = 0.5 * item_size_y;

    float item_top = item_size_y * hover_id;
    float item_bottom = item_top + item_size_y;

    float win_top = window->Scroll.y;
    float win_bottom = window->Scroll.y + window->Size.y;

    if (item_bottom + item_delta >= win_bottom)
        ImGui::SetScrollY(win_top + item_size_y);
    else if (item_top - item_delta <= win_top)
        ImGui::SetScrollY(win_top - item_size_y);
}

// Use this function instead of ImGui::IsKeyPressed.
// ImGui::IsKeyPressed is related for *GImGui.IO.KeysDownDuration[user_key_index]
// And after first key pressing IsKeyPressed() return "true" always even if key wasn't pressed
static void process_key_down(ImGuiKey imgui_key, std::function<void()> f)
{
    if (ImGui::IsKeyDown(ImGui::GetKeyIndex(imgui_key)))
    {
        f();
        // set KeysDown to false to avoid redundant key down processing
        ImGuiContext& g = *GImGui;
        g.IO.KeysDown[ImGui::GetKeyIndex(imgui_key)] = false;
    }
}

void ImGuiWrapper::search_list(const ImVec2& size_, bool (*items_getter)(int, const char** label, const char** tooltip), char* search_str,
                               Search::OptionViewParameters& view_params, int& selected, bool& edited, int& mouse_wheel, bool is_localized)
{
    int& hovered_id = view_params.hovered_id;
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

        if (!ImGui::IsAnyItemFocused() && !ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0))
            ImGui::SetKeyboardFocusHere(0);

        // The press on Esc key invokes editing of InputText (removes last changes)
        // So we should save previous value...
        std::string str = search_str;
        ImGui::InputTextEx("", NULL, search_str, 40, search_size, ImGuiInputTextFlags_AutoSelectAll, NULL, NULL);
        edited = ImGui::IsItemEdited();
        if (edited)
            hovered_id = 0;

        process_key_down(ImGuiKey_Escape, [&selected, search_str, str]() {
            // use 9999 to mark selection as a Esc key
            selected = 9999;
            // ... and when Esc key was pressed, than revert search_str value
            strcpy(search_str, str.c_str());
        });

        ImGui::BeginChildFrame(id, frame_bb.GetSize());
    }

    int i = 0;
    const char* item_text;
    const char* tooltip;
    int mouse_hovered = -1;

    while (items_getter(i, &item_text, &tooltip))
    {
        selectable(item_text, i == hovered_id);

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", /*item_text*/tooltip);
                hovered_id = -1;
            mouse_hovered = i;
        }

        if (ImGui::IsItemClicked())
            selected = i;
        i++;
    }

    // Process mouse wheel
    if (mouse_hovered > 0)
        process_mouse_wheel(mouse_wheel);

    // process Up/DownArrows and Enter
    process_key_down(ImGuiKey_UpArrow, [&hovered_id, mouse_hovered]() {
        if (mouse_hovered > 0)
            scroll_up();
        else {
            if (hovered_id > 0)
                --hovered_id;
            scroll_y(hovered_id);
        }
    });

    process_key_down(ImGuiKey_DownArrow, [&hovered_id, mouse_hovered, i]() {
        if (mouse_hovered > 0)
            scroll_down();
        else {
            if (hovered_id < 0)
                hovered_id = 0;
            else if (hovered_id < i - 1)
                ++hovered_id;
            scroll_y(hovered_id);
        }
    });

    process_key_down(ImGuiKey_Enter, [&selected, hovered_id]() {
        selected = hovered_id;
    });

    ImGui::ListBoxFooter();

    auto check_box = [&edited, this](const wxString& label, bool& check) {
        ImGui::SameLine();
        bool ch = check;
        checkbox(label, ch);
        if (ImGui::IsItemClicked()) {
            check = !check;
            edited = true;
        }
    };

    ImGui::AlignTextToFramePadding();

    // add checkboxes for show/hide Categories and Groups
    text(_L("Use for search")+":");
    check_box(_L("Category"),   view_params.category);
    if (is_localized)
        check_box(_L("Search in English"), view_params.english);
}

void ImGuiWrapper::title(const std::string& str)
{
    text(str);
    ImGui::Separator();
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


std::vector<unsigned char> ImGuiWrapper::load_svg(const std::string& bitmap_name, unsigned target_width, unsigned target_height)
{
    std::vector<unsigned char> empty_vector;

    NSVGimage* image = BitmapCache::nsvgParseFromFileWithReplace(Slic3r::var(bitmap_name + ".svg").c_str(), "px", 96.0f, { { "\"#808080\"", "\"#FFFFFF\"" } });
    if (image == nullptr)
        return empty_vector;

    float svg_scale = target_height != 0 ?
        (float)target_height / image->height : target_width != 0 ?
        (float)target_width / image->width : 1;

    int   width = (int)(svg_scale * image->width + 0.5f);
    int   height = (int)(svg_scale * image->height + 0.5f);
    int   n_pixels = width * height;
    if (n_pixels <= 0) {
        ::nsvgDelete(image);
        return empty_vector;
    }

    NSVGrasterizer* rast = ::nsvgCreateRasterizer();
    if (rast == nullptr) {
        ::nsvgDelete(image);
        return empty_vector;
    }

    std::vector<unsigned char> data(n_pixels * 4, 0);
    ::nsvgRasterize(rast, image, 0, 0, svg_scale, data.data(), width, height, width * 4);
    ::nsvgDeleteRasterizer(rast);
    ::nsvgDelete(image);

    return data;
}

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
            throw Slic3r::RuntimeError("ImGui: Could not load deafult font");
        }
    }

#ifdef __APPLE__
    ImFontConfig config;
    config.MergeMode = true;
    if (! m_font_cjk) {
		// Apple keyboard shortcuts are only contained in the CJK fonts.
        [[maybe_unused]]ImFont *font_cjk = io.Fonts->AddFontFromFileTTF((Slic3r::resources_dir() + "/fonts/NotoSansCJK-Regular.ttc").c_str(), m_font_size, &config, ranges_keyboard_shortcuts);
        assert(font_cjk != nullptr);
    }
#endif

    float font_scale = m_font_size/15;
    int icon_sz = lround(16 * font_scale); // default size of icon is 16 px

    int rect_id = io.Fonts->CustomRects.Size;  // id of the rectangle added next
    // add rectangles for the icons to the font atlas
    for (auto& icon : font_icons)
        io.Fonts->AddCustomRectFontGlyph(font, icon.first, icon_sz, icon_sz, 3.0 * font_scale + icon_sz);
    for (auto& icon : font_icons_large)
        io.Fonts->AddCustomRectFontGlyph(font, icon.first, icon_sz * 2, icon_sz * 2, 3.0 * font_scale + icon_sz * 2);
    for (auto& icon : font_icons_extra_large)
        io.Fonts->AddCustomRectFontGlyph(font, icon.first, icon_sz * 4, icon_sz * 4, 3.0 * font_scale + icon_sz * 4);

    // Build texture atlas
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);   // Load as RGBA 32-bits (75% of the memory is wasted, but default font is so small) because it is more likely to be compatible with user's existing shaders. If your ImTextureId represent a higher-level concept than just a GL texture id, consider calling GetTexDataAsAlpha8() instead to save on GPU memory.

    // Fill rectangles from the SVG-icons
    for (auto icon : font_icons) {
        if (const ImFontAtlas::CustomRect* rect = io.Fonts->GetCustomRectByIndex(rect_id)) {
            assert(rect->Width == icon_sz);
            assert(rect->Height == icon_sz);
            std::vector<unsigned char> raw_data = load_svg(icon.second, icon_sz, icon_sz);
            const ImU32* pIn = (ImU32*)raw_data.data();
            for (int y = 0; y < icon_sz; y++) {
                ImU32* pOut = (ImU32*)pixels + (rect->Y + y) * width + (rect->X);
                for (int x = 0; x < icon_sz; x++)
                    *pOut++ = *pIn++;
            }
        }
        rect_id++;
    }

    icon_sz *= 2; // default size of large icon is 32 px
    for (auto icon : font_icons_large) {
        if (const ImFontAtlas::CustomRect* rect = io.Fonts->GetCustomRectByIndex(rect_id)) {
            assert(rect->Width == icon_sz);
            assert(rect->Height == icon_sz);
            std::vector<unsigned char> raw_data = load_svg(icon.second, icon_sz, icon_sz);
            const ImU32* pIn = (ImU32*)raw_data.data();
            for (int y = 0; y < icon_sz; y++) {
                ImU32* pOut = (ImU32*)pixels + (rect->Y + y) * width + (rect->X);
                for (int x = 0; x < icon_sz; x++)
                    *pOut++ = *pIn++;
            }
        }
        rect_id++;
    }

    icon_sz *= 2; // default size of extra large icon is 64 px
    for (auto icon : font_icons_extra_large) {
        if (const ImFontAtlas::CustomRect* rect = io.Fonts->GetCustomRectByIndex(rect_id)) {
            assert(rect->Width == icon_sz);
            assert(rect->Height == icon_sz);
            std::vector<unsigned char> raw_data = load_svg(icon.second, icon_sz, icon_sz);
            const ImU32* pIn = (ImU32*)raw_data.data();
            for (int y = 0; y < icon_sz; y++) {
                ImU32* pOut = (ImU32*)pixels + (rect->Y + y) * width + (rect->X);
                for (int x = 0; x < icon_sz; x++)
                    *pOut++ = *pIn++;
            }
        }
        rect_id++;
    }

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
    io.KeyMap[ImGuiKey_KeyPadEnter] = WXK_NUMPAD_ENTER;
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

    auto set_color = [&](ImGuiCol_ entity, ImVec4 color) {
        style.Colors[entity] = color;
    };

    // Window
    style.WindowRounding = 4.0f;
    set_color(ImGuiCol_WindowBg, COL_WINDOW_BACKGROUND);
    set_color(ImGuiCol_TitleBgActive, COL_ORANGE_DARK);

    // Generics
    set_color(ImGuiCol_FrameBg, COL_GREY_DARK);
    set_color(ImGuiCol_FrameBgHovered, COL_GREY_LIGHT);
    set_color(ImGuiCol_FrameBgActive, COL_GREY_LIGHT);

    // Text selection
    set_color(ImGuiCol_TextSelectedBg, COL_ORANGE_DARK);

    // Buttons
    set_color(ImGuiCol_Button, COL_BUTTON_BACKGROUND);
    set_color(ImGuiCol_ButtonHovered, COL_BUTTON_HOVERED);
    set_color(ImGuiCol_ButtonActive, COL_BUTTON_ACTIVE);

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

    // Tabs
    set_color(ImGuiCol_Tab, COL_ORANGE_DARK);
    set_color(ImGuiCol_TabHovered, COL_ORANGE_LIGHT);
    set_color(ImGuiCol_TabActive, COL_ORANGE_LIGHT);
    set_color(ImGuiCol_TabUnfocused, COL_GREY_DARK);
    set_color(ImGuiCol_TabUnfocusedActive, COL_GREY_LIGHT);

    // Scrollbars
    set_color(ImGuiCol_ScrollbarGrab, COL_ORANGE_DARK);
    set_color(ImGuiCol_ScrollbarGrabHovered, COL_ORANGE_LIGHT);
    set_color(ImGuiCol_ScrollbarGrabActive, COL_ORANGE_LIGHT);
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
