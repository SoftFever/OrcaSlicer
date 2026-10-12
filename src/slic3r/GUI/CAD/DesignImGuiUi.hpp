#ifndef slic3r_DesignImGuiUi_hpp_
#define slic3r_DesignImGuiUi_hpp_

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "slic3r/GUI/CAD/DesignRibbonTable.hpp"

namespace Slic3r { namespace GUI {

class ImGuiWrapper;

// The Design tab's navigation drawn on the canvas with ImGui (feature tree here, ribbon in
// DesignRibbon). These classes own NO document state: they take a read-only view each frame and hand
// back events, which DesignPanel applies on the next turn of the event loop through the very same
// handlers the wx tree calls. That is what makes the two UIs behave identically.

// ---- feature tree -------------------------------------------------------------------------
struct TreeRowView {
    std::string label;
    bool        suppressed{false};   // disabled: grey + struck through
    bool        failed{false};       // replay threw here: red + `note` in a tooltip
    bool        conflict{false};     // mate conflict: orange-red + `note`
    std::string note;
};

struct TreeView {
    std::vector<TreeRowView> rows;
    int  selected{-1};
    int  rollback{-1};        // history is rolled back to this row; -1 = at the end
    bool show_origin{true};
    bool show_bed{true};
};

struct TreeEvent {
    enum Kind { Select, Deselect, Edit, ToggleEnabled, Delete, Zoom, MoveUp, MoveDown, Rename, SetRollback,
                ToggleOrigin, ToggleBed, DockBack } kind{Deselect};
    int         row{-1};
    std::string text;   // Rename: the new name
};

class DesignTreeOverlay
{
public:
    std::function<TreeView()>                       get_view;   // rebuilt by the owner every frame (cheap: names + flags)
    std::function<void(std::vector<TreeEvent>)>     emit;       // events from this frame, applied later by the owner
    std::function<bool(int row)>                    can_zoom;   // called only while a row menu is open

    // `inset_left/top` keep the window clear of the canvas's own corner widgets.
    void render(ImGuiWrapper& imgui, float scale, float inset_left, float inset_top);

    // Is the pointer over the tree window? (The canvas also asks ImGui, this is for the rig/tests.)
    bool hovered() const { return m_hovered; }

private:
    bool        m_hovered{false};
    int         m_rename_row{-1};
    char        m_rename_buf[128]{};
    bool        m_rename_focus{false};
    int         m_rename_age{0};
    int         m_menu_row{-1};
    int         m_drag_rollback{-1};   // where the bar is being dragged to (valid while m_dragging)
    bool        m_dragging{false};
    float       m_last_inset_top{-1.f};
    std::vector<float> m_row_mid;      // screen y of each row centre, from the previous frame (the bar drag reads it)
};

// ---- ribbon ------------------------------------------------------------------------------------
// Top-anchored tabs -> groups -> tools (DesignRibbonTable.hpp). One function draws it, so a TabCtrl
// renderer could replace it without touching the table or the panel. A click only EMITS the verb;
// the panel runs it through the offer's own action router, i.e. the state machine the wx toolbar
// and the right-click menu already share (one Confirm/Cancel bar, Esc cancels).
class DesignRibbonOverlay
{
public:
    struct VerbState { bool enabled{true}; std::string why; };
    std::function<VerbState(const OfferVerb&)>                    state;
    std::function<void(const OfferVerb&)>                         run;
    std::function<bool()>                                         sketching;   // live sketch mode: show the Sketch tab
    std::function<std::string(const std::string& group)>          load_last;   // last-used variant of a split group
    std::function<void(const std::string& group, const std::string& verb_id)> save_last;

    void render(ImGuiWrapper& imgui, float scale, float inset_left, float inset_top);
    float height() const { return m_height; }
    bool  hovered() const { return m_hovered; }

private:
    std::vector<RibbonTab> m_tabs;
    bool   m_hovered{false};
    float  m_height{0.f};
    bool   m_was_sketching{false};
};

// An ImGui popup (ribbon dropdown, tree row menu) is open anywhere? Esc closes it, before the
// panel's own Esc ladder, which knows nothing about ImGui.
bool imgui_popup_open();
void imgui_close_popups();

}} // namespace Slic3r::GUI

#endif // slic3r_DesignImGuiUi_hpp_
