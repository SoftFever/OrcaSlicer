#ifndef slic3r_GUI_RADIOGROUP_hpp_
#define slic3r_GUI_RADIOGROUP_hpp_

#include "../wxExtensions.hpp"

#include <wx/wx.h>
#include <wx/bannerwindow.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#include <wx/generic/statbmpg.h>

#include <vector>
#include <string>

class RadioGroup : public wxPanel
{

public:
    RadioGroup();

    RadioGroup(
        wxWindow* parent,
        const std::vector<wxString>& labels = {"1", "2", "3"},
        long direction = wxHORIZONTAL,
        int row_col_limit = -1
    );

    void Create(
        wxWindow* parent,
        const std::vector<wxString>& labels = {"1", "2", "3"},
        long direction = wxHORIZONTAL,
        int row_col_limit = -1
    );

    int  GetSelection();

    void SetSelection(int index);

    void SelectNext(bool focus = true);

    void SelectPrevious(bool focus = true);

    bool Enable(bool enable = true) override;

    bool IsEnabled();

    bool Disable();

    void SetRadioTooltip(int i, wxString tooltip);

private:
    std::vector<wxString>               m_labels;
    std::vector<wxGenericStaticBitmap*> m_radioButtons;
    std::vector<wxBannerWindow*>        m_labelButtons;

    int  m_selectedIndex;
    bool m_focused;
    bool m_enabled;
    int  m_item_count;
    ScalableBitmap m_on;
    ScalableBitmap m_off;
    ScalableBitmap m_on_hover;
    ScalableBitmap m_off_hover;
    ScalableBitmap m_disabled;
    wxColour       m_text_color;
    wxColour       m_focus_color;
    wxFont         m_font;

    void OnClick(int i);

    void SetRadioIcon(int i, bool hover);

};

#endif // !slic3r_GUI_RADIOGROUP_hpp_
