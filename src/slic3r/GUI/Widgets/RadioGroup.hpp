#ifndef slic3r_GUI_RADIOGROUP_hpp_
#define slic3r_GUI_RADIOGROUP_hpp_

#include "../wxExtensions.hpp"

#include <wx/wx.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>

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

private:
    std::vector<wxString>        m_labels;
    std::vector<wxStaticBitmap*> m_radioButtons;
    std::vector<wxStaticText*>   m_labelButtons;

    int  m_selectedIndex;
    bool m_focused;
    ScalableBitmap m_on;
    ScalableBitmap m_off;
    ScalableBitmap m_on_hover;
    ScalableBitmap m_off_hover;
    ScalableBitmap m_disabled;

    void OnClick(int i);

    void UpdateFocus(bool focus);

    void SetRadioIcon(int i, bool hover);

    void OnKeyDown(wxKeyEvent& e);
};

#endif // !slic3r_GUI_RADIOGROUP_hpp_
