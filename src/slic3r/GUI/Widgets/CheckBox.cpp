#include "CheckBox.hpp"

/*
SetFont
on_dpi_changed
on dark mode changed
*/

CheckBox::CheckBox(wxWindow *parent, wxString label)
    : wxPanel(parent, wxID_ANY)
    , m_on(           this, "check_on"                  , 18)
    , m_half(         this, "check_half"                , 18)
    , m_off(          this, "check_off"                 , 18)
    , m_on_disabled(  this, "check_on_disabled"         , 18)
    , m_half_disabled(this, "check_half_disabled"       , 18)
    , m_off_disabled( this, "check_off_disabled"        , 18)
    , m_on_focused(   this, "check_on_focused"          , 18) 
    , m_half_focused( this, "check_half_focused"        , 18)
    , m_off_focused(  this, "check_off_focused"         , 18)
    , m_on_hover(     this, "check_on_hovered"          , 18) 
    , m_half_hover(   this, "check_half_hovered"        , 18)
    , m_off_hover(    this, "check_off_hovered"         , 18)
    , m_on_hvrfcs(    this, "check_on_focused_hovered"  , 18) 
    , m_half_hvrfcs(  this, "check_half_focused_hovered", 18)
    , m_off_hvrfcs(   this, "check_off_focused_hovered" , 18)
    , m_font(Label::Body_14)
    , m_value(false)
{
    if (parent)
        SetBackgroundColour(parent->GetBackgroundColour());
    else{
        auto scroll_parent = GetScrollParent(this);
        if(scroll_parent)
            SetBackgroundColour(scroll_parent->GetBackgroundColour());
    }

    //LoadIcons();
    m_label = label;

    Bind(wxEVT_CHECKBOX,([this](wxCommandEvent e) {
        // Crashes if all checkboxes not uses wxEVT_CHECKBOX event
        //SetValue(e.GetInt());
        //e.SetEventObject(this);
        //e.SetId(GetId());
        //GetEventHandler()->ProcessEvent(e);
        e.Skip();
    }));

    AcceptsFocusFromKeyboard();
    SetCanFocus(true);
    Bind(wxEVT_SET_FOCUS ,([this](wxFocusEvent e) {
        m_focused = true ;
        UpdateIcon();
        if(m_has_text){
            m_focus_rect->Show();
            m_focus_rect->UpdatePosition();
        }
        Refresh();
        e.Skip();
    }));
    Bind(wxEVT_KILL_FOCUS,([this](wxFocusEvent e) {
        m_focused = false;
        UpdateIcon();
        if(m_has_text)
            m_focus_rect->Hide();
        Refresh();
        e.Skip();
    }));

    Bind(wxEVT_PAINT,([this](wxPaintEvent e) { // without this it makes glitches on sidebar

        wxPaintDC dc(this);
        dc.Clear();
        /* kept this solution if FocusRect solution not works
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        if (m_has_text){
            dc.SetPen(wxPen(StateColor::darkModeColorFor(wxColour("#009688")), 1, wxPENSTYLE_SOLID));
            dc.DrawRectangle(
                m_focused ? wxRect(
                    m_text->GetRect().GetTopLeft()     + wxPoint(4, -3),
                    m_text->GetRect().GetBottomRight() + wxPoint(4, 1)
                ) : wxRect(0,0,0,0)
            );
        }
        */

        // Check background color changes to refresh icons on system color change
        // experimental solution that works. binding wxEVT_SYS_COLOUR_CHANGED not working
        if(m_bg_track != GetBackgroundColour()){
            m_bg_track = GetBackgroundColour();
            Rescale(); // refresh bitmap icons to get correct colors
            UpdateIcon();
        }

        e.Skip();
        if (m_focused)
            SetFocus(); // Required to take focus again since Refresh causing lossing focus
    }));
            

    // DPIDialog's uses wxEVT_CHAR_HOOK
    Bind(wxEVT_CHAR_HOOK, ([this](wxKeyEvent&e){
        int  k = e.GetKeyCode();
        if(m_focused && k == WXK_SPACE){
            SetValue(!m_value);
            e.Skip(false);
        }else{
            e.Skip();
        }
    }));

    auto h_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_check = new wxStaticBitmap(this, wxID_ANY, m_off.bmp(), wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxNO_BORDER);
    m_check->SetCanFocus(false);
    m_check->Bind(wxEVT_LEFT_DOWN   ,([this](wxMouseEvent e) {
        if (e.GetEventType() == wxEVT_LEFT_DCLICK) return;
        OnClick();
        e.Skip();
    }));
    m_check->Bind(wxEVT_LEFT_DCLICK ,([this](wxMouseEvent e) {
        OnClick();
        e.Skip();
    }));
    m_check->Bind(wxEVT_ENTER_WINDOW,([this](wxMouseEvent e) {
        m_hovered = true;
        UpdateIcon();
        e.Skip();
    }));
    m_check->Bind(wxEVT_LEAVE_WINDOW,([this](wxMouseEvent e) {
        // prevent removing hover effect while switching between button and its text
        auto win = wxFindWindowAtPoint(wxGetMousePosition());
        if(!m_has_text || !win || (m_has_text && win->GetId() != m_text->GetId()))
            m_hovered = false;
        UpdateIcon();
        e.Skip();
    }));
    h_sizer->Add(m_check, 0, wxALIGN_CENTER_VERTICAL);

    if(!label.IsEmpty()){
        m_has_text = true;
        m_text = new wxStaticText(this, wxID_ANY, "  " + label); // use spacing instead margin to capture all events
        m_text->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#363636")));
        m_text->SetFont(m_font);
        m_text->SetCanFocus(false);
        m_text->Bind(wxEVT_LEFT_DOWN   ,([this](wxMouseEvent e) {
            if (e.GetEventType() == wxEVT_LEFT_DCLICK) return;
            OnClick();
            e.Skip();
        }));
        m_text->Bind(wxEVT_LEFT_DCLICK ,([this](wxMouseEvent e) {
            OnClick();
            e.Skip();
        }));
        m_text->Bind(wxEVT_ENTER_WINDOW,([this](wxMouseEvent e) {
            m_hovered = true;
            UpdateIcon();
            e.Skip();
        }));
        m_text->Bind(wxEVT_LEAVE_WINDOW,([this](wxMouseEvent e) {
            // prevent removing hover effect while switching between button and its text
            auto win = wxFindWindowAtPoint(wxGetMousePosition());
            if(!win || win->GetId() != m_check->GetId())
                m_hovered = false;
            UpdateIcon();
            e.Skip();
        }));
        /*
        m_text->Bind(wxEVT_PAINT,([this](wxPaintEvent e) {
            //wxPaintDC dc(this);
            //dc.Clear();
            e.Skip();
            if (m_focused)
                DrawFocusBorder();
        }));
        */
        m_focus_rect = new FocusRect(this, m_text);
        h_sizer->Add(m_text, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, FromDIP(4));
        h_sizer->AddSpacer(FromDIP(10));
    }

    SetSizerAndFit(h_sizer);
    Layout();

	Refresh();
}

