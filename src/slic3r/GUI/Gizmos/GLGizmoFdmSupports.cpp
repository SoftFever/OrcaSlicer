#include "GLGizmoFdmSupports.hpp"

#include "libslic3r/Model.hpp"

//#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/Utils/UndoRedo.hpp"


#include <GL/glew.h>


namespace Slic3r::GUI {



void GLGizmoFdmSupports::on_shutdown()
{
    m_highlight_by_angle_threshold_deg = 0.f;
    m_parent.use_slope(false);
    m_parent.toggle_model_objects_visibility(true);
}



std::string GLGizmoFdmSupports::on_get_name() const
{
    return _u8L("Paint-on supports");
}



bool GLGizmoFdmSupports::on_init()
{
    m_shortcut_key = WXK_CONTROL_L;

    m_desc["clipping_of_view"] = _L("Clipping of view") + ": ";
    m_desc["reset_direction"]  = _L("Reset direction");
    m_desc["cursor_size"]      = _L("Brush size") + ": ";
    m_desc["cursor_type"]      = _L("Brush shape") + ": ";
    m_desc["enforce_caption"]  = _L("Left mouse button") + ": ";
    m_desc["enforce"]          = _L("Enforce supports");
    m_desc["block_caption"]    = _L("Right mouse button") + ": ";
    m_desc["block"]            = _L("Block supports");
    m_desc["remove_caption"]   = _L("Shift + Left mouse button") + ": ";
    m_desc["remove"]           = _L("Remove selection");
    m_desc["remove_all"]       = _L("Remove all selection");
    m_desc["circle"]           = _L("Circle");
    m_desc["sphere"]           = _L("Sphere");
    m_desc["pointer"]          = _L("Triangles");
    m_desc["highlight_by_angle"] = _L("Highlight overhang by angle");
    m_desc["enforce_button"]   = _L("Enforce");
    m_desc["cancel"]           = _L("Cancel");

    m_desc["tool_type"]        = _L("Tool type") + ": ";
    m_desc["tool_brush"]       = _L("Brush");
    m_desc["tool_smart_fill"]  = _L("Smart fill");

    m_desc["smart_fill_angle"] = _L("Smart fill angle");

    m_desc["split_triangles"]   = _L("Split triangles");
    m_desc["on_overhangs_only"] = _L("On overhangs only");

    return true;
}



void GLGizmoFdmSupports::render_painter_gizmo() const
{
    const Selection& selection = m_parent.get_selection();

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    render_triangles(selection);
    m_c->object_clipper()->render_cut();
    m_c->instances_hider()->render_cut();
    render_cursor();

    glsafe(::glDisable(GL_BLEND));
}



void GLGizmoFdmSupports::on_render_input_window(float x, float y, float bottom_limit)
{
    if (! m_c->selection_info()->model_object())
        return;

    const float approx_height = m_imgui->scaled(23.f);
    y = std::min(y, bottom_limit - approx_height);
    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always);

    m_imgui->begin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    // First calculate width of all the texts that are could possibly be shown. We will decide set the dialog width based on that:
    const float clipping_slider_left           = std::max(m_imgui->calc_text_size(m_desc.at("clipping_of_view")).x,
                                                          m_imgui->calc_text_size(m_desc.at("reset_direction")).x) + m_imgui->scaled(1.5f);
    const float cursor_slider_left             = m_imgui->calc_text_size(m_desc.at("cursor_size")).x + m_imgui->scaled(1.f);
    const float smart_fill_slider_left         = m_imgui->calc_text_size(m_desc.at("smart_fill_angle")).x + m_imgui->scaled(1.f);
    const float autoset_slider_label_max_width = m_imgui->scaled(7.5f);
    const float autoset_slider_left            = m_imgui->calc_text_size(m_desc.at("highlight_by_angle"), autoset_slider_label_max_width).x + m_imgui->scaled(1.f);

    const float cursor_type_radio_circle  = m_imgui->calc_text_size(m_desc["circle"]).x + m_imgui->scaled(2.5f);
    const float cursor_type_radio_sphere  = m_imgui->calc_text_size(m_desc["sphere"]).x + m_imgui->scaled(2.5f);
    const float cursor_type_radio_pointer = m_imgui->calc_text_size(m_desc["pointer"]).x + m_imgui->scaled(2.5f);

