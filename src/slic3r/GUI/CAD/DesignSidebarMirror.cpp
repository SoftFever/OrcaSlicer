// The Design tab's left panel drawn with ImGui.
//
// The panel's wx form stays alive, hidden, as the MODEL: every card, list and button it builds keeps its handlers, its
// enable rules and its values. This file walks that form each frame and draws what is shown in it with ImGui; a click or an
// edit changes the wx control and queues the very event the wx control would have sent, so the handlers (preview, validation,
// undo) run exactly as they do from the wx panel, on the next turn of the event loop and not inside the ImGui frame.
//
// Controls it knows: labels, rules, icons, number spins, combos, check boxes, buttons, text fields, the row lists of the
// feature tree and the bodies, and the variables table. Anything else is skipped (and named once in the log).

#include "slic3r/GUI/CAD/DesignPanel.hpp"
#include "slic3r/GUI/CAD/DesignCanvas.hpp"
#include "slic3r/GUI/CAD/DesignImGuiUi.hpp"
#include "slic3r/GUI/CAD/DesignRowList.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/CheckBox.hpp"
#include "slic3r/GUI/Widgets/ComboBox.hpp"

#include <boost/log/trivial.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <wx/accel.h>
#include <wx/button.h>
#include <wx/colour.h>
#include <wx/event.h>
#include <wx/font.h>
#include <wx/gdicmn.h>
#include <wx/listbase.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbmp.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>
#include <wx/window.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <cstdlib>
#include <cmath>
#include <set>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