/*
void CheckBox::DrawFocusBorder() {
    wxClientDC dc(this);

    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetPen(wxPen(StateColor::darkModeColorFor(wxColour("#009688")), 1, wxPENSTYLE_SOLID));
    dc.DrawRectangle(wxRect(
            m_text->GetRect().GetTopLeft()     + wxPoint(4, -3),
            m_text->GetRect().GetBottomRight() + wxPoint(4, 1)
        )
    );
}
*/

void CheckBox::Rescale()
{
    m_on.msw_rescale();
    m_half.msw_rescale();
    m_off.msw_rescale();
    m_on_disabled.msw_rescale();
    m_half_disabled.msw_rescale();
    m_off_disabled.msw_rescale();
    m_on_focused.msw_rescale();
    m_half_focused.msw_rescale();
    m_off_focused.msw_rescale();
    m_on_hover.msw_rescale();
    m_half_hover.msw_rescale();
    m_off_hover.msw_rescale();
    m_on_hvrfcs.msw_rescale();
    m_off_hvrfcs.msw_rescale();
    m_half_hvrfcs.msw_rescale();

    m_check->SetSize(m_on.GetBmpSize());
	Refresh();
}

void CheckBox::OnClick()
{
    m_focused = true;
    SetFocus();
    SetValue(!m_value);
}