    const float button_width = m_imgui->calc_text_size(m_desc.at("remove_all")).x + m_imgui->scaled(1.f);
    const float button_enforce_width = m_imgui->calc_text_size(m_desc.at("enforce_button")).x;
    const float button_cancel_width = m_imgui->calc_text_size(m_desc.at("cancel")).x;
    const float buttons_width = std::max(button_enforce_width, button_cancel_width) + m_imgui->scaled(0.5f);
    const float minimal_slider_width = m_imgui->scaled(4.f);

    const float tool_type_radio_left       = m_imgui->calc_text_size(m_desc["tool_type"]).x + m_imgui->scaled(1.f);
    const float tool_type_radio_brush      = m_imgui->calc_text_size(m_desc["tool_brush"]).x + m_imgui->scaled(2.5f);
    const float tool_type_radio_smart_fill = m_imgui->calc_text_size(m_desc["tool_smart_fill"]).x + m_imgui->scaled(2.5f);

    const float split_triangles_checkbox_width   = m_imgui->calc_text_size(m_desc["split_triangles"]).x + m_imgui->scaled(2.5f);
    const float on_overhangs_only_checkbox_width = m_imgui->calc_text_size(m_desc["on_overhangs_only"]).x + m_imgui->scaled(2.5f);

    float caption_max    = 0.f;
    float total_text_max = 0.f;
    for (const auto &t : std::array<std::string, 3>{"enforce", "block", "remove"}) {
        caption_max    = std::max(caption_max, m_imgui->calc_text_size(m_desc[t + "_caption"]).x);
        total_text_max = std::max(total_text_max, m_imgui->calc_text_size(m_desc[t]).x);
    }
    total_text_max += caption_max + m_imgui->scaled(1.f);
    caption_max    += m_imgui->scaled(1.f);

    const float sliders_left_width = std::max(std::max(autoset_slider_left, smart_fill_slider_left), std::max(cursor_slider_left, clipping_slider_left));
#if ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    const float slider_icon_width  = m_imgui->get_slider_icon_size().x;
    float       window_width       = minimal_slider_width + sliders_left_width + slider_icon_width;
#else
    float       window_width       = minimal_slider_width + sliders_left_width;
#endif // ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    window_width = std::max(window_width, total_text_max);
    window_width = std::max(window_width, button_width);
    window_width = std::max(window_width, split_triangles_checkbox_width);
    window_width = std::max(window_width, on_overhangs_only_checkbox_width);
    window_width = std::max(window_width, cursor_type_radio_circle + cursor_type_radio_sphere + cursor_type_radio_pointer);
    window_width = std::max(window_width, tool_type_radio_left + tool_type_radio_brush + tool_type_radio_smart_fill);
    window_width = std::max(window_width, 2.f * buttons_width + m_imgui->scaled(1.f));

    auto draw_text_with_caption = [this, &caption_max](const wxString& caption, const wxString& text) {
        m_imgui->text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, caption);
        ImGui::SameLine(caption_max);
        m_imgui->text(text);
    };

    for (const auto &t : std::array<std::string, 3>{"enforce", "block", "remove"})
        draw_text_with_caption(m_desc.at(t + "_caption"), m_desc.at(t));

    ImGui::Separator();

    float position_before_text_y = ImGui::GetCursorPos().y;
    ImGui::AlignTextToFramePadding();
    m_imgui->text_wrapped(m_desc["highlight_by_angle"] + ":", autoset_slider_label_max_width);
    ImGui::AlignTextToFramePadding();
    float position_after_text_y  = ImGui::GetCursorPos().y;
    std::string format_str = std::string("%.f") + I18N::translate_utf8("°",
        "Degree sign to use in the respective slider in FDM supports gizmo,"
        "placed after the number with no whitespace in between.");
    ImGui::SameLine(sliders_left_width);

    float slider_height = m_imgui->get_slider_float_height();
    // Makes slider to be aligned to bottom of the multi-line text.
    float slider_start_position_y = std::max(position_before_text_y, position_after_text_y - slider_height);
    ImGui::SetCursorPosY(slider_start_position_y);