namespace Slic3r { namespace GUI {

struct SidebarCtx {
    float em{1.f};
    int   depth{0};
    // a rename in progress in one of the row lists (the wx editor would open invisibly in the hidden pane)
    DesignRowList* rename_list{nullptr};
    int            rename_row{-1};
    std::string*   rename_text{nullptr};
    bool*          rename_focus{nullptr};
    std::function<void(bool commit)> rename_done;
    // A queued action may leave the keyboard on a hidden wx control (a list that took the focus when it selected a row); the
    // canvas must have it back, or the next key never reaches the ImGui field.
    std::function<void()> refocus;
};

namespace {

std::string u8(const wxString& s) { return s.utf8_string(); }

ImVec4 col_of(const wxColour& c) { return ImVec4(c.Red() / 255.f, c.Green() / 255.f, c.Blue() / 255.f, 1.f); }

// A colour worth honouring (a status green or red), as opposed to the panel's plain text colour.
bool is_accent(const wxColour& c)
{
    const int mx = std::max({int(c.Red()), int(c.Green()), int(c.Blue())}), mn = std::min({int(c.Red()), int(c.Green()), int(c.Blue())});
    return c.IsOk() && mx - mn > 60;
}

void tip_of(wxWindow* w)
{
    if (!ImGui::IsItemHovered()) return;
    const wxString t = w->GetToolTipText();
    if (!t.IsEmpty()) ImGui::SetTooltip("%s", u8(t).c_str());
}

// Queue (never process) an event: the handler runs on the next turn of the loop, outside the ImGui frame.
void post(wxWindow* w, wxEvent& e)
{
    e.SetEventObject(w);
    wxQueueEvent(w->GetEventHandler(), e.Clone());
}

void post_command(wxWindow* w, wxEventType type, int value = 0, const wxString& s = wxString())
{
    wxCommandEvent e(type, w->GetId());
    e.SetInt(value);
    e.SetString(s);
    post(w, e);
}

// A number as the spin shows it: its own digits, trailing zeros kept.
std::string fmt_number(double v, int digits)
{
    char b[48];
    std::snprintf(b, sizeof(b), "%.*f", std::clamp(digits, 0, 6), v);
    return b;
}

// What the field being typed in holds, and which control owns it.
const void* g_edit_owner = nullptr;
std::string g_edit_text;

// An ImGui text field over a value the user types: shows the control's value until it is clicked into, keeps the typed text
// while it is active, and hands it back on Enter or when focus leaves after an edit.
bool text_field(const void* owner, const char* id, const std::string& shown, std::string& out, float width, ImGuiInputTextFlags flags)
{
    char buf[256];
    const std::string& src = (g_edit_owner == owner) ? g_edit_text : shown;
    std::snprintf(buf, sizeof(buf), "%s", src.c_str());
    if (width > 0.f) ImGui::SetNextItemWidth(width);
    const bool enter = ImGui::InputText(id, buf, sizeof(buf), flags | ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    if (ImGui::IsItemActivated()) g_edit_owner = owner;
    if (ImGui::IsItemActive()) wxGetApp().imgui()->set_requires_extra_frame();   // keep frames coming while a field is being typed in
    if (g_edit_owner == owner) g_edit_text = buf;
    bool commit = enter || ImGui::IsItemDeactivatedAfterEdit();
    if (commit || ImGui::IsItemDeactivated()) { if (g_edit_owner == owner) g_edit_owner = nullptr; }
    if (commit) out = buf;
    return commit;
}

float text_w(const std::string& s) { return ImGui::CalcTextSize(s.c_str()).x; }

// How wide an item will be drawn, for a row that right-aligns what follows a stretch spacer.
float estimate_width(wxSizerItem* it, float em)
{
    if (it == nullptr || !it->IsShown()) return 0.f;
    if (it->IsSpacer()) return it->GetSpacer().x > 0 ? float(it->GetSpacer().x) : 0.f;
    if (it->IsSizer()) {
        float w = 0.f;
        wxSizer* s = it->GetSizer();
        for (wxSizerItem* c : s->GetChildren()) w += estimate_width(c, em) + 4.f * em;
        return w;
    }
    wxWindow* win = it->GetWindow();
    if (win == nullptr || !win->IsShown()) return 0.f;
    if (dynamic_cast<CheckBox*>(win)) return 24.f * em;
    if (dynamic_cast<ScalableButton*>(win)) return 28.f * em;
    if (auto* b = dynamic_cast<::Button*>(win)) return text_w(u8(b->GetLabel())) + 18.f * em;
    if (auto* st = dynamic_cast<wxStaticText*>(win)) return text_w(u8(st->GetLabel()));
    return 110.f * em;
}

} // namespace

// ---- one control ---------------------------------------------------------------------------------------------------

namespace {

void draw_label(wxStaticText* st, DesignPanel& panel, SidebarCtx& cx, const std::vector<std::pair<const wxWindow*, std::string>>& folds)
{
    (void) panel; (void) cx; (void) folds;
    const std::string t = u8(st->GetLabel());
    if (t.empty()) return;
    const wxColour c = st->GetForegroundColour();
    const bool bold = st->GetFont().GetWeight() >= wxFONTWEIGHT_SEMIBOLD;
    if (is_accent(c)) ImGui::PushStyleColor(ImGuiCol_Text, col_of(c));
    else if (bold)    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
    ImGui::TextWrapped("%s", t.c_str());
    if (is_accent(c) || bold) ImGui::PopStyleColor();
    tip_of(st);
}

void draw_spin(wxSpinCtrlDouble* sp, float em, float width)
{
    ImGui::PushID(sp);
    const bool en = sp->IsEnabled();
    if (!en) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
    const double lo = sp->GetMin(), hi = sp->GetMax(), inc = sp->GetIncrement() > 0.0 ? sp->GetIncrement() : 1.0;
    const int    digits = sp->GetDigits();
    const double v = sp->GetValue();
    const float  bw = ImGui::GetFrameHeight();
    const float  total = width > 0.f ? width : ImGui::GetContentRegionAvail().x;
    auto apply = [&](double nv) {
        nv = std::clamp(nv, lo, hi);
        if (std::abs(nv - v) < 1e-12) return;
        sp->SetValue(nv);
        wxSpinDoubleEvent e(wxEVT_SPINCTRLDOUBLE, sp->GetId());
        e.SetValue(nv);
        post(sp, e);
    };
    std::string typed;
    if (text_field(sp, "##v", fmt_number(v, digits), typed, std::max(total - 2.f * bw - 2.f * ImGui::GetStyle().ItemSpacing.x, 40.f * em), ImGuiInputTextFlags_CharsDecimal)) {
        char* end = nullptr;
        const double nv = std::strtod(typed.c_str(), &end);
        if (end != typed.c_str()) apply(nv);
    }
    ImGui::SameLine();
    if (ImGui::Button("-", ImVec2(bw, 0.f))) apply(v - inc);
    ImGui::SameLine(0.f, 2.f);
    if (ImGui::Button("+", ImVec2(bw, 0.f))) apply(v + inc);
    if (!en) { ImGui::PopStyleVar(); ImGui::PopItemFlag(); }
    ImGui::PopID();
    tip_of(sp);
}

void draw_combo(ComboBox* cb, float width)
{
    ImGui::PushID(cb);
    const bool en = cb->IsEnabled();
    if (!en) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
    const int n = int(cb->GetCount()), sel = cb->GetSelection();
    const std::string preview = (sel >= 0 && sel < n) ? u8(cb->GetString(unsigned(sel))) : std::string();
    ImGui::SetNextItemWidth(width > 0.f ? width : -1.f);
    if (ImGui::BeginCombo("##c", preview.c_str())) {
        for (int i = 0; i < n; ++i) {
            const std::string s = u8(cb->GetString(unsigned(i)));
            ImGui::PushID(i);
            if (ImGui::Selectable(s.c_str(), i == sel) && i != sel) {
                cb->SetSelection(i);
                post_command(cb, wxEVT_COMBOBOX, i, cb->GetString(unsigned(i)));
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (!en) { ImGui::PopStyleVar(); ImGui::PopItemFlag(); }
    ImGui::PopID();
    tip_of(cb);
}

void draw_check(CheckBox* cb)
{
    ImGui::PushID(cb);
    const bool en = cb->IsEnabled();
    if (!en) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
    bool v = cb->GetValue();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);   // an unchecked box has to be seen on the dark panel
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.55f, 0.56f, 0.60f, 1.f));
    const bool toggled = ImGui::Checkbox("##k", &v);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (toggled) {
        cb->SetValue(v);
        post_command(cb, wxEVT_TOGGLEBUTTON, v ? 1 : 0);
    }
    if (!en) { ImGui::PopStyleVar(); ImGui::PopItemFlag(); }
    ImGui::PopID();
    tip_of(cb);
}

void draw_text_ctrl(wxTextCtrl* tc, float width)
{
    ImGui::PushID(tc);
    const bool en = tc->IsEnabled() && tc->IsEditable();
    if (!en) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
    std::string typed;
    if (text_field(tc, "##t", u8(tc->GetValue()), typed, width > 0.f ? width : -1.f, ImGuiInputTextFlags_None)) {
        tc->ChangeValue(wxString::FromUTF8(typed));
        post_command(tc, wxEVT_TEXT, 0, wxString::FromUTF8(typed));
    }
    if (!en) { ImGui::PopStyleVar(); ImGui::PopItemFlag(); }
    ImGui::PopID();
}

// ---- the card tables (DesignCardModel.hpp): the fields are drawn and set directly, with no wx control behind them ----------

void field_tip(const CardField* f)
{
    if (ImGui::IsItemHovered() && !f->tip().IsEmpty()) ImGui::SetTooltip("%s", u8(f->tip()).c_str());
}

struct DisabledScope {   // a field that cannot be edited is dimmed and takes no input
    bool on;
    explicit DisabledScope(bool enabled) : on(!enabled)
    {
        if (on) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
    }
    ~DisabledScope() { if (on) { ImGui::PopStyleVar(); ImGui::PopItemFlag(); } }
};

void draw_num_field(NumField* sp, float em, float width)
{
    ImGui::PushID(sp);
    DisabledScope dis(sp->IsEnabled());
    const double lo = sp->GetMin(), hi = sp->GetMax(), inc = sp->GetIncrement() > 0.0 ? sp->GetIncrement() : 1.0;
    const double v = sp->GetValue();
    const float  bw = ImGui::GetFrameHeight();
    const float  total = width > 0.f ? width : ImGui::GetContentRegionAvail().x;
    std::string typed;
    if (text_field(sp, "##v", fmt_number(v, sp->GetDigits()), typed, std::max(total - 2.f * bw - 2.f * ImGui::GetStyle().ItemSpacing.x, 40.f * em), ImGuiInputTextFlags_CharsDecimal)) {
        char* end = nullptr;
        const double nv = std::strtod(typed.c_str(), &end);
        if (end != typed.c_str()) sp->user_set(nv);
    }
    ImGui::SameLine();
    if (ImGui::Button("-", ImVec2(bw, 0.f))) sp->user_set(std::clamp(v - inc, lo, hi));
    ImGui::SameLine(0.f, 2.f);
    if (ImGui::Button("+", ImVec2(bw, 0.f))) sp->user_set(std::clamp(v + inc, lo, hi));
    ImGui::PopID();
    field_tip(sp);
}

void draw_choice_field(ChoiceField* cb, float width)
{
    ImGui::PushID(cb);
    DisabledScope dis(cb->IsEnabled());
    const int n = int(cb->GetCount()), sel = cb->GetSelection();
    const std::string preview = (sel >= 0 && sel < n) ? u8(cb->GetString(unsigned(sel))) : std::string();
    ImGui::SetNextItemWidth(width > 0.f ? width : -1.f);
    if (ImGui::BeginCombo("##c", preview.c_str())) {
        for (int i = 0; i < n; ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(u8(cb->GetString(unsigned(i))).c_str(), i == sel))
                cb->user_select(i);
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    field_tip(cb);
}

void draw_toggle_field(ToggleField* cb)
{
    ImGui::PushID(cb);
    DisabledScope dis(cb->IsEnabled());
    bool v = cb->GetValue();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);   // an unchecked box has to be seen on the dark panel
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.55f, 0.56f, 0.60f, 1.f));
    if (ImGui::Checkbox("##k", &v)) cb->user_toggle();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopID();
    field_tip(cb);
}

bool draw_button(wxWindow* w, float em)
{
    ImGui::PushID(w);
    const bool en = w->IsEnabled();
    if (!en) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
    bool clicked = false;
    if (auto* sb = dynamic_cast<ScalableButton*>(w)) {
        const ImTextureID tex = (ImTextureID) design_icon_texture(sb->GetBmpName());
        const float isz = 18.f * em;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
        clicked = tex ? ImGui::ImageButton(tex, ImVec2(isz, isz)) : ImGui::Button(u8(sb->GetLabel()).empty() ? "?" : u8(sb->GetLabel()).c_str());
        ImGui::PopStyleColor();
    } else {
        const std::string label = u8(w->GetLabel());
        const auto* ob = dynamic_cast<::Button*>(w);
        const ImTextureID tex = (ob != nullptr && label.empty() && !ob->GetIconName().empty()) ? (ImTextureID) design_icon_texture(ob->GetIconName()) : nullptr;
        if (tex) {   // an icon-only button of the panel's headers
            const float isz = 18.f * em;
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
            clicked = ImGui::ImageButton(tex, ImVec2(isz, isz));
            ImGui::PopStyleColor();
        } else {
            clicked = ImGui::Button((label.empty() ? std::string("?") : label).c_str());
        }
    }
    if (!en) { ImGui::PopStyleVar(); ImGui::PopItemFlag(); }
    ImGui::PopID();
    tip_of(w);
    if (clicked && en) post_command(w, wxEVT_BUTTON);
    return clicked && en;
}

// The rows of a DesignRowList (the feature tree, the Origin / Bed rows, the bodies): the owner's own callbacks do the work.
void draw_rows(DesignRowList* list, float em, SidebarCtx& cx)
{
    ImGui::PushID(list);
    const auto& rows = list->rows();
    const int sel = list->selection();
    const bool selectable = list->selectable();
    const float row_h = std::max(20.f * em, ImGui::GetFrameHeight());
    const float isz   = 16.f * em;
    for (int i = 0; i < int(rows.size()); ++i) {
        const DesignRowList::Row& r = rows[size_t(i)];
        ImGui::PushID(i);
        const float avail = ImGui::GetContentRegionAvail().x;
        const float cell  = isz + 6.f * em;
        const float acts_w = cell * float(r.actions.size());
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const bool selected = selectable && i == sel;
        ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowItemOverlap, ImVec2(std::max(avail - acts_w, 20.f * em), row_h));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            const bool dbl = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            list->CallAfter([list, i, dbl, selectable, rf = cx.refocus] {
                if (selectable) list->select(i);
                if (dbl && list->on_activate) list->on_activate();
                if (rf) rf();
            });
        }
        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && list->on_menu) {
            const wxPoint pos = wxGetMousePosition();
            list->CallAfter([list, i, pos, selectable, rf = cx.refocus] {
                if (selectable) list->select(i);
                list->on_menu(i, pos);
                if (rf) rf();
            });
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (cx.rename_list == list && cx.rename_row == i && cx.rename_text != nullptr) {   // the field replaces the label
            ImGui::SetCursorScreenPos(ImVec2(p0.x + 4.f * em + (r.icon.empty() ? 0.f : isz + 6.f * em), p0.y + 1.f));
            if (cx.rename_focus != nullptr && *cx.rename_focus) { ImGui::SetKeyboardFocusHere(); *cx.rename_focus = false; }
            wxGetApp().imgui()->set_requires_extra_frame();   // the canvas draws on demand: the field is only active on the NEXT frame, and keys typed before it is are not its own
            char buf[200];
            std::snprintf(buf, sizeof(buf), "%s", cx.rename_text->c_str());
            ImGui::SetNextItemWidth(std::max(avail - acts_w - isz - 14.f * em, 60.f * em));
            const bool entered = ImGui::InputText("##rename", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            *cx.rename_text = buf;
            if (entered) { if (cx.rename_done) cx.rename_done(true); }
            else if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)) || ImGui::IsItemDeactivated()) { if (cx.rename_done) cx.rename_done(false); }
            ImGui::PopID();
            ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + row_h + ImGui::GetStyle().ItemSpacing.y));
            continue;
        }
        float x = p0.x + 4.f * em;
        if (!r.icon.empty()) {
            if (const ImTextureID tex = (ImTextureID) design_icon_texture(r.icon))
                dl->AddImage(tex, ImVec2(x, p0.y + (row_h - isz) * 0.5f), ImVec2(x + isz, p0.y + (row_h + isz) * 0.5f));
            x += isz + 6.f * em;
        }
        ImVec4 c = r.colour.IsOk() ? col_of(r.colour) : ImGui::GetStyle().Colors[ImGuiCol_Text];
        dl->AddText(ImVec2(x, p0.y + (row_h - ImGui::GetFontSize()) * 0.5f), ImGui::GetColorU32(c), u8(r.label).c_str());
        // the action cells, at the right end of the row
        if (!r.actions.empty()) {
            ImGui::SameLine(0.f, 0.f);
            ImGui::SetCursorScreenPos(ImVec2(p0.x + avail - acts_w, p0.y));
        }
        for (size_t a = 0; a < r.actions.size(); ++a) {
            const DesignRowList::Action& act = r.actions[a];
            ImGui::PushID(int(a));
            if (a > 0) ImGui::SameLine(0.f, 0.f);
            const ImTextureID tex = act.icon.empty() ? nullptr : (ImTextureID) design_icon_texture(act.icon);
            if (tex) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.f * em, (row_h - isz) * 0.5f));
                if (ImGui::ImageButton(tex, ImVec2(isz, isz)) && list->on_action) {
                    const int id = act.id;
                    list->CallAfter([list, i, id, rf = cx.refocus] { if (list->on_action) list->on_action(i, id); if (rf) rf(); });
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered() && !act.tip.IsEmpty()) ImGui::SetTooltip("%s", u8(act.tip).c_str());
            } else {
                ImGui::Dummy(ImVec2(cell, row_h));
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    if (rows.empty()) ImGui::Dummy(ImVec2(1.f, 4.f * em));
    ImGui::PopID();
}

