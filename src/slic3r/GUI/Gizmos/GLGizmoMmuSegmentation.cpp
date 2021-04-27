#include "GLGizmoMmuSegmentation.hpp"

#include "libslic3r/Model.hpp"

//#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/PresetBundle.hpp"


#include <GL/glew.h>


namespace Slic3r::GUI {

void GLGizmoMmuSegmentation::on_shutdown()
{
//    m_seed_fill_angle = 0.f;
//    m_seed_fill_enabled = false;
    m_parent.use_slope(false);
}

std::string GLGizmoMmuSegmentation::on_get_name() const
{
    // FIXME Lukas H.: Discuss and change shortcut
    return (_L("MMU painting") + " [N]").ToUTF8().data();
}

bool GLGizmoMmuSegmentation::on_is_selectable() const
{
    return (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptFFF
            && wxGetApp().get_mode() != comSimple && wxGetApp().extruders_cnt() > 1);
}

bool GLGizmoMmuSegmentation::on_init()
{
    // FIXME Lukas H.: Discuss and change shortcut
    m_shortcut_key = WXK_CONTROL_N;

    m_desc["reset_direction"]      = _L("Reset direction");
    m_desc["clipping_of_view"]     = _L("Clipping of view") + ": ";
    m_desc["cursor_size"]          = _L("Brush size") + ": ";
    m_desc["cursor_type"]          = _L("Brush shape") + ": ";
    m_desc["first_color_caption"]  = _L("Left mouse button") + ": ";
    m_desc["first_color"]          = _L("First color");
    m_desc["second_color_caption"] = _L("Right mouse button") + ": ";
    m_desc["second_color"]         = _L("Second color");
    m_desc["remove_caption"]       = _L("Shift + Left mouse button") + ": ";
    m_desc["remove"]               = _L("Remove painted color");
    m_desc["remove_all"]           = _L("Remove all painted colors");
    m_desc["circle"]               = _L("Circle");
    m_desc["sphere"]               = _L("Sphere");
    m_desc["seed_fill_angle"]      = _L("Seed fill angle");

    return true;
}

void GLGizmoMmuSegmentation::render_painter_gizmo() const
{
    const Selection& selection = m_parent.get_selection();

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    render_triangles(selection);

    m_c->object_clipper()->render_cut();
    render_cursor();

    glsafe(::glDisable(GL_BLEND));
}

void GLGizmoMmuSegmentation::on_render_input_window(float x, float y, float bottom_limit)
{
    if (!m_c->selection_info()->model_object())
        return;

    const float approx_height = m_imgui->scaled(23.0f);
                            y = std::min(y, bottom_limit - approx_height);
    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always);

    m_imgui->begin(on_get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    // First calculate width of all the texts that are could possibly be shown. We will decide set the dialog width based on that:
    const float clipping_slider_left = std::max(m_imgui->calc_text_size(m_desc.at("clipping_of_view")).x,
                                                m_imgui->calc_text_size(m_desc.at("reset_direction")).x) + m_imgui->scaled(1.5f);
    const float cursor_slider_left       = m_imgui->calc_text_size(m_desc.at("cursor_size")).x + m_imgui->scaled(1.f);
    const float autoset_slider_left      = m_imgui->calc_text_size(m_desc.at("seed_fill_angle")).x + m_imgui->scaled(1.f);
    const float cursor_type_radio_left   = m_imgui->calc_text_size(m_desc.at("cursor_type")).x + m_imgui->scaled(1.f);
    const float cursor_type_radio_width1 = m_imgui->calc_text_size(m_desc["circle"]).x + m_imgui->scaled(2.5f);
    const float cursor_type_radio_width2 = m_imgui->calc_text_size(m_desc["sphere"]).x + m_imgui->scaled(2.5f);
    const float button_width             = m_imgui->calc_text_size(m_desc.at("remove_all")).x + m_imgui->scaled(1.f);
    const float buttons_width            = m_imgui->scaled(0.5f);
    const float minimal_slider_width     = m_imgui->scaled(4.f);

    float caption_max    = 0.f;
    float total_text_max = 0.;
    for (const std::string &t : {"first_color", "second_color", "remove"}) {
        caption_max    = std::max(caption_max, m_imgui->calc_text_size(m_desc.at(t + "_caption")).x);
        total_text_max = std::max(total_text_max, caption_max + m_imgui->calc_text_size(m_desc.at(t)).x);
    }
    caption_max += m_imgui->scaled(1.f);
    total_text_max += m_imgui->scaled(1.f);

    float window_width = minimal_slider_width + std::max(autoset_slider_left, std::max(cursor_slider_left, clipping_slider_left));
    window_width       = std::max(window_width, total_text_max);
    window_width       = std::max(window_width, button_width);
    window_width       = std::max(window_width, cursor_type_radio_left + cursor_type_radio_width1 + cursor_type_radio_width2);
    window_width       = std::max(window_width, 2.f * buttons_width + m_imgui->scaled(1.f));

    auto draw_text_with_caption = [this, &caption_max](const wxString &caption, const wxString &text) {
        m_imgui->text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, caption);
        ImGui::SameLine(caption_max);
        m_imgui->text(text);
    };

