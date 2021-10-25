#include "GLGizmoMmuSegmentation.hpp"

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/BitmapCache.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Model.hpp"
#include "slic3r/Utils/UndoRedo.hpp"


#include <GL/glew.h>

namespace Slic3r::GUI {

static inline void show_notification_extruders_limit_exceeded()
{
    wxGetApp()
        .plater()
        ->get_notification_manager()
        ->push_notification(NotificationType::MmSegmentationExceededExtrudersLimit, NotificationManager::NotificationLevel::PrintInfoNotificationLevel,
                            GUI::format(_L("Your printer has more extruders than the multi-material painting gizmo supports. For this reason, only the "
                                           "first %1% extruders will be able to be used for painting."), GLGizmoMmuSegmentation::EXTRUDERS_LIMIT));
}

void GLGizmoMmuSegmentation::on_opening()
{
    if (wxGetApp().extruders_edited_cnt() > int(GLGizmoMmuSegmentation::EXTRUDERS_LIMIT))
        show_notification_extruders_limit_exceeded();
}

void GLGizmoMmuSegmentation::on_shutdown()
{
    m_parent.use_slope(false);
    m_parent.toggle_model_objects_visibility(true);
}

std::string GLGizmoMmuSegmentation::on_get_name() const
{
    return _u8L("Multimaterial painting");
}

bool GLGizmoMmuSegmentation::on_is_selectable() const
{
    return (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptFFF
            && wxGetApp().get_mode() != comSimple && wxGetApp().extruders_edited_cnt() > 1);
}

bool GLGizmoMmuSegmentation::on_is_activable() const
{
    return GLGizmoPainterBase::on_is_activable() && wxGetApp().extruders_edited_cnt() > 1;
}

static std::vector<std::array<float, 4>> get_extruders_colors()
{
    unsigned char                     rgb_color[3] = {};
    std::vector<std::string>          colors       = Slic3r::GUI::wxGetApp().plater()->get_extruder_colors_from_plater_config();
    std::vector<std::array<float, 4>> colors_out(colors.size());
    for (const std::string &color : colors) {
        Slic3r::GUI::BitmapCache::parse_color(color, rgb_color);
        size_t color_idx      = &color - &colors.front();
        colors_out[color_idx] = {float(rgb_color[0]) / 255.f, float(rgb_color[1]) / 255.f, float(rgb_color[2]) / 255.f, 1.f};
    }

    return colors_out;
}

static std::vector<std::string> get_extruders_names()
{
    size_t                   extruders_count = wxGetApp().extruders_edited_cnt();
    std::vector<std::string> extruders_out;
    extruders_out.reserve(extruders_count);
    for (size_t extruder_idx = 1; extruder_idx <= extruders_count; ++extruder_idx)
        extruders_out.emplace_back("Extruder " + std::to_string(extruder_idx));

    return extruders_out;
}

static std::vector<int> get_extruder_id_for_volumes(const ModelObject &model_object)
{
    std::vector<int> extruders_idx;
    extruders_idx.reserve(model_object.volumes.size());
    for (const ModelVolume *model_volume : model_object.volumes) {
        if (!model_volume->is_model_part())
            continue;

        extruders_idx.emplace_back(model_volume->extruder_id());
    }

    return extruders_idx;
}

void GLGizmoMmuSegmentation::init_extruders_data()
{
    m_original_extruders_names     = get_extruders_names();
    m_original_extruders_colors    = get_extruders_colors();
    m_modified_extruders_colors    = m_original_extruders_colors;
    m_first_selected_extruder_idx  = 0;
    m_second_selected_extruder_idx = 1;
}

bool GLGizmoMmuSegmentation::on_init()
{
    m_shortcut_key = WXK_CONTROL_N;

    m_desc["reset_direction"]      = _L("Reset direction");
    m_desc["clipping_of_view"]     = _L("Clipping of view") + ": ";
    m_desc["cursor_size"]          = _L("Brush size") + ": ";
    m_desc["cursor_type"]          = _L("Brush shape");
    m_desc["first_color_caption"]  = _L("Left mouse button") + ": ";
    m_desc["first_color"]          = _L("First color");
    m_desc["second_color_caption"] = _L("Right mouse button") + ": ";
    m_desc["second_color"]         = _L("Second color");
    m_desc["remove_caption"]       = _L("Shift + Left mouse button") + ": ";
    m_desc["remove"]               = _L("Remove painted color");
    m_desc["remove_all"]           = _L("Remove all painted areas");
    m_desc["circle"]               = _L("Circle");
    m_desc["sphere"]               = _L("Sphere");
    m_desc["pointer"]              = _L("Triangles");

    m_desc["tool_type"]            = _L("Tool type");
    m_desc["tool_brush"]           = _L("Brush");
    m_desc["tool_smart_fill"]      = _L("Smart fill");
    m_desc["tool_bucket_fill"]     = _L("Bucket fill");

    m_desc["smart_fill_angle"]     = _L("Smart fill angle");
    m_desc["split_triangles"]      = _L("Split triangles");

    init_extruders_data();

    return true;
}

void GLGizmoMmuSegmentation::render_painter_gizmo() const
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

void GLGizmoMmuSegmentation::set_painter_gizmo_data(const Selection &selection)
{
    GLGizmoPainterBase::set_painter_gizmo_data(selection);

    if (m_state != On || wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptFFF || wxGetApp().extruders_edited_cnt() <= 1)
        return;

    ModelObject *model_object         = m_c->selection_info()->model_object();
    if (int prev_extruders_count = int(m_original_extruders_colors.size());
        prev_extruders_count != wxGetApp().extruders_edited_cnt() || get_extruders_colors() != m_original_extruders_colors) {
        if (wxGetApp().extruders_edited_cnt() > int(GLGizmoMmuSegmentation::EXTRUDERS_LIMIT))
            show_notification_extruders_limit_exceeded();

        this->init_extruders_data();
        // Reinitialize triangle selectors because of change of extruder count need also change the size of GLIndexedVertexArray
        if (prev_extruders_count != wxGetApp().extruders_edited_cnt())
            this->init_model_triangle_selectors();
    } else if (model_object != nullptr && get_extruder_id_for_volumes(*model_object) != m_original_volumes_extruder_idxs) {
        this->init_model_triangle_selectors();
    }
}

void GLGizmoMmuSegmentation::render_triangles(const Selection &selection) const
{
    ClippingPlaneDataWrapper clp_data = this->get_clipping_plane_data();
    auto                    *shader   = wxGetApp().get_shader("mm_gouraud");
    if (!shader)
        return;
    shader->start_using();
    shader->set_uniform("clipping_plane", clp_data.clp_dataf);
    shader->set_uniform("z_range", clp_data.z_range);
    ScopeGuard guard([shader]() { if (shader) shader->stop_using(); });

    const ModelObject *mo      = m_c->selection_info()->model_object();
    int                mesh_id = -1;
    for (const ModelVolume *mv : mo->volumes) {
        if (!mv->is_model_part())
            continue;

        ++mesh_id;

        const Transform3d trafo_matrix = mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();

        bool is_left_handed = trafo_matrix.matrix().determinant() < 0.;
        if (is_left_handed)
            glsafe(::glFrontFace(GL_CW));

        glsafe(::glPushMatrix());
        glsafe(::glMultMatrixd(trafo_matrix.data()));

        shader->set_uniform("volume_world_matrix", trafo_matrix);
        shader->set_uniform("volume_mirrored", is_left_handed);
        m_triangle_selectors[mesh_id]->render(m_imgui);

        glsafe(::glPopMatrix());
        if (is_left_handed)
            glsafe(::glFrontFace(GL_CCW));
    }
}

static void render_extruders_combo(const std::string                       &label,
                                   const std::vector<std::string>          &extruders,
                                   const std::vector<std::array<float, 4>> &extruders_colors,
                                   size_t                                  &selection_idx)
{
    assert(!extruders_colors.empty());
    assert(extruders_colors.size() == extruders_colors.size());

    auto convert_to_imu32 = [](const std::array<float, 4> &color) -> ImU32 {
        return IM_COL32(uint8_t(color[0] * 255.f), uint8_t(color[1] * 255.f), uint8_t(color[2] * 255.f), uint8_t(color[3] * 255.f));
    };

    size_t selection_out = selection_idx;
    // It is necessary to use BeginGroup(). Otherwise, when using SameLine() is called, then other items will be drawn inside the combobox.
    ImGui::BeginGroup();
    ImVec2 combo_pos = ImGui::GetCursorScreenPos();
    if (ImGui::BeginCombo(label.c_str(), "")) {
        for (size_t extruder_idx = 0; extruder_idx < std::min(extruders.size(), GLGizmoMmuSegmentation::EXTRUDERS_LIMIT); ++extruder_idx) {
            ImGui::PushID(int(extruder_idx));
            ImVec2 start_position = ImGui::GetCursorScreenPos();

            if (ImGui::Selectable("", extruder_idx == selection_idx))
                selection_out = extruder_idx;

            ImGui::SameLine();
            ImGuiStyle &style  = ImGui::GetStyle();
            float       height = ImGui::GetTextLineHeight();
            ImGui::GetWindowDrawList()->AddRectFilled(start_position, ImVec2(start_position.x + height + height / 2, start_position.y + height), convert_to_imu32(extruders_colors[extruder_idx]));
            ImGui::GetWindowDrawList()->AddRect(start_position, ImVec2(start_position.x + height + height / 2, start_position.y + height), IM_COL32_BLACK);

            ImGui::SetCursorScreenPos(ImVec2(start_position.x + height + height / 2 + style.FramePadding.x, start_position.y));
            ImGui::Text("%s", extruders[extruder_idx].c_str());
            ImGui::PopID();
        }

        ImGui::EndCombo();
    }

    ImVec2      backup_pos = ImGui::GetCursorScreenPos();
    ImGuiStyle &style      = ImGui::GetStyle();

    ImGui::SetCursorScreenPos(ImVec2(combo_pos.x + style.FramePadding.x, combo_pos.y + style.FramePadding.y));
    ImVec2 p      = ImGui::GetCursorScreenPos();
    float  height = ImGui::GetTextLineHeight();

    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + height + height / 2, p.y + height), convert_to_imu32(extruders_colors[selection_idx]));
    ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + height + height / 2, p.y + height), IM_COL32_BLACK);

    ImGui::SetCursorScreenPos(ImVec2(p.x + height + height / 2 + style.FramePadding.x, p.y));
    ImGui::Text("%s", extruders[selection_out].c_str());
    ImGui::SetCursorScreenPos(backup_pos);
    ImGui::EndGroup();

    selection_idx = selection_out;
}

