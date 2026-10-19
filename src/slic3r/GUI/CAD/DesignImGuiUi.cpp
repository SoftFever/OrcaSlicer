#include "slic3r/GUI/CAD/DesignImGuiUi.hpp"
#include "libslic3r/CAD/PaletteScore.hpp"

#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"   // Label::Body_12, the Confirm/Cancel font
#include <wx/dcscreen.h>
#include "slic3r/GUI/3DScene.hpp"   // glsafe
#include <glad/gl.h>
#include <nanosvg/nanosvg.h>
#include <nanosvg/nanosvgrast.h>
#include <boost/nowide/fstream.hpp>
#include <cctype>
#include <iterator>
#include <map>
#include "libslic3r/libslic3r.h"   // SLIC3R_APP_KEY
#include "libslic3r/Utils.hpp"    // Slic3r::var
#include <wx/intl.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>   // PushItemFlag (ImGui 1.83 has no BeginDisabled)

#include <algorithm>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace Slic3r {
namespace GUI {

namespace {
// Colours for the controls the overlays draw. The stock ImGui button / tab colours came out white on
// white and orange here, so every overlay window sets its own, per the app's dark/light mode, with
// Orca's accent for the active tab and for pressed buttons.
struct ThemeScope {
    int n{0};
    ThemeScope() {
        const bool dark = GUI_App::dark_mode();
        auto c = [&](ImGuiCol i, float r, float g, float b, float a = 1.f) { ImGui::PushStyleColor(i, ImVec4(r, g, b, a)); ++n; };
        const ImVec4 acc = ImGuiWrapper::COL_ORCA;
        if (dark) {
            c(ImGuiCol_Text, 0.92f, 0.93f, 0.95f);
            c(ImGuiCol_WindowBg, 0.13f, 0.14f, 0.15f, 0.96f);
            c(ImGuiCol_PopupBg, 0.16f, 0.17f, 0.18f, 0.98f);
            c(ImGuiCol_Button, 0.25f, 0.26f, 0.28f);
            c(ImGuiCol_ButtonHovered, 0.33f, 0.35f, 0.38f);
            c(ImGuiCol_Header, 0.20f, 0.36f, 0.34f);
            c(ImGuiCol_HeaderHovered, 0.28f, 0.30f, 0.33f);
            c(ImGuiCol_Tab, 0.19f, 0.20f, 0.22f);
            c(ImGuiCol_TabHovered, 0.30f, 0.32f, 0.35f);
        } else {
            c(ImGuiCol_Text, 0.10f, 0.11f, 0.12f);
            c(ImGuiCol_WindowBg, 0.96f, 0.96f, 0.97f, 0.97f);
            c(ImGuiCol_PopupBg, 0.98f, 0.98f, 0.99f, 0.99f);
            c(ImGuiCol_Button, 0.86f, 0.87f, 0.89f);
            c(ImGuiCol_ButtonHovered, 0.78f, 0.80f, 0.83f);
            c(ImGuiCol_Header, 0.70f, 0.85f, 0.82f);
            c(ImGuiCol_HeaderHovered, 0.85f, 0.87f, 0.90f);
            c(ImGuiCol_Tab, 0.88f, 0.89f, 0.91f);
            c(ImGuiCol_TabHovered, 0.78f, 0.80f, 0.83f);
        }
        c(ImGuiCol_ButtonActive, acc.x, acc.y, acc.z);
        c(ImGuiCol_TabActive, acc.x * 0.8f, acc.y * 0.8f, acc.z * 0.8f);
        c(ImGuiCol_TabUnfocusedActive, acc.x * 0.6f, acc.y * 0.6f, acc.z * 0.6f);
    }
    ~ThemeScope() { ImGui::PopStyleColor(n); }
};
}

void DesignTreeOverlay::render(ImGuiWrapper& imgui, float scale, float inset_left, float inset_top)
{
    m_hovered = false;
    if (!get_view) return;
    const TreeView view = get_view();
    const float    em   = imgui.get_style_scaling();
    const ImVec2   ds   = ImGui::GetIO().DisplaySize;
    std::vector<TreeEvent> ev;
    auto push = [&ev](TreeEvent::Kind k, int row = -1, std::string text = {}) {
        TreeEvent e; e.kind = k; e.row = row; e.text = std::move(text); ev.push_back(std::move(e));
    };

    const ImVec4 col_dim    (0.55f, 0.55f, 0.58f, 1.f);
    const ImVec4 col_red    (0.93f, 0.40f, 0.40f, 1.f);
    const ImVec4 col_warn   (0.95f, 0.55f, 0.35f, 1.f);

    ImGuiWrapper::push_common_window_style(scale);
    // After the common style, which sets button colours of its own: the later push wins. Popped
    // before it, because the style stack is LIFO.
    auto theme = std::make_unique<ThemeScope>();
    // Placed under the ribbon, and re-placed when the ribbon's height changes; otherwise it is the
    // user's to drag.
    ImGui::SetNextWindowPos(ImVec2(inset_left + 8.f * em, inset_top + 8.f * em),
                            std::abs(inset_top - m_last_inset_top) > 0.5f ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    m_last_inset_top = inset_top;
    ImGui::SetNextWindowSizeConstraints(ImVec2(230.f * em, 40.f * em), ImVec2(380.f * em, std::max(ds.y * 0.7f, 120.f * em)));
    imgui.begin(_u8L("Feature tree") + "##design_tree",
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoScrollbar);
    m_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    if (std::getenv("ORCA_CAD_UITRACE")) std::fprintf(stderr, "[UI] tree window rows=%zu sel=%d rollback=%d\n", view.rows.size(), view.selected, view.rollback);

    // Header: reorder and dock back (the wx tree stays one click away).
    if (ImGui::SmallButton(_u8L("Up").c_str()))   push(TreeEvent::MoveUp, view.selected);
    ImGui::SameLine();
    if (ImGui::SmallButton(_u8L("Down").c_str())) push(TreeEvent::MoveDown, view.selected);
    ImGui::SameLine();
    if (ImGui::SmallButton(_u8L("Dock").c_str())) push(TreeEvent::DockBack);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", _u8L("Show the tree in the side panel again").c_str());
    ImGui::Separator();

    // Origin / Bed: view switches, not history. A click flips them.
    auto switch_row = [&](const char* id, const std::string& name, bool shown, TreeEvent::Kind k) {
        ImGui::PushID(id);
        if (!shown) ImGui::PushStyleColor(ImGuiCol_Text, col_dim);
        const std::string label = name + (shown ? "" : "  (" + _u8L("hidden") + ")");
        if (ImGui::Selectable(label.c_str(), false)) push(k);
        if (!shown) ImGui::PopStyleColor();
        ImGui::PopID();
    };
    switch_row("origin", _u8L("Origin"), view.show_origin, TreeEvent::ToggleOrigin);
    switch_row("bed",    _u8L("Bed"),    view.show_bed,    TreeEvent::ToggleBed);
    ImGui::Separator();

    const int n = int(view.rows.size());
    // While the bar is being dragged it only PREVIEWS the new position; the model is rebuilt once,
    // on release (rebuilding on every mouse move also disturbed the drag itself).
    const int eff_rollback = m_dragging ? m_drag_rollback : view.rollback;
    // Where the rollback bar sits: after the last replayed row, or under the whole list.
    const int bar_after = (eff_rollback >= 0 && eff_rollback < n - 1) ? eff_rollback : n - 1;
    std::vector<float> mids(n, 0.f);
    auto draw_bar = [&]() {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float  w = std::max(ImGui::GetContentRegionAvail().x, 20.f * em);
        const float  h = 7.f * em;
        ImGui::PushID("##rollback");
        ImGui::InvisibleButton("bar", ImVec2(w, h));
        const bool active = ImGui::IsItemActive(), hot = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 c = ImGui::GetColorU32(active || hot ? ImVec4(1.f, 0.65f, 0.15f, 1.f) : ImVec4(0.85f, 0.55f, 0.15f, 0.8f));
        dl->AddLine(ImVec2(p.x, p.y + h * 0.5f), ImVec2(p.x + w, p.y + h * 0.5f), c, 2.f);
        dl->AddRectFilled(ImVec2(p.x, p.y + h * 0.15f), ImVec2(p.x + 10.f * em, p.y + h * 0.85f), c, 2.f);
        if (hot && !active) ImGui::SetTooltip("%s", _u8L("Drag to roll the history back. Double-click to return to the end.").c_str());
        if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.f) && int(m_row_mid.size()) == n && n > 0) {
            const float my = ImGui::GetIO().MousePos.y;
            int after = -1;
            for (int i = 0; i < n; ++i) if (m_row_mid[i] < my) after = i;
            m_drag_rollback = (after >= n - 1) ? -1 : after;
            m_dragging = true;
        }
        if (!active && m_dragging) {   // released: apply once
            m_dragging = false;
            if (m_drag_rollback != view.rollback) push(TreeEvent::SetRollback, m_drag_rollback);
        }
        if (hot && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) push(TreeEvent::SetRollback, -1);
        ImGui::PopID();
    };