#if ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
    wxString tooltip = format_wxstr(_L("Preselects faces by overhang angle. It is possible to restrict paintable facets to only preselected faces when "
            "the option \"%1%\" is enabled."), m_desc["on_overhangs_only"]);
    if (m_imgui->slider_float("##angle_threshold_deg", &m_highlight_by_angle_threshold_deg, 0.f, 90.f, format_str.data(), 1.0f, true, tooltip)) {
#else
    ImGui::PushItemWidth(window_width - sliders_left_width);
    if (m_imgui->slider_float("##angle_threshold_deg", &m_highlight_by_angle_threshold_deg, 0.f, 90.f, format_str.data())) {
#endif // ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
        m_parent.set_slope_normal_angle(90.f - m_highlight_by_angle_threshold_deg);
        if (! m_parent.is_using_slope()) {
            m_parent.use_slope(true);
            m_parent.set_as_dirty();
        }
    }

    // Restores the cursor position to be below the multi-line text.
    ImGui::SetCursorPosY(std::max(position_before_text_y + slider_height, position_after_text_y));

    const float max_tooltip_width = ImGui::GetFontSize() * 20.0f;
#if !ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    if (ImGui::IsItemHovered())
        m_imgui->tooltip(format_wxstr(_L("Preselects faces by overhang angle. It is possible to restrict paintable facets to only preselected faces when "
                                           "the option \"%1%\" is enabled."), m_desc["on_overhangs_only"]), max_tooltip_width);
#endif // !ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT

    m_imgui->disabled_begin(m_highlight_by_angle_threshold_deg == 0.f);
    ImGui::NewLine();
    ImGui::SameLine(window_width - 2.f*buttons_width - m_imgui->scaled(0.5f));
    if (m_imgui->button(m_desc["enforce_button"], buttons_width, 0.f)) {
        select_facets_by_angle(m_highlight_by_angle_threshold_deg, false);
        m_highlight_by_angle_threshold_deg = 0.f;
        m_parent.use_slope(false);
    }
    ImGui::SameLine(window_width - buttons_width);
    if (m_imgui->button(m_desc["cancel"], buttons_width, 0.f)) {
        m_highlight_by_angle_threshold_deg = 0.f;
        m_parent.use_slope(false);
    }
    m_imgui->disabled_end();


    ImGui::Separator();

    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc["tool_type"]);

    float tool_type_offset = tool_type_radio_left + (window_width - tool_type_radio_left - tool_type_radio_brush - tool_type_radio_smart_fill + m_imgui->scaled(0.5f)) / 2.f;
    ImGui::SameLine(tool_type_offset);
    ImGui::PushItemWidth(tool_type_radio_brush);
    if (m_imgui->radio_button(m_desc["tool_brush"], m_tool_type == ToolType::BRUSH))
        m_tool_type = ToolType::BRUSH;

    if (ImGui::IsItemHovered())
        m_imgui->tooltip(_L("Paints facets according to the chosen painting brush."), max_tooltip_width);

    ImGui::SameLine(tool_type_offset + tool_type_radio_brush);
    ImGui::PushItemWidth(tool_type_radio_smart_fill);
    if (m_imgui->radio_button(m_desc["tool_smart_fill"], m_tool_type == ToolType::SMART_FILL))
        m_tool_type = ToolType::SMART_FILL;

    if (ImGui::IsItemHovered())
        m_imgui->tooltip(_L("Paints neighboring facets whose relative angle is less or equal to set angle."), max_tooltip_width);

    m_imgui->checkbox(m_desc["on_overhangs_only"], m_paint_on_overhangs_only);
    if (ImGui::IsItemHovered())
        m_imgui->tooltip(format_wxstr(_L("Allows painting only on facets selected by: \"%1%\""), m_desc["highlight_by_angle"]), max_tooltip_width);

    ImGui::Separator();

    if (m_tool_type == ToolType::BRUSH) {
        m_imgui->text(m_desc.at("cursor_type"));
        ImGui::NewLine();

        float cursor_type_offset = (window_width - cursor_type_radio_sphere - cursor_type_radio_circle - cursor_type_radio_pointer + m_imgui->scaled(1.5f)) / 2.f;
        ImGui::SameLine(cursor_type_offset);
        ImGui::PushItemWidth(cursor_type_radio_sphere);
        if (m_imgui->radio_button(m_desc["sphere"], m_cursor_type == TriangleSelector::CursorType::SPHERE))
            m_cursor_type = TriangleSelector::CursorType::SPHERE;

        if (ImGui::IsItemHovered())
            m_imgui->tooltip(_L("Paints all facets inside, regardless of their orientation."), max_tooltip_width);

        ImGui::SameLine(cursor_type_offset + cursor_type_radio_sphere);
        ImGui::PushItemWidth(cursor_type_radio_circle);

        if (m_imgui->radio_button(m_desc["circle"], m_cursor_type == TriangleSelector::CursorType::CIRCLE))
            m_cursor_type = TriangleSelector::CursorType::CIRCLE;

        if (ImGui::IsItemHovered())
            m_imgui->tooltip(_L("Ignores facets facing away from the camera."), max_tooltip_width);

        ImGui::SameLine(cursor_type_offset + cursor_type_radio_sphere + cursor_type_radio_circle);
        ImGui::PushItemWidth(cursor_type_radio_pointer);

        if (m_imgui->radio_button(m_desc["pointer"], m_cursor_type == TriangleSelector::CursorType::POINTER))
            m_cursor_type = TriangleSelector::CursorType::POINTER;

        if (ImGui::IsItemHovered())
            m_imgui->tooltip(_L("Paints only one facet."), max_tooltip_width);

        m_imgui->disabled_begin(m_cursor_type != TriangleSelector::CursorType::SPHERE && m_cursor_type != TriangleSelector::CursorType::CIRCLE);

        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("cursor_size"));
        ImGui::SameLine(sliders_left_width);
