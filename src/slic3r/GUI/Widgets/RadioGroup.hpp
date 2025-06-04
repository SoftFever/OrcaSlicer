#ifndef slic3r_GUI_RADIOGROUP_hpp_
#define slic3r_GUI_RADIOGROUP_hpp_

#include "../wxExtensions.hpp"

#include <wx/tglbtn.h>
#include <wx/wx.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>

#include <vector>
#include <string>

class RadioGroup : public wxPanel
{
    std::vector<wxString>              m_labels;
    std::vector<wxBitmapToggleButton*> m_radioButtons;
    std::vector<wxStaticText*>         m_labelButtons;
    
    int m_selectedIndex;
    ScalableBitmap m_on;
    ScalableBitmap m_off;
    ScalableBitmap m_on_hover;
    ScalableBitmap m_off_hover;
    ScalableBitmap m_disabled;

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
    
private:
    void OnToggleClick(wxCommandEvent& event);

    void OnLabelClick(wxStaticText* sel);

    void DrawFocus(int item);

    void KillFocus();

    void OnKeyDown(wxKeyEvent& event);

    wxDECLARE_EVENT_TABLE();
};

#endif // !slic3r_GUI_RADIOGROUP_hpp_
