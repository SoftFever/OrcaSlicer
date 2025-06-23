#include "RadioGroup.hpp"
#include "Label.hpp"
#include "StateColor.hpp"

/*
DescriptiveText on bottom while hovering radio buttons
*/

RadioGroup::RadioGroup(
    wxWindow* parent,
    const std::vector<wxString>& labels,
    long  direction,
    int row_col_limit
)
    : wxPanel(parent, wxID_ANY)
    , m_on(       this, "radio_on"       , 18)
    , m_off(      this, "radio_off"      , 18)
    , m_on_hover( this, "radio_on_hover" , 18)
    , m_off_hover(this, "radio_off_hover", 18)
    , m_disabled( this, "radio_disabled" , 18)
    , m_selectedIndex(0)
    , m_focused(false)
    , m_enabled(true)
    , m_font(Label::Body_14)
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
    if(parent)
        this->SetBackgroundColour(parent->GetBackgroundColour());
    m_labels        = labels;
    m_item_count    = m_labels.size();
    auto bmp_size   = m_on.GetBmpSize();
    int  item_limit = row_col_limit < 0 ? 1 : row_col_limit > m_item_count ? m_item_count : row_col_limit;
    int  count      = (int(m_item_count / item_limit) + (m_item_count % item_limit));
    int  rows       = (direction & wxHORIZONTAL) ? item_limit : count;
    int  cols       = (direction & wxHORIZONTAL) ? count : item_limit;
    wxFlexGridSizer* f_sizer = new wxFlexGridSizer(rows, cols, 0, 0);

    SetFont(m_font);

    SetDoubleBuffered(true);
    AcceptsFocusFromKeyboard();

    Bind(wxEVT_SET_FOCUS ,([this](wxFocusEvent e) {m_focused = true ; Refresh();}));// e.Skip();}));
    Bind(wxEVT_KILL_FOCUS,([this](wxFocusEvent e) {m_focused = false; Refresh();}));// e.Skip();}));
    Bind(wxEVT_PAINT,([this](wxPaintEvent e) {
        //if (m_focused && !HasFocus()) // Required to take focus again since Refresh causing losing focus
        //    SetFocus();
        // call darkModeColorFor one time instead calling each control rendered
        m_text_color  = StateColor::darkModeColorFor(wxColour(m_enabled ? "#363636" : "#6B6B6B"));
        m_focus_color = StateColor::darkModeColorFor(wxColour("#009688"));
        e.Skip();
    }));
    // DPIDialog's uses wxEVT_CHAR_HOOK
    Bind(wxEVT_CHAR_HOOK, ([this](wxKeyEvent&e){
        int  k = e.GetKeyCode();
        bool is_next = (k == WXK_DOWN || k == WXK_RIGHT);
        bool is_prev = (k == WXK_LEFT || k == WXK_UP);
        if(m_focused){
            if      (is_next) SelectNext();
            else if (is_prev) SelectPrevious();
            e.Skip(!(is_next || is_prev));
        }else{
            e.Skip();
        }
    }));

    auto text_start = FromDIP(wxPoint(4,2));
    auto rect_end   = FromDIP(wxSize(10,0));
    auto total_add  = FromDIP(wxSize(18,4));

    for (int i = 0; i < m_item_count; ++i){
        auto rb = new wxGenericStaticBitmap(this, wxID_ANY, m_off.bmp(), wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxNO_BORDER);
        m_radioButtons.push_back(rb);
        rb->Bind(wxEVT_LEFT_DOWN   ,([this, i](wxMouseEvent e) {OnClick(i)            ; e.Skip();}));
        rb->Bind(wxEVT_ENTER_WINDOW,([this, i](wxMouseEvent e) {SetRadioIcon(i, true) ; e.Skip();}));
        rb->Bind(wxEVT_LEAVE_WINDOW,([this, i](wxMouseEvent e) {
            // prevent removing hover effect while switching between button and its text
            auto win = wxFindWindowAtPoint(wxGetMousePosition());
            if(!win || win->GetId() != m_labelButtons[i]->GetId())
                SetRadioIcon(i, false);
            e.Skip();
        }));

        auto tx = new wxBannerWindow(this, wxID_ANY); // best solution to prevent stealing focus
        tx->SetMinSize(GetTextExtent(m_labels[i]) + total_add);
        tx->SetBackgroundColour(GetBackgroundColour());
        tx->DisableFocusFromKeyboard();
        m_labelButtons.push_back(tx);
        tx->Bind(wxEVT_LEFT_DOWN   ,([this, i](wxMouseEvent e) {OnClick(i)            ; e.Skip();}));
        tx->Bind(wxEVT_ENTER_WINDOW,([this, i](wxMouseEvent e) {SetRadioIcon(i, true) ; e.Skip();}));
        tx->Bind(wxEVT_LEAVE_WINDOW,([this, i](wxMouseEvent e) {
            // prevent removing hover effect while switching between button and its text
            auto win = wxFindWindowAtPoint(wxGetMousePosition());
            if(!win || win->GetId() != m_radioButtons[i]->GetId())
                SetRadioIcon(i, false);
            e.Skip();
        }));
        tx->Bind(wxEVT_PAINT,([this, i, tx, text_start, rect_end](wxPaintEvent e) {
            wxPaintDC dc(tx);
            dc.Clear();

            dc.SetTextForeground(m_text_color);
            dc.DrawText(m_labels[i], text_start);

            dc.SetPen((m_focused && m_selectedIndex == i) ? wxPen(m_focus_color, 1, wxPENSTYLE_SOLID) : *wxTRANSPARENT_PEN);
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(wxPoint(0,0), tx->GetSize() - rect_end);
        }));

        wxBoxSizer* radio_sizer = new wxBoxSizer(wxHORIZONTAL);
        radio_sizer->Add(rb, 0, wxALIGN_CENTER_VERTICAL);
        radio_sizer->Add(tx, 0, wxALIGN_CENTER_VERTICAL);
        f_sizer->Add(radio_sizer, 0, wxTOP | wxBOTTOM, FromDIP(4));
    }

    SetSelection(m_selectedIndex);
    SetSizer(f_sizer);
}