#if ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
        ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
        m_imgui->slider_float("##cursor_radius", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f", 1.0f, true, _L("Alt + Mouse wheel"));
#else
        ImGui::PushItemWidth(window_width - sliders_left_width);
        m_imgui->slider_float("##cursor_radius", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f");
        if (ImGui::IsItemHovered())
            m_imgui->tooltip(_L("Alt + Mouse wheel"), max_tooltip_width);
#endif // ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT

        m_imgui->checkbox(m_desc["split_triangles"], m_triangle_splitting_enabled);

        if (ImGui::IsItemHovered())
            m_imgui->tooltip(_L("Splits bigger facets into smaller ones while the object is painted."), max_tooltip_width);

        m_imgui->disabled_end();
    } else {
        assert(m_tool_type == ToolType::SMART_FILL);
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc["smart_fill_angle"] + ":");

        ImGui::SameLine(sliders_left_width);
#if ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
        ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
        if (m_imgui->slider_float("##smart_fill_angle", &m_smart_fill_angle, SmartFillAngleMin, SmartFillAngleMax, format_str.data(), 1.0f, true, _L("Alt + Mouse wheel")))
#else
        ImGui::PushItemWidth(window_width - sliders_left_width);
        if (m_imgui->slider_float("##smart_fill_angle", &m_smart_fill_angle, SmartFillAngleMin, SmartFillAngleMax, format_str.data()))
#endif // ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
            for (auto &triangle_selector : m_triangle_selectors) {
                triangle_selector->seed_fill_unselect_all_triangles();
                triangle_selector->request_update_render_data();
            }

#if !ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
        if (ImGui::IsItemHovered())
            m_imgui->tooltip(_L("Alt + Mouse wheel"), max_tooltip_width);
#endif // !ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    }

    ImGui::Separator();
    if (m_c->object_clipper()->get_position() == 0.f) {
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("clipping_of_view"));
    }
    else {
        if (m_imgui->button(m_desc.at("reset_direction"))) {
            wxGetApp().CallAfter([this](){
                    m_c->object_clipper()->set_position(-1., false);
                });
        }
    }

    auto clp_dist = float(m_c->object_clipper()->get_position());
    ImGui::SameLine(sliders_left_width);