void GLGizmoMmuSegmentation::on_render_input_window(float x, float y, float bottom_limit)
{
    if (!m_c->selection_info()->model_object())
        return;

    const float approx_height = m_imgui->scaled(22.0f);
                            y = std::min(y, bottom_limit - approx_height);
    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always);

    m_imgui->begin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    // First calculate width of all the texts that are could possibly be shown. We will decide set the dialog width based on that:
    const float clipping_slider_left = std::max(m_imgui->calc_text_size(m_desc.at("clipping_of_view")).x,
                                                m_imgui->calc_text_size(m_desc.at("reset_direction")).x) + m_imgui->scaled(1.5f);
    const float cursor_slider_left       = m_imgui->calc_text_size(m_desc.at("cursor_size")).x + m_imgui->scaled(1.f);
    const float smart_fill_slider_left   = m_imgui->calc_text_size(m_desc.at("smart_fill_angle")).x + m_imgui->scaled(1.f);

    const float cursor_type_radio_circle  = m_imgui->calc_text_size(m_desc["circle"]).x + m_imgui->scaled(2.5f);
    const float cursor_type_radio_sphere  = m_imgui->calc_text_size(m_desc["sphere"]).x + m_imgui->scaled(2.5f);
    const float cursor_type_radio_pointer = m_imgui->calc_text_size(m_desc["pointer"]).x + m_imgui->scaled(2.5f);

    const float button_width             = m_imgui->calc_text_size(m_desc.at("remove_all")).x + m_imgui->scaled(1.f);
    const float buttons_width            = m_imgui->scaled(0.5f);
    const float minimal_slider_width     = m_imgui->scaled(4.f);
    const float color_button_width       = m_imgui->calc_text_size("").x + m_imgui->scaled(1.75f);
    const float combo_label_width        = std::max(m_imgui->calc_text_size(m_desc.at("first_color")).x,
                                                    m_imgui->calc_text_size(m_desc.at("second_color")).x) + m_imgui->scaled(1.f);

    const float tool_type_radio_brush       = m_imgui->calc_text_size(m_desc["tool_brush"]).x + m_imgui->scaled(2.5f);
    const float tool_type_radio_bucket_fill = m_imgui->calc_text_size(m_desc["tool_bucket_fill"]).x + m_imgui->scaled(2.5f);
    const float tool_type_radio_smart_fill  = m_imgui->calc_text_size(m_desc["tool_smart_fill"]).x + m_imgui->scaled(2.5f);

    const float split_triangles_checkbox_width = m_imgui->calc_text_size(m_desc["split_triangles"]).x + m_imgui->scaled(2.5f);

    float caption_max    = 0.f;
    float total_text_max = 0.f;
    for (const auto &t : std::array<std::string, 3>{"first_color", "second_color", "remove"}) {
        caption_max    = std::max(caption_max, m_imgui->calc_text_size(m_desc[t + "_caption"]).x);
        total_text_max = std::max(total_text_max, m_imgui->calc_text_size(m_desc[t]).x);
    }
    total_text_max += caption_max + m_imgui->scaled(1.f);
    caption_max    += m_imgui->scaled(1.f);

    float sliders_width = std::max(smart_fill_slider_left, std::max(cursor_slider_left, clipping_slider_left));
    float window_width = minimal_slider_width + sliders_width;
    window_width       = std::max(window_width, total_text_max);
    window_width       = std::max(window_width, button_width);
    window_width       = std::max(window_width, split_triangles_checkbox_width);
    window_width       = std::max(window_width, cursor_type_radio_circle + cursor_type_radio_sphere + cursor_type_radio_pointer);
    window_width       = std::max(window_width, tool_type_radio_brush + tool_type_radio_bucket_fill + tool_type_radio_smart_fill);
    window_width       = std::max(window_width, 2.f * buttons_width + m_imgui->scaled(1.f));

    auto draw_text_with_caption = [this, &caption_max](const wxString &caption, const wxString &text) {
        m_imgui->text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, caption);
        ImGui::SameLine(caption_max);
        m_imgui->text(text);
    };

    for (const auto &t : std::array<std::string, 3>{"first_color", "second_color", "remove"})
        draw_text_with_caption(m_desc.at(t + "_caption"), m_desc.at(t));

    ImGui::Separator();

    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("first_color"));
    ImGui::SameLine(combo_label_width);
    ImGui::PushItemWidth(window_width - combo_label_width - color_button_width);
    render_extruders_combo("##first_color_combo", m_original_extruders_names, m_original_extruders_colors, m_first_selected_extruder_idx);
    ImGui::SameLine();

    const std::array<float, 4> &select_first_color = m_modified_extruders_colors[m_first_selected_extruder_idx];
    ImVec4                      first_color        = ImVec4(select_first_color[0], select_first_color[1], select_first_color[2], select_first_color[3]);
    if(ImGui::ColorEdit4("First color##color_picker", (float*)&first_color, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
        m_modified_extruders_colors[m_first_selected_extruder_idx] = {first_color.x, first_color.y, first_color.z, first_color.w};

    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("second_color"));
    ImGui::SameLine(combo_label_width);
    ImGui::PushItemWidth(window_width - combo_label_width - color_button_width);
    render_extruders_combo("##second_color_combo", m_original_extruders_names, m_original_extruders_colors, m_second_selected_extruder_idx);
    ImGui::SameLine();

    const std::array<float, 4> &select_second_color = m_modified_extruders_colors[m_second_selected_extruder_idx];
    ImVec4                      second_color        = ImVec4(select_second_color[0], select_second_color[1], select_second_color[2], select_second_color[3]);
    if(ImGui::ColorEdit4("Second color##color_picker", (float*)&second_color, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
        m_modified_extruders_colors[m_second_selected_extruder_idx] = {second_color.x, second_color.y, second_color.z, second_color.w};

    const float max_tooltip_width = ImGui::GetFontSize() * 20.0f;

    ImGui::Separator();

    m_imgui->text(m_desc.at("tool_type"));
    ImGui::NewLine();

    float tool_type_offset = (window_width - tool_type_radio_brush - tool_type_radio_bucket_fill - tool_type_radio_smart_fill + m_imgui->scaled(1.5f)) / 2.f;
    ImGui::SameLine(tool_type_offset);
    ImGui::PushItemWidth(tool_type_radio_brush);
    if (m_imgui->radio_button(m_desc["tool_brush"], m_tool_type == ToolType::BRUSH)) {
        m_tool_type = ToolType::BRUSH;
        for (auto &triangle_selector : m_triangle_selectors) {
            triangle_selector->seed_fill_unselect_all_triangles();
            triangle_selector->request_update_render_data();
        }
    }

    if (ImGui::IsItemHovered())
        m_imgui->tooltip(_L("Paints facets according to the chosen painting brush."), max_tooltip_width);

    ImGui::SameLine(tool_type_offset + tool_type_radio_brush);
    ImGui::PushItemWidth(tool_type_radio_smart_fill);
    if (m_imgui->radio_button(m_desc["tool_smart_fill"], m_tool_type == ToolType::SMART_FILL)) {
        m_tool_type = ToolType::SMART_FILL;
        for (auto &triangle_selector : m_triangle_selectors) {
            triangle_selector->seed_fill_unselect_all_triangles();
            triangle_selector->request_update_render_data();
        }
    }

    if (ImGui::IsItemHovered())
        m_imgui->tooltip(_L("Paints neighboring facets whose relative angle is less or equal to set angle."), max_tooltip_width);

    ImGui::SameLine(tool_type_offset + tool_type_radio_brush + tool_type_radio_smart_fill);
    ImGui::PushItemWidth(tool_type_radio_bucket_fill);
    if (m_imgui->radio_button(m_desc["tool_bucket_fill"], m_tool_type == ToolType::BUCKET_FILL)) {
        m_tool_type = ToolType::BUCKET_FILL;
        for (auto &triangle_selector : m_triangle_selectors) {
            triangle_selector->seed_fill_unselect_all_triangles();
            triangle_selector->request_update_render_data();
        }
    }

    if (ImGui::IsItemHovered())
        m_imgui->tooltip(_L("Paints neighboring facets that have the same color."), max_tooltip_width);

    ImGui::Separator();

    if(m_tool_type == ToolType::BRUSH) {
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
        ImGui::SameLine(sliders_width);
        ImGui::PushItemWidth(window_width - sliders_width);
        m_imgui->slider_float("##cursor_radius", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f");
        if (ImGui::IsItemHovered())
            m_imgui->tooltip(_L("Alt + Mouse wheel"), max_tooltip_width);

        m_imgui->checkbox(m_desc["split_triangles"], m_triangle_splitting_enabled);

        if (ImGui::IsItemHovered())
            m_imgui->tooltip(_L("Split bigger facets into smaller ones while the object is painted."), max_tooltip_width);

        m_imgui->disabled_end();

        ImGui::Separator();
    } else if(m_tool_type == ToolType::SMART_FILL) {
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc["smart_fill_angle"] + ":");
        std::string format_str = std::string("%.f") + I18N::translate_utf8("°", "Degree sign to use in the respective slider in MMU gizmo,"
                                                                                "placed after the number with no whitespace in between.");
        ImGui::SameLine(sliders_width);
        ImGui::PushItemWidth(window_width - sliders_width);
        if(m_imgui->slider_float("##smart_fill_angle", &m_smart_fill_angle, SmartFillAngleMin, SmartFillAngleMax, format_str.data()))
            for (auto &triangle_selector : m_triangle_selectors) {
                triangle_selector->seed_fill_unselect_all_triangles();
                triangle_selector->request_update_render_data();
            }

        if (ImGui::IsItemHovered())
            m_imgui->tooltip(_L("Alt + Mouse wheel"), max_tooltip_width);

        ImGui::Separator();
    }

    if (m_c->object_clipper()->get_position() == 0.f) {
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("clipping_of_view"));
    } else {
        if (m_imgui->button(m_desc.at("reset_direction"))) {
            wxGetApp().CallAfter([this]() { m_c->object_clipper()->set_position(-1., false); });
        }
    }

    ImGui::SameLine(sliders_width);
    ImGui::PushItemWidth(window_width - sliders_width);
    auto clp_dist = float(m_c->object_clipper()->get_position());
    if (m_imgui->slider_float("##clp_dist", &clp_dist, 0.f, 1.f, "%.2f"))
        m_c->object_clipper()->set_position(clp_dist, true);

    if (ImGui::IsItemHovered())
        m_imgui->tooltip(_L("Ctrl + Mouse wheel"), max_tooltip_width);

    ImGui::Separator();
    if (m_imgui->button(m_desc.at("remove_all"))) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Reset selection"),
                                      UndoRedo::SnapshotType::GizmoAction);
        ModelObject *        mo  = m_c->selection_info()->model_object();
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

    if (updated) {
        const ModelObjectPtrs &mos = wxGetApp().model().objects;
        wxGetApp().obj_list()->update_info_items(std::find(mos.begin(), mos.end(), mo) - mos.begin());
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    }
}

