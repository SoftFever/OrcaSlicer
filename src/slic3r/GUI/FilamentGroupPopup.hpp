#ifndef FILAMENT_GROUP_HOVER_HPP
#define FILAMENT_GROUP_HOVER_HPP

#include <wx/graphics.h>
#include <wx/hyperlink.h>
#include "wxExtensions.hpp"
#include "Widgets/PopupWindow.hpp"

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
    void StartTimer();
    void ResetTimer();

    void OnRadioBtn(wxCommandEvent &event);
    void OnLeaveWindow(wxMouseEvent &);
    void OnEnterWindow(wxMouseEvent &);
    void OnTimer(wxTimerEvent &event);
    void OnRemindBtn(wxCommandEvent &event);

    void Init(bool connect_status);
    void UpdateButtonStatus(int hover_idx = -1);
    void DrawRoundedCorner(int radius);

private:
    enum ButtonType { btForFlush, btForMatch, btManual, btCount };

    const std::vector<FilamentMapMode> mode_list = {fmmAutoForFlush, fmmAutoForMatch, fmmManual};

    FilamentMapMode m_mode;
    wxTimer        *m_timer;

    std::vector<wxBitmapButton *> radio_btns;
    std::vector<wxStaticText *>   button_labels;
    std::vector<wxStaticText *>   button_desps;
    std::vector<wxStaticText *>   detail_infos;

    wxStaticText *wiki_link;
    wxCheckBox   *remind_checkbox;
};
}} // namespace Slic3r::GUI
#endif