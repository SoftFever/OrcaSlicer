#include "Notebook.hpp"

#ifdef _WIN32

#include "GUI_App.hpp"
#include "wxExtensions.hpp"

#include <wx/button.h>
#include <wx/sizer.h>

wxDEFINE_EVENT(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED, wxCommandEvent);

ButtonsListCtrl::ButtonsListCtrl(wxWindow *parent, bool add_mode_buttons/* = false*/) :
    wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    m_sizer = new wxBoxSizer(wxHORIZONTAL);
    this->SetSizer(m_sizer);

    if (add_mode_buttons) {
        m_mode_sizer = new ModeSizer(this, int(0.5 * em_unit(this)));
        m_sizer->AddStretchSpacer(20);
        m_sizer->Add(m_mode_sizer, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    }

    this->Bind(wxEVT_PAINT, &ButtonsListCtrl::OnPaint, this);
}

void ButtonsListCtrl::OnPaint(wxPaintEvent&)
{
    Slic3r::GUI::wxGetApp().UpdateDarkUI(this);
    const wxSize sz = GetSize();
    wxPaintDC dc(this);

    if (m_selection < 0 || m_selection >= (int)m_pageButtons.size())
        return;

    // highlight selected button

    const wxColour& selected_btn_bg  = Slic3r::GUI::wxGetApp().get_color_selected_btn_bg();
    const wxColour& default_btn_bg   = Slic3r::GUI::wxGetApp().get_highlight_default_clr();
    const wxColour& btn_marker_color = Slic3r::GUI::wxGetApp().get_color_hovered_btn_label();
    for (int idx = 0; idx < m_pageButtons.size(); idx++) {
        wxButton* btn = m_pageButtons[idx];

        btn->SetBackgroundColour(idx == m_selection ? selected_btn_bg : default_btn_bg);

        wxPoint pos = btn->GetPosition();
        wxSize size = btn->GetSize();
        const wxColour& clr = idx == m_selection ? btn_marker_color : default_btn_bg;
        dc.SetPen(clr);
        dc.SetBrush(clr);
        dc.DrawRectangle(pos.x, sz.y - 3, size.x, 3);
    }

    dc.SetPen(btn_marker_color);
    dc.SetBrush(btn_marker_color);
    dc.DrawRectangle(1, sz.y - 1, sz.x, 1);
}

void ButtonsListCtrl::UpdateMode()
{
    m_mode_sizer->SetMode(Slic3r::GUI::wxGetApp().get_mode());
}

void ButtonsListCtrl::Rescale()
{
    m_mode_sizer->msw_rescale();
    for (ScalableButton* btn : m_pageButtons)
        btn->msw_rescale();
}

void ButtonsListCtrl::SetSelection(int sel)
{
    if (m_selection == sel)
        return;
    m_selection = sel;
    Refresh();
}

bool ButtonsListCtrl::InsertPage(size_t n, const wxString& text, bool bSelect/* = false*/, const std::string& bmp_name/* = ""*/)
{
    ScalableButton* btn = new ScalableButton(this, wxID_ANY, bmp_name, text, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER | (bmp_name.empty() ? 0 : wxBU_LEFT));
    btn->Bind(wxEVT_BUTTON, [this, btn](wxCommandEvent& event) {
        if (auto it = std::find(m_pageButtons.begin(), m_pageButtons.end(), btn); it != m_pageButtons.end()) {
            m_selection = it - m_pageButtons.begin();
            wxCommandEvent evt = wxCommandEvent(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED);
            evt.SetId(m_selection);
            wxPostEvent(this->GetParent(), evt);
            Refresh();
        }
    });
    Slic3r::GUI::wxGetApp().UpdateDarkUI(btn);
    m_pageButtons.insert(m_pageButtons.begin() + n, btn);
    m_sizer->Insert(n, new wxSizerItem(btn, 0, wxEXPAND | wxRIGHT | wxBOTTOM, 3));
    m_sizer->Layout();
    return true;
}

void ButtonsListCtrl::RemovePage(size_t n)
{
    ScalableButton* btn = m_pageButtons[n];
    m_pageButtons.erase(m_pageButtons.begin() + n);
    m_sizer->Remove(n);
    btn->Reparent(nullptr);
    btn->Destroy();
    m_sizer->Layout();
}

bool ButtonsListCtrl::SetPageImage(size_t n, const std::string& bmp_name) const
{
    if (n >= m_pageButtons.size())
        return false;
     return m_pageButtons[n]->SetBitmap_(bmp_name);
}

void ButtonsListCtrl::SetPageText(size_t n, const wxString& strText)
{
    ScalableButton* btn = m_pageButtons[n];
    btn->SetLabel(strText);
}

wxString ButtonsListCtrl::GetPageText(size_t n) const
{
    ScalableButton* btn = m_pageButtons[n];
    return btn->GetLabel();
}

#endif // _WIN32


