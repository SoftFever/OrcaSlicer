// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoText.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"

#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/Model.hpp"

#include "libslic3r/Shape/TextShape.hpp"

#include <numeric>

#include <GL/glew.h>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>

namespace Slic3r {
namespace GUI {

static double g_normal_precise = 0.0015;

GLGizmoText::GLGizmoText(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{
}

bool GLGizmoText::on_init()
{
    m_avail_font_names = init_occt_fonts();
    m_shortcut_key = WXK_CONTROL_T;
    return true;
}

void GLGizmoText::on_set_state()
{
}

CommonGizmosDataID GLGizmoText::on_get_requirements() const
{
    return CommonGizmosDataID::SelectionInfo;
}

std::string GLGizmoText::on_get_name() const
{
    return _u8L("Text shape");
}

bool GLGizmoText::on_is_activable() const
{
    // This is assumed in GLCanvas3D::do_rotate, do not change this
    // without updating that function too.
    return m_parent.get_selection().is_single_full_instance();
}

void GLGizmoText::on_render()
{
    // TODO:
}

void GLGizmoText::on_render_for_picking()
{
    // TODO:
}

void GLGizmoText::push_combo_style(const float scale)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 1.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGuiWrapper::COL_WINDOW_BG);
    ImGui::PushStyleColor(ImGuiCol_BorderActive, ImVec4(0.00f, 0.68f, 0.26f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.00f, 0.68f, 0.26f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.00f, 0.68f, 0.26f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.00f, 0.68f, 0.26f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImGuiWrapper::COL_WINDOW_BG);
    ImGui::PushStyleColor(ImGuiCol_Button, { 1.00f, 1.00f, 1.00f, 0.0f });
}

void GLGizmoText::pop_combo_style()
{
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(7);
}

// BBS
void GLGizmoText::on_render_input_window(float x, float y, float bottom_limit)
{
    const float win_h = ImGui::GetWindowHeight();
    y = std::min(y, bottom_limit - win_h);
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);

    static float last_y = 0.0f;
    static float last_h = 0.0f;

    const float currt_scale = m_parent.get_scale();
    ImGuiWrapper::push_toolbar_style(currt_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0,5.0) * currt_scale);
    GizmoImguiBegin("Text", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    float space_size = m_imgui->get_style_scaling() * 8;
    float font_cap = m_imgui->calc_text_size(_L("Font")).x;
    float size_cap = m_imgui->calc_text_size(_L("Size")).x;
    float thickness_cap = m_imgui->calc_text_size(_L("Thickness")).x;
    float input_cap = m_imgui->calc_text_size(_L("Input text")).x;
    float caption_size = std::max(std::max(font_cap, size_cap), std::max(thickness_cap, input_cap)) + space_size + ImGui::GetStyle().WindowPadding.x;

    float input_text_size = m_imgui->scaled(12.0f);
    float button_size = m_imgui->scaled(1.4f);
    float input_size = input_text_size - button_size * 2 - ImGui::GetStyle().ItemSpacing.x * 4;

    ImTextureID normal_B = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TEXT_B);
    ImTextureID press_B_hover = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TEXT_B_HOVER);
    ImTextureID press_B_press = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TEXT_B_PRESS);

    ImTextureID normal_T = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TEXT_T);
    ImTextureID press_T_hover = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TEXT_T_HOVER);
    ImTextureID press_T_press = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TEXT_T_PRESS);

    // adjust window position to avoid overlap the view toolbar
    if (last_h != win_h || last_y != y) {
        // ask canvas for another frame to render the window in the correct position
        m_imgui->set_requires_extra_frame();
        if (last_h != win_h)
            last_h = win_h;
        if (last_y != y)
            last_y = y;
    }

    ImGui::AlignTextToFramePadding();

    const char** cstr_font_names = (const char**)calloc(m_avail_font_names.size(), sizeof(const char*));
    for (int i = 0; i < m_avail_font_names.size(); i++)
        cstr_font_names[i] = m_avail_font_names[i].c_str();

    m_imgui->text(_L("Font"));
    ImGui::SameLine(caption_size);
    ImGui::PushItemWidth(input_text_size + ImGui::GetFrameHeight() * 2);
    push_combo_style(currt_scale);
    if (ImGui::BBLBeginCombo("##Font", cstr_font_names[m_curr_font_idx], 0)) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 0.0f) * currt_scale);
        for (int i = 0; i < m_avail_font_names.size(); i++) {
            const bool is_selected = (m_curr_font_idx == i);
            if (ImGui::BBLSelectable(cstr_font_names[i], is_selected)) {
                m_curr_font_idx = i;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::PopStyleVar(2);
        ImGui::EndCombo();
    }

    ImGui::AlignTextToFramePadding();
    pop_combo_style();
    m_imgui->text(_L("Size"));
    ImGui::SameLine(caption_size);
    ImGui::PushItemWidth(input_size);
    ImGui::InputFloat("###font_size", &m_font_size, 0.0f, 0.0f, "%.2f");
    if (m_font_size < 3.0f)m_font_size = 3.0f;
    ImGui::SameLine();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0);
    ImGui::BBLImageButton(normal_B,press_B_hover,press_B_press,{button_size,button_size},m_bold);
    ImGui::SameLine();
    ImGui::BBLImageButton(normal_T,press_T_hover,press_T_press,{button_size,button_size},m_italic);
    ImGui::PopStyleVar();

    ImGui::AlignTextToFramePadding();
    m_imgui->text(_L("Thickness"));
    ImGui::SameLine(caption_size);
    ImGui::PushItemWidth(input_text_size);
    ImGui::InputFloat("###text_thickness", &m_thickness,0.0f, 0.0f, "%.2f");
    if (m_thickness < 0.1f)m_thickness = 0.1f;

    ImGui::AlignTextToFramePadding();
    m_imgui->text(_L("Input text"));
    ImGui::SameLine(caption_size);
    ImGui::PushItemWidth(input_text_size);
    ImGui::InputText("", m_text, sizeof(m_text));
    
    ImGui::Separator();
    m_imgui->disabled_begin(m_text[0] == '\0' || m_text[0] == ' ');
    float offset =  caption_size + input_text_size -  m_imgui->calc_text_size(_L("Add")).x - space_size;
    ImGui::Dummy({0.0, 0.0});
    ImGui::SameLine(offset);
    bool add_clicked = m_imgui->button(_L("Add"));
    if (add_clicked) {
        TriangleMesh mesh;
        load_text_shape(m_text, m_font_name.c_str(), m_font_size, m_thickness, m_bold, m_italic, mesh);
        ObjectList* obj_list = wxGetApp().obj_list();
        obj_list->load_mesh_part(mesh, "text_shape");
    }
    m_imgui->disabled_end();

    GizmoImguiEnd();
    ImGui::PopStyleVar();
    ImGuiWrapper::pop_toolbar_style();
}

} // namespace GUI
} // namespace Slic3r