void GLGizmoMmuSegmentation::init_model_triangle_selectors()
{
    const ModelObject *mo = m_c->selection_info()->model_object();
    m_triangle_selectors.clear();

    // Don't continue when extruders colors are not initialized
    if(m_original_extruders_colors.empty())
        return;

    for (const ModelVolume *mv : mo->volumes) {
        if (!mv->is_model_part())
            continue;

        // This mesh does not account for the possible Z up SLA offset.
        const TriangleMesh *mesh = &mv->mesh();

        int extruder_idx = (mv->extruder_id() > 0) ? mv->extruder_id() - 1 : 0;
        m_triangle_selectors.emplace_back(std::make_unique<TriangleSelectorMmGui>(*mesh, m_modified_extruders_colors, m_original_extruders_colors[size_t(extruder_idx)]));
        // Reset of TriangleSelector is done inside TriangleSelectorMmGUI's constructor, so we don't need it to perform it again in deserialize().
        m_triangle_selectors.back()->deserialize(mv->mmu_segmentation_facets.get_data(), false);
        m_triangle_selectors.back()->request_update_render_data();
    }
    m_original_volumes_extruder_idxs = get_extruder_id_for_volumes(*mo);
}

void GLGizmoMmuSegmentation::update_from_model_object()
{
    wxBusyCursor wait;

    // Extruder colors need to be reloaded before calling init_model_triangle_selectors to render painted triangles
    // using colors from loaded 3MF and not from printer profile in Slicer.
    if (int prev_extruders_count = int(m_original_extruders_colors.size());
        prev_extruders_count != wxGetApp().extruders_edited_cnt() || get_extruders_colors() != m_original_extruders_colors)
        this->init_extruders_data();

    this->init_model_triangle_selectors();
}

