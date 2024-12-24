#ifndef FILAMENT_GROUP_HOVER_HPP
#define FILAMENT_GROUP_HOVER_HPP

#include "Widgets/PopupWindow.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/Label.hpp"

namespace Slic3r { namespace GUI {

bool is_pop_up_required();
FilamentMapMode get_prefered_map_mode();

class FilamentGroupPopup : public PopupWindow
{
public:
    FilamentGroupPopup(wxWindow *parent);
    void tryPopup(bool connect_status);
    void tryClose();

    FilamentMapMode GetSelectedMode() const { return m_mode; }

private:
    void OnPaint(wxPaintEvent&event);
    void StartTimer();
    void ResetTimer();

    void OnRadioBtn(int idx);
    void OnLeaveWindow(wxMouseEvent &);
    void OnEnterWindow(wxMouseEvent &);
    void OnTimer(wxTimerEvent &event);
    void OnRemindBtn(wxCommandEvent &event);

    void Init();
    void UpdateButtonStatus(int hover_idx = -1);
    void DrawRoundedCorner(int radius);

private:
    enum ButtonType { btForFlush, btForMatch, btManual, btCount };

    const std::vector<FilamentMapMode> mode_list = {fmmAutoForFlush, fmmAutoForMatch, fmmManual};

    FilamentMapMode m_mode;
    bool m_connected{ false };
    wxTimer        *m_timer;

    std::vector<wxBitmapButton *> radio_btns;
    std::vector<Label *>   button_labels;
    std::vector<Label *>   button_desps;
    std::vector<Label *>   detail_infos;

    wxStaticText *wiki_link;
    CheckBox* remind_checkbox;
};
}} // namespace Slic3r::GUI
#endif