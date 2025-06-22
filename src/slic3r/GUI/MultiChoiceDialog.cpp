#include "MultiChoiceDialog.hpp"

#include "GUI_App.hpp"
#include "MainFrame.hpp"

namespace Slic3r { namespace GUI {

CheckList::CheckList(
    wxWindow* parent,
    const wxArrayString& choices,
    long scroll_style
)
    : wxWindow(parent, wxID_ANY)
    , m_cb_on (this, "check_on" , 18)
    , m_cb_off(this, "check_off", 18)
    , m_search(this, "search", 16)
    , m_menu(this, "menu", 18)
    , m_font(Label::Body_13)
    , m_first_load(true)
{
    Freeze();
    w_sizer = new wxBoxSizer(wxVERTICAL);

    f_sizer = new wxBoxSizer(wxHORIZONTAL);
    f_bar   = new wxPanel(this, wxID_ANY);
    f_bar->SetBackgroundColour(parent->GetBackgroundColour());
    m_filter_box = new TextInput(f_bar, "", "", "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    m_filter_box->SetIcon(m_search.bmp());
    m_filter_box->SetMinSize(FromDIP(wxSize(200,24)));
    m_filter_box->SetSize(FromDIP(wxSize(-1,24)));
    m_filter_box->SetFocus();
    m_filter_box->SetToolTip("Use ::sel to filter selected items \nUse ::nonsel to filter non selected items");
    m_filter_ctrl = m_filter_box->GetTextCtrl();
    m_filter_ctrl->SetFont(Label::Body_13);
    m_filter_ctrl->SetSize(wxSize(-1, FromDIP(16))); // Centers text vertically
    m_filter_ctrl->SetHint("Type to filter...");
    m_filter_ctrl->Bind(wxEVT_TEXT,       [this](auto &e) {Filter(m_filter_ctrl->GetValue());});
    m_filter_ctrl->Bind(wxEVT_TEXT_ENTER, [this](auto &e) {Filter(m_filter_ctrl->GetValue());});
    m_filter_ctrl->Bind(wxEVT_SET_FOCUS,  [this](auto &e) {Filter(m_filter_ctrl->GetValue());e.Skip();});
    m_filter_ctrl->Bind(wxEVT_KILL_FOCUS, [this](auto &e) {Filter(m_filter_ctrl->GetValue());e.Skip();});
    f_sizer->Add(m_filter_box, 1, wxEXPAND);

    fb_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto create_btn = [this] (wxString title, bool select){
        auto btn = new wxStaticText(f_bar, wxID_ANY, title);
        btn->SetForegroundColour("#009687");
        btn->SetCursor(wxCURSOR_HAND);
        btn->SetFont(Label::Body_13);
        btn->Bind(wxEVT_LEFT_DOWN, [this, select](wxMouseEvent &e) {SelectAll(select);});
        fb_sizer->Add(btn, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(10));
    };
    f_sizer->Add(fb_sizer,0 ,wxALIGN_CENTER_VERTICAL);
    create_btn("All" , true);
    create_btn("None", false);

    m_menu_button = new wxStaticBitmap(f_bar, wxID_ANY, m_menu.bmp());
    m_menu_button->SetCursor(wxCURSOR_HAND);
    m_menu_button->Bind(wxEVT_LEFT_DOWN, &CheckList::ShowMenu, this);
    f_sizer->Add(m_menu_button,0 ,wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(10));
    
    f_bar->SetSizerAndFit(f_sizer);
    w_sizer->Add(f_bar, 0, wxEXPAND);

    auto separator = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(1)));
    separator->SetBackgroundColour(parent->GetBackgroundColour());
    w_sizer->Add(separator, 0, wxEXPAND);

    s_sizer       = new wxBoxSizer(wxVERTICAL);
    m_scroll_area = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, scroll_style);
    m_scroll_area->SetScrollRate(0, 10);
    m_scroll_area->SetSizer(s_sizer);
    m_scroll_area->SetBackgroundColour(parent->GetBackgroundColour());
    m_scroll_area->Bind(wxEVT_RIGHT_DOWN, &CheckList::ShowMenu, this);