void RadioGroup::OnClick(int i)
{
    if(!m_enabled) return;
    m_focused = true; // prevents 2 time refresh
    SetSelection(i);
}

void RadioGroup::SetSelection(int index)
{
    if (index >= 0 && index < static_cast<int>(m_item_count)){
        m_selectedIndex = index;
        for (size_t i = 0; i < m_item_count; ++i)
            SetRadioIcon(i, m_focused && i == m_selectedIndex);

        wxCommandEvent evt(wxEVT_COMMAND_RADIOBOX_SELECTED, GetId());
        evt.SetInt(index);
        evt.SetString(m_labels[index]);
        GetEventHandler()->ProcessEvent(evt);

        Refresh(); // refresh on every change
    }
}

int RadioGroup::GetSelection()
{ 
    return m_selectedIndex; 
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
    auto icon = !m_enabled ? m_disabled : m_selectedIndex == i ? (hover ? m_on_hover : m_on) : (hover ? m_off_hover : m_off);
    m_radioButtons[i]->SetBitmap(icon.bmp());
}

bool RadioGroup::Enable(bool enable)
{
    m_enabled = enable;
    bool result = wxPanel::Enable(enable);

    if(!enable && m_focused)
        m_focused = false;

    for (size_t i = 0; i < m_item_count; ++i)
        SetRadioIcon(i, false);

    Refresh();

    return result;
};

bool RadioGroup::Disable() {return RadioGroup::Enable(false);};

bool RadioGroup::IsEnabled(){return m_enabled;};

void RadioGroup::SetRadioTooltip(int i, wxString tooltip)
{
    m_radioButtons[i]->SetToolTip(tooltip);
    m_labelButtons[i]->SetToolTip(tooltip);
}