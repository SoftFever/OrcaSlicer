#include "Search.hpp"

#include <cstddef>
#include <string>
#include <boost/optional.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "GUI_App.hpp"
#include "Tab.hpp"
#include "PresetBundle.hpp"

#define FTS_FUZZY_MATCH_IMPLEMENTATION
#include "fts_fuzzy_match.h"

#include "imgui/imconfig.h"

using boost::optional;

namespace Slic3r {

using GUI::from_u8;
using GUI::into_u8;

namespace Search {

FMFlag Option::fuzzy_match_simple(char const * search_pattern) const
{
    return  fts::fuzzy_match_simple(search_pattern, label_local.utf8_str())     ? fmLabelLocal      :
            fts::fuzzy_match_simple(search_pattern, group_local.utf8_str())     ? fmGroupLocal      :
            fts::fuzzy_match_simple(search_pattern, category_local.utf8_str())  ? fmCategoryLocal   :
            fts::fuzzy_match_simple(search_pattern, opt_key.c_str())            ? fmOptKey          :
            fts::fuzzy_match_simple(search_pattern, label.utf8_str())           ? fmLabel           :
            fts::fuzzy_match_simple(search_pattern, group.utf8_str())           ? fmGroup           :
            fts::fuzzy_match_simple(search_pattern, category.utf8_str())        ? fmCategory        : fmUndef   ;
}

FMFlag Option::fuzzy_match_simple(const wxString& search) const
{
    char const* search_pattern = search.utf8_str();
    return fuzzy_match_simple(search_pattern);
}

FMFlag Option::fuzzy_match_simple(const std::string& search) const
{
    char const* search_pattern = search.c_str();
    return fuzzy_match_simple(search_pattern);
}

FMFlag Option::fuzzy_match(char const* search_pattern, int& outScore) const
{
    FMFlag flag = fmUndef;
    int score;

    if (fts::fuzzy_match(search_pattern, label_local.utf8_str(),    score) && outScore < score) {
        outScore = score; flag = fmLabelLocal   ; }
    if (fts::fuzzy_match(search_pattern, group_local.utf8_str(),    score) && outScore < score) {
        outScore = score; flag = fmGroupLocal   ; }
    if (fts::fuzzy_match(search_pattern, category_local.utf8_str(), score) && outScore < score) {
        outScore = score; flag = fmCategoryLocal; }
    if (fts::fuzzy_match(search_pattern, opt_key.c_str(),           score) && outScore < score) {
        outScore = score; flag = fmOptKey       ; }
    if (fts::fuzzy_match(search_pattern, label.utf8_str(),          score) && outScore < score) {
        outScore = score; flag = fmLabel        ; }
    if (fts::fuzzy_match(search_pattern, group.utf8_str(),          score) && outScore < score) {
        outScore = score; flag = fmGroup        ; }
    if (fts::fuzzy_match(search_pattern, category.utf8_str(),       score) && outScore < score) {
        outScore = score; flag = fmCategory     ; }

    return flag;
}

FMFlag Option::fuzzy_match(const wxString& search, int& outScore) const
{
    char const* search_pattern = search.utf8_str();
    return fuzzy_match(search_pattern, outScore); 
}

FMFlag Option::fuzzy_match(const std::string& search, int& outScore) const
{
    char const* search_pattern = search.c_str();
    return fuzzy_match(search_pattern, outScore);
}

void FoundOption::get_label(const char** out_text) const
{
    *out_text = label.utf8_str();
}

void FoundOption::get_marked_label(const char** out_text) const
{
    *out_text = marked_label.utf8_str();
}

template<class T>
//void change_opt_key(std::string& opt_key, DynamicPrintConfig* config)
void change_opt_key(std::string& opt_key, DynamicPrintConfig* config, int& cnt)
{
    T* opt_cur = static_cast<T*>(config->option(opt_key));
    cnt = opt_cur->values.size();
    return;

    if (opt_cur->values.size() > 0)
        opt_key += "#" + std::to_string(0);
}

void OptionsSearcher::append_options(DynamicPrintConfig* config, Preset::Type type, ConfigOptionMode mode)
{
    auto emplace = [this, type](const std::string opt_key, const wxString& label)
    {
        const GroupAndCategory& gc = groups_and_categories[opt_key];
        if (gc.group.IsEmpty() || gc.category.IsEmpty())
            return;

        wxString suffix;
        if (gc.category == "Machine limits")
            suffix = opt_key.back()=='1' ? L("Stealth") : L("Normal");

        if (!label.IsEmpty())
            options.emplace_back(Option{ opt_key, type,
                                        label+ " " + suffix, _(label)+ " " + _(suffix),
                                        gc.group, _(gc.group),
                                        gc.category, _(gc.category) });
    };

    for (std::string opt_key : config->keys())
    {
        const ConfigOptionDef& opt = config->def()->options.at(opt_key);
        if (opt.mode > mode)
            continue;

        int cnt = 0;

        if ( (type == Preset::TYPE_SLA_MATERIAL || type == Preset::TYPE_PRINTER) && opt_key != "bed_shape")
            switch (config->option(opt_key)->type())
            {
            case coInts:	change_opt_key<ConfigOptionInts		>(opt_key, config, cnt);	break;
            case coBools:	change_opt_key<ConfigOptionBools	>(opt_key, config, cnt);	break;
            case coFloats:	change_opt_key<ConfigOptionFloats	>(opt_key, config, cnt);	break;
            case coStrings:	change_opt_key<ConfigOptionStrings	>(opt_key, config, cnt);	break;
            case coPercents:change_opt_key<ConfigOptionPercents	>(opt_key, config, cnt);	break;
            case coPoints:	change_opt_key<ConfigOptionPoints	>(opt_key, config, cnt);	break;
            default:		break;
            }

        wxString label = opt.full_label.empty() ? opt.label : opt.full_label;

        if (cnt == 0)
            emplace(opt_key, label);
        else
            for (int i = 0; i < cnt; ++i)
                emplace(opt_key + "#" + std::to_string(i), label);

        /*const GroupAndCategory& gc = groups_and_categories[opt_key];
        if (gc.group.IsEmpty() || gc.category.IsEmpty())
            continue;

        if (!label.IsEmpty())
            options.emplace_back(Option{opt_key, type,
                                        label, _(label),
                                        gc.group, _(gc.group),
                                        gc.category, _(gc.category) });*/
    }
}

// Wrap a string with ColorMarkerStart and ColorMarkerEnd symbols
static wxString wrap_string(const wxString& str)
{
    return wxString::Format("%c%s%c", ImGui::ColorMarkerStart, str, ImGui::ColorMarkerEnd);
}

// Mark a string using ColorMarkerStart and ColorMarkerEnd symbols
static void mark_string(wxString& str, const wxString& search_str)
{
    // Try to find whole search string
    if (str.Replace(search_str, wrap_string(search_str), false) != 0)
        return;

    // Try to find whole capitalized search string
    wxString search_str_capitalized = search_str.Capitalize();
    if (str.Replace(search_str_capitalized, wrap_string(search_str_capitalized), false) != 0)
        return;

    // if search string is just a one letter now, there is no reason to continue 
    if (search_str.Len()==1)
        return;

    // Split a search string for two strings (string without last letter and last letter)
    // and repeat a function with new search strings
    mark_string(str, search_str.SubString(0, search_str.Len() - 2));
    mark_string(str, search_str.Last());
}

// clear marked string from a redundant use of ColorMarkers
static void clear_marked_string(wxString& str)
{
    // Check if the string has a several ColorMarkerStart in a row and replace them to only one, if any
    wxString delete_string = wxString::Format("%c%c", ImGui::ColorMarkerStart, ImGui::ColorMarkerStart);
    str.Replace(delete_string, ImGui::ColorMarkerStart, true);
    // If there were several ColorMarkerStart in a row, it means there should be a several ColorMarkerStop in a row,
    // replace them to only one
    delete_string = wxString::Format("%c%c", ImGui::ColorMarkerEnd, ImGui::ColorMarkerEnd);
    str.Replace(delete_string, ImGui::ColorMarkerEnd, true);

    // And we should to remove redundant ColorMarkers, if they are in "End, Start" sequence in a row
    delete_string = wxString::Format("%c%c", ImGui::ColorMarkerEnd, ImGui::ColorMarkerStart);
    str.Replace(delete_string, wxEmptyString, true);
}

bool OptionsSearcher::search(const std::string& search, bool force/* = false*/)
{
    if (search_line == search && !force)
        return false;

    found.clear();

    bool full_list = search.empty();

    auto get_label = [this](const Option& opt)
    {
        wxString label;
        if (category)
            label += opt.category_local + " > ";
        if (group)
            label += opt.group_local + " > ";
        label += opt.label_local;
        return label;
    };

    for (size_t i=0; i < options.size(); i++)
    {
        const Option &opt = options[i];
        if (full_list) {
            wxString label = get_label(opt);
            found.emplace_back(FoundOption{ label, label, i, 0 });
            continue;
        }

        int score = 0;

        FMFlag fuzzy_match_flag = opt.fuzzy_match(search, score);
        if (fuzzy_match_flag != fmUndef)
        {
            wxString label = get_label(opt); //opt.category_local + " > " + opt.group_local  + " > " + opt.label_local;

            if (     fuzzy_match_flag == fmLabel   ) label += "(" + opt.label    + ")";
            else if (fuzzy_match_flag == fmGroup   ) label += "(" + opt.group    + ")";
            else if (fuzzy_match_flag == fmCategory) label += "(" + opt.category + ")";
            else if (fuzzy_match_flag == fmOptKey  ) label += "(" + opt.opt_key  + ")";

            wxString marked_label = label;
            mark_string(marked_label, from_u8(search));
            clear_marked_string(marked_label);

            found.emplace_back(FoundOption{ label, marked_label, i, score });
        }
    }

    if (!full_list)
        sort_found();

    search_line = search;
    return true;
}

void OptionsSearcher::init(std::vector<InputInfo> input_values)
{
    options.clear();
    for (auto i : input_values)
        append_options(i.config, i.type, i.mode);
    sort_options();

    search(search_line, true);
}

void OptionsSearcher::apply(DynamicPrintConfig* config, Preset::Type type, ConfigOptionMode mode)
{
    if (options.empty())
        return;

    options.erase(std::remove_if(options.begin(), options.end(), [type](Option opt) {
            return opt.type == type;
        }), options.end());

    append_options(config, type, mode);

    sort_options();

    search(search_line, true);
}

const Option& OptionsSearcher::get_option(size_t pos_in_filter) const
{
    assert(pos_in_filter != size_t(-1) && found[pos_in_filter].option_idx != size_t(-1));
    return options[found[pos_in_filter].option_idx];
}

void OptionsSearcher::add_key(const std::string& opt_key, const wxString& group, const wxString& category)
{
    groups_and_categories[opt_key] = GroupAndCategory{group, category};
}


//------------------------------------------
//          SearchComboPopup
//------------------------------------------


void SearchComboPopup::Init()
{
    this->Bind(wxEVT_MOTION,    &SearchComboPopup::OnMouseMove,     this);
    this->Bind(wxEVT_LEFT_UP,   &SearchComboPopup::OnMouseClick,    this);
    this->Bind(wxEVT_KEY_DOWN,  &SearchComboPopup::OnKeyDown,       this);
}

bool SearchComboPopup::Create(wxWindow* parent)
{
    return wxListBox::Create(parent, 1, wxPoint(0, 0), wxDefaultSize);
}

void SearchComboPopup::SetStringValue(const wxString& s)
{
    int n = wxListBox::FindString(s);
    if (n >= 0 && n < wxListBox::GetCount())
        wxListBox::Select(n);

    // save a combo control's string
    m_input_string = s;
}

void SearchComboPopup::ProcessSelection(int selection) 
{
    wxCommandEvent event(wxEVT_LISTBOX, GetId());
    event.SetInt(selection);
    event.SetEventObject(this);
    ProcessEvent(event);

    Dismiss();
}

void SearchComboPopup::OnMouseMove(wxMouseEvent& event)
{
    wxPoint pt = wxGetMousePosition() - this->GetScreenPosition();
    int selection = this->HitTest(pt);
    wxListBox::Select(selection);
}

void SearchComboPopup::OnMouseClick(wxMouseEvent&)
{
    int selection = wxListBox::GetSelection();
    SetSelection(wxNOT_FOUND);
    ProcessSelection(selection);
}

void SearchComboPopup::OnKeyDown(wxKeyEvent& event)
{
    int key = event.GetKeyCode();

    // change selected item in the list
    if (key == WXK_UP || key == WXK_DOWN)
    {
        int selection = wxListBox::GetSelection();

        if (key == WXK_UP && selection > 0)
            selection--;
        if (key == WXK_DOWN && selection < int(wxListBox::GetCount() - 1))
            selection++;

        wxListBox::Select(selection);
    }
    // send wxEVT_LISTBOX event if "Enter" was pushed
    else if (key == WXK_NUMPAD_ENTER || key == WXK_RETURN)
        ProcessSelection(wxListBox::GetSelection());
    else
        event.Skip(); // !Needed to have EVT_CHAR generated as well
}

//------------------------------------------
//          SearchCtrl
//------------------------------------------

SearchCtrl::SearchCtrl(wxWindow* parent) :
    wxComboCtrl(parent, wxID_ANY, _L("Type here to search"), wxDefaultPosition, wxSize(25 * GUI::wxGetApp().em_unit(), -1), wxTE_PROCESS_ENTER)
{
    default_string = _L("Type here to search");

    this->UseAltPopupWindow();

    wxBitmap bmp_norm = create_scaled_bitmap("search_gray");
    wxBitmap bmp_hov = create_scaled_bitmap("search");
    this->SetButtonBitmaps(bmp_norm, true, bmp_hov, bmp_hov, bmp_norm);

    popupListBox = new SearchComboPopup();

    // It is important to call SetPopupControl() as soon as possible
    this->SetPopupControl(popupListBox);

    this->Bind(wxEVT_TEXT,                 &SearchCtrl::OnInputText, this);
    this->Bind(wxEVT_TEXT_ENTER,           &SearchCtrl::PopupList, this);
    this->Bind(wxEVT_COMBOBOX_DROPDOWN,    &SearchCtrl::PopupList, this);

    this->GetTextCtrl()->Bind(wxEVT_LEFT_UP,    &SearchCtrl::OnLeftUpInTextCtrl, this);
    popupListBox->Bind(wxEVT_LISTBOX,           &SearchCtrl::OnSelect,           this);
}

void SearchCtrl::OnInputText(wxCommandEvent& )
{
    if (prevent_update)
        return;

    this->GetTextCtrl()->SetInsertionPointEnd();

    wxString input_string = this->GetValue();
    if (input_string == default_string)
        input_string.Clear();

    GUI::wxGetApp().sidebar().get_search_line() = into_u8(input_string);

    editing = true;
    GUI::wxGetApp().sidebar().search_and_apply_tab_search_lines();
    editing = false;
}

void SearchCtrl::PopupList(wxCommandEvent& e)
{
    update_list(GUI::wxGetApp().sidebar().get_searcher().found_options());
    if (e.GetEventType() == wxEVT_TEXT_ENTER)
        this->Popup();

    e.Skip();
}

void SearchCtrl::set_search_line(const std::string& line)
{
    prevent_update = true;
    this->SetValue(line.empty() && !editing ? default_string : from_u8(line));
    prevent_update = false;
}

void SearchCtrl::msw_rescale()
{
    wxSize size = wxSize(25 * GUI::wxGetApp().em_unit(), -1);
    // Set rescaled min height to correct layout
    this->SetMinSize(size);

    wxBitmap bmp_norm = create_scaled_bitmap("search_gray");
    wxBitmap bmp_hov = create_scaled_bitmap("search");
    this->SetButtonBitmaps(bmp_norm, true, bmp_hov, bmp_hov, bmp_norm);
}

void SearchCtrl::OnSelect(wxCommandEvent& event)
{
    int selection = event.GetSelection();
    if (selection < 0)
        return;

    prevent_update = true;
    GUI::wxGetApp().sidebar().jump_to_option(selection);
    prevent_update = false;
}

void SearchCtrl::update_list(const std::vector<FoundOption>& filters)
{
    if (!filters.empty() && popupListBox->GetCount() == filters.size() &&
        popupListBox->GetString(0) == filters[0].label &&
        popupListBox->GetString(popupListBox->GetCount()-1) == filters[filters.size()-1].label)
        return;

    popupListBox->Clear();
    for (const FoundOption& item : filters)
        popupListBox->Append(item.label);
}

void SearchCtrl::OnLeftUpInTextCtrl(wxEvent &event)
{
    if (this->GetValue() == default_string)
        this->SetValue("");

    event.Skip();
}


//------------------------------------------
//          PopupSearchList
//------------------------------------------

PopupSearchList::PopupSearchList(wxWindow* parent) :
    wxPopupTransientWindow(parent, /*wxSTAY_ON_TOP*/wxWANTS_CHARS | wxBORDER_NONE)
{
    panel = new wxPanel(this, wxID_ANY);

    text = new wxTextCtrl(panel, 1);
    list = new wxListBox(panel, 2, wxDefaultPosition, wxSize(GUI::wxGetApp().em_unit() * 40, -1));
    check = new wxCheckBox(panel, 3, "Group");

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    text->Bind(wxEVT_MOUSE_CAPTURE_CHANGED, [](wxEvent& e) {
        int i = 0; });

//    text->Bind(wxEVT_LEFT_DOWN, [this](wxEvent& e) {
    text->Bind(wxEVT_LEFT_UP, [this](wxEvent& e) {
        text->SetValue("mrrrrrty");
    });

    text->Bind(wxEVT_MOTION, [this](wxMouseEvent& evt)
    {
        wxPoint pt = wxGetMousePosition() - text->GetScreenPosition();
        long pos;
        text->HitTest(pt, &pos);

        if (pos == wxTE_HT_UNKNOWN)
            return;

        list->SetSelection(wxNOT_FOUND);
        text->SetSelection(0, pos);
    });

    text->Bind(wxEVT_TEXT, [this](wxCommandEvent& e)
    {
        text->SetSelection(0, 3);
    });

    this->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        int key = event.GetKeyCode();

        // change selected item in the list
        if (key == WXK_UP || key == WXK_DOWN)
        {
            int selection = list->GetSelection();

            if (key == WXK_UP && selection > 0)
                selection--;
            if (key == WXK_DOWN && selection < int(list->GetCount() - 1))
                selection++;

            list->Select(selection);
        }
        else
            event.Skip(); // !Needed to have EVT_CHAR generated as well
    });