    m_no_items = new wxStaticText(m_scroll_area, wxID_ANY, "");
    m_no_items->SetFont(Label::Body_13);
    s_sizer->Add(m_no_items, 1, wxALIGN_CENTER_HORIZONTAL | wxALL, FromDIP(10));
    m_no_items->Hide();

    SetBackgroundColour(StateColor::darkModeColorFor("#DBDBDB")); // draws border on wxScrolledWindow

    w_sizer->Add(m_scroll_area, 1, wxEXPAND | wxALL, FromDIP(1)); // 1 for border
    s_sizer->Layout();

    SetFont(m_font);

    m_list_size = choices.size();

    m_checks.reserve(m_list_size);

    for (size_t i = 0; i < m_list_size; ++i){
        m_checks.emplace_back(new wxCheckBox(m_scroll_area, wxID_ANY, choices[i]));
        s_sizer->Add(m_checks[i], 0, wxTOP, FromDIP(4));
    }

    m_scroll_area->FitInside();
    s_sizer->Layout();

    SetSizer(w_sizer);
    Layout();
    Thaw();
};

void CheckList::SetSelections(wxArrayInt sel_array){
    if(!m_first_load){
        for (size_t i = 0; i < m_list_size; ++i)
            Check(i, false);
        m_first_load = false;
    }

    for (int i : sel_array)
        Check(i, true);
}

wxArrayInt CheckList::GetSelections()
{
    wxArrayInt checks;
    for (size_t i = 0; i < m_list_size; ++i)
        if (m_checks[i]->GetValue())
            checks.push_back(i);
    return checks;
}

void CheckList::Check(int i, bool checked)
{
    if (i > -1 && i < m_list_size)
        m_checks[i]->SetValue(checked);
}

void CheckList::SelectAll(bool value)
{
    for (size_t i = 0; i < m_list_size; ++i)
        Check(i, value);
}

void CheckList::SelectVisible(bool value)
{
    auto filter = m_filter_ctrl->GetValue().Lower();
    if((!value && filter == "::unsel") || (value && filter == "::sel"))
        m_filter_ctrl->SetValue("");
    for (size_t i = 0; i < m_list_size; ++i)
        if(m_checks[i]->IsShown())
            Check(i, value);
}

bool CheckList::IsChecked(int i)
{
    if (i > -1 && i < m_list_size)
        return false;
    return m_checks[i]->GetValue();
}

void CheckList::Filter(const wxString& filterText)
{
    Freeze();
    if(filterText.Lower() == "::sel"){
        if(m_filter_ctrl->GetValue().Lower() != "::sel"){ // not text input
            m_filter_ctrl->SetValue("::sel");
            m_filter_ctrl->SetSelection(0,-1);
        }
        fb_sizer->Show(false);
        for (auto& cb : m_checks) 
            cb->Show(cb->GetValue());
    }
    else if(filterText.Lower() == "::nonsel"){
        if(m_filter_ctrl->GetValue().Lower() != "::nonsel"){ // not text input
            m_filter_ctrl->SetValue("::nonsel");
            m_filter_ctrl->SetSelection(0,-1);
        }
        fb_sizer->Show(false);
        for (auto& cb : m_checks) 
            cb->Show(!cb->GetValue());
    }
    else{
        bool clear = filterText.IsEmpty();
        fb_sizer->Show(clear);
        for (auto& cb : m_checks) {
            bool show = clear || cb->GetLabel().Lower().Contains(filterText.Lower());
            cb->Show(show);
        }
    }
    int c_count = 0;
    for (const auto& child : m_scroll_area->GetChildren()) {
        if (child->GetId() == m_no_items->GetId())
            continue;
        if (child->IsShown())
            c_count++;
        if (c_count > 1){
            m_no_items->Hide();
            break;
        }
    }
    if (c_count == 0){
        if      (filterText.Lower() == "::sel")
            m_no_items->SetLabel("No selected items...");
        else if (filterText.Lower() == "::nonsel")
            m_no_items->SetLabel("All items selected...");
        else
            m_no_items->SetLabel("No matching items...");
        m_no_items->Show();
    }

    m_scroll_area->FitInside();
    f_sizer->Layout();
    Thaw();
}

