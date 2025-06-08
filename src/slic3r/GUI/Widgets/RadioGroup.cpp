#include "RadioGroup.hpp"
#include "Label.hpp"
#include "StateColor.hpp"

RadioGroup::RadioGroup(
    wxWindow* parent,
    const std::vector<wxString>& labels,
    long  direction,
    int row_col_limit
)
    : wxPanel(parent)
    , m_on(       this, "radio_on"       , 18)
    , m_off(      this, "radio_off"      , 18)
    , m_on_hover( this, "radio_on_hover" , 18)
    , m_off_hover(this, "radio_off_hover", 18)
    , m_disabled( this, "radio_off_hover", 18)
    , m_selectedIndex(0)
{
    Create(parent, labels, direction, row_col_limit);
}

void RadioGroup::Create(
    wxWindow* parent,
    const std::vector<wxString>& labels,
    long  direction,   /* wxHORIZONTAL / wxVERTICAL */
    int row_col_limit  /* sets column/row count depends on direction. creates new row if wxHORIZONTAL used after limit reached */
)
{
    m_labels = labels;
    auto bg = parent->GetBackgroundColour();
    this->SetBackgroundColour(bg);
    auto bmp_size   = m_on.GetBmpSize();
    int  item_count = m_labels.size();
    int  item_limit = row_col_limit < 0 ? 1 : row_col_limit > item_count ? item_count : row_col_limit;
    int  count      = (int(item_count / item_limit) + (item_count % item_limit));
    int  rows       = (direction & wxHORIZONTAL) ? item_limit : count;
    int  cols       = (direction & wxHORIZONTAL) ? count : item_limit;
    wxFlexGridSizer* f_sizer = new wxFlexGridSizer(rows, cols, 0, 0);

    SetDoubleBuffered(true);
    AcceptsFocusFromKeyboard();
    SetFocusIgnoringChildren();
    Bind(wxEVT_SET_FOCUS ,([this](wxFocusEvent e) {UpdateFocus(true) ; e.Skip();}));
    Bind(wxEVT_KILL_FOCUS,([this](wxFocusEvent e) {UpdateFocus(false); e.Skip();}));
    Bind(wxEVT_PAINT,([this](wxPaintEvent e) {
        wxPaintDC dc(this);
        dc.Clear();
        dc.SetPen(wxPen(StateColor::darkModeColorFor(wxColour("#009688")), 1, wxPENSTYLE_SOLID));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(m_focus_rect);
    }));

    for (int i = 0; i < item_count; ++i){
        auto rb = new wxStaticBitmap(this, wxID_ANY, m_off.bmp(), wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxNO_BORDER);
        m_radioButtons.push_back(rb);
        rb->Bind(wxEVT_LEFT_DOWN   ,([this, i](wxMouseEvent e) {OnClick(i)            ; e.Skip();}));
        rb->Bind(wxEVT_ENTER_WINDOW,([this, i](wxMouseEvent e) {SetRadioIcon(i, true) ; e.Skip();}));
        rb->Bind(wxEVT_LEAVE_WINDOW,([this, i](wxMouseEvent e) {SetRadioIcon(i, false); e.Skip();}));

        auto tx = new wxStaticText(this, wxID_ANY, " " + m_labels[i], wxDefaultPosition, wxDefaultSize);
        tx->SetForegroundColour(wxColour("#363636"));
        tx->SetFont(Label::Body_14);
        m_labelButtons.push_back(tx);
        tx->Bind(wxEVT_LEFT_DOWN   ,([this, i](wxMouseEvent e) {OnClick(i)            ; e.Skip();}));
        tx->Bind(wxEVT_ENTER_WINDOW,([this, i](wxMouseEvent e) {SetRadioIcon(i, true) ; e.Skip();}));
        tx->Bind(wxEVT_LEAVE_WINDOW,([this, i](wxMouseEvent e) {SetRadioIcon(i, false); e.Skip();}));

        wxBoxSizer* radio_sizer = new wxBoxSizer(wxHORIZONTAL);
        radio_sizer->Add(rb, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 1);
        radio_sizer->Add(tx, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, this->FromDIP(15));
        f_sizer->Add(radio_sizer, 0, wxTOP | wxBOTTOM, this->FromDIP(4));
    }
    SetSelection(m_selectedIndex);
    SetSizer(f_sizer);
}

void RadioGroup::OnClick(int i)
{
    SetFocus();
    SetSelection(i);
}

void RadioGroup::SetSelection(int index)
{
    if (index >= 0 && index < static_cast<int>(m_labels.size())){
        m_selectedIndex = index;
        for (size_t i = 0; i < m_labels.size(); ++i)
            SetRadioIcon(i, HasFocus() && i == m_selectedIndex);
        if (HasFocus())
            UpdateFocus(true);

        wxCommandEvent evt(wxEVT_COMMAND_RADIOBOX_SELECTED, GetId());
        evt.SetInt(index);
        evt.SetString(m_labels[index]);
        GetEventHandler()->ProcessEvent(evt);
    }
}

int RadioGroup::GetSelection()
{ 
    return m_selectedIndex; 
}

void RadioGroup::UpdateFocus(bool focus)
{
    m_focus_rect = focus ? wxRect(
        m_radioButtons[m_selectedIndex]->GetRect().GetTopLeft()     - wxPoint(1, 3),
        m_labelButtons[m_selectedIndex]->GetRect().GetBottomRight() + wxPoint(4, 1)
    ) : wxRect(0,0,0,0);
    Refresh();
}

void RadioGroup::SelectNext(bool focus)
{
    SetSelection(m_selectedIndex + 1 > (m_radioButtons.size() - 1) ? 0 : m_selectedIndex + 1);
}

void RadioGroup::SelectPrevious(bool focus)
{
    SetSelection(m_selectedIndex - 1 < 0 ? (m_radioButtons.size() - 1) : m_selectedIndex - 1);
}

void RadioGroup::SetRadioIcon(int i, bool hover)
{
    auto icon = m_selectedIndex == i ? (hover ? m_on_hover : m_on) : (hover ? m_off_hover : m_off);
    m_radioButtons[i]->SetBitmap(icon.bmp());
}