    for (const std::string &t : {"first_color", "second_color", "remove"})
        draw_text_with_caption(m_desc.at(t + "_caption"), m_desc.at(t));

    m_imgui->text("");
    ImGui::Separator();

    if (m_imgui->checkbox(_L("Seed fill"), m_seed_fill_enabled))
        if (!m_seed_fill_enabled)
            for (auto &triangle_selector : m_triangle_selectors)
                triangle_selector->seed_fill_unselect_all_triangles();

    m_imgui->text(m_desc["seed_fill_angle"] + ":");
    ImGui::AlignTextToFramePadding();
    std::string format_str = std::string("%.f") + I18N::translate_utf8("°", "Degree sign to use in the respective slider in FDM supports gizmo,"
                                                                            "placed after the number with no whitespace in between.");
    ImGui::SameLine(autoset_slider_left);
    ImGui::PushItemWidth(window_width - autoset_slider_left);
    m_imgui->disabled_begin(!m_seed_fill_enabled);
    m_imgui->slider_float("", &m_seed_fill_angle, 0.f, 90.f, format_str.data());
    m_imgui->disabled_end();

    ImGui::NewLine();
    ImGui::SameLine(window_width - 2.f * buttons_width - m_imgui->scaled(0.5f));

    ImGui::Separator();

    if (m_imgui->button(m_desc.at("remove_all"))) {
        Plater::TakeSnapshot(wxGetApp().plater(), wxString(_L("Reset selection")));
        ModelObject *mo  = m_c->selection_info()->model_object();
        int          idx = -1;
        for (ModelVolume *mv : mo->volumes) {
            if (mv->is_model_part()) {
                ++idx;
                m_triangle_selectors[idx]->reset();
            }
        }

        update_model_object();
        m_parent.set_as_dirty();
    }

    const float max_tooltip_width = ImGui::GetFontSize() * 20.0f;

    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("cursor_size"));
    ImGui::SameLine(cursor_slider_left);
    ImGui::PushItemWidth(window_width - cursor_slider_left);
    ImGui::SliderFloat(" ", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(max_tooltip_width);
        ImGui::TextUnformatted(_L("Alt + Mouse wheel").ToUTF8().data());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("cursor_type"));
    ImGui::SameLine(cursor_type_radio_left + m_imgui->scaled(0.f));
    ImGui::PushItemWidth(cursor_type_radio_width1);

    bool sphere_sel = m_cursor_type == TriangleSelector::CursorType::SPHERE;
    if (m_imgui->radio_button(m_desc["sphere"], sphere_sel))
        sphere_sel = true;

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(max_tooltip_width);
        ImGui::TextUnformatted(_L("Paints all facets inside, regardless of their orientation.").ToUTF8().data());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    ImGui::SameLine(cursor_type_radio_left + cursor_type_radio_width2 + m_imgui->scaled(0.f));
    ImGui::PushItemWidth(cursor_type_radio_width2);

    if (m_imgui->radio_button(m_desc["circle"], !sphere_sel))
        sphere_sel = false;

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(max_tooltip_width);
        ImGui::TextUnformatted(_L("Ignores facets facing away from the camera.").ToUTF8().data());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    m_cursor_type = sphere_sel ? TriangleSelector::CursorType::SPHERE : TriangleSelector::CursorType::CIRCLE;

    m_imgui->checkbox(_L("Split triangles"), m_triangle_splitting_enabled);

    ImGui::Separator();
    if (m_c->object_clipper()->get_position() == 0.f) {
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("clipping_of_view"));
    } else {
        if (m_imgui->button(m_desc.at("reset_direction"))) {
            wxGetApp().CallAfter([this]() { m_c->object_clipper()->set_position(-1., false); });
        }
    }

    ImGui::SameLine(clipping_slider_left);
    ImGui::PushItemWidth(window_width - clipping_slider_left);
    float clp_dist = m_c->object_clipper()->get_position();
    if (ImGui::SliderFloat("  ", &clp_dist, 0.f, 1.f, "%.2f"))
        m_c->object_clipper()->set_position(clp_dist, true);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(max_tooltip_width);
        ImGui::TextUnformatted(_L("Ctrl + Mouse wheel").ToUTF8().data());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    m_imgui->end();
}

void GLGizmoMmuSegmentation::update_model_object() const
{
    bool updated = false;
    ModelObject* mo = m_c->selection_info()->model_object();
    int idx = -1;
    for (ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;
        ++idx;
        updated |= mv->mmu_segmentation_facets.set(*m_triangle_selectors[idx].get());
    }

    if (updated)
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
}

void GLGizmoMmuSegmentation::update_from_model_object()
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
        m_triangle_selectors.back()->deserialize(mv->mmu_segmentation_facets.get_data());
    }
}

PainterGizmoType GLGizmoMmuSegmentation::get_painter_type() const
{
    return PainterGizmoType::MMU_SEGMENTATION;
}


} // namespace Slic3r