PainterGizmoType GLGizmoMmuSegmentation::get_painter_type() const
{
    return PainterGizmoType::MMU_SEGMENTATION;
}

std::array<float, 4> GLGizmoMmuSegmentation::get_cursor_sphere_left_button_color() const
{
    const std::array<float, 4> &color = m_modified_extruders_colors[m_first_selected_extruder_idx];
    return {color[0], color[1], color[2], 0.25f};
}

std::array<float, 4> GLGizmoMmuSegmentation::get_cursor_sphere_right_button_color() const
{
    const std::array<float, 4> &color = m_modified_extruders_colors[m_second_selected_extruder_idx];
    return {color[0], color[1], color[2], 0.25f};
}

void TriangleSelectorMmGui::render(ImGuiWrapper *imgui)
{
    if (m_update_render_data)
        update_render_data();

    auto *shader = wxGetApp().get_current_shader();
    if (!shader)
        return;
    assert(shader->get_name() == "mm_gouraud");

    for (size_t color_idx = 0; color_idx < m_gizmo_scene.triangle_indices.size(); ++color_idx)
        if (m_gizmo_scene.has_VBOs(color_idx)) {
            if (color_idx > m_colors.size()) // Seed fill VBO
                shader->set_uniform("uniform_color", TriangleSelectorGUI::get_seed_fill_color(color_idx == (m_colors.size() + 1) ? m_default_volume_color : m_colors[color_idx - (m_colors.size() + 1) - 1]));
            else                             // Normal VBO
                shader->set_uniform("uniform_color", color_idx == 0 ? m_default_volume_color : m_colors[color_idx - 1]);

            m_gizmo_scene.render(color_idx);
        }

    if (m_paint_contour.has_VBO()) {
        ScopeGuard guard_mm_gouraud([shader]() { shader->start_using(); });
        shader->stop_using();

        auto *contour_shader = wxGetApp().get_shader("mm_contour");
        contour_shader->start_using();

        glsafe(::glDepthFunc(GL_LEQUAL));
        m_paint_contour.render();
        glsafe(::glDepthFunc(GL_LESS));

        contour_shader->stop_using();
    }

    m_update_render_data = false;
}

