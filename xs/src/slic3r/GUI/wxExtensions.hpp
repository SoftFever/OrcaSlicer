#ifndef slic3r_GUI_wxExtensions_hpp_
#define slic3r_GUI_wxExtensions_hpp_

#include <wx/checklst.h>
#include <wx/combo.h>

class wxCheckListBoxComboPopup : public wxCheckListBox, public wxComboPopup
{
    static const unsigned int Height;

    wxString m_text;

public:
    virtual bool Create(wxWindow* parent);
    virtual wxWindow* GetControl();
    virtual void SetStringValue(const wxString& value);
    virtual wxString GetStringValue() const;
    virtual wxSize GetAdjustedSize(int minWidth, int prefHeight, int maxHeight);

    void OnCheckListBox(wxCommandEvent& evt);
    void OnListBoxSelection(wxCommandEvent& evt);
};

#endif // slic3r_GUI_wxExtensions_hpp_
