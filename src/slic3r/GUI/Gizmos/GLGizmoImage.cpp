#include "GLGizmoImage.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/TextureDisplacement.hpp"
#include "libslic3r/format.hpp"

#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GLTexture.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/TextureLibrary.hpp"
#include "slic3r/GUI/Shortcuts.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoPainterBase.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoUtils.hpp"
#include "slic3r/GUI/Jobs/Worker.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <imgui.h>
#include <optional>
#include <string>
#include <vector>
#include <wx/string.h>

namespace Slic3r::GUI {

GLGizmoImage::GLGizmoImage(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id)
    : GLGizmoTextureDisplacement(parent, icon_filename, sprite_id)
{
}

bool GLGizmoImage::on_init()
{
    if (!GLGizmoTextureDisplacement::on_init())
        return false;
    // Everything the base set up stays - it is the same painting machinery - except the key that
    // opens it, which is this tool's own.
    m_shortcut = Shortcut::GizmoImage;
    return true;
}

std::string GLGizmoImage::on_get_name() const
{
    return _u8L("Image");
}

wxString GLGizmoImage::handle_snapshot_action_name(bool shift_down, GLGizmoPainterBase::Button button_down) const
{
    // Same paint, same masks; only the undo step's wording follows the tool the user is in.
    const bool erasing = shift_down || (m_erase_mode != (button_down == GLGizmoPainterBase::Button::Right));
    return erasing ? _L("Remove image from the area") : _L("Place image");
}

bool GLGizmoTextureDisplacement::pick_background_at(const Vec2d &mouse_pos)
{
    ModelVolume              *mv    = texture_volume();
    ModelObject              *mo    = m_c->selection_info()->model_object();
    TextureDisplacementLayer *layer = mv != nullptr ? active_layer() : nullptr;
    if (mv == nullptr || mo == nullptr || layer == nullptr || layer->empty())
        return false;

    const int idx = texture_volume_raycaster_index();
    if (idx < 0)
        return false;
    const Selection  &selection = m_parent.get_selection();
    const Transform3d trafo = mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();
    const Camera     &camera = wxGetApp().plater()->get_camera();

    Vec3f hit = Vec3f::Zero(), normal = Vec3f::Zero();
    if (!m_c->raycaster()->raycasters()[size_t(idx)]->unproject_on_mesh(
            mouse_pos, trafo, camera, hit, normal,
            m_c->object_clipper()->get_clipping_plane()))
        return false;

    // The picture's own colour at that point, read through the projection the bake uses, rather
    // than the pixel on screen: the screen pixel carries the lighting and the filament the colour
    // was already quantized to, so keying on it would remove the wrong thing.
    //
    // Uncorrected, deliberately. The key is applied at the end of the correction chain, against
    // colours the earlier passes have already moved, so it has to be stated in those terms - the
    // same reason the panel's dropper reads the corrected preview.
    const indexed_triangle_set &base = mv->mesh().its;
    const indexed_triangle_set  patch =
        extract_painted_patch(base, mv->texture_displacement_facet(layer->slot).get_data());
    Vec3f center = Vec3f::Zero(), axis = Vec3f::UnitZ(), average_normal = Vec3f::UnitZ();
    if (!patch.indices.empty())
        texture_displacement_patch_frame(patch, texture_displacement_vertex_normals(base), center, axis,
                                         average_normal);

    DecodedHeightTexture tex = decode_height_texture(*layer);
    apply_color_adjustments(tex, *layer);
    Vec3f rgb;
    if (!sample_layer_color(tex, *layer, hit, normal, rgb, center, axis))
        return false;

    for (int c = 0; c < 3; ++c)
        layer->bg_color[c] = std::clamp(rgb[c], 0.f, 1.f);
    layer->bg_from_corners = false;
    layer->bg_remove       = true;
    m_image_bg_picking     = false;
    m_preview_params_dirty = true;
    m_parent.set_as_dirty();
    return true;
}

void GLGizmoTextureDisplacement::set_file_drag_over(bool over)
{
    if (!image_mode() || m_file_drag_over == over)
        return;
    m_file_drag_over = over;
    // A drag is not a mouse move as far as the canvas is concerned, so nothing would repaint and the
    // zone would appear only once something else happened to trigger a frame.
    m_parent.set_as_dirty();
    m_parent.request_extra_frame();
}

// Drawn over the whole canvas on the foreground list, from inside the panel's ImGui frame, which is
// what puts it above the 3D view and above the panel itself.
void GLGizmoTextureDisplacement::render_image_drop_hint()
{
    if (!m_file_drag_over)
        return;

    const ImVec2 size = ImGui::GetIO().DisplaySize;
    if (size.x < 1.f || size.y < 1.f)
        return;
    ImDrawList *dl   = ImGui::GetForegroundDrawList();
    const ImU32 teal = ImGui::GetColorU32(ImGuiWrapper::COL_ORCA);

    // Dim the view so the zone reads as a state the canvas is in, not as decoration on top of it.
    dl->AddRectFilled(ImVec2(0.f, 0.f), size, IM_COL32(0, 0, 0, 70));

    const float inset = std::round(ImGui::GetFontSize() * 1.2f);
    const ImVec2 a(inset, inset), b(size.x - inset, size.y - inset);
    if (b.x <= a.x || b.y <= a.y)
        return;

    // A dashed border, drawn as runs along the four edges: ImGui 1.83 has no dashed stroke.
    const float dash = std::round(ImGui::GetFontSize() * 0.75f);
    const float gap  = dash;
    const float th   = std::max(2.f, std::round(ImGui::GetFontSize() * 0.12f));
    for (float x = a.x; x < b.x; x += dash + gap) {
        const float x1 = std::min(x + dash, b.x);
        dl->AddLine(ImVec2(x, a.y), ImVec2(x1, a.y), teal, th);
        dl->AddLine(ImVec2(x, b.y), ImVec2(x1, b.y), teal, th);
    }
    for (float y = a.y; y < b.y; y += dash + gap) {
        const float y1 = std::min(y + dash, b.y);
        dl->AddLine(ImVec2(a.x, y), ImVec2(a.x, y1), teal, th);
        dl->AddLine(ImVec2(b.x, y), ImVec2(b.x, y1), teal, th);
    }

    // What dropping here will do. Worded as an invitation rather than a promise: anything that is
    // not a picture falls through to the model loader, exactly as it did before this tool existed.
    const std::string text = _u8L("Drop a picture to put it on the model");
    const ImVec2      ts   = ImGui::CalcTextSize(text.c_str());
    const float       pad  = std::round(ImGui::GetFontSize() * 0.8f);
    const ImVec2      c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const ImVec2      r0(c.x - ts.x * 0.5f - pad, c.y - ts.y * 0.5f - pad * 0.6f);
    const ImVec2      r1(c.x + ts.x * 0.5f + pad, c.y + ts.y * 0.5f + pad * 0.6f);
    dl->AddRectFilled(r0, r1, ImGui::GetColorU32(ImGuiCol_WindowBg, 0.92f), ImGui::GetStyle().FrameRounding);
    dl->AddRect(r0, r1, teal, ImGui::GetStyle().FrameRounding, 0, th * 0.5f);
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), text.c_str());
}