    this->Bind(wxEVT_CHAR, [this](wxKeyEvent& e) {
        int key = e.GetKeyCode();
        wxChar symbol = e.GetUnicodeKey();
        search_str += symbol;

        text->SetValue(search_str);
    });


    list->Append("One");
    list->Append("Two");
    list->Append("Three");

    list->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& evt)
        {
            int selection = list->GetSelection();
        });

    list->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& evt)
    {
        int selection = list->GetSelection();
        list->SetSelection(wxNOT_FOUND);

        wxCommandEvent event(wxEVT_LISTBOX, list->GetId());
        event.SetInt(selection);
        event.SetEventObject(this);
        ProcessEvent(event);

        Dismiss();
    });

    list->Bind(wxEVT_MOTION, [this](wxMouseEvent& evt)
    {
        wxPoint pt = wxGetMousePosition() - list->GetScreenPosition();
        int selection = list->HitTest(pt);
        list->Select(selection);
    });

    list->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        int key = event.GetKeyCode();

        // change selected item in the list
        if (key == WXK_UP || key == WXK_DOWN)
        {
            int selection = list->GetSelection();

            if (key == WXK_UP && selection > 0)
                selection--;
            if (key == WXK_DOWN && selection < int(list->GetCount() - 1))
                selection++;

            list->Select(selection);
        }
        // send wxEVT_LISTBOX event if "Enter" was pushed
        else if (key == WXK_NUMPAD_ENTER || key == WXK_RETURN)
        {
            int selection = list->GetSelection();

            wxCommandEvent event(wxEVT_LISTBOX, list->GetId());
            event.SetInt(selection);
            event.SetEventObject(this);
            ProcessEvent(event);

            Dismiss();
        }
        else
            event.Skip(); // !Needed to have EVT_CHAR generated as well
    });

    topSizer->Add(text, 0, wxEXPAND | wxALL, 2);
    topSizer->Add(list, 0, wxEXPAND | wxALL, 2);
    topSizer->Add(check, 0, wxEXPAND | wxALL, 2);

    panel->SetSizer(topSizer);

    topSizer->Fit(panel);
    SetClientSize(panel->GetSize());
}

