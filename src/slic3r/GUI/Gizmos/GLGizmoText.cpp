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

// BBS
void GLGizmoText::on_render_input_window(float x, float y, float bottom_limit)
{
    static float last_y = 0.0f;
    static float last_h = 0.0f;

    float space_size = m_imgui->get_style_scaling() * 8;
    float font_cap = m_imgui->calc_text_size("Font ").x;
    float size_cap = m_imgui->calc_text_size("Size ").x;
    float thickness_cap = m_imgui->calc_text_size("Thickness ").x;
    float caption_size = std::max(std::max(font_cap, size_cap), thickness_cap) + 2 * space_size;

    m_imgui->begin(_L("Text"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    // adjust window position to avoid overlap the view toolbar
    const float win_h = ImGui::GetWindowHeight();
    y = std::min(y, bottom_limit - win_h);
    ImGui::SetWindowPos(ImVec2(x, y), ImGuiCond_Always);
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

    ImGui::InputText("", m_text, sizeof(m_text));

    ImGui::PushItemWidth(caption_size);
    ImGui::Text("Font ");
    ImGui::SameLine();
    ImGui::PushItemWidth(150);
    ImGui::Combo("##Font", &m_curr_font_idx, cstr_font_names, m_avail_font_names.size());

    ImGui::PushItemWidth(caption_size);
    ImGui::Text("Size ");
    ImGui::SameLine();
    ImGui::PushItemWidth(150);
    ImGui::InputFloat("###font_size", &m_font_size);

    ImGui::PushItemWidth(caption_size);
    ImGui::Text("Thickness ");
    ImGui::SameLine();
    ImGui::PushItemWidth(150);
    ImGui::InputFloat("###text_thickness", &m_thickness);

    ImGui::Checkbox("Bold", &m_bold);
    ImGui::SameLine();
    ImGui::Checkbox("Italic", &m_italic);

    ImGui::Separator();

    bool add_clicked = m_imgui->button(_L("Add"));
    if (add_clicked) {
        TriangleMesh mesh;
        load_text_shape(m_text, m_font_name.c_str(), m_font_size, m_thickness, m_bold, m_italic, mesh);
        ObjectList* obj_list = wxGetApp().obj_list();
        obj_list->load_mesh_part(mesh, "text_shape");
    }

    m_imgui->end();
}

} // namespace GUI
} // namespace Slic3r