    for (int i = 0; i < n; ++i) {
        const TreeRowView& r = view.rows[i];
        const bool rolled = eff_rollback >= 0 && i > eff_rollback;   // not replayed right now
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_drag_src = -1;
        ImGui::PushID(i);
        const bool selected = (i == view.selected) || std::find(view.selected_set.begin(), view.selected_set.end(), i) != view.selected_set.end();
        const ImVec4* tint = r.failed ? &col_red : r.conflict ? &col_warn : (r.suppressed || rolled) ? &col_dim : nullptr;

        if (m_rename_row == i) {
            if (m_rename_focus) { ImGui::SetKeyboardFocusHere(); m_rename_focus = false; m_rename_age = 0; }
            const bool entered = ImGui::InputText("##rename", m_rename_buf, sizeof(m_rename_buf),
                                                  ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (entered) {
                std::string name(m_rename_buf);
                while (!name.empty() && name.back() == ' ') name.pop_back();
                if (!name.empty()) push(TreeEvent::Rename, i, name);
                m_rename_row = -1;
            } else if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)) ||
                       ImGui::IsItemDeactivated() ||
                       (++m_rename_age > 3 && !ImGui::IsItemActive())) {   // never left editing without focus
                m_rename_row = -1;
            }
        } else {
            if (tint) ImGui::PushStyleColor(ImGuiCol_Text, *tint);
            if (ImGui::Selectable(r.label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                const ImGuiIO& io = ImGui::GetIO();   // Ctrl toggles a row in the selection, Shift extends it from the primary one
                push(TreeEvent::Select, i, (io.KeyCtrl || (test_mods & 2)) ? "ctrl" : (io.KeyShift || (test_mods & 1)) ? "shift" : "");
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) push(TreeEvent::Edit, i);
            }
            if (tint) ImGui::PopStyleColor();
            // Drag a row onto another to move it there (the panel stops at the first move the dependencies refuse).
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                m_drag_src = i;   // not read back from the payload: ImGui keeps the FIRST payload of a drag and a lingering one would name a stale row
                ImGui::SetDragDropPayload("DESIGN_ROW", &i, sizeof(int));
                ImGui::TextUnformatted(r.label.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("DESIGN_ROW")) {
                    (void) pl;
                    const int src = m_drag_src;
                    if (src >= 0 && src != i) push(TreeEvent::Reorder, src, std::to_string(i));
                }
                ImGui::EndDragDropTarget();
            }
            if (r.suppressed) {   // ImGui 1.83 has no strikethrough: draw the line over the text
                const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
                const float  tw = ImGui::CalcTextSize(r.label.c_str()).x;
                ImGui::GetWindowDrawList()->AddLine(ImVec2(mn.x, (mn.y + mx.y) * 0.5f),
                                                    ImVec2(mn.x + tw, (mn.y + mx.y) * 0.5f),
                                                    ImGui::GetColorU32(col_dim), 1.f);
            }
            if (ImGui::IsItemHovered() && !r.note.empty()) ImGui::SetTooltip("%s", r.note.c_str());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected) push(TreeEvent::Select, i);
            mids[i] = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f;
            m_row_x = ImGui::GetItemRectMin().x + 12.f;

            if (ImGui::BeginPopupContextItem("rowmenu")) {
                if (m_menu_row != i) { m_menu_row = i; }
                if (ImGui::MenuItem(_u8L("Rename").c_str())) {
                    m_rename_row = i; m_rename_focus = true;
                    std::snprintf(m_rename_buf, sizeof(m_rename_buf), "%s", r.label.c_str());
                }
                if (ImGui::MenuItem(_u8L("Edit").c_str())) push(TreeEvent::Edit, i);
                if (ImGui::MenuItem(_u8L("Copy").c_str())) push(TreeEvent::Copy, i);
                if (!view.can_paste) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
                if (ImGui::MenuItem(_u8L("Paste").c_str())) push(TreeEvent::Paste, i);
                if (!view.can_paste) { ImGui::PopStyleVar(); ImGui::PopItemFlag(); }
                if (ImGui::MenuItem(r.suppressed ? _u8L("Unsuppress").c_str() : _u8L("Suppress").c_str()))
                    push(TreeEvent::ToggleEnabled, i);
                ImGui::Separator();
                if (ImGui::MenuItem(_u8L("Move up").c_str()))   push(TreeEvent::MoveUp, i);
                if (ImGui::MenuItem(_u8L("Move down").c_str())) push(TreeEvent::MoveDown, i);
                const bool zoomable = can_zoom ? can_zoom(i) : false;
                if (!zoomable) {   // ImGui 1.83: no BeginDisabled
                    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
                }
                if (ImGui::MenuItem(_u8L("Zoom to selection").c_str())) push(TreeEvent::Zoom, i);
                if (!zoomable) {
                    ImGui::PopStyleVar();
                    ImGui::PopItemFlag();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", _u8L("This feature has nothing to frame").c_str());
                }
                ImGui::Separator();
                if (ImGui::MenuItem(_u8L("Delete").c_str())) push(TreeEvent::Delete, i);
                ImGui::EndPopup();
            }
        }
        ImGui::PopID();
        if (i == bar_after) draw_bar();
    }
    if (n == 0) { ImGui::PushStyleColor(ImGuiCol_Text, col_dim); ImGui::TextUnformatted(_u8L("No features yet").c_str()); ImGui::PopStyleColor(); }

    // Empty-area click drops the selection, as in the wx list.
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
        push(TreeEvent::Deselect);

    imgui.end();
    theme.reset();
    ImGuiWrapper::pop_common_window_style();
    m_row_mid = std::move(mids);

    if (!ev.empty() && emit) emit(std::move(ev));
}

