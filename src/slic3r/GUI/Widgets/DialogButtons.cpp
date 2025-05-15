#include "DialogButtons.hpp"

#include "slic3r/GUI/I18N.hpp"

namespace Slic3r { namespace GUI {

// ORCA standardize dialog buttons
DialogButtons::DialogButtons(wxWindow* parent, std::vector<wxString> non_translated_labels, const wxString& focused_btn_label) {
    m_parent = parent;
    m_sizer  = new wxBoxSizer(wxHORIZONTAL);
    m_focus  = focused_btn_label; // better to use translated label for non-standad buttons

    // Add all to array
    for (wxString label : non_translated_labels) {
        Button* btn = new Button(m_parent, _L(label));
        wxString l = label;
        l.LowerCase();
        // missing ones Transfer / Update / Create
        if       (l == "ok")      btn->SetId(wxID_OK);
        else if  (l == "yes")     btn->SetId(wxID_YES);
        else if  (l == "apply")   btn->SetId(wxID_APPLY);
        else if  (l == "confirm") btn->SetId(wxID_APPLY); // no id for confirm
        else if  (l == "no")      btn->SetId(wxID_NO);
        else if  (l == "cancel")  btn->SetId(wxID_CANCEL);
        else if  (l == "open")    btn->SetId(wxID_OPEN);
        else if  (l == "add")     btn->SetId(wxID_ADD);
        else if  (l == "remove")  btn->SetId(wxID_REMOVE);
        else if  (l == "delete")  btn->SetId(wxID_DELETE);
        else if  (l == "refresh") btn->SetId(wxID_REFRESH);
        else if  (l == "retry")   btn->SetId(wxID_RETRY);
        else if  (l == "copy")    btn->SetId(wxID_COPY);
        else if  (l == "save")    btn->SetId(wxID_SAVE);
        else if  (l == "save as") btn->SetId(wxID_SAVEAS);
        else if  (l == "back")    btn->SetId(wxID_BACKWARD);
        else if  (l == "next")    btn->SetId(wxID_FORWARD);
        else if  (l == "help")    btn->SetId(wxID_HELP);
        else if  (l == "abort")   btn->SetId(wxID_ABORT);
        else if  (l == "ignore")  btn->SetId(wxID_IGNORE);
        else if  (l == "stop")    btn->SetId(wxID_STOP);
        m_buttons.push_back(btn);
    }

    m_parent->Bind(wxEVT_DPI_CHANGED, &DialogButtons::on_dpi_changed, this);

    Refresh();
}

DialogButtons::~DialogButtons() {
    m_parent->Unbind(wxEVT_DPI_CHANGED, &DialogButtons::on_dpi_changed, this);
}

void DialogButtons::on_dpi_changed(wxDPIChangedEvent& event) {
    Refresh();
    m_sizer->Layout();
    m_parent->Layout();
    event.Skip();
}

Button* DialogButtons::GetButtonFromID(wxStandardID id) {
    for (Button* btn : m_buttons)
        if (btn->GetId() == id)
            return btn;
    return nullptr;
}

Button* DialogButtons::GetButtonFromLabel(wxString label) {
    for (Button* btn : m_buttons)
        if (btn->GetLabel() == label)
            return btn;
    return nullptr;
}

// shorthands for common buttons
Button* DialogButtons::GetOK()     {return GetButtonFromID(wxID_OK)      ;}
Button* DialogButtons::GetYES()    {return GetButtonFromID(wxID_YES)     ;}
Button* DialogButtons::GetAPPLY()  {return GetButtonFromID(wxID_APPLY)   ;}
Button* DialogButtons::GetCONFIRM(){return GetButtonFromID(wxID_APPLY)   ;}
Button* DialogButtons::GetNO()     {return GetButtonFromID(wxID_NO)      ;}
Button* DialogButtons::GetCANCEL() {return GetButtonFromID(wxID_CANCEL)  ;}
Button* DialogButtons::GetBACK()   {return GetButtonFromID(wxID_BACKWARD);}
Button* DialogButtons::GetFORWARD(){return GetButtonFromID(wxID_FORWARD) ;}

void DialogButtons::SetFocus(wxString label) {
    // use _L("Create") translated text for custom buttons
    // prefer standart primary buttons if label empty
    Button* btn;
    if(label.IsEmpty()){
        if     (GetOK()      != nullptr) btn = GetOK();
        else if(GetYES()     != nullptr) btn = GetYES();
        else if(GetAPPLY()   != nullptr) btn = GetAPPLY();
        else if(GetCONFIRM() != nullptr) btn = GetCONFIRM();
    }else
        btn = GetButtonFromLabel(label);

    btn->SetFocus();
    // we won't need color definations after button style management
    StateColor clr_bg = StateColor(
        std::pair(wxColour("#009688"), (int)StateColor::NotHovered),
        std::pair(wxColour("#DFDFDF"), (int)StateColor::Disabled),
        std::pair(wxColour("#009688"), (int)StateColor::Pressed),
        std::pair(wxColour("#26A69A"), (int)StateColor::Hovered),
        std::pair(wxColour("#009688"), (int)StateColor::Normal),
        std::pair(wxColour("#009688"), (int)StateColor::Enabled)
    );
    StateColor clr_br = StateColor(
        std::pair(wxColour("#009688"), (int)StateColor::NotFocused),
        std::pair(wxColour("#26A69A"), (int)StateColor::Focused)
    );
    StateColor clr_tx = StateColor(
        std::pair(wxColour("#6B6A6A"), (int)StateColor::Disabled),
        std::pair(wxColour("#FEFEFE"), (int)StateColor::Hovered),
        std::pair(wxColour("#FEFEFE"), (int)StateColor::Normal)
    );
    btn->SetBackgroundColor(clr_bg);
    btn->SetBorderColor(clr_br);
    btn->SetTextColor(clr_tx);
}

void DialogButtons::Refresh() {
    m_sizer->Clear();
    // we won't need color definations after button style management
    StateColor clr_bg = StateColor(
        std::pair(wxColour("#DFDFDF"), (int)StateColor::NotHovered),
        std::pair(wxColour("#DFDFDF"), (int)StateColor::Disabled),
        std::pair(wxColour("#DFDFDF"), (int)StateColor::Pressed),
        std::pair(wxColour("#D4D4D4"), (int)StateColor::Hovered),
        std::pair(wxColour("#DFDFDF"), (int)StateColor::Normal),
        std::pair(wxColour("#DFDFDF"), (int)StateColor::Enabled)
    );
    StateColor clr_br = StateColor(
        std::pair(wxColour("#DFDFDF"), (int)StateColor::NotFocused),
        std::pair(wxColour("#009688"), (int)StateColor::Focused)
    );
    StateColor clr_tx = StateColor(
        std::pair(wxColour("#6B6A6A"), (int)StateColor::Disabled),
        std::pair(wxColour("#262E30"), (int)StateColor::Hovered),
        std::pair(wxColour("#262E30"), (int)StateColor::Normal)
    );

    // Apply standard style to all
    for (Button* btn : m_buttons) {
        btn->SetFont(Label::Body_14);
        //btn->SetSize(   wxSize(m_parent->FromDIP(100),m_parent->FromDIP(32)));
        btn->SetMinSize(wxSize(m_parent->FromDIP(100),m_parent->FromDIP(32)));
        btn->SetPaddingSize( m_parent->FromDIP(wxSize(12,8)));
        btn->SetCornerRadius(m_parent->FromDIP(4));
        btn->SetBorderWidth( m_parent->FromDIP(1));
        btn->SetBackgroundColor(clr_bg);
        btn->SetBorderColor(clr_br);
        btn->SetTextColor(clr_tx);
        btn->Bind(wxEVT_KEY_DOWN, &DialogButtons::on_keydown, this);
    }

    int btn_gap = m_parent->FromDIP(10);

    std::set<int> list {wxID_DELETE, wxID_BACKWARD, wxID_FORWARD};
    auto is_left_aligned = [list](int id){
        return list.find(id) != list.end();
    };

    for (Button* btn : m_buttons)  // Left aligned
        if(is_left_aligned(btn->GetId()))
            m_sizer->Add(btn, 0,  wxLEFT | wxTOP | wxBOTTOM | wxALIGN_CENTER_VERTICAL, btn_gap);

    m_sizer->AddStretchSpacer();

    for (Button* btn : m_buttons) // Right aligned
        if(!is_left_aligned(btn->GetId()))
            m_sizer->Add(btn, 0, wxRIGHT | wxTOP | wxBOTTOM | wxALIGN_CENTER_VERTICAL, btn_gap);

    SetFocus(m_focus);
}

void DialogButtons::AddTo(wxBoxSizer* sizer) {
    sizer->Add(m_sizer, 0, wxEXPAND);
}

// This might be helpful for future use

// Append Button

// Prepend Button

void DialogButtons::on_keydown(wxKeyEvent& e) {
        wxObject* current = e.GetEventObject();
        int key = e.GetKeyCode();
        int cnt = m_buttons.size();
        if(cnt > 1){
            int i = -1;
            for (Button* btn : m_buttons){
                i++;
                if(btn->HasFocus())
                    break;
            }
            // possible issue if button hidden
            if      (key == WXK_LEFT)  {m_buttons[i - 1 < 0 ? (cnt - 1) : i - 1]->SetFocus();}
            else if (key == WXK_RIGHT) {m_buttons[i + 1 > (cnt - 1) ? 0 : i + 1]->SetFocus();}
        }
        e.Skip();
}

}} // namespace Slic3r::GUI