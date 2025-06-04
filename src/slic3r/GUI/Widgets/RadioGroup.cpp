#include "RadioGroup.hpp"
#include "Label.hpp"
#include "StateColor.hpp"

BEGIN_EVENT_TABLE(RadioGroup, wxPanel)

EVT_TOGGLEBUTTON(wxID_ANY, RadioGroup::OnToggleClick)

//EVT_KEY_DOWN(RadioGroup::OnKeyDown)

END_EVENT_TABLE()

/*
TODO
-select with Keyboard navigation
-m_on_hover
-Scaling
-disable - enable
*/

RadioGroup::RadioGroup(
        wxWindow* parent,
        const std::vector<wxString>& labels,
        long  direction,
        int row_col_limit
)
        : wxPanel(parent),
          m_on(       this, "radio_on"       , 18),
          m_off(      this, "radio_off"      , 18),
          m_on_hover( this, "radio_on_hover" , 18),
          m_off_hover(this, "radio_off_hover", 18),
          m_disabled( this, "radio_off_hover", 18),
          m_selectedIndex(0)
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

    for (int i = 0; i < item_count; ++i){
        auto rb = new wxBitmapToggleButton(this, wxID_ANY, m_off.bmp(), wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxNO_BORDER);
        rb->SetBitmapPressed(m_on.bmp());
        rb->SetBitmapHover(m_off_hover.bmp()); // only works on non selected items
        rb->SetBitmapDisabled(m_disabled.bmp());
        rb->SetBackgroundColour(bg);
        
        rb->SetSize(bmp_size);
        rb->SetMinSize(bmp_size);
        m_radioButtons.push_back(rb);
        rb->Bind(wxEVT_SET_FOCUS,([this, i](wxFocusEvent e) {
            DrawFocus(i);
            e.Skip();
        }));
        rb->Bind(wxEVT_KILL_FOCUS,([this, i](wxFocusEvent e) {
            KillFocus();
            e.Skip();
        }));
        rb->Bind(wxEVT_KEY_DOWN, &RadioGroup::OnKeyDown, this);

        auto tx = new wxStaticText(this, wxID_ANY, " " + m_labels[i], wxDefaultPosition, wxDefaultSize);
        tx->SetForegroundColour(wxColour("#363636"));
        tx->SetFont(Label::Body_14);
        m_labelButtons.push_back(tx);
        tx->Bind(wxEVT_LEFT_DOWN,([this, tx](wxMouseEvent e) {
            if (e.GetEventType() == wxEVT_LEFT_DCLICK) return;
            OnLabelClick(tx);
            e.Skip();
        }));
        tx->Bind(wxEVT_LEFT_DCLICK,([this, tx](wxMouseEvent e) {
            OnLabelClick(tx);
            e.Skip();
        }));
        tx->Bind(wxEVT_ENTER_WINDOW,([this, i](wxMouseEvent e) {
            m_radioButtons[i]->SetBitmap(m_selectedIndex == i ? m_on_hover.bmp() : m_off_hover.bmp());
            e.Skip();
        }));
        tx->Bind(wxEVT_LEAVE_WINDOW,([this, i](wxMouseEvent e) {
            m_radioButtons[i]->SetBitmap(m_selectedIndex == i ? m_on.bmp() : m_off.bmp());
            e.Skip();
        }));

        wxBoxSizer* radio_sizer = new wxBoxSizer(wxHORIZONTAL);
        radio_sizer->Add(rb, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 1);
        radio_sizer->Add(tx, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, this->FromDIP(15));
        f_sizer->Add(radio_sizer, 0, wxTOP | wxBOTTOM, this->FromDIP(4));
    }
    SetSelection(m_selectedIndex);
    SetSizer(f_sizer);
}

void RadioGroup::OnToggleClick(wxCommandEvent& event)
{
    wxBitmapToggleButton* sel = dynamic_cast<wxBitmapToggleButton*>(event.GetEventObject());
    if (!sel)
        return;

    int sel_index = -1;
    for (size_t i = 0; i < m_labels.size(); ++i){
        if (m_radioButtons[i] == sel){
            sel_index = static_cast<int>(i);
            break;
        }
    }
    SetSelection(sel_index);

    wxCommandEvent evt(wxEVT_COMMAND_RADIOBOX_SELECTED, GetId());
    evt.SetInt(sel_index);
    evt.SetString(m_labels[sel_index]);
    GetEventHandler()->ProcessEvent(evt);
}

void RadioGroup::OnLabelClick(wxStaticText* sel)
{
    int sel_index = -1;
    for (size_t i = 0; i < m_labels.size(); ++i){
        if (m_labelButtons[i] == sel){
            sel_index = static_cast<int>(i);
            break;
        }
    }
    SetSelection(sel_index);

    wxCommandEvent evt(wxEVT_COMMAND_RADIOBOX_SELECTED, GetId());
    evt.SetInt(sel_index);
    evt.SetString(m_labels[sel_index]);
    GetEventHandler()->ProcessEvent(evt);
}

void RadioGroup::SetSelection(int index)
{
    if (index >= 0 && index < static_cast<int>(m_labels.size())){
        m_selectedIndex = index;
        for (size_t i = 0; i < m_labels.size(); ++i){
            auto rb = m_radioButtons[i];
            rb->SetValue(i == index);
            rb->SetBitmap(m_selectedIndex == i ? m_on.bmp() : m_off.bmp());
            if(m_selectedIndex == i)
                rb->AcceptsFocusFromKeyboard();
            else
                rb->DisableFocusFromKeyboard();
        }
        Refresh();
    }
}

int RadioGroup::GetSelection()
{ 
    return m_selectedIndex; 
}

void RadioGroup::DrawFocus(int item)
{
    auto rb = m_radioButtons[item];
    auto tx = m_labelButtons[item];
    wxRect area = wxRect(
        rb->GetRect().GetTopLeft(),
        tx->GetRect().GetBottomRight()
    );
    area.Inflate(1);
    area.y -= 3;
    area.height += 4;
    area.width += 5;
    wxClientDC dc(this);
    dc.Clear();
    dc.SetPen(wxPen(StateColor::darkModeColorFor(wxColour("#009688"))));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(area); 
}

void RadioGroup::KillFocus()
{
    bool on_focus = false;
    for (size_t i = 0; i < m_labels.size(); ++i){
        if(m_radioButtons[i]->HasFocus()){
            on_focus = true;
            break;
        }
    }
    if(!on_focus){
        // this->Refresh();
        wxClientDC dc(this);
        dc.Clear();
    }
}

void RadioGroup::OnKeyDown(wxKeyEvent& event)
{
    //if (!dynamic_cast<wxBitmapToggleButton*>(event.GetEventObject())){
    //    event.Skip();
    //    return;
    //}
    int i = -1;
    for (auto btn : m_radioButtons){
        i++;
        if(btn->HasFocus())
            break;
    }
    int key = event.GetKeyCode();
    int cnt = m_radioButtons.size();
    if(key == WXK_LEFT || key == WXK_UP ){
        int nav_to = i - 1 < 0 ? (cnt - 1) : i - 1;
        SetSelection(nav_to);
        m_radioButtons[nav_to]->SetFocus();
    }
    else if(key == WXK_RIGHT || key == WXK_DOWN ){
        int nav_to = i + 1 > (cnt - 1) ? 0 : i + 1;
        SetSelection(nav_to);
        m_radioButtons[nav_to]->SetFocus();
    }
    else if (key == WXK_TAB){
        event.Skip();
        return;
    }
    event.StopPropagation();
        //case WXK_RETURN:
        //case WXK_SPACE:
}