// ---- ribbon ------------------------------------------------------------------------------------
namespace {
std::string tr_group(const std::string& s) { return std::string(wxGetTranslation(wxString::FromUTF8(s), SLIC3R_APP_KEY).ToUTF8()); }
std::string tr_verb(const OfferVerb& v)     { return std::string(I18N::translate(v.name, "Design").ToUTF8()); }

// ---- ribbon icons -------------------------------------------------------------------------------
// FreeCAD's LGPL icons (resources/images/fc_<verb id>.svg, imported by scripts/CAD/import_freecad_icons.py),
// else the verb's own design_* glyph, else none (text-only button). Rasterised once per theme at 64 px
// and drawn down-scaled. FreeCAD draws on a light ground: near-neutral dark strokes (and, on a light
// UI, near-white ones) are re-tinted at load so an outline never disappears into the ribbon.
static bool hex_rgb(const std::string& s, size_t i, int& r, int& g, int& b)
{
    if (i + 7 > s.size() || s[i] != '#') return false;
    for (size_t k = 1; k <= 6; ++k) if (!std::isxdigit(static_cast<unsigned char>(s[i + k]))) return false;
    if (i + 7 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 7]))) return false;   // longer token
    r = std::stoi(s.substr(i + 1, 2), nullptr, 16); g = std::stoi(s.substr(i + 3, 2), nullptr, 16); b = std::stoi(s.substr(i + 5, 2), nullptr, 16);
    return true;
}
static void retint_neutrals(std::string& s, bool dark)
{
    for (size_t i = 0; i < s.size(); ++i) {
        int r, g, b;
        if (s[i] != '#' || !hex_rgb(s, i, r, g, b)) continue;
        const int mx = std::max({r, g, b}), mn = std::min({r, g, b});
        const bool  neutral = (mx - mn) <= 40;                      // grey-ish, not a hue
        const float lum     = (0.299f * r + 0.587f * g + 0.114f * b) / 255.f;
        const char* to      = nullptr;
        if (dark && neutral && lum < 0.30f)       to = "#d3d7cf";   // Tango aluminium 2
        else if (!dark && neutral && lum > 0.93f) to = "#555753";   // Tango aluminium 5
        if (to) s.replace(i, 7, to);
    }
}
static ImTextureID ribbon_icon_texture(const OfferVerb& v)
{
    static std::map<std::string, ImTextureID> cache;
    static bool cache_dark = false;
    const bool dark = GUI_App::dark_mode();
    if (dark != cache_dark) {   // theme flipped: the tint is baked into the raster
        for (auto& kv : cache) if (kv.second) { GLuint t = (GLuint)(intptr_t)kv.second; glsafe(::glDeleteTextures(1, &t)); }
        cache.clear(); cache_dark = dark;
    }
    const std::string key = v.id;
    if (auto it = cache.find(key); it != cache.end()) return it->second;

    ImTextureID tex = nullptr;
    std::vector<std::string> names = { std::string("fc_") + v.id };
    if (v.icon) names.push_back(v.icon);
    for (const std::string& n : names) {
        const std::string path = Slic3r::var(n + ".svg");
        boost::nowide::ifstream f(path, std::ios::binary);
        if (!f) continue;
        std::string svg((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (n.rfind("fc_", 0) == 0) retint_neutrals(svg, dark);
        else if (dark) { size_t p = 0; while ((p = svg.find("\"#808080\"", p)) != std::string::npos) svg.replace(p, 9, "\"#d0d0d0\""); }
        NSVGimage* img = nsvgParse(svg.data(), "px", 96.f);   // parses in place: svg is our own copy
        if (!img) continue;
        constexpr int N = 64;
        const float sc = float(N) / std::max(img->width, img->height);
        std::vector<unsigned char> px(size_t(N) * N * 4, 0);
        if (NSVGrasterizer* rast = nsvgCreateRasterizer()) {
            const float ox = (N - img->width * sc) * 0.5f, oy = (N - img->height * sc) * 0.5f;   // centre non-square art
            nsvgRasterize(rast, img, ox, oy, sc, px.data(), N, N, N * 4);
            nsvgDeleteRasterizer(rast);
            GLint  last = 0;
            GLuint id   = 0;
            glsafe(::glGetIntegerv(GL_TEXTURE_BINDING_2D, &last));
            glsafe(::glGenTextures(1, &id));
            glsafe(::glBindTexture(GL_TEXTURE_2D, id));
            glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
            glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
            glsafe(::glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
            glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data()));
            glsafe(::glBindTexture(GL_TEXTURE_2D, last));
            tex = (ImTextureID)(intptr_t)id;
        }
        nsvgDelete(img);
        if (tex) break;
    }
    cache[key] = tex;   // nullptr cached too: no icon for this verb, don't hit the disk every frame
    return tex;
}
// The ribbon's text is sized to the Confirm / Cancel buttons beside it (Label::Body_12), not to the
// canvas overlay's default ImGui font, which is a good deal larger. Applied per window.
static float ribbon_font_scale()
{
    wxScreenDC dc;
    const float px = Label::Body_12.GetFractionalPointSize() * float(dc.GetPPI().y) / 72.f;
    // The font's own size: GetFontSize() already includes the window's previous-frame font scale, so
    // dividing by it made the scale flip between two values every frame (small <-> big).
    const float base = ImGui::GetFont()->FontSize * ImGui::GetIO().FontGlobalScale;
    const float sc   = px / base;
    return std::min(1.f, std::max(0.5f, sc));
}
// Label clipped to `maxw` with a trailing ".." so a long verb name cannot widen its button column.
static std::string fit_label(const std::string& s, float maxw)
{
    if (ImGui::CalcTextSize(s.c_str()).x <= maxw) return s;
    std::string t = s;
    while (t.size() > 2 && ImGui::CalcTextSize((t + "..").c_str()).x > maxw) t.pop_back();
    return t + "..";
}

}

