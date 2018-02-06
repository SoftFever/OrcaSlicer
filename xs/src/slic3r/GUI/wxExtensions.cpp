#include "wxExtensions.hpp"

const unsigned int wxCheckListBoxComboPopup::Height = 210;

bool wxCheckListBoxComboPopup::Create(wxWindow* parent)
{
    return wxCheckListBox::Create(parent, wxID_HIGHEST + 1, wxPoint(0, 0));
}

wxWindow* wxCheckListBoxComboPopup::GetControl()
{
    return this;
}

void wxCheckListBoxComboPopup::SetStringValue(const wxString& value)
{
    m_text = value;
}

wxString wxCheckListBoxComboPopup::GetStringValue() const
{
    return m_text;
}

wxSize wxCheckListBoxComboPopup::GetAdjustedSize(int minWidth, int prefHeight, int maxHeight)
{
    // matches owner wxComboCtrl's width

    wxComboCtrl* cmb = GetComboCtrl();
    if (cmb != nullptr)
    {
        wxSize size = GetComboCtrl()->GetSize();
        size.SetHeight(Height);
        return size;
    }
    else
        return wxSize(200, Height);
}

void wxCheckListBoxComboPopup::OnCheckListBox(wxCommandEvent& evt)
{
    // forwards the checklistbox event to the owner wxComboCtrl

    wxComboCtrl* cmb = GetComboCtrl();
    if (cmb != nullptr)
    {
        wxCommandEvent event(wxEVT_CHECKLISTBOX, cmb->GetId());
        event.SetEventObject(cmb);
        cmb->ProcessWindowEvent(event);
    }
}

void wxCheckListBoxComboPopup::OnListBoxSelection(wxCommandEvent& evt)
{
    // transforms list box item selection event into checklistbox item toggle event 

    int selId = GetSelection();
    if (selId != wxNOT_FOUND)
    {
        Check((unsigned int)selId, !IsChecked((unsigned int)selId));
        SetSelection(wxNOT_FOUND);

        wxCommandEvent event(wxEVT_CHECKLISTBOX, GetId());
        event.SetInt(selId);
        event.SetEventObject(this);
        ProcessEvent(event);
    }
}