void TriangleSelectorMmGui::update_render_data()
{
    m_gizmo_scene.release_geometry();
    m_vertices.reserve(m_vertices.size() * 3);
    for (const Vertex &vr : m_vertices) {
        m_gizmo_scene.vertices.emplace_back(vr.v.x());
        m_gizmo_scene.vertices.emplace_back(vr.v.y());
        m_gizmo_scene.vertices.emplace_back(vr.v.z());
    }
    m_gizmo_scene.finalize_vertices();

    for (const Triangle &tr : m_triangles)
        if (tr.valid() && !tr.is_split()) {
            int               color = int(tr.get_state()) <= int(m_colors.size()) ? int(tr.get_state()) : 0;
            assert(m_colors.size() + 1 + color < m_gizmo_scene.triangle_indices.size());
            std::vector<int> &iva   = m_gizmo_scene.triangle_indices[color + tr.is_selected_by_seed_fill() * (m_colors.size() + 1)];

            if (iva.size() + 3 > iva.capacity())
                iva.reserve(next_highest_power_of_2(iva.size() + 3));

            iva.emplace_back(tr.verts_idxs[0]);
            iva.emplace_back(tr.verts_idxs[1]);
            iva.emplace_back(tr.verts_idxs[2]);
        }

    for (size_t color_idx = 0; color_idx < m_gizmo_scene.triangle_indices.size(); ++color_idx)
        m_gizmo_scene.triangle_indices_sizes[color_idx] = m_gizmo_scene.triangle_indices[color_idx].size();

    m_gizmo_scene.finalize_triangle_indices();

    m_paint_contour.release_geometry();
    std::vector<Vec2i> contour_edges = this->get_seed_fill_contour();
    m_paint_contour.contour_vertices.reserve(contour_edges.size() * 6);
    for (const Vec2i &edge : contour_edges) {
        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(0)].v.x());
        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(0)].v.y());
        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(0)].v.z());

        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(1)].v.x());
        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(1)].v.y());
        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(1)].v.z());
    }

    m_paint_contour.contour_indices.assign(m_paint_contour.contour_vertices.size() / 3, 0);
    std::iota(m_paint_contour.contour_indices.begin(), m_paint_contour.contour_indices.end(), 0);
    m_paint_contour.contour_indices_size = m_paint_contour.contour_indices.size();

    m_paint_contour.finalize_geometry();
}

