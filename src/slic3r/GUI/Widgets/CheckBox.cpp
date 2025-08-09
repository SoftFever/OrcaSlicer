#include "CheckBox.hpp"

/*
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
    if (auto sParent = GetScrollParent(this))
        SetBackgroundColour(sParent->GetBackgroundColour());

    m_label = label;

    Bind(wxEVT_CHECKBOX, ([this](wxCommandEvent e) {
        // Crashes if all checkboxes not uses wxEVT_CHECKBOX event
        //SetValue(e.GetInt());
        //e.SetEventObject(this);
        //e.SetId(GetId());
        //GetEventHandler()->ProcessEvent(e);
        e.Skip();
    }));

    /*
    Bind(wxEVT_PAINT, ([this](wxPaintEvent e) { // without this it makes glitches on sidebar
        // Check background color changes to refresh icons on system color change
        // experimental solution that works. binding wxEVT_SYS_COLOUR_CHANGED not working
        if(m_bg_track != GetBackgroundColour()){
            m_bg_track = GetBackgroundColour();
            Rescale(); // refresh bitmap icons to get correct colors
            UpdateIcon();
        }
    }));
    */

    // DPIDialog's uses wxEVT_CHAR_HOOK
    Bind(wxEVT_CHAR_HOOK, ([this](wxKeyEvent&e){
        int  k = e.GetKeyCode();
        if(HasFocus() && k == WXK_SPACE){
            SetValue(!m_value);
            e.Skip(false);
        }else
            e.Skip();
    }));

    auto h_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_check = new Button(this, "", "check_off", 0, 18);
    m_check->SetPaddingSize(FromDIP(wxSize(0,0)));
    m_check->SetBackgroundColor(GetBackgroundColour());
    m_check->SetCornerRadius(0);
    m_check->SetBorderWidth(0);

    if(label.IsEmpty()){
        //m_check->SetCanFocus(true);
        //m_check->AcceptsFocusFromKeyboard();
        m_check->Bind(wxEVT_SET_FOCUS ,([this](wxFocusEvent e) {UpdateIcon(); e.Skip(); }));
        m_check->Bind(wxEVT_KILL_FOCUS,([this](wxFocusEvent e) {UpdateIcon(); e.Skip(); }));
    }

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

    h_sizer->Add(m_check, 0, wxALIGN_CENTER_VERTICAL);//) | wxTOP | wxBOTTOM, FromDIP(4));

    if(!label.IsEmpty()){
        m_has_text = true;

        m_text_color = StateColor(
            std::pair(wxColour("#6B6A6A"), (int)StateColor::Disabled),
            std::pair(wxColour("#363636"), (int)StateColor::Enabled)
        );

        m_focus_color = StateColor(
            std::pair(GetBackgroundColour() , (int)StateColor::NotFocused),
            std::pair(wxColour("#009688")   , (int)StateColor::Focused)
        );

        //m_focus_color.colorForStates(StateColor::NotFocused) = GetBackgroundColour();

        m_text = new Button(this, label);
        m_text->SetPaddingSize(FromDIP(wxSize(5,2)));
        m_text->SetBackgroundColor(GetBackgroundColour());
        m_text->SetCornerRadius(0);
        m_text->SetBorderWidth(FromDIP(1));
        m_text->SetBorderColor(m_focus_color);
        m_text->SetTextColor(m_text_color);
        m_text->SetFont(m_font);
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
        m_text->Bind(wxEVT_SET_FOCUS ,([this](wxFocusEvent e) {UpdateIcon(); e.Skip(); }));
        m_text->Bind(wxEVT_KILL_FOCUS,([this](wxFocusEvent e) {UpdateIcon(); e.Skip(); }));

        h_sizer->Add(m_text, 0, wxALIGN_CENTER_VERTICAL);// | wxTOP | wxBOTTOM, FromDIP(4));
        h_sizer->AddSpacer(FromDIP(10));
    }
    /*
    else
    {
        // dummy button to manage focus events
        m_text = new Button(this, "");
        m_text->SetPaddingSize(FromDIP(wxSize(0,0)));
        m_text->SetBackgroundColor(GetBackgroundColour());
        m_text->SetBorderColor(GetBackgroundColour());
        m_text->Bind(wxEVT_SET_FOCUS ,([this](wxFocusEvent e) {UpdateIcon(); e.Skip(); }));
        m_text->Bind(wxEVT_KILL_FOCUS,([this](wxFocusEvent e) {UpdateIcon(); e.Skip(); }));
        h_sizer->Add(m_text, 0, wxALIGN_CENTER_VERTICAL);// | wxTOP | wxBOTTOM, FromDIP(4));
        h_sizer->AddSpacer(FromDIP(10));
    }
    */

    SetSizerAndFit(h_sizer);
    Layout();

    Refresh();
}

void CheckBox::Rescale()
{
    /*
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
    */
    m_check->Rescale();
    if(m_has_text)
        m_text->Rescale();
    Refresh();
}

void CheckBox::OnClick()
{
    if(m_has_text)
        m_text->SetFocus();
    else
        m_check->SetFocus();
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
    bool focus = HasFocus();
    if      (!m_enabled)
        icon = m_half_checked ? m_half_disabled : m_value ? m_on_disabled : m_off_disabled;
    else if (m_hovered && focus)
        icon = m_half_checked ? m_half_hvrfcs   : m_value ? m_on_hvrfcs   : m_off_hvrfcs;
    else if (m_hovered && !focus)
        icon = m_half_checked ? m_half_hover    : m_value ? m_on_hover    : m_off_hover;
    else if (!m_hovered && focus)
        icon = m_half_checked ? m_half_focused  : m_value ? m_on_focused  : m_off_focused;
    else
        icon = m_half_checked ? m_half          : m_value ? m_on          : m_off;
    m_check->SetIcon(icon.name());
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
    m_half_checked = false;
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