void CheckList::ShowMenu(wxMouseEvent &evt)
{
    wxMenu m;
    bool filtering  = !m_filter_ctrl->GetValue().IsEmpty();
    bool list_empty = m_no_items->IsShown();
    m.Append(wxID_FILE1, "Select All"  )->Enable(!filtering);
    m.Append(wxID_FILE2, "Deselect All")->Enable(!filtering);
    m.AppendSeparator();
    m.Append(wxID_FILE3, "Select visible"  )->Enable(!list_empty && filtering);
    m.Append(wxID_FILE4, "Deselect visible")->Enable(!list_empty && filtering);
    m.AppendSeparator();
    m.Append(wxID_FILE5, "Filter selected"   );
    m.Append(wxID_FILE6, "Filter nonSelected");
    m.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { switch (e.GetId()){
        case wxID_FILE1: SelectAll(true)     ; break;
        case wxID_FILE2: SelectAll(false)    ; break;
        case wxID_FILE3: SelectVisible(true) ; break;
        case wxID_FILE4: SelectVisible(false); break;
        case wxID_FILE5: Filter("::sel")     ; break;
        case wxID_FILE6: Filter("::nonsel")  ; break;
        default: break;
    }},wxID_FILE1, wxID_FILE6);

    wxWindow* p = dynamic_cast<wxWindow*>(evt.GetEventObject());
    if     (p->GetId() == m_scroll_area->GetId())
        p->PopupMenu(&m, evt.GetPosition());
    else if(p->GetId() == m_menu_button->GetId()){ // use a static position
        /*
        wxClientDC dc(m_menu_button);
        dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

        int m_width = 0;
        for (const auto* item : m.GetMenuItems()){
            wxCoord w, h;
            dc.GetTextExtent(item->GetItemLabelText(), &w, &h);
            m_width = std::max(m_width, w);
        }

        m_width += 40; // this should be platform specific
        p->PopupMenu(&m, m_menu_button->GetPosition() - wxPoint(m_width,0));
        */
        p->PopupMenu(&m, evt.GetPosition());
    }
}

/*
void CheckList::SetSize(const wxSize& size)
{
    wxScrolledWindow::SetMinSize(size);
    wxScrolledWindow::SetMaxSize(size);
    m_sizer->Layout();
    FitInside();
}
*/

MultiChoiceDialog::MultiChoiceDialog(
    wxWindow*            parent,
    const wxString&      message,
    const wxString&      caption,
    const wxArrayString& choices
)
    : DPIDialog(parent ? parent : static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, caption, wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    SetBackgroundColour(*wxWHITE);

    wxBoxSizer* w_sizer = new wxBoxSizer(wxVERTICAL);

    if(!message.IsEmpty()){
        wxStaticText *msg = new wxStaticText(this, wxID_ANY, message);
        msg->SetFont(Label::Body_14);
        msg->Wrap(-1);
        w_sizer->Add(msg, 0, wxALL, FromDIP(10));
    }

    m_check_list = new CheckList(this, choices);
    m_check_list->SetSize(FromDIP(wxSize(300, 300)));
    m_check_list->SetMinSize(FromDIP(wxSize(300, 300)));

    w_sizer->Add(m_check_list, 1, wxRIGHT | wxLEFT | wxBOTTOM | wxEXPAND, FromDIP(10));

    auto dlg_btns = new DialogButtons(this, {"OK", "Cancel"});

    dlg_btns->GetOK()->Bind(    wxEVT_BUTTON, [this](wxCommandEvent &e) {EndModal(wxID_OK);});
    dlg_btns->GetCANCEL()->Bind(wxEVT_BUTTON, [this](wxCommandEvent &e) {EndModal(wxID_CANCEL);});

    w_sizer->Add(dlg_btns, 0, wxEXPAND);

    SetSizer(w_sizer);
    Layout();
    w_sizer->Fit(this);
    wxGetApp().UpdateDlgDarkUI(this);
}

wxArrayInt MultiChoiceDialog::GetSelections() const
{
    return m_check_list->GetSelections();
}

void MultiChoiceDialog::SetSelections(wxArrayInt sel_array)
{
    m_check_list->SetSelections(sel_array);
}

MultiChoiceDialog::~MultiChoiceDialog() {}

void MultiChoiceDialog::on_dpi_changed(const wxRect &suggested_rect) {}

}} // namespace Slic3r::GUI