wxString GLGizmoMmuSegmentation::handle_snapshot_action_name(bool shift_down, GLGizmoPainterBase::Button button_down) const
{
    wxString action_name;
    if (shift_down)
        action_name = _L("Remove painted color");
    else {
        size_t extruder_id = (button_down == Button::Left ? m_first_selected_extruder_idx : m_second_selected_extruder_idx) + 1;
        action_name        = GUI::format(_L("Painted using: Extruder %1%"), extruder_id);
    }
    return action_name;
}

void GLMmSegmentationGizmo3DScene::release_geometry() {
    if (this->vertices_VBO_id) {
        glsafe(::glDeleteBuffers(1, &this->vertices_VBO_id));
        this->vertices_VBO_id = 0;
    }
    for(auto &triangle_indices_VBO_id : triangle_indices_VBO_ids) {
        glsafe(::glDeleteBuffers(1, &triangle_indices_VBO_id));
        triangle_indices_VBO_id = 0;
    }
    this->clear();
}

void GLMmSegmentationGizmo3DScene::render(size_t triangle_indices_idx) const
{
    assert(triangle_indices_idx < this->triangle_indices_VBO_ids.size());
    assert(this->triangle_indices_sizes.size() == this->triangle_indices_VBO_ids.size());
    assert(this->vertices_VBO_id != 0);
    assert(this->triangle_indices_VBO_ids[triangle_indices_idx] != 0);

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, this->vertices_VBO_id));
    glsafe(::glVertexPointer(3, GL_FLOAT, 3 * sizeof(float), (const void*)(0 * sizeof(float))));

    glsafe(::glEnableClientState(GL_VERTEX_ARRAY));

    // Render using the Vertex Buffer Objects.
    if (this->triangle_indices_sizes[triangle_indices_idx] > 0) {
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->triangle_indices_VBO_ids[triangle_indices_idx]));
        glsafe(::glDrawElements(GL_TRIANGLES, GLsizei(this->triangle_indices_sizes[triangle_indices_idx]), GL_UNSIGNED_INT, nullptr));
        glsafe(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    }

    glsafe(::glDisableClientState(GL_VERTEX_ARRAY));

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
}