void DesignRibbonOverlay::render(ImGuiWrapper& imgui, float scale, float inset_left, float inset_top)
{
    m_hovered = false;
    if (m_tabs.empty()) m_tabs = build_ribbon_table();
    const float  em = imgui.get_style_scaling();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const bool   sk = sketching ? sketching() : false;

    ImGuiWrapper::push_common_window_style(scale);
    auto theme = std::make_unique<ThemeScope>();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,  ImVec2(4.f * em, 2.f * em));   // compact
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.f * em, 3.f * em));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f * em, 4.f * em));
    ImGui::SetNextWindowPos(ImVec2(inset_left, inset_top), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(std::max(ds.x - inset_left, 100.f), 0.f), ImGuiCond_Always);
    imgui.begin(std::string("##design_ribbon"),
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);   // a narrow canvas scrolls instead of clipping
    ImGui::SetWindowFontScale(ribbon_font_scale());
    m_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    if (std::getenv("ORCA_CAD_UITRACE")) {
        const ImVec2 wp = ImGui::GetWindowPos(), wsz = ImGui::GetWindowSize();
        std::fprintf(stderr, "[UI] ribbon window pos=%.0f,%.0f size=%.0f,%.0f tabs=%zu display=%.0fx%.0f\n", wp.x, wp.y, wsz.x, wsz.y, m_tabs.size(), ds.x, ds.y);
    }

    const bool switch_to_sketch = sk && !m_was_sketching;   // entering a session shows the Sketch tab once
    m_was_sketching = sk;
    if (ImGui::BeginTabBar("##ribbon_tabs", ImGuiTabBarFlags_None)) {
        for (size_t ti = 0; ti < m_tabs.size(); ++ti) {
            const RibbonTab& t = m_tabs[ti];
            if (t.groups.empty()) continue;
            ImGuiTabItemFlags tf = (switch_to_sketch && ti == 2) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (!ImGui::BeginTabItem((tr_group(t.name) + "##tab").c_str(), nullptr, tf)) continue;
            for (size_t gi = 0; gi < t.groups.size(); ++gi) {
                const RibbonGroup& g = t.groups[gi];
                if (gi > 0) { ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine(); }
                ImGui::BeginGroup();
                ImGui::PushID(int(ti * 100 + gi));
                // The variant the main button runs: the last one used, else the first.
                const OfferVerb* main = g.verbs.front();
                if (g.verbs.size() > 1 && load_last) {
                    const std::string last = load_last(g.name);
                    for (const OfferVerb* v : g.verbs) if (last == v->id) { main = v; break; }
                }
                auto draw_verb_button = [&](const OfferVerb& v, bool as_menu_item) {
                    const VerbState st = state ? state(v) : VerbState{};
                    const bool usable = st.enabled && v.action != nullptr;
                    if (!usable) {   // ImGui 1.83: no BeginDisabled
                        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
                    }
                    const std::string label = tr_verb(v);
                    const ImTextureID tex   = ribbon_icon_texture(v);
                    bool clicked;
                    if (as_menu_item) {
                        if (tex) { ImGui::Image(tex, ImVec2(16.f * em, 16.f * em)); ImGui::SameLine(); }
                        clicked = ImGui::MenuItem(label.c_str());
                    } else if (tex) {   // icon over a short label, the way FreeCAD / Onshape lay a toolbar out
                        const float isz = 32.f * em;
                        ImGui::BeginGroup();
                        ImGui::PushID(v.id);
                        clicked = ImGui::ImageButton(tex, ImVec2(isz, isz));
                        ImGui::PopID();
                        const float bw  = isz + 2.f * ImGui::GetStyle().FramePadding.x;
                        const std::string txt = fit_label(label, bw + 34.f * em);
                        const float tw  = ImGui::CalcTextSize(txt.c_str()).x;
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, (bw - tw) * 0.5f));
                        ImGui::TextUnformatted(txt.c_str());
                        ImGui::EndGroup();
                    } else {
                        clicked = ImGui::Button(label.c_str());
                    }
                    if (!usable) { ImGui::PopStyleVar(); ImGui::PopItemFlag(); }
                    if (!usable && ImGui::IsItemHovered()) {
                        const std::string why = !st.why.empty() ? st.why : (v.refusal ? tr_group(v.refusal) : std::string());
                        if (!why.empty()) ImGui::SetTooltip("%s", why.c_str());
                    } else if (usable && ImGui::IsItemHovered()) {
                        if (v.hint) ImGui::SetTooltip("%s\n%s", label.c_str(), tr_group(v.hint).c_str());
                        else        ImGui::SetTooltip("%s", label.c_str());
                    }
                    return clicked && usable;
                };
                if (draw_verb_button(*main, false)) {
                    if (save_last) save_last(g.name, main->id);
                    if (run) run(*main);
                }
                if (g.verbs.size() > 1) {
                    ImGui::SameLine(0.f, 1.f * em);
                    // Half the size the stock arrow button had before the ribbon font was reduced.
                    const float asz = 0.5f * (ImGui::GetFont()->FontSize * ImGui::GetIO().FontGlobalScale + 2.f * ImGui::GetStyle().FramePadding.y);
                    if (ribbon_icon_texture(*main))
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.f, (32.f * em - asz) * 0.5f));
                    if (ImGui::ArrowButtonEx("##dd", ImGuiDir_Down, ImVec2(asz, asz))) ImGui::OpenPopup("##variants");
                    if (ImGui::BeginPopup("##variants")) {   // Esc and a click away close it; nothing is armed
                        ImGui::SetWindowFontScale(ribbon_font_scale());
                        for (const OfferVerb* v : g.verbs)
                            if (draw_verb_button(*v, true)) {
                                if (save_last) save_last(g.name, v->id);
                                if (run) run(*v);
                            }
                        ImGui::EndPopup();
                    }
                }
                ImGui::TextDisabled("%s", tr_group(g.name).c_str());
                ImGui::PopID();
                ImGui::EndGroup();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    m_height = ImGui::GetWindowHeight();
    imgui.end();
    ImGui::PopStyleVar(3);
    theme.reset();
    ImGuiWrapper::pop_common_window_style();
}