void CheckBox::SetTooltip(wxString label)
{
    m_check->SetToolTip(label);
    if(m_has_text)
        m_text->SetToolTip(label);
}

void CheckBox::UpdateIcon()
{
    ScalableBitmap icon;
    if      (!m_enabled)
        icon = m_half_checked ? m_half_disabled : m_value ? m_on_disabled : m_off_disabled;
    else if (m_hovered && m_focused)
        icon = m_half_checked ? m_half_hvrfcs   : m_value ? m_on_hvrfcs   : m_off_hvrfcs;
    else if (m_hovered && !m_focused)
        icon = m_half_checked ? m_half_hover    : m_value ? m_on_hover    : m_off_hover;
    else if (!m_hovered && m_focused)
        icon = m_half_checked ? m_half_focused  : m_value ? m_on_focused  : m_off_focused;
    else
        icon = m_half_checked ? m_half          : m_value ? m_on          : m_off;
    m_check->SetBitmap(icon.bmp());
    m_check->Refresh();
}

wxWindow* CheckBox::GetScrollParent(wxWindow *pWindow)
{
    wxWindow *pWin = pWindow;
    while (pWin->GetParent()) {
        auto pWin2 = pWin->GetParent();
        if (auto top = dynamic_cast<wxScrollHelper *>(pWin2))
            return dynamic_cast<wxWindow *>(pWin);
        pWin = pWin2;
    }
    return nullptr;
}

void CheckBox::SetValue(bool value){
    m_value = value;
    UpdateIcon();

    // temporary solution to handle different events
    // all events should be unify in wxEVT_CHECKBOX
    try {
        wxCommandEvent evt(wxEVT_CHECKBOX, GetId());
        evt.SetInt(m_value);
        evt.SetString(m_label);
        GetEventHandler()->ProcessEvent(evt);
    }
    catch (const std::runtime_error& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        try {
            wxCommandEvent evt(wxEVT_TOGGLEBUTTON, GetId());
            evt.SetInt(m_value);
            evt.SetString(m_label);
            GetEventHandler()->ProcessEvent(evt);
        }
        catch (const std::runtime_error& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }

     Refresh();
}

CheckBox::FocusRect::FocusRect(wxWindow* parent, wxStaticText* target)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNO_BORDER | wxTRANSPARENT_WINDOW),
      m_target(target) {
    SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
    #ifdef __WXMSW__
        SetBackgroundColour(wxColour(0, 0, 0, 0));
    #endif
    Disable(); // disable to prevent stealing focus
    UpdatePosition();
    Bind(wxEVT_PAINT,([this](wxPaintEvent e) {
        wxPaintDC dc(this);
        auto pen = wxPen(StateColor::darkModeColorFor(wxColour("#009688")), 1, wxPENSTYLE_SOLID);
        wxSize size = GetSize();
        #if wxUSE_GRAPHICS_CONTEXT
            wxGCDC gdc(dc); // not sure using wxGCDC has any advantage or disadvantage
            if (gdc.IsOk()) {
                gdc.SetPen(pen);
                gdc.SetBrush(*wxTRANSPARENT_BRUSH);
                gdc.DrawRectangle(0, 0, size.x, size.y);
                return;
            }
        #endif
            dc.SetPen(pen);
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(0, 0, size.x, size.y);
    }));

    // Do nothing to kept transparency
    Bind(wxEVT_ERASE_BACKGROUND,([this](wxEraseEvent e) {}));

    Hide();
}

void CheckBox::FocusRect::UpdatePosition() {
    if (m_target) {
        SetPosition(m_target->GetPosition() + wxPoint(3, -3));
        SetSize(    m_target->GetSize()     + wxSize(0, 5));
    }
    Raise(); // ensure stays on top
}