#include "slic3r/GUI/CAD/DesignImGuiUi.hpp"

#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "libslic3r/libslic3r.h"   // SLIC3R_APP_KEY
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
        ImGui::PushID(i);
        const bool selected = (i == view.selected);
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
                push(TreeEvent::Select, i);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) push(TreeEvent::Edit, i);
            }
            if (tint) ImGui::PopStyleColor();
            if (r.suppressed) {   // ImGui 1.83 has no strikethrough: draw the line over the text
                const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
                const float  tw = ImGui::CalcTextSize(r.label.c_str()).x;
                ImGui::GetWindowDrawList()->AddLine(ImVec2(mn.x, (mn.y + mx.y) * 0.5f),
                                                    ImVec2(mn.x + tw, (mn.y + mx.y) * 0.5f),
                                                    ImGui::GetColorU32(col_dim), 1.f);
            }
            if (ImGui::IsItemHovered() && !r.note.empty()) ImGui::SetTooltip("%s", r.note.c_str());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) push(TreeEvent::Select, i);
            mids[i] = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f;

            if (ImGui::BeginPopupContextItem("rowmenu")) {
                if (m_menu_row != i) { m_menu_row = i; }
                if (ImGui::MenuItem(_u8L("Rename").c_str())) {
                    m_rename_row = i; m_rename_focus = true;
                    std::snprintf(m_rename_buf, sizeof(m_rename_buf), "%s", r.label.c_str());
                }
                if (ImGui::MenuItem(_u8L("Edit").c_str())) push(TreeEvent::Edit, i);
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
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
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
                    const bool clicked = as_menu_item ? ImGui::MenuItem(label.c_str()) : ImGui::Button(label.c_str());
                    if (!usable) { ImGui::PopStyleVar(); ImGui::PopItemFlag(); }
                    if (!usable && ImGui::IsItemHovered()) {
                        const std::string why = !st.why.empty() ? st.why : (v.refusal ? tr_group(v.refusal) : std::string());
                        if (!why.empty()) ImGui::SetTooltip("%s", why.c_str());
                    } else if (usable && ImGui::IsItemHovered() && v.hint) {
                        ImGui::SetTooltip("%s", tr_group(v.hint).c_str());
                    }
                    return clicked && usable;
                };
                if (draw_verb_button(*main, false)) {
                    if (save_last) save_last(g.name, main->id);
                    if (run) run(*main);
                }
                if (g.verbs.size() > 1) {
                    ImGui::SameLine(0.f, 1.f * em);
                    if (ImGui::ArrowButton("##dd", ImGuiDir_Down)) ImGui::OpenPopup("##variants");
                    if (ImGui::BeginPopup("##variants")) {   // Esc and a click away close it; nothing is armed
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

bool imgui_popup_open()
{
    return ImGui::GetCurrentContext() != nullptr && ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

void imgui_close_popups()
{
    if (ImGui::GetCurrentContext() != nullptr) ImGui::ClosePopupToLevel(0, true);
}

}} // namespace Slic3r::GUI