// ---- offer popup -------------------------------------------------------------------------------------
void DesignOfferPopup::open(std::string header, std::vector<OfferNode> nodes, float x, float y)
{
    m_header = std::move(header);
    m_nodes  = std::move(nodes);
    m_x = x; m_y = y;
    m_open = true;
    m_opened_now = true;
}

void DesignOfferPopup::render(ImGuiWrapper& imgui, float scale)
{
    if (!m_open) return;
    ImGuiWrapper::push_common_window_style(scale);
    auto theme = std::make_unique<ThemeScope>();
    ImGui::SetNextWindowPos(ImVec2(m_x, m_y), ImGuiCond_Always);
    if (m_opened_now) { ImGui::OpenPopup("##design_offer"); m_opened_now = false; }

    std::function<void(const std::vector<OfferNode>&)> draw = [&](const std::vector<OfferNode>& nodes) {
        for (const OfferNode& n : nodes) {
            if (n.separator_before) ImGui::Separator();
            if (!n.children.empty()) {
                if (ImGui::BeginMenu(n.label.c_str(), n.enabled)) { draw(n.children); ImGui::EndMenu(); }
                continue;
            }
            const bool usable = n.enabled && n.verb != nullptr;
            if (!usable) {   // ImGui 1.83: no BeginDisabled
                ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
            }
            const bool clicked = ImGui::MenuItem(n.label.c_str(), n.shortcut.empty() ? nullptr : n.shortcut.c_str());
            if (!usable) { ImGui::PopStyleVar(); ImGui::PopItemFlag(); }
            // The reason only on hover of a DISABLED item: a tooltip on every row is noise.
            if (!usable && !n.why.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", n.why.c_str());
            if (clicked && usable && run) { const OfferVerb* v = n.verb; ImGui::CloseCurrentPopup(); run(*v); }
        }
    };

    if (ImGui::BeginPopup("##design_offer", ImGuiWindowFlags_NoSavedSettings)) {
        // One disabled header says WHAT the rows act on ("Nothing selected" included); the rows
        // below it do not repeat the reason.
        ImGui::TextDisabled("%s", m_header.empty() ? "" : m_header.c_str());
        ImGui::Separator();
        draw(m_nodes);
        ImGui::EndPopup();
    } else {
        m_open = false;
    }
    theme.reset();
    ImGuiWrapper::pop_common_window_style();
}

// ---- command palette ----------------------------------------------------------------------------------
void DesignPalette::open(float x, float y)
{
    m_items = items ? items() : std::vector<PaletteItem>();
    m_x = x; m_y = y;
    m_buf[0] = '\0';
    m_query.clear();
    m_cursor = 0;
    m_move = 0;
    m_activate = false;
    m_open = true;
    m_opened_now = true;
}

void DesignPalette::render(ImGuiWrapper& imgui, float scale)
{
    if (!m_open) return;
    ImGuiWrapper::push_common_window_style(scale);
    auto theme = std::make_unique<ThemeScope>();
    const float em = imgui.get_style_scaling();
    ImGui::SetNextWindowPos(ImVec2(m_x, m_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(320.f * em, 0.f));
    const bool first = m_opened_now;
    if (first) { ImGui::OpenPopup("##design_palette"); m_opened_now = false; }
    if (ImGui::BeginPopup("##design_palette", ImGuiWindowFlags_NoSavedSettings)) {
        // Focus is requested ONCE, on the frame the box opens. Asking every frame makes the field re-grab
        // the keyboard after each key repeat and the caret never settles.
        if (first) ImGui::SetKeyboardFocusHere();
        const bool edited = ImGui::InputTextWithHint("##q", "Search commands", m_buf, sizeof(m_buf));
        if (edited || first) { m_query = m_buf; m_cursor = 0; }

        // Rank: recents (query empty) or score (query set).
        std::vector<std::pair<int, size_t>> ranked;
        std::vector<std::string> recents = (m_query.empty() && load_recents) ? load_recents() : std::vector<std::string>();
        for (size_t i = 0; i < m_items.size(); ++i) {
            const int s = palette_score(m_query, m_items[i].label, m_items[i].enabled);
            if (s < 0) continue;
            int boost = 0;
            for (size_t r = 0; r < recents.size(); ++r) if (recents[r] == m_items[i].id) { boost = 5000 - int(r); break; }
            ranked.emplace_back(s + boost, i);
        }
        std::stable_sort(ranked.begin(), ranked.end(), [](const std::pair<int, size_t>& a, const std::pair<int, size_t>& b) { return a.first > b.first; });
        if (ranked.size() > 12) ranked.resize(12);
        m_shown.clear();
        for (const auto& r : ranked) m_shown.push_back(m_items[r.second].id);

        const int n = int(ranked.size());
        m_cursor = n ? std::clamp(m_cursor + m_move, 0, n - 1) : 0;   // clamp, no wrap
        m_move = 0;
        const bool activate = std::exchange(m_activate, false);

        const PaletteItem* chosen = nullptr;
        ImGui::Separator();
        for (int i = 0; i < n; ++i) {
            const PaletteItem& it = m_items[ranked[i].second];
            const bool usable = it.enabled && it.verb != nullptr;
            if (!usable) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
            ImGui::PushID(i);
            if (ImGui::Selectable(it.label.c_str(), i == m_cursor)) { m_cursor = i; chosen = &it; }
            ImGui::PopID();
            if (!it.shortcut.empty()) {
                ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(it.shortcut.c_str()).x - 12.f * em);
                ImGui::TextDisabled("%s", it.shortcut.c_str());
            }
            if (!usable) ImGui::PopStyleVar();
            if (!usable && !it.why.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", it.why.c_str());
        }
        if (n == 0) ImGui::TextDisabled("No command matches");
        if (!chosen && n && activate) chosen = &m_items[ranked[m_cursor].second];

        if (chosen && chosen->enabled && chosen->verb) {   // a disabled command stays listed and does nothing: the tooltip said why
            std::vector<std::string> rec = load_recents ? load_recents() : std::vector<std::string>();
            rec.erase(std::remove(rec.begin(), rec.end(), chosen->id), rec.end());
            rec.insert(rec.begin(), chosen->id);
            if (rec.size() > 12) rec.resize(12);
            if (save_recents) save_recents(rec);
            const OfferVerb* v = chosen->verb;
            ImGui::CloseCurrentPopup();
            m_open = false;
            if (run) run(*v);
        }
        ImGui::EndPopup();
    } else {
        m_open = false;
    }
    theme.reset();
    ImGuiWrapper::pop_common_window_style();
}

void DesignCandidateMenu::open(float x, float y, std::vector<std::string> labels)
{
    m_x = x; m_y = y; m_labels = std::move(labels);
    m_hover = -1; m_force_hover = -2; m_force_choose = -1;
    m_open = !m_labels.empty(); m_opened_now = m_open;
}

void DesignCandidateMenu::render(ImGuiWrapper& imgui, float scale)
{
    if (!m_open) return;
    ImGuiWrapper::push_common_window_style(scale);
    auto theme = std::make_unique<ThemeScope>();
    const float em = imgui.get_style_scaling();
    ImGui::SetNextWindowPos(ImVec2(m_x, m_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(200.f * em, 0.f));
    if (m_opened_now) { ImGui::OpenPopup("##design_other"); m_opened_now = false; }
    int hovered = -1, chosen = std::exchange(m_force_choose, -1);
    if (ImGui::BeginPopup("##design_other", ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextDisabled("Select other");
        ImGui::Separator();
        for (int i = 0; i < int(m_labels.size()); ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(m_labels[i].c_str(), false)) chosen = i;
            if (ImGui::IsItemHovered()) hovered = i;
            ImGui::PopID();
        }
        if (hovered < 0 && m_force_hover != -2) hovered = m_force_hover;   // a rig-driven hover holds until the menu closes
        if (chosen >= 0) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    } else {
        m_open = false;   // a click elsewhere or Esc closed it
        chosen = -1; hovered = -1;
        m_force_hover = -2;
    }
    if (hovered != m_hover) { m_hover = hovered; if (on_hover) on_hover(hovered); }
    if (chosen >= 0 && chosen < int(m_labels.size())) {
        m_open = false;
        if (on_hover) on_hover(-1);
        if (on_choose) on_choose(chosen);
    }
    theme.reset();
    ImGuiWrapper::pop_common_window_style();
}

bool imgui_wants_text()
{
    return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantTextInput;
}

bool imgui_popup_open()
{
    return ImGui::GetCurrentContext() != nullptr && ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

void imgui_close_popups()
{
    if (ImGui::GetCurrentContext() != nullptr) ImGui::ClosePopupToLevel(0, true);
}

}} // namespace Slic3r::GUI