#if ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
    if (m_imgui->slider_float("##clp_dist", &clp_dist, 0.f, 1.f, "%.2f", 1.0f, true, _L("Ctrl + Mouse wheel")))
        m_c->object_clipper()->set_position(clp_dist, true);
#else
    ImGui::PushItemWidth(window_width - sliders_left_width);
    if (m_imgui->slider_float("##clp_dist", &clp_dist, 0.f, 1.f, "%.2f"))
        m_c->object_clipper()->set_position(clp_dist, true);

    if (ImGui::IsItemHovered())
        m_imgui->tooltip(_L("Ctrl + Mouse wheel"), max_tooltip_width);
#endif // ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT

    ImGui::Separator();
    if (m_imgui->button(m_desc.at("remove_all"))) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Reset selection"), UndoRedo::SnapshotType::GizmoAction);
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

    m_imgui->end();
}



void GLGizmoFdmSupports::select_facets_by_angle(float threshold_deg, bool block)
{
    float threshold = (float(M_PI)/180.f)*threshold_deg;
    const Selection& selection = m_parent.get_selection();
    const ModelObject* mo = m_c->selection_info()->model_object();
    const ModelInstance* mi = mo->instances[selection.get_instance_idx()];

    int mesh_id = -1;
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++mesh_id;

        const Transform3d trafo_matrix = mi->get_matrix(true) * mv->get_matrix(true);
        Vec3f down  = (trafo_matrix.inverse() * (-Vec3d::UnitZ())).cast<float>().normalized();
        Vec3f limit = (trafo_matrix.inverse() * Vec3d(std::sin(threshold), 0, -std::cos(threshold))).cast<float>().normalized();

        float dot_limit = limit.dot(down);

        // Now calculate dot product of vert_direction and facets' normals.
        int idx = 0;
        const indexed_triangle_set &its = mv->mesh().its;
        for (const stl_triangle_vertex_indices &face : its.indices) {
            if (its_face_normal(its, face).dot(down) > dot_limit) {
                m_triangle_selectors[mesh_id]->set_facet(idx, block ? EnforcerBlockerType::BLOCKER : EnforcerBlockerType::ENFORCER);
                m_triangle_selectors.back()->request_update_render_data();
            }
            ++ idx;
        }
    }

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), block ? _L("Block supports by angle")
                                                    : _L("Add supports by angle"));
    update_model_object();
    m_parent.set_as_dirty();
}



void GLGizmoFdmSupports::update_model_object() const
{
    bool updated = false;
    ModelObject* mo = m_c->selection_info()->model_object();
    int idx = -1;
    for (ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;
        ++idx;
        updated |= mv->supported_facets.set(*m_triangle_selectors[idx].get());
    }

    if (updated) {
        const ModelObjectPtrs& mos = wxGetApp().model().objects;
        wxGetApp().obj_list()->update_info_items(std::find(mos.begin(), mos.end(), mo) - mos.begin());

        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    }
}



void GLGizmoFdmSupports::update_from_model_object()
{
    wxBusyCursor wait;

    const ModelObject* mo = m_c->selection_info()->model_object();
    m_triangle_selectors.clear();

    int volume_id = -1;
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++volume_id;

        // This mesh does not account for the possible Z up SLA offset.
        const TriangleMesh* mesh = &mv->mesh();

        m_triangle_selectors.emplace_back(std::make_unique<TriangleSelectorGUI>(*mesh));
        // Reset of TriangleSelector is done inside TriangleSelectorGUI's constructor, so we don't need it to perform it again in deserialize().
        m_triangle_selectors.back()->deserialize(mv->supported_facets.get_data(), false);
        m_triangle_selectors.back()->request_update_render_data();
    }
}



PainterGizmoType GLGizmoFdmSupports::get_painter_type() const
{
    return PainterGizmoType::FDM_SUPPORTS;
}

wxString GLGizmoFdmSupports::handle_snapshot_action_name(bool shift_down, GLGizmoPainterBase::Button button_down) const
{
    wxString action_name;
    if (shift_down)
        action_name = _L("Remove selection");
    else {
        if (button_down == Button::Left)
            action_name = _L("Add supports");
        else
            action_name = _L("Block supports");
    }
    return action_name;
}

} // namespace Slic3r::GUI