void GLMmSegmentationGizmo3DScene::finalize_vertices()
{
    assert(this->vertices_VBO_id == 0);
    if (!this->vertices.empty()) {
        glsafe(::glGenBuffers(1, &this->vertices_VBO_id));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, this->vertices_VBO_id));
        glsafe(::glBufferData(GL_ARRAY_BUFFER, this->vertices.size() * sizeof(float), this->vertices.data(), GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
        this->vertices.clear();
    }
}

void GLMmSegmentationGizmo3DScene::finalize_triangle_indices()
{
    assert(std::all_of(triangle_indices_VBO_ids.cbegin(), triangle_indices_VBO_ids.cend(), [](const auto &ti_VBO_id) { return ti_VBO_id == 0; }));

    assert(this->triangle_indices.size() == this->triangle_indices_VBO_ids.size());
    for (size_t buffer_idx = 0; buffer_idx < this->triangle_indices.size(); ++buffer_idx)
        if (!this->triangle_indices[buffer_idx].empty()) {
            glsafe(::glGenBuffers(1, &this->triangle_indices_VBO_ids[buffer_idx]));
            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->triangle_indices_VBO_ids[buffer_idx]));
            glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, this->triangle_indices[buffer_idx].size() * sizeof(int), this->triangle_indices[buffer_idx].data(), GL_STATIC_DRAW));
            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
            this->triangle_indices[buffer_idx].clear();
        }
}

} // namespace Slic3r