void PopupSearchList::Popup(wxWindow* WXUNUSED(focus))
{
    wxPopupTransientWindow::Popup();
}

void PopupSearchList::OnDismiss()
{
    wxPopupTransientWindow::OnDismiss();
}

bool PopupSearchList::ProcessLeftDown(wxMouseEvent& event)
{
    return wxPopupTransientWindow::ProcessLeftDown(event);
}
bool PopupSearchList::Show(bool show)
{
    return wxPopupTransientWindow::Show(show);
}

void PopupSearchList::OnSize(wxSizeEvent& event)
{
    event.Skip();
}

void PopupSearchList::OnSetFocus(wxFocusEvent& event)
{
    event.Skip();
}

void PopupSearchList::OnKillFocus(wxFocusEvent& event)
{
    event.Skip();
}


//------------------------------------------
//          SearchCtrl
//------------------------------------------

SearchButton::SearchButton(wxWindow* parent) :
    ScalableButton(parent, wxID_ANY, "search")
{
    popup_win = new PopupSearchList(parent);
    this->Bind(wxEVT_BUTTON, &SearchButton::PopupSearch, this);
}
    
void SearchButton::PopupSearch(wxCommandEvent& e)
{
//    popup_win->update_list(wxGetApp().sidebar().get_search_list().filters);
    wxPoint pos = this->ClientToScreen(wxPoint(0, 0));
    wxSize sz = wxSize(GUI::wxGetApp().em_unit()*40, -1);
    pos.x -= sz.GetWidth();
    pos.y += this->GetSize().y;
    popup_win->Position(pos, sz);
    popup_win->Popup();
    e.Skip();
}


}

}    // namespace Slic3r::GUI
