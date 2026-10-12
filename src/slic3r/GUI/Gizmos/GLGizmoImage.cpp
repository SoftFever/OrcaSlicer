#include "GLGizmoImage.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/TextureDisplacement.hpp"
#include "libslic3r/format.hpp"

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GLTexture.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Shortcuts.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoPainterBase.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoUtils.hpp"
#include "slic3r/GUI/Jobs/Worker.hpp"

#include <algorithm>
#include <cstdint>
#include <imgui.h>
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
    if (apply_standard_mode_presets(mv) || apply_image_mode_presets(mv))
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
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);

    ImGuiWrapper::push_toolbar_style(m_parent.get_scale());
    GizmoImguiBegin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    ensure_panel_icons();

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

    m_imgui->push_bold_font();
    m_imgui->text(wxString::FromUTF8(get_name(false)));
    m_imgui->pop_bold_font();
    ImGui::Separator();

    if (layer == nullptr) {
        // Nothing loaded yet, so the window is one button. Picking an image is what creates the
        // layer; there is no "add a layer first" step to understand.
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + panel_w);
        ImGui::TextDisabled("%s", _u8L("Load a picture, then click the model to put it there.").c_str());
        ImGui::PopTextWrapPos();
        m_imgui->disabled_begin(busy || mv == nullptr);
        if (ImGui::Button((_u8L("Load image") + "##load").c_str(), ImVec2(panel_w, 0.f))) {
            add_texture_layer();           // seeds the first library image that has colour in it
            m_picker_slot         = m_active_layer_slot;
            m_picker_open_request = true;  // ... and offers the library, Import included, straight away
        }
        m_imgui->disabled_end();
    } else {
        // The picture: thumbnail and name both reopen the library, which is also where Import lives.
        const std::string name = layer->name.empty() ? _u8L("Image") : layer->name;
        if (GLTexture *thumb = get_layer_thumbnail(*layer)) {
            if (ImGui::ImageButton((ImTextureID) (intptr_t) thumb->get_id(), ImVec2(frame_h, frame_h),
                                   ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), 0)) {
                m_picker_slot         = layer->slot;
                m_picker_open_request = true;
            }
        } else if (ImGui::Button("?##thumb", ImVec2(frame_h, frame_h))) {
            m_picker_slot         = layer->slot;
            m_picker_open_request = true;
        }
        hover_tip(_u8L("Choose a different picture. Where you placed it and how it is corrected stay as they are."));
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(name.c_str());

        if (!height_texture_has_color(*layer)) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + panel_w);
            m_imgui->warning_text(_L("This image is grayscale, so it has no colors to print. Choose a color picture."));
            ImGui::PopTextWrapPos();
        }

        // Placing it: one click floods the surface it lands on, out to the first fold sharper than
        // the angle below. No brush, no erase toggle - on a flat side or a cube face the click alone
        // is the whole gesture, and the slider is there for the cases where it is not.
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + panel_w);
        ImGui::TextDisabled("%s", mv != nullptr && mv->is_texture_displacement_painted() ?
                                      _u8L("Click the model to move the picture somewhere else.").c_str() :
                                      _u8L("Click the model to put the picture there.").c_str());
        ImGui::PopTextWrapPos();
        float fill_angle = m_smart_fill_angle;
        if (float_row("##fill_angle", _L("Spread"), &fill_angle, SmartFillAngleMin, SmartFillAngleMax, "%.0f°", false))
            m_smart_fill_angle = fill_angle;
        hover_tip(_u8L("How far a click spreads from the spot you hit: it keeps going while the surface stays "
                       "flatter than this, and stops at a sharper fold. Small values keep it to one face."));

        ImGui::Separator();

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

        ImGui::Separator();

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
        m_preview_params_dirty |= float_row("##gamma", _L("Gamma"), &layer->gamma, 0.2f, 5.f, "%.2f", true);
        hover_tip(_u8L("Lifts or drops the midtones without touching the brightest and darkest parts. Use it "
                       "when the middle of the picture is the part coming out wrong."));
        if (m_imgui->bbl_checkbox(_L("Negative"), layer->color_invert))
            m_preview_params_dirty = true;
        hover_tip(_u8L("Prints the picture inverted, light for dark and each color for its opposite."));
        ImGui::SameLine();
        if (m_imgui->button(_u8L("Reset"))) {
            layer->brightness      = 0.f;
            layer->contrast        = 0.f;
            layer->saturation      = 1.f;
            layer->gamma           = 1.f;
            layer->color_invert    = false;
            m_preview_params_dirty = true;
        }
        hover_tip(_u8L("Puts every correction above back to where it started, leaving the picture as it was loaded."));
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

    ImGui::Separator();
    {
        const bool painted   = mv != nullptr && mv->is_texture_displacement_painted();
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
            m_imgui->tooltip(layer == nullptr ? _u8L("Load a picture first.") :
                             !painted         ? _u8L("Click the model to place the picture first.") :
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

    GizmoImguiEnd();
    ImGuiWrapper::pop_toolbar_style();
}

} // namespace Slic3r::GUI
