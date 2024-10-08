#include "GLGizmoFuzzySkin.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include <GL/glew.h>
#include <algorithm>

namespace Slic3r::GUI {

void GLGizmoFuzzySkin::on_shutdown()
{
    m_parent.use_slope(false);
    m_parent.toggle_model_objects_visibility(true);
}

std::string GLGizmoFuzzySkin::on_get_name() const
{
    return _u8L("Paint-on fuzzy skin");
}

bool GLGizmoFuzzySkin::on_init()
{
    m_shortcut_key = WXK_CONTROL_H;

    m_desc["clipping_of_view"]          = _u8L("Clipping of view") + ": ";
    m_desc["reset_direction"]           = _u8L("Reset direction");
    m_desc["cursor_size"]               = _u8L("Brush size") + ": ";
    m_desc["cursor_type"]               = _u8L("Brush shape") + ": ";
    m_desc["add_fuzzy_skin_caption"]    = _u8L("Left mouse button") + ": ";
    m_desc["add_fuzzy_skin"]            = _u8L("Add fuzzy skin");
    m_desc["remove_fuzzy_skin_caption"] = _u8L("Shift + Left mouse button") + ": ";
    m_desc["remove_fuzzy_skin"]         = _u8L("Remove fuzzy skin");
    m_desc["remove_all"]                = _u8L("Remove all selection");
    m_desc["circle"]                    = _u8L("Circle");
    m_desc["sphere"]                    = _u8L("Sphere");
    m_desc["pointer"]                   = _u8L("Triangles");
    m_desc["tool_type"]                 = _u8L("Tool type") + ": ";
    m_desc["tool_brush"]                = _u8L("Brush");
    m_desc["tool_smart_fill"]           = _u8L("Smart fill");
    m_desc["smart_fill_angle"]          = _u8L("Smart fill angle");
    m_desc["split_triangles"]           = _u8L("Split triangles");

    return true;
}

void GLGizmoFuzzySkin::render_painter_gizmo()
{
    const Selection &selection = m_parent.get_selection();

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    render_triangles(selection);
    m_c->object_clipper()->render_cut();
    m_c->instances_hider()->render_cut();
    render_cursor();

    glsafe(::glDisable(GL_BLEND));
}

void GLGizmoFuzzySkin::on_render_input_window(float x, float y, float bottom_limit)
{
    if (!m_c->selection_info()->model_object())
        return;

    const float approx_height = m_imgui->scaled(22.f);

    ImGuiWrapper& imgui = *wxGetApp().imgui();
    y = std::min(y, bottom_limit - approx_height);
    imgui.set_next_window_pos(x, y, ImGuiCond_Always);

    imgui.begin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    // First calculate width of all the texts that are could possibly be shown. We will decide set the dialog width based on that:
    const float clipping_slider_left   = std::max(imgui.calc_text_size(m_desc.at("clipping_of_view")).x,
                                                  imgui.calc_text_size(m_desc.at("reset_direction")).x) + m_imgui->scaled(1.5f);
    const float cursor_slider_left     = imgui.calc_text_size(m_desc.at("cursor_size")).x + m_imgui->scaled(1.f);
    const float smart_fill_slider_left = imgui.calc_text_size(m_desc.at("smart_fill_angle")).x + m_imgui->scaled(1.f);

    const float cursor_type_radio_circle  = imgui.calc_text_size(m_desc["circle"]).x + m_imgui->scaled(2.5f);
    const float cursor_type_radio_sphere  = imgui.calc_text_size(m_desc["sphere"]).x + m_imgui->scaled(2.5f);
    const float cursor_type_radio_pointer = imgui.calc_text_size(m_desc["pointer"]).x + m_imgui->scaled(2.5f);

    const float button_width         = imgui.calc_text_size(m_desc.at("remove_all")).x + m_imgui->scaled(1.f);
    const float buttons_width        = m_imgui->scaled(0.5f);
    const float minimal_slider_width = m_imgui->scaled(4.f);

    const float tool_type_radio_left       = imgui.calc_text_size(m_desc["tool_type"]).x + m_imgui->scaled(1.f);
    const float tool_type_radio_brush      = imgui.calc_text_size(m_desc["tool_brush"]).x + m_imgui->scaled(2.5f);
    const float tool_type_radio_smart_fill = imgui.calc_text_size(m_desc["tool_smart_fill"]).x + m_imgui->scaled(2.5f);

    const float split_triangles_checkbox_width = imgui.calc_text_size(m_desc["split_triangles"]).x + m_imgui->scaled(2.5f);

    float caption_max    = 0.f;
    float total_text_max = 0.f;
    for (const std::string t : {"add_fuzzy_skin", "remove_fuzzy_skin"}) {
        caption_max    = std::max(caption_max, imgui.calc_text_size(m_desc[t + "_caption"]).x);
        total_text_max = std::max(total_text_max, imgui.calc_text_size(m_desc[t]).x);
    }

    total_text_max += caption_max + m_imgui->scaled(1.f);
    caption_max    += m_imgui->scaled(1.f);

    const float sliders_left_width = std::max(smart_fill_slider_left, std::max(cursor_slider_left, clipping_slider_left));
    const float slider_icon_width  = imgui.get_slider_icon_size().x;
    float       window_width       = minimal_slider_width + sliders_left_width + slider_icon_width;
    window_width                   = std::max(window_width, total_text_max);
    window_width                   = std::max(window_width, button_width);
    window_width                   = std::max(window_width, split_triangles_checkbox_width);
    window_width                   = std::max(window_width, cursor_type_radio_circle + cursor_type_radio_sphere + cursor_type_radio_pointer);
    window_width                   = std::max(window_width, tool_type_radio_left + tool_type_radio_brush + tool_type_radio_smart_fill);
    window_width                   = std::max(window_width, 2.f * buttons_width + m_imgui->scaled(1.f));

    auto draw_text_with_caption = [&caption_max, &imgui](const std::string& caption, const std::string& text) {
        imgui.text_colored(imgui.COL_ORANGE_LIGHT, caption);
        ImGui::SameLine(caption_max);
        imgui.text(text);
    };

    for (const std::string t : {"add_fuzzy_skin", "remove_fuzzy_skin"}) {
        draw_text_with_caption(m_desc.at(t + "_caption"), m_desc.at(t));
    }

    ImGui::Separator();

    std::string format_str = std::string("%.f") + I18N::translate_utf8("°",
        "Degree sign to use in the respective slider in fuzzy skin gizmo,"
        "placed after the number with no whitespace in between.");

    const float max_tooltip_width = ImGui::GetFontSize() * 20.0f;

    ImGui::AlignTextToFramePadding();
    imgui.text(m_desc["tool_type"]);

    float tool_type_offset = tool_type_radio_left + (window_width - tool_type_radio_left - tool_type_radio_brush - tool_type_radio_smart_fill + m_imgui->scaled(0.5f)) / 2.f;
    ImGui::SameLine(tool_type_offset);
    ImGui::PushItemWidth(tool_type_radio_brush);
    if (imgui.radio_button(m_desc["tool_brush"], m_tool_type == ToolType::BRUSH))
        m_tool_type = ToolType::BRUSH;

    if (ImGui::IsItemHovered())
        imgui.tooltip(_u8L("Paints facets according to the chosen painting brush."), max_tooltip_width);

    ImGui::SameLine(tool_type_offset + tool_type_radio_brush);
    ImGui::PushItemWidth(tool_type_radio_smart_fill);
    if (imgui.radio_button(m_desc["tool_smart_fill"], m_tool_type == ToolType::SMART_FILL))
        m_tool_type = ToolType::SMART_FILL;

    if (ImGui::IsItemHovered())
        imgui.tooltip(_u8L("Paints neighboring facets whose relative angle is less or equal to set angle."), max_tooltip_width);

    ImGui::Separator();

    if (m_tool_type == ToolType::BRUSH) {
        imgui.text(m_desc.at("cursor_type"));
        ImGui::NewLine();

        float cursor_type_offset = (window_width - cursor_type_radio_sphere - cursor_type_radio_circle - cursor_type_radio_pointer + m_imgui->scaled(1.5f)) / 2.f;
        ImGui::SameLine(cursor_type_offset);
        ImGui::PushItemWidth(cursor_type_radio_sphere);
        if (imgui.radio_button(m_desc["sphere"], m_cursor_type == TriangleSelector::CursorType::SPHERE))
            m_cursor_type = TriangleSelector::CursorType::SPHERE;

        if (ImGui::IsItemHovered())
            imgui.tooltip(_u8L("Paints all facets inside, regardless of their orientation."), max_tooltip_width);

        ImGui::SameLine(cursor_type_offset + cursor_type_radio_sphere);
        ImGui::PushItemWidth(cursor_type_radio_circle);

        if (imgui.radio_button(m_desc["circle"], m_cursor_type == TriangleSelector::CursorType::CIRCLE))
            m_cursor_type = TriangleSelector::CursorType::CIRCLE;

        if (ImGui::IsItemHovered())
            imgui.tooltip(_u8L("Ignores facets facing away from the camera."), max_tooltip_width);

        ImGui::SameLine(cursor_type_offset + cursor_type_radio_sphere + cursor_type_radio_circle);
        ImGui::PushItemWidth(cursor_type_radio_pointer);

        if (imgui.radio_button(m_desc["pointer"], m_cursor_type == TriangleSelector::CursorType::POINTER))
            m_cursor_type = TriangleSelector::CursorType::POINTER;

        if (ImGui::IsItemHovered())
            imgui.tooltip(_u8L("Paints only one facet."), max_tooltip_width);

        m_imgui->disabled_begin(m_cursor_type != TriangleSelector::CursorType::SPHERE && m_cursor_type != TriangleSelector::CursorType::CIRCLE);

        ImGui::AlignTextToFramePadding();
        imgui.text(m_desc.at("cursor_size"));
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
        m_imgui->slider_float("##cursor_radius", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f", 1.0f, true, _L("Alt + Mouse wheel"));

        imgui.checkbox(m_desc["split_triangles"], m_triangle_splitting_enabled);

        if (ImGui::IsItemHovered())
            imgui.tooltip(_u8L("Splits bigger facets into smaller ones while the object is painted."), max_tooltip_width);

        m_imgui->disabled_end();
    } else {
        assert(m_tool_type == ToolType::SMART_FILL);
        ImGui::AlignTextToFramePadding();
        imgui.text(m_desc["smart_fill_angle"] + ":");

        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
        if (m_imgui->slider_float("##smart_fill_angle", &m_smart_fill_angle, SmartFillAngleMin, SmartFillAngleMax, format_str.data(), 1.0f, true, _L("Alt + Mouse wheel")))
            for (auto &triangle_selector : m_triangle_selectors) {
                triangle_selector->seed_fill_unselect_all_triangles();
                triangle_selector->request_update_render_data();
            }
    }

    ImGui::Separator();
    if (m_c->object_clipper()->get_position() == 0.f) {
        ImGui::AlignTextToFramePadding();
        imgui.text(m_desc.at("clipping_of_view"));
    } else {
        if (imgui.button(m_desc.at("reset_direction"))) {
            wxGetApp().CallAfter([this]() { m_c->object_clipper()->set_position_by_ratio(-1., false); });
        }
    }

    auto clp_dist = float(m_c->object_clipper()->get_position());
    ImGui::SameLine(sliders_left_width);
    ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
    if (m_imgui->slider_float("##clp_dist", &clp_dist, 0.f, 1.f, "%.2f", 1.0f, true, from_u8(GUI::shortkey_ctrl_prefix()) + _L("Mouse wheel")))
        m_c->object_clipper()->set_position_by_ratio(clp_dist, true);

    ImGui::Separator();
    if (imgui.button(m_desc.at("remove_all"))) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Reset selection"), UndoRedo::SnapshotType::GizmoAction);
        ModelObject         *mo  = m_c->selection_info()->model_object();
        int                  idx = -1;
        for (ModelVolume *mv : mo->volumes)
            if (mv->is_model_part()) {
                ++idx;
                m_triangle_selectors[idx]->reset();
                m_triangle_selectors[idx]->request_update_render_data();
            }

        update_model_object();
        m_parent.set_as_dirty();
    }

    imgui.end();
}

void GLGizmoFuzzySkin::update_model_object()
{
    bool         updated = false;
    ModelObject *mo      = m_c->selection_info()->model_object();
    int          idx     = -1;
    for (ModelVolume *mv : mo->volumes) {
        if (!mv->is_model_part())
            continue;

        ++idx;
        updated |= mv->fuzzy_skin_facets.set(*m_triangle_selectors[idx]);
    }

    if (updated) {
        const ModelObjectPtrs &mos = wxGetApp().model().objects;
        wxGetApp().obj_list()->update_info_items(std::find(mos.begin(), mos.end(), mo) - mos.begin());

        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    }
}

void GLGizmoFuzzySkin::update_from_model_object(bool first_update)
{
    wxBusyCursor wait;

    const ModelObject *mo = m_c->selection_info()->model_object();
    m_triangle_selectors.clear();

    int volume_id = -1;
    std::vector<ColorRGBA> ebt_colors;
    ebt_colors.push_back(GLVolume::NEUTRAL_COLOR);
    ebt_colors.push_back(TriangleSelectorGUI::enforcers_color);
    for (const ModelVolume *mv : mo->volumes) {
        if (!mv->is_model_part())
            continue;

        ++volume_id;

        // This mesh does not account for the possible Z up SLA offset.
        const TriangleMesh* mesh = &mv->mesh();
        m_triangle_selectors.emplace_back(std::make_unique<TriangleSelectorPatch>(*mesh, ebt_colors));
        // Reset of TriangleSelector is done inside TriangleSelectorGUI's constructor, so we don't need it to perform it again in deserialize().
        m_triangle_selectors.back()->deserialize(mv->fuzzy_skin_facets.get_data(), false);
        m_triangle_selectors.back()->request_update_render_data();
    }
}

PainterGizmoType GLGizmoFuzzySkin::get_painter_type() const
{
    return PainterGizmoType::FUZZY_SKIN;
}

wxString GLGizmoFuzzySkin::handle_snapshot_action_name(bool shift_down, GLGizmoPainterBase::Button button_down) const
{
    return shift_down ? _L("Remove fuzzy skin") : _L("Add fuzzy skin");
}

} // namespace Slic3r::GUI