bool GLGizmoTextureDisplacement::drop_image_file(const std::string &path)
{
    if (!image_mode() || texture_volume() == nullptr)
        return false;

    // Imported rather than just read: the same thing the panel's Import tile does, so a dropped
    // picture lands in "My images" and is there the next time without hunting for the file again.
    std::string                              error;
    const std::optional<TextureLibraryEntry> entry = import_texture_to_library(path, error);
    if (!entry) {
        show_error(nullptr, error);
        return false;
    }

    // The tool starts with no layer at all, so a drop before anything is loaded has to make one.
    if (active_layer() == nullptr)
        add_texture_layer();
    TextureDisplacementLayer *layer = active_layer();
    if (layer == nullptr)
        return false;
    set_layer_texture(*layer, *entry);
    m_parent.set_as_dirty();
    return true;
}

// ---------------------------------------------------------------------------------------------
// The Image tool's window.
//
// A member of GLGizmoTextureDisplacement rather than of GLGizmoImage, and defined here rather than
// in that class's own file: it needs the same private machinery the displacement panel uses (the
// layer, the texture picker, the previews, the bake), and keeping it here leaves that 1700 line
// panel untouched instead of threading it with tests for which tool is drawing.
//
// What it deliberately leaves out, all of which only means something once the surface moves or once
// there is more than one picture: brushes and the erase toggle, the layer stack, blend modes, depth,
// midlevel, edge fade, tiling mode, the unwrap projection and its UV editor, seams, the wireframe
// and the mesh-preparation sections.
// ---------------------------------------------------------------------------------------------
void GLGizmoTextureDisplacement::render_image_input_window(float x, float y, float bottom_limit)
{
    ModelObject *mo = m_c->selection_info()->model_object();
    if (mo == nullptr)
        return;
    ModelVolume *mv = texture_volume();

    // The palette follows project state nobody tells this gizmo about (the filaments in the
    // sidebar), so it is checked once a frame and a change re-runs the preview.
    cached_palette();
    if (m_palette_changed)
        m_preview_params_dirty = true;

    // Everything this tool fixes, re-pinned each frame so what the preview shows is what Apply will
    // produce: Standard's recipe, zero depth with colour on, a click that floods the connected
    // surface rather than a brush, and the fast preview, which with no relief to build is both
    // instant and exactly the final colour.
    // Two statements, not one `||`: short-circuiting would skip the image presets on any frame the
    // standard ones changed something.
    const bool std_changed   = apply_standard_mode_presets(mv);
    const bool image_changed = apply_image_mode_presets(mv);
    if (std_changed || image_changed)
        m_preview_params_dirty = true;
    m_tool_type   = ToolType::SMART_FILL;
    m_cursor_type = TriangleSelector::CursorType::POINTER;
    m_erase_mode  = false;
    m_auto_update = true;
    if (!m_use_shaded_preview)
        apply_view_mode(1);

    float scale = m_parent.get_scale();
#ifdef WIN32
    const int dpi = get_dpi_for_window(wxGetApp().GetTopWindow());
    scale *= (float) dpi / (float) DPI_DEFAULT;
#endif // WIN32

    const float approx_height = m_imgui->scaled(22.f);
    y = std::min(y, bottom_limit - approx_height);

    // Docked (the default) it is pinned to the right edge of the 3D canvas and cannot be moved, the
    // same place and the same rule as the displacement panel, so the two tools do not put their
    // controls in different corners. Undocked it becomes an ordinary floating window with a title
    // bar to drag it by; the position is deliberately not seeded on undock, so it stays exactly
    // where it was and the user simply gains the ability to move it.
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse;
    if (!m_undocked) {
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;
        // Right-aligned (pivot 1), so the width the window auto-resized to last frame does not need
        // to be known here.
        float right = float(m_parent.get_canvas_size().get_width()) - m_imgui->scaled(0.5f);
        GizmoImguiSetNextWIndowPos(right, y, 0.f, 0.f, ImGuiCond_Always, 1.0f, 0.0f);
    }

    ImGuiWrapper::push_toolbar_style(m_parent.get_scale());
    GizmoImguiBegin(get_name(), flags);
    ensure_panel_icons();
    const float previous_body_h   = m_panel_body_h;
    const float previous_footer_h = m_panel_footer_h;

    const ImGuiStyle &style    = ImGui::GetStyle();
    const bool        busy     = m_bake_in_progress || m_prepare_in_progress;
    const float       panel_w  = m_imgui->scaled(16.f);
    const float       frame_h  = ImGui::GetFrameHeight();
    const float       icon_sm  = std::round(frame_h * 1.05f);
    const float       gap_s    = std::round(m_imgui->scaled(0.4f));
    const float       label_w  = m_imgui->scaled(4.6f);
    const float       wrap_w   = m_imgui->scaled(15.f);
    const ImVec4      orca     = ImGuiWrapper::COL_ORCA;
    const ImVec4      col_frame = ImGui::GetStyleColorVec4(ImGuiCol_Separator);

    const auto hover_tip = [&](const auto &text) {
        if (ImGui::IsItemHovered())
            m_imgui->tooltip(text, wrap_w);
    };
    // Label column plus a slider filling the rest of the row, the same shape the displacement panel
    // uses, so the two tools read as one family.
    const auto float_row = [&](const char *id, const wxString &label, float *v, float v_min, float v_max,
                               const char *format, bool log) -> bool {
        const float x0 = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        m_imgui->text(label);
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x0 + label_w));
        ImGui::SetNextItemWidth(x0 + panel_w - ImGui::GetCursorPosX());
        return ImGui::SliderFloat(id, v, v_min, v_max, format,
                                  ImGuiSliderFlags_AlwaysClamp | (log ? ImGuiSliderFlags_Logarithmic : 0));
    };
    // Borderless icon button, for the dock toggle: always the grey monochrome variant.
    const auto icon_button = [&](int uid, const std::string &iconfile, const wxString &label,
                                 const wxString &tip_text) -> bool {
        const auto  it  = m_panel_icon_map.find(iconfile);
        const float pad = std::max(1.f, std::round(icon_sm * 0.14f));
        bool        clicked = false;
        ImGui::PushID(uid);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
        if (it != m_panel_icon_map.end() && !it->second.empty() && it->second[0]->is_valid()) {
            const IconManager::Icon &ic = *it->second[0];
            clicked = m_imgui->image_button((ImTextureID) (intptr_t) ic.tex_id,
                                            ImVec2(icon_sm - 2.f * pad, icon_sm - 2.f * pad), ic.tl, ic.br, int(pad));
        } else {
            clicked = ImGui::Button(label.ToUTF8().data(), ImVec2(0.f, icon_sm));
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::PopID();
        hover_tip(tip_text);
        return clicked;
    };
    // A section header that folds its contents away. ImGui's own CollapsingHeader draws a filled bar
    // that is louder than anything else in this window, so this is the same triangle-and-text the
    // displacement panel's "More settings" link uses, at heading weight.
    const auto section = [&](const char *id, const wxString &label, bool &open) {
        const float  arrow = std::round(ImGui::GetFontSize() * 0.55f);
        const ImVec2 ts    = ImGui::CalcTextSize(label.ToUTF8().data());
        const ImVec2 p     = ImGui::GetCursorScreenPos();
        ImGui::PushID(id);
        if (ImGui::InvisibleButton("##sec", ImVec2(panel_w, ts.y + gap_s)))
            open = !open;
        const bool  hot = ImGui::IsItemHovered();
        ImDrawList *dl  = ImGui::GetWindowDrawList();
        const ImU32 col = ImGui::GetColorU32(hot ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        const float cx = p.x + 0.5f * arrow, cy = p.y + 0.5f * ts.y, hs = 0.32f * arrow;
        if (open)
            dl->AddTriangleFilled(ImVec2(cx - hs, cy - 0.5f * hs), ImVec2(cx + hs, cy - 0.5f * hs),
                                  ImVec2(cx, cy + 0.5f * hs), col);
        else
            dl->AddTriangleFilled(ImVec2(cx - 0.5f * hs, cy - hs), ImVec2(cx - 0.5f * hs, cy + hs),
                                  ImVec2(cx + 0.5f * hs, cy), col);
        dl->AddText(ImVec2(p.x + arrow + gap_s, p.y), col, label.ToUTF8().data());
        ImGui::PopID();
        return open;
    };

    // The integer twin of float_row, for the colour count.
    const auto int_row = [&](const char *id, const wxString &label, int *v, int v_min, int v_max,
                             const char *format) -> bool {
        const float x0 = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        m_imgui->text(label);
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x0 + label_w));
        ImGui::SetNextItemWidth(x0 + panel_w - ImGui::GetCursorPosX());
        return ImGui::SliderInt(id, v, v_min, v_max, format, ImGuiSliderFlags_AlwaysClamp);
    };
    // Framed icon toggle, the displacement panel's one cut down to what this window needs.
    const auto icon_toggle = [&](int uid, const std::string &iconfile, bool active, const wxString &label,
                                 const wxString &tip_text) -> bool {
        const auto  it   = m_panel_icon_map.find(iconfile);
        const float pad  = std::max(1.f, std::round(icon_sm * 0.14f));
        bool        clicked = false;
        ImGui::PushID(uid);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(orca.x, orca.y, orca.z, 0.22f) : ImVec4(0.f, 0.f, 0.f, 0.f));
        if (it != m_panel_icon_map.end() && int(it->second.size()) > (active ? 1 : 0) &&
            it->second[active ? 1 : 0]->is_valid()) {
            const IconManager::Icon &ic = *it->second[active ? 1 : 0];
            clicked = m_imgui->image_button((ImTextureID) (intptr_t) ic.tex_id,
                                            ImVec2(icon_sm - 2.f * pad, icon_sm - 2.f * pad), ic.tl, ic.br, int(pad));
        } else {
            clicked = ImGui::Button(label.ToUTF8().data(), ImVec2(0.f, icon_sm));
        }
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                            ImGui::GetColorU32(active ? orca : col_frame), style.FrameRounding);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::PopID();
        hover_tip(tip_text);
        return clicked;
    };

    TextureDisplacementLayer *layer = mv != nullptr ? active_layer() : nullptr;

    {
        const float x0 = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        m_imgui->push_bold_font();
        m_imgui->text(wxString::FromUTF8(get_name(false)));
        m_imgui->pop_bold_font();
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x0 + panel_w - icon_sm));
        if (icon_button(920, "canvas_drag.svg", m_undocked ? _L("Dock panel") : _L("Undock panel"),
                        _L("Detach this panel so it can be dragged anywhere over the 3D view, or dock it back "
                           "against the right edge.")))
            m_undocked = !m_undocked;
    }
    ImGui::Separator();

    // Everything between the title and the footer scrolls as one, so Apply and Close stay on screen
    // on a short canvas instead of being the first thing pushed off the bottom. Sized from last
    // frame's content and footer heights, as the displacement panel's body is.
    const float bottom     = m_undocked ? ImGui::GetIO().DisplaySize.y : bottom_limit;
    const float max_body_h = std::max(m_imgui->scaled(8.f), bottom - ImGui::GetCursorScreenPos().y -
                                                                m_panel_footer_h - style.WindowPadding.y -
                                                                style.ItemSpacing.y);
    const float body_h      = m_panel_body_h > 0.f ? std::min(m_panel_body_h + 1.f, max_body_h) : max_body_h;
    const float scrollbar_w = m_imgui->scaled(0.5f);
    const ImVec4 grab       = wxGetApp().dark_mode() ? ImVec4(1.f, 1.f, 1.f, 0.26f) : ImVec4(0.f, 0.f, 0.f, 0.26f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, scrollbar_w);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.5f * scrollbar_w);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, grab);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(grab.x, grab.y, grab.z, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(grab.x, grab.y, grab.z, 0.65f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::BeginChild("##image_body", ImVec2(panel_w, body_h), false);

    // The picture, shown whole and the full width of the window: it is the thing being placed, so a
    // row-sized thumbnail beside a file name is the wrong size for it. Clicking it opens the library,
    // which is also where Import lives, and before anything is loaded the same box is an empty frame
    // with a plus in it - one target, in the same place, whichever state the tool is in.
    const auto open_library = [&](int slot) {
        m_picker_slot         = slot;
        m_picker_open_request = true;
    };
    // A layer with no picture on it is the starting state, not a broken one: the tool creates the
    // layer empty and the panel stays on the frame below until a picture has been chosen.
    const bool has_picture = layer != nullptr && !layer->empty();
    const bool placed      = mv != nullptr && mv->is_texture_displacement_painted();

    // The handle comes up by itself the first moment there is something to anchor it to, so the
    // picture can be nudged straight after the click that placed it without hunting for a toggle.
    // Armed once: turning it back off has to stay off, not be undone on the next frame.
    if (has_picture && placed && !m_image_handle_armed) {
        m_image_handle_armed = true;
        if (update_adjust_anchor())
            m_adjust_texture_mode = true;
    }
    if (!has_picture) {
        const float  box_h = std::round(panel_w * 0.6f);
        const ImVec2 p0    = ImGui::GetCursorScreenPos();
        m_imgui->disabled_begin(busy || mv == nullptr);
        const bool clicked = ImGui::InvisibleButton("##load", ImVec2(panel_w, box_h));
        m_imgui->disabled_end();
        const bool  hot = ImGui::IsItemHovered();
        ImDrawList *dl  = ImGui::GetWindowDrawList();
        const ImU32 col = ImGui::GetColorU32(hot ? orca : col_frame);
        dl->AddRect(p0, ImVec2(p0.x + panel_w, p0.y + box_h), col, style.FrameRounding);
        const float cx = p0.x + 0.5f * panel_w, cy = p0.y + 0.5f * box_h;
        const float arm = std::round(box_h * 0.18f), thick = std::max(1.f, std::round(box_h * 0.035f));
        dl->AddLine(ImVec2(cx - arm, cy), ImVec2(cx + arm, cy), col, thick);
        dl->AddLine(ImVec2(cx, cy - arm), ImVec2(cx, cy + arm), col, thick);
        hover_tip(_u8L("Choose a picture to put on the model."));
        if (clicked) {
            if (layer == nullptr)
                add_texture_layer();           // created with no picture on it, on purpose
            open_library(layer != nullptr ? layer->slot : m_active_layer_slot);
        }
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + panel_w);
        ImGui::TextDisabled("%s", _u8L("Choose a picture, then click the model to put it there.").c_str());
        ImGui::PopTextWrapPos();
    } else {
        // The colour texture is the corrected image at full resolution, which is exactly what should
        // be previewed - move a correction slider and the box follows. A grayscale picture has no
        // colour texture, so it falls back to the height thumbnail.
        GLTexture  *shown  = get_layer_color_texture(*layer);
        if (shown == nullptr)
            shown = get_layer_thumbnail(*layer);
        const float aspect = std::max(0.05f, layer_texture_aspect(*layer));
        const float box_h  = std::clamp(std::round(panel_w / aspect), std::round(panel_w * 0.35f),
                                        std::round(panel_w * 1.1f));
        const ImVec2 box_min = ImGui::GetCursorScreenPos();
        bool         box_hit = false;
        if (shown != nullptr) {
            box_hit = ImGui::ImageButton((ImTextureID) (intptr_t) shown->get_id(), ImVec2(panel_w, box_h),
                                         ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), 0);
        } else {
            box_hit = ImGui::Button("?##thumb", ImVec2(panel_w, box_h));
        }
        hover_tip(m_image_bg_picking ?
                      _u8L("Click the background, here or on the model.") :
                      _u8L("Choose a different picture. Where you placed it and how it is corrected stay as "
                           "they are."));
        if (box_hit) {
            if (!m_image_bg_picking) {
                open_library(layer->slot);
            } else {
                // Read straight out of the decoded image rather than off the screen: the preview is
                // scaled, and it already has the corrections applied, so a screen read would key the
                // corrected colour against itself.
                const ImVec2 rel((ImGui::GetIO().MousePos.x - box_min.x) / panel_w,
                                 (ImGui::GetIO().MousePos.y - box_min.y) / box_h);
                const DecodedHeightTexture &tex = decode_height_texture(*layer);
                if (tex.has_color()) {
                    const int px = std::clamp(int(rel.x * float(tex.width)), 0, tex.width - 1);
                    const int py = std::clamp(int(rel.y * float(tex.height)), 0, tex.height - 1);
                    const size_t i = (size_t(py) * size_t(tex.width) + size_t(px)) * 3;
                    for (int c = 0; c < 3; ++c)
                        layer->bg_color[c] = float(tex.rgb[i + size_t(c)]) / 255.f;
                    layer->bg_from_corners = false;
                    layer->bg_remove       = true;
                    m_preview_params_dirty = true;
                }
                m_image_bg_picking = false;
            }
        }
        const std::string name = layer->name.empty() ? _u8L("Image") : layer->name;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + panel_w);
        ImGui::TextDisabled("%s", name.c_str());
        ImGui::PopTextWrapPos();

        if (!height_texture_has_color(*layer)) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + panel_w);
            m_imgui->warning_text(_L("This image is grayscale, so it has no colors to print. Choose a color picture."));
            ImGui::PopTextWrapPos();
        }

        // Placing it: one click floods the surface it lands on, out to the first fold sharper than
        // the angle below. No brush, no erase toggle - on a flat side or a cube face the click alone
        // is the whole gesture, and the slider is there for the cases where it is not.
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + panel_w);
        ImGui::TextDisabled("%s", placed ? _u8L("Click the model to move the picture somewhere else.").c_str() :
                                           _u8L("Click the model to put the picture there.").c_str());
        ImGui::PopTextWrapPos();
        float fill_angle = m_smart_fill_angle;
        if (float_row("##fill_angle", _L("Spread"), &fill_angle, SmartFillAngleMin, SmartFillAngleMax, "%.0f°", false))
            m_smart_fill_angle = fill_angle;
        hover_tip(_u8L("How far a click spreads from the spot you hit: it keeps going while the surface stays "
                       "flatter than this, and stops at a sharper fold. Small values keep it to one face."));

        ImGui::Separator();
        if (section("place", _L("Placement"), m_image_sec_place)) {

        // Projection, minus Unwrap: that one is an editor's worth of UI (seams, islands, the UV pane)
        // for a tool whose whole point is a click and a few sliders.
        {
            struct Mapping { const char *file; wxString label; wxString tip; TextureProjectionMethod method; };
            const Mapping mappings[] = {
                { "menu_obj_cube.svg", _L("Flat"),
                  _L("Flat - lays the picture straight onto the surface from whichever axis it faces, and blends "
                     "where it wraps around an edge. The right choice for a face or a flat side."),
                  TextureProjectionMethod::Triplanar },
                { "menu_obj_cylinder.svg", _L("Around"),
                  _L("Around - wraps the picture around the area, for a round part like a mug or a tube."),
                  TextureProjectionMethod::Cylindrical },
                { "menu_obj_sphere.svg", _L("Ball"),
                  _L("Ball - wraps the picture around the area in both directions, for a dome or a sphere."),
                  TextureProjectionMethod::Spherical },
                { "texture_displacement_map_view.svg", _L("From view"),
                  _L("From view - throws the picture onto the area from where you are looking now, like a slide "
                     "projector. Line the view up with the surface first, because faces turned away stretch."),
                  TextureProjectionMethod::ViewProjected },
            };
            const float x0 = ImGui::GetCursorPosX();
            ImGui::AlignTextToFramePadding();
            // Plain text, like every other label in this window: the displacement panel greys its
            // headings because it has many, and here there are five rows in total.
            m_imgui->text(_L_CONTEXT("Mapping", "Texture Displacement"));
            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x0 + label_w));
            for (int mi = 0; mi < int(IM_ARRAYSIZE(mappings)); ++mi) {
                if (mi > 0)
                    ImGui::SameLine(0.f, gap_s);
                const bool on = layer->projection_method == mappings[mi].method;
                if (icon_toggle(900 + mi, mappings[mi].file, on, mappings[mi].label, mappings[mi].tip) && !on) {
                    // Capture the current view the moment "From view" is chosen, so it does something
                    // sensible immediately rather than projecting from a stale direction.
                    if (mappings[mi].method == TextureProjectionMethod::ViewProjected)
                        capture_view_projection(*layer);
                    layer->projection_method = mappings[mi].method;
                    m_preview_params_dirty   = true;
                }
            }
        }
        if (layer->projection_method == TextureProjectionMethod::ViewProjected) {
            if (m_imgui->button(_u8L("Capture current view"))) {
                capture_view_projection(*layer);
                m_preview_params_dirty = true;
            }
            hover_tip(_u8L("Throws the picture on again from where you are looking now. Orbit the model, press "
                           "this, and it is re-laid from the new angle."));
        }

        m_preview_params_dirty |= float_row("##size", _L("Size"), &layer->tiling_scale, 1.f, 300.f, "%.1f mm", true);
        hover_tip(_u8L("How wide the picture is on the model, in millimetres."));
        m_preview_params_dirty |= float_row("##rotation", _L("Rotation"), &layer->rotation_deg, 0.f, 360.f, "%.0f°", false);
        hover_tip(_u8L("Turns the picture on the surface, for lining it up with an edge of the model."));

        // Sliding it into place, and whether it repeats. Both belong on one row: the handle is the
        // coarse control and Tile decides what it is sliding - one copy, or a pattern.
        {
            const float x0 = ImGui::GetCursorPosX();
            ImGui::AlignTextToFramePadding();
            m_imgui->text(_L("Move"));
            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x0 + label_w));
            if (icon_toggle(910, "texture_displacement_adjust.svg", m_adjust_texture_mode, _L("Move on the model"),
                            _L("Move on the model - drag the handle that appears on the surface to slide the "
                               "picture around, or one of its two arrows to slide it along just that axis."))) {
                if (!m_adjust_texture_mode) {
                    update_model_object(); // flush any pending placement before anchoring to it
                    if (update_adjust_anchor())
                        m_adjust_texture_mode = true;
                    else
                        show_error(nullptr, _u8L("Click the model to put the picture somewhere first."));
                } else {
                    m_adjust_texture_mode = false;
                    m_adjust_drag_handle  = AdjustHandle::None;
                }
            }
            ImGui::SameLine(0.f, gap_s * 2.f);
            if (m_imgui->bbl_checkbox(_L("Tile"), layer->tile_enabled))
                m_preview_params_dirty = true;
            hover_tip(_u8L("Repeats the picture over the area instead of placing a single copy. Off by "
                           "default, because a photo or a logo goes on once; Size then sets how big that "
                           "one copy is."));
        }

        // The one number that decides how sharp the printed picture is. Each triangle prints in a
        // single filament, so the mesh under the paint has to be finer than the detail wanted out of
        // the image - and the budget above is what stops that running away.
        {
            TextureDisplacementOptions &opts = mv->texture_displacement_options;
            float                       edge = opts.v2_refine_mm;
            m_imgui->disabled_begin(busy);
            if (float_row("##resolution", _L("Detail"), &edge, 0.02f, 2.f, "%.2f mm", true)) {
                opts.v2_refine_mm      = edge;
                m_preview_params_dirty = true;
            }
            m_imgui->disabled_end();
            hover_tip(_u8L("How fine the mesh is made under the picture, in millimetres. Each triangle prints "
                           "in one filament, so this is how sharp an edge between two colors can come out. "
                           "Smaller costs triangles fast: halving it needs four times as many."));

            // Refining past the budget is not an error - the bake simplifies back down to it - but the
            // result then carries less of the picture than this slider asks for, and without saying so
            // the only sign is a print that came out softer than expected.
            const size_t budget = size_t(std::max(0, opts.v2_max_triangles_k)) * 1000;
            const size_t needed = estimated_refined_triangles(*mv, edge);
            if (budget > 0 && needed > budget * 5 / 4) {
                const auto count = [](size_t n) {
                    return n >= 1000000 ? Slic3r::format("%1$.1f M", double(n) / 1000000.) :
                                          Slic3r::format("%1% k", (n + 500) / 1000);
                };
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + panel_w);
                m_imgui->warning_text(Slic3r::format(_u8L("Needs about %1% triangles, budget %2% - it will be "
                                                          "simplified back down and come out softer."),
                                                     count(needed), count(budget)));
                ImGui::PopTextWrapPos();
            }
        }

        } // Placement

        ImGui::Separator();
        if (section("color", _L("Color"), m_image_sec_color)) {

        // Colour correction. Every one of these changes which filament each area lands in, which is
        // why they are here rather than left to whatever the file happened to contain: a photo that
        // prints as mud usually only needs contrast.
        m_preview_params_dirty |= float_row("##brightness", _L("Brightness"), &layer->brightness, -1.f, 1.f, "%.2f", false);
        hover_tip(_u8L("Lightens or darkens the whole picture before it is matched to your filaments."));
        m_preview_params_dirty |= float_row("##contrast", _L("Contrast"), &layer->contrast, -1.f, 1.f, "%.2f", false);
        hover_tip(_u8L("Pushes light and dark apart. Raise it when a photo prints as mud; lower it when "
                       "everything collapses into the lightest and darkest filament."));
        m_preview_params_dirty |= float_row("##saturation", _L("Saturation"), &layer->saturation, 0.f, 2.f, "%.2f", false);
        hover_tip(_u8L("How strong the colors are. At 0 the picture is gray, so it prints in your grays and "
                       "blacks; raising it pushes each area towards a purer filament."));
        m_preview_params_dirty |= float_row("##hue", _L("Hue"), &layer->hue, -180.f, 180.f, "%.0f°", false);
        hover_tip(_u8L("Turns every color of the picture around the color wheel, without changing how light "
                       "it is. Your filaments are a fixed, arbitrary set, so this is what lets the picture "
                       "meet them: if you have orange loaded but no red, a small turn puts the reds on it."));
        m_preview_params_dirty |= float_row("##gamma", _L("Gamma"), &layer->gamma, 0.2f, 5.f, "%.2f", true);
        hover_tip(_u8L("Lifts or drops the midtones without touching the brightest and darkest parts. Use it "
                       "when the middle of the picture is the part coming out wrong."));
        m_preview_params_dirty |= float_row("##sharpen", _L("Sharpen"), &layer->sharpness, 0.f, 1.f, "%.2f", false);
        hover_tip(_u8L("Crisps up the edges before the picture is matched to your filaments. Worth more here "
                       "than in a photo: a soft edge between two colors quantizes into a ragged band of "
                       "both, and sharpening first makes it land in one place."));

        // How many colours the picture may use. 2 is a logo in two flat colours, 4 or 5 a poster,
        // off a photograph. This is not the same as the displacement tool's "Mixed colors", which
        // caps how many mixed filament slots a bake may create rather than what the picture uses.
        {
            int count = layer->color_count;
            if (int_row("##colors", _L("Colors"), &count, 0, 12, count == 0 ? _u8L("Off").c_str() : "%d")) {
                // 1 is not a picture, so the slider steps over it on the way out of Off.
                layer->color_count     = count == 1 ? 2 : count;
                m_preview_params_dirty = true;
            }
            hover_tip(_u8L("Reduces the picture to this many colors before your filaments are matched to "
                           "it, so it prints in at most that many. The colors are the ones the picture is "
                           "actually made of, not an even split of the range. Off leaves it alone."));
        }

        if (m_imgui->bbl_checkbox(_L("Negative"), layer->color_invert))
            m_preview_params_dirty = true;
        hover_tip(_u8L("Prints the picture inverted, light for dark and each color for its opposite."));

        } // Color

        ImGui::Separator();
        if (!section("bg", _L("Background"), m_image_sec_bg)) {
            // Folded away, but still worth saying whether it is doing anything.
            ImGui::SameLine();
            ImGui::TextDisabled("%s", layer->bg_remove ? _u8L("removed").c_str() : _u8L("kept").c_str());
        } else {
        if (m_imgui->bbl_checkbox(_L("Remove background"), layer->bg_remove))
            m_preview_params_dirty = true;
        hover_tip(_u8L("Leaves the background unprinted, so the object's own color shows through instead of "
                       "a slab of white behind a logo. A picture that is already transparent needs none of "
                       "this: those parts are left out either way."));
        if (layer->bg_remove) {
            // Which colour is being removed, shown rather than described: the swatch is also a
            // picker, and the dropper beside it takes the colour from the preview above.
            const float x0 = ImGui::GetCursorPosX();
            ImGui::AlignTextToFramePadding();
            m_imgui->text(_L("Color"));
            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x0 + label_w));
            float shown_bg[3] = { layer->bg_color[0], layer->bg_color[1], layer->bg_color[2] };
            if (layer->bg_from_corners) {
                // Nothing has been picked, so show what the corners actually gave: the same median
                // the bake will key on, not a placeholder white.
                const DecodedHeightTexture &tex = decode_height_texture(*layer);
                if (tex.has_color() && tex.width > 1 && tex.height > 1) {
                    const size_t n = size_t(tex.width) * size_t(tex.height);
                    const size_t corner[4] = { 0, size_t(tex.width) - 1,
                                               (size_t(tex.height) - 1) * size_t(tex.width), n - 1 };
                    for (int c = 0; c < 3; ++c) {
                        std::array<float, 4> v{};
                        for (int i = 0; i < 4; ++i)
                            v[size_t(i)] = float(tex.rgb[corner[i] * 3 + size_t(c)]);
                        std::sort(v.begin(), v.end());
                        shown_bg[c] = 0.5f * (v[1] + v[2]) / 255.f;
                    }
                }
            }
            ImGui::SetNextItemWidth(x0 + panel_w - ImGui::GetCursorPosX() - icon_sm - gap_s);
            if (ImGui::ColorEdit3("##bg_color", shown_bg,
                                  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
                for (int c = 0; c < 3; ++c)
                    layer->bg_color[c] = shown_bg[c];
                layer->bg_from_corners = false;
                m_preview_params_dirty = true;
            }
            hover_tip(layer->bg_from_corners ?
                          _u8L("Taken from the four corners of the picture, which is right for a logo on a "
                               "plain card. Click to choose a color yourself instead.") :
                          _u8L("The color being removed. Click to change it."));
            ImGui::SameLine(0.f, gap_s);
                if (icon_toggle(930, "texture_displacement_pipette.svg", m_image_bg_picking, _L("Pick the color"),
                            _L("Pick the color - then click the background, either in the preview above or on "
                               "the model itself.")))
                m_image_bg_picking = !m_image_bg_picking;
            if (!layer->bg_from_corners) {
                ImGui::SameLine(0.f, gap_s);
                if (m_imgui->button(_u8L("Corners"))) {
                    layer->bg_from_corners = true;
                    m_preview_params_dirty = true;
                }
                hover_tip(_u8L("Back to taking the color from the picture's four corners."));
            }

            m_preview_params_dirty |= float_row("##bg_tol", _L("Tolerance"), &layer->bg_tolerance, 0.01f, 0.6f, "%.2f", false);
            hover_tip(_u8L("How different from that color still counts as background. Raise it when a "
                           "shadow or a gradient is left behind; lower it when it starts eating the picture."));
        }

        } // Background

        // Outside both sections, because it clears the colour corrections and the background key
        // alike, and a reset that lives inside one of them would look like it only reached that far.
        ImGui::Separator();
        if (m_imgui->button(_u8L("Reset all"))) {
            layer->brightness      = 0.f;
            layer->contrast        = 0.f;
            layer->saturation      = 1.f;
            layer->hue             = 0.f;
            layer->gamma           = 1.f;
            layer->sharpness       = 0.f;
            layer->color_count     = 0;
            layer->bg_remove       = false;
            layer->bg_tolerance    = 0.15f;
            layer->bg_from_corners = true;
            layer->bg_color[0] = layer->bg_color[1] = layer->bg_color[2] = 1.f;
            m_image_bg_picking     = false;
            layer->color_invert    = false;
            m_preview_params_dirty = true;
        }
        hover_tip(_u8L("Puts every correction and the background back to where they started, leaving the "
                       "picture as it was loaded. Where you placed it and how big it is are not touched."));
    }

    // The library popup lives in this window's scope, where its open request and the popup itself
    // share one ID stack.
    {
        const ImVec2 win_min = ImGui::GetWindowPos();
        const ImVec2 win_sz  = ImGui::GetWindowSize();
        render_texture_library_popup(win_min, ImVec2(win_min.x + win_sz.x, win_min.y + win_sz.y));
    }

    // ImGui sliders report "changed" on every frame of a drag, and a preview rebuild is real work,
    // so with the mouse still down it waits for the release.
    if (m_preview_params_dirty && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        rebuild_preview();
        m_preview_params_dirty = false;
    }

    m_panel_body_h = ImGui::GetCursorPosY() - style.ItemSpacing.y;
    ImGui::EndChild();
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(2);

    const float footer_top = ImGui::GetCursorScreenPos().y;
    ImGui::Separator();
    {
        const bool painted   = placed;
        const bool no_colors = mv != nullptr && !any_layer_colors(*mv);
        // Colour is the only thing this bake produces, so with a grayscale picture there would be
        // nothing to show for it - and it is not a cheap no-op: the bake still refines the painted
        // area and replaces the mesh with the result.
        const bool can_apply = !busy && mv != nullptr && painted && !no_colors;

        const float button_h = std::round(frame_h * 1.25f);
        const float button_w = std::max({ ImGui::CalcTextSize(_u8L("Apply").c_str()).x,
                                          ImGui::CalcTextSize(_u8L("Close").c_str()).x,
                                          ImGui::CalcTextSize(_u8L("Stop").c_str()).x,
                                          ImGui::CalcTextSize(_u8L("Working...").c_str()).x }) +
                               m_imgui->scaled(0.5f);
        const float row_y  = ImGui::GetCursorPosY();
        const float icon_h = 21.f * scale;
        const float row_h  = std::max(icon_h, button_h);

        ImGui::SetCursorPosY(row_y + (row_h - icon_h) * .5f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(int(m_imgui->scaled(.5f)), style.ItemSpacing.y));
        GLGizmoUtils::render_wiki_guide_button(m_parent, scale,
                                               "https://www.orcaslicer.com/wiki/print_prepare/prepare_texture_displacement");
        ImGui::PopStyleVar(1);

        ImGui::SameLine(panel_w - button_w * 2 - style.ItemSpacing.x);
        GLGizmoUtils::push_orca_button_style();
        m_imgui->push_bold_font();
        m_imgui->disabled_begin(!can_apply);
        ImGui::SetCursorPosY(row_y + (row_h - button_h) * .5f);
        const std::string apply_label = busy ? _u8L("Working...") : _u8L("Apply");
        if (ImGui::Button((apply_label + "##apply").c_str(), ImVec2(button_w, button_h)))
            bake_standard();
        m_imgui->disabled_end();
        m_imgui->pop_bold_font();
        GLGizmoUtils::pop_orca_button_style();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            m_imgui->tooltip(!has_picture ? _u8L("Choose a picture first.") :
                             !painted     ? _u8L("Click the model to place the picture first.") :
                             no_colors        ? _u8L("This picture is grayscale, so there is nothing to print. "
                                                     "Choose a color picture.") :
                                                _u8L("Prints the picture where you placed it, in the nearest of your "
                                                     "loaded filaments. The surface itself is left exactly where it "
                                                     "is: this adds color, not relief."),
                             wrap_w);

        ImGui::SameLine();
        if (busy) {
            if (ImGui::Button((_u8L("Stop") + "##stop").c_str(), ImVec2(button_w, button_h)))
                wxGetApp().plater()->get_ui_job_worker().cancel_all();
            hover_tip(_u8L("Stops it. Whatever it had already finished stays on the model, and can be undone."));
        } else {
            if (ImGui::Button((_u8L("Close") + "##close").c_str(), ImVec2(button_w, button_h)))
                m_parent.reset_all_gizmos();
            hover_tip(_u8L("Closes the tool. Your picture and where you put it stay with the model."));
        }
    }

    m_panel_footer_h = ImGui::GetCursorScreenPos().y - footer_top;
    // Both heights feed back into next frame's layout, so the window needs one more frame whenever
    // either of them moved.
    if (std::abs(m_panel_body_h - previous_body_h) > 0.5f || std::abs(m_panel_footer_h - previous_footer_h) > 0.5f)
        m_parent.request_extra_frame();

    // Last, on the foreground list, so it covers the panel as well as the view.
    render_image_drop_hint();

    GizmoImguiEnd();
    ImGuiWrapper::pop_toolbar_style();
}

} // namespace Slic3r::GUI