// The variables table (name, expression); a double click edits through the panel's own dialog.
void draw_variables(wxListCtrl* lc, std::function<void()> edit, float em, std::function<void()> refocus)
{
    (void) em;
    ImGui::PushID(lc);
    const int n = lc->GetItemCount();
    const long sel = lc->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (ImGui::BeginTable("##vars", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(_u8L("Name").c_str(), ImGuiTableColumnFlags_WidthStretch, 0.4f);
        ImGui::TableSetupColumn(_u8L("Expression").c_str(), ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < n; ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i);
            const bool dbl_before = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            if (ImGui::Selectable(u8(lc->GetItemText(i, 0)).c_str(), i == sel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                lc->CallAfter([lc, i, dbl_before, edit, refocus] {
                    lc->SetItemState(i, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
                    if (dbl_before && edit) edit();
                    if (refocus) refocus();
                });
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(u8(lc->GetItemText(i, 1)).c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
}

} // namespace

// ---- walking the form ----------------------------------------------------------------------------------------------

namespace {
std::set<std::string>& warned() { static std::set<std::string> s; return s; }
}

void DesignPanel::mirror_window(wxWindow* w, SidebarCtx& cx, float width)
{
    if (w == nullptr || !w->IsShown()) return;
    const float em = cx.em;
    ImGui::PushID(w);
    if (auto* rl = dynamic_cast<DesignRowList*>(w))          draw_rows(rl, em, cx);
    else if (auto* lc = dynamic_cast<wxListCtrl*>(w))        draw_variables(lc, [this] { on_edit_variable(); }, em, cx.refocus);
    else if (auto* sp = dynamic_cast<wxSpinCtrlDouble*>(w))  draw_spin(sp, em, width);
    else if (auto* cb = dynamic_cast<ComboBox*>(w))          draw_combo(cb, width);
    else if (auto* ck = dynamic_cast<CheckBox*>(w))          draw_check(ck);
    else if (dynamic_cast<ScalableButton*>(w) || dynamic_cast<::Button*>(w) || dynamic_cast<wxButton*>(w)) draw_button(w, em);
    else if (auto* tc = dynamic_cast<wxTextCtrl*>(w))        draw_text_ctrl(tc, width);
    else if (auto* st = dynamic_cast<wxStaticText*>(w)) {
        bool chevron = false;
        for (FoldSection& f : m_folds)
            if (f.chevron == st) {   // the fold arrow of a card header
                chevron = true;
                const std::string key = f.key;
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
                if (ImGui::ArrowButtonEx("##fold", f.collapsed ? ImGuiDir_Right : ImGuiDir_Down, ImVec2(ImGui::GetFrameHeight() * 0.8f, ImGui::GetFrameHeight() * 0.8f)))
                    CallAfter([this, key] { toggle_fold(key); });
                ImGui::PopStyleColor();
                break;
            }
        if (!chevron) draw_label(st, *this, cx, {});
    }
    else if (auto* bm = dynamic_cast<wxStaticBitmap*>(w)) {
        const auto it = m_ctl_icon.find(bm);
        if (it != m_ctl_icon.end())
            if (const ImTextureID tex = (ImTextureID) design_icon_texture(it->second)) ImGui::Image(tex, ImVec2(18.f * em, 18.f * em));
    }
    else if (dynamic_cast<wxStaticLine*>(w)) ImGui::Separator();
    else if (wxSizer* s = w->GetSizer()) {   // a card or a panel: its own layout
        if (cx.depth < 24) { ++cx.depth; mirror_sizer(s, cx); --cx.depth; }
    }
    else if (warned().insert(typeid(*w).name()).second)
        BOOST_LOG_TRIVIAL(info) << "Design sidebar: no ImGui view for " << typeid(*w).name();
    ImGui::PopID();
}

// One card of the tables: its header, then a label and its field per row (the same 42 % / 58 % split the wx forms had).
void DesignPanel::mirror_card(CardModel& card, SidebarCtx& cx)
{
    const float em = cx.em;
    if (const ImTextureID tex = (ImTextureID) design_icon_texture(card.icon())) {
        ImGui::Image(tex, ImVec2(18.f * em, 18.f * em));
        ImGui::SameLine();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
    ImGui::TextUnformatted(u8(card.title()->GetLabel()).c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();
    const float lw = ImGui::GetContentRegionAvail().x * 0.42f;
    for (const CardRow& row : card.rows()) {
        CardField* f = row.field;
        if (!f->IsShown()) continue;
        ImGui::PushID(f);
        if (row.kind == CardRow::Kind::Note) {
            ImGui::TextWrapped("%s", u8(static_cast<CardText*>(f)->GetLabel()).c_str());
            ImGui::PopID();
            continue;
        }
        ImGui::AlignTextToFramePadding();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + lw - 6.f * em);
        ImGui::TextWrapped("%s", u8(row.label).c_str());
        ImGui::PopTextWrapPos();
        ImGui::SameLine(lw + ImGui::GetStyle().WindowPadding.x);
        const float w = std::max(ImGui::GetContentRegionAvail().x, 60.f * em);
        switch (f->kind()) {
        case CardField::Kind::Num:    draw_num_field(static_cast<NumField*>(f), em, w);       break;
        case CardField::Kind::Choice: draw_choice_field(static_cast<ChoiceField*>(f), w);     break;
        case CardField::Kind::Toggle: draw_toggle_field(static_cast<ToggleField*>(f));        break;
        case CardField::Kind::Text:   ImGui::AlignTextToFramePadding(); ImGui::TextWrapped("%s", u8(static_cast<CardText*>(f)->GetLabel()).c_str()); break;
        }
        ImGui::PopID();
        ImGui::Spacing();
    }
}

void DesignPanel::mirror_item(wxSizerItem* it, SidebarCtx& cx, float width)
{
    if (it == nullptr || !it->IsShown()) return;
    if (it->IsSizer()) {
        const auto cm = m_card_models.find(it->GetSizer());
        if (cm != m_card_models.end()) mirror_card(*cm->second, cx);
        else                           mirror_sizer(it->GetSizer(), cx);
    }
    else if (it->IsWindow())  { mirror_window(it->GetWindow(), cx, width); }
    else if (it->IsSpacer())  { const wxSize sz = it->GetSpacer(); if (sz.y > 0) ImGui::Dummy(ImVec2(1.f, std::min(float(sz.y), 12.f))); }
}

void DesignPanel::mirror_sizer(wxSizer* s, SidebarCtx& cx)
{
    if (s == nullptr) return;
    const float em = cx.em;
    std::vector<wxSizerItem*> items;
    for (wxSizerItem* c : s->GetChildren()) items.push_back(c);
    if (auto* grid = dynamic_cast<wxGridSizer*>(s)) {   // the two-column forms: a label, then its control
        const int cols = std::max(1, grid->GetEffectiveColsCount());
        const float avail = ImGui::GetContentRegionAvail().x;
        const float lw = avail * 0.42f;
        int k = 0;
        for (wxSizerItem* it : items) {
            if (it == nullptr) continue;
            const bool shown = it->IsShown();
            if (cols == 2) {
                if (k % 2 == 0) {   // label cell
                    if (shown) {
                        ImGui::AlignTextToFramePadding();
                        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + lw - 6.f * em);
                        mirror_item(it, cx, 0.f);
                        ImGui::PopTextWrapPos();
                    }
                } else if (shown) {
                    ImGui::SameLine(lw + ImGui::GetStyle().WindowPadding.x);
                    mirror_item(it, cx, std::max(ImGui::GetContentRegionAvail().x, 60.f * em));
                }
            } else if (shown) {
                mirror_item(it, cx, 0.f);
            }
            ++k;
        }
        return;
    }
    const auto* box = dynamic_cast<wxBoxSizer*>(s);
    if (box != nullptr && box->GetOrientation() == wxHORIZONTAL) {   // a row: items side by side, what follows a stretch spacer at the right end
        bool first = true, placed = false;   // placed: the next item already has its x (after a stretch), so no SameLine
        for (size_t i = 0; i < items.size(); ++i) {
            wxSizerItem* it = items[i];
            if (it == nullptr || !it->IsShown()) continue;
            if (it->IsSpacer()) {
                if (it->GetProportion() > 0) {   // stretch: align the rest to the right
                    float rest = 0.f;
                    for (size_t j = i + 1; j < items.size(); ++j) rest += estimate_width(items[j], em) + 4.f * em;
                    const float x = ImGui::GetWindowContentRegionMax().x - rest;
                    if (!first && x > ImGui::GetCursorPosX()) { ImGui::SameLine(x); placed = true; }
                    else if (first) { ImGui::SetCursorPosX(std::max(x, ImGui::GetCursorPosX())); first = false; placed = true; }
                } else if (it->GetSpacer().x > 0 && !first) {
                    ImGui::SameLine(0.f, float(it->GetSpacer().x));
                    placed = true;
                }
                continue;
            }
            if (!first && !placed) ImGui::SameLine();
            first = false;
            placed = false;
            mirror_item(it, cx, 0.f);
        }
        return;
    }
    for (wxSizerItem* it : items) mirror_item(it, cx, 0.f);   // a column
}

void DesignPanel::draw_sidebar_imgui()
{
    if (m_form == nullptr || m_form->GetSizer() == nullptr) return;
    SidebarCtx cx;
    cx.em = wxGetApp().imgui()->get_style_scaling();
    cx.refocus      = [this] { if (m_viewport != nullptr) m_viewport->SetFocus(); };
    cx.rename_list  = m_sb_rename_list;
    cx.rename_row   = m_sb_rename_row;
    cx.rename_text  = &m_sb_rename_text;
    cx.rename_focus = &m_sb_rename_focus;
    cx.rename_done  = [this](bool commit) {
        DesignRowList* l = m_sb_rename_list;
        const int row = m_sb_rename_row;
        std::string name = m_sb_rename_text;
        m_sb_rename_list = nullptr; m_sb_rename_row = -1;
        while (!name.empty() && name.back() == ' ') name.pop_back();
        if (commit && l != nullptr && !name.empty())   // an empty commit cancels, as the wx editor does
            CallAfter([l, row, name] { if (l->on_rename) l->on_rename(row, wxString::FromUTF8(name)); });
    };
    // collapse the panel (the canvas shows an arrow at its edge to bring it back)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
    if (ImGui::ArrowButtonEx("##collapse", ImGuiDir_Left, ImVec2(ImGui::GetFrameHeight() * 0.8f, ImGui::GetFrameHeight() * 0.8f)))
        CallAfter([this] { collapse_sidebar(true); });
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", _u8L("Collapse sidebar").c_str());
    // the root of the form: its cards one under the other
    for (wxSizerItem* it : m_form->GetSizer()->GetChildren()) {
        if (it == nullptr || !it->IsShown()) continue;
        mirror_item(it, cx, 0.f);
        ImGui::Spacing();
    }
}

}} // namespace Slic3r::GUI
