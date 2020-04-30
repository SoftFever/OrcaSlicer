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

static std::map<Preset::Type, std::string> NameByType = {
    { Preset::TYPE_PRINT,           L("Print")     },
    { Preset::TYPE_FILAMENT,        L("Filament")  },
    { Preset::TYPE_SLA_MATERIAL,    L("Material")  },
    { Preset::TYPE_SLA_PRINT,       L("Print")     },
    { Preset::TYPE_PRINTER,         L("Printer")   }
};

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

void FoundOption::get_marked_label_and_tooltip(const char** label_, const char** tooltip_) const
{
    *label_   = marked_label.c_str();
    *tooltip_ = tooltip.c_str();
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

bool OptionsSearcher::search()
{
    return search(search_line, true);
}

bool OptionsSearcher::search(const std::string& search, bool force/* = false*/)
{
    if (search_line == search && !force)
        return false;

    found.clear();

    bool full_list = search.empty();
    wxString sep = " : ";

    auto get_label = [this, sep](const Option& opt)
    {
        wxString label;
        if (view_params.type)
            label += _(NameByType[opt.type]) + sep;
        if (view_params.category)
            label += opt.category_local + sep;
        if (view_params.group)
            label += opt.group_local + sep;
        label += opt.label_local;
        return label;
    };

    auto get_tooltip = [this, sep](const Option& opt)
    {
        return  _(NameByType[opt.type]) + sep +
                opt.category_local + sep +
                opt.group_local + sep + opt.label_local;
    };

    for (size_t i=0; i < options.size(); i++)
    {
        const Option &opt = options[i];
        if (full_list) {
            std::string label = into_u8(get_label(opt));
            found.emplace_back(FoundOption{ label, label, into_u8(get_tooltip(opt)), i, 0 });
            continue;
        }

        int score = 0;

        FMFlag fuzzy_match_flag = opt.fuzzy_match(search, score);
        if (fuzzy_match_flag != fmUndef)
        {
            wxString label = get_label(opt);

            if (     fuzzy_match_flag == fmLabel   ) label += "(" + opt.label    + ")";
            else if (fuzzy_match_flag == fmGroup   ) label += "(" + opt.group    + ")";
            else if (fuzzy_match_flag == fmCategory) label += "(" + opt.category + ")";
            else if (fuzzy_match_flag == fmOptKey  ) label += "(" + opt.opt_key  + ")";

            wxString marked_label = label;
            mark_string(marked_label, from_u8(search));
            clear_marked_string(marked_label);

            found.emplace_back(FoundOption{ into_u8(label), into_u8(marked_label), into_u8(get_tooltip(opt)), i, score });
        }
    }

    if (!full_list)
        sort_found();

    if (search_line != search)
        search_line = search;

    return true;
}

OptionsSearcher::OptionsSearcher()
{
    search_dialog = new SearchDialog(this);
}

OptionsSearcher::~OptionsSearcher()
{
    if (search_dialog)
        search_dialog->Destroy();
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
    if (n >= 0 && n < int(wxListBox::GetCount()))
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
//          SearchDialog
//------------------------------------------

SearchDialog::SearchDialog(OptionsSearcher* searcher)
    : GUI::DPIDialog(NULL, wxID_ANY, _L("Search"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    searcher(searcher)
{
    SetFont(GUI::wxGetApp().normal_font());
    wxColour bgr_clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    SetBackgroundColour(bgr_clr);

    default_string = _L("Type here to search");
    int border = 10;

    search_line = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize);

    // wxWANTS_CHARS style is neede for process Enter key press
    search_list = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(em_unit() * 40, em_unit() * 30), 0, NULL, wxWANTS_CHARS);

    wxBoxSizer* check_sizer = new wxBoxSizer(wxHORIZONTAL);

    check_type      = new wxCheckBox(this, wxID_ANY, _L("Type"));
    check_category  = new wxCheckBox(this, wxID_ANY, _L("Category"));
    check_group     = new wxCheckBox(this, wxID_ANY, _L("Group"));

    wxStdDialogButtonSizer* cancel_btn = this->CreateStdDialogButtonSizer(wxCANCEL);

    check_sizer->Add(check_type,     0, wxALIGN_CENTER_VERTICAL | wxRIGHT, border);
    check_sizer->Add(check_category, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, border);
    check_sizer->Add(check_group,    0, wxALIGN_CENTER_VERTICAL | wxRIGHT, border);
    check_sizer->AddStretchSpacer(border);
    check_sizer->Add(cancel_btn,     0, wxALIGN_CENTER_VERTICAL);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(search_line, 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(search_list, 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(check_sizer, 0, wxEXPAND | wxALL, border);

    search_line->Bind(wxEVT_TEXT,    &SearchDialog::OnInputText, this);
    search_line->Bind(wxEVT_LEFT_UP, &SearchDialog::OnLeftUpInTextCtrl, this);
    // process wxEVT_KEY_DOWN to navigate inside search_list, if ArrowUp/Down was pressed
    search_line->Bind(wxEVT_KEY_DOWN,&SearchDialog::OnKeyDown, this);

    search_list->Bind(wxEVT_MOTION,  &SearchDialog::OnMouseMove, this);
    search_list->Bind(wxEVT_LEFT_UP, &SearchDialog::OnMouseClick, this);
    search_list->Bind(wxEVT_KEY_DOWN,&SearchDialog::OnKeyDown, this);

    check_type    ->Bind(wxEVT_CHECKBOX, &SearchDialog::OnCheck, this);
    check_category->Bind(wxEVT_CHECKBOX, &SearchDialog::OnCheck, this);
    check_group   ->Bind(wxEVT_CHECKBOX, &SearchDialog::OnCheck, this);

    this->Bind(wxEVT_LISTBOX, &SearchDialog::OnSelect, this);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);
}

void SearchDialog::Popup(wxPoint position /*= wxDefaultPosition*/)
{
    const std::string& line = searcher->search_string();
    search_line->SetValue(line.empty() ? default_string : from_u8(line));
    search_line->SetFocus();
    search_line->SelectAll();

    update_list();

    const OptionViewParameters& params = searcher->view_params;
    check_type->SetValue(params.type);
    check_category->SetValue(params.category);
    check_group->SetValue(params.group);

    this->SetPosition(position);
    this->ShowModal();
}

void SearchDialog::ProcessSelection(int selection)
{
    if (selection < 0)
        return;

    GUI::wxGetApp().sidebar().jump_to_option(selection);
    this->EndModal(wxID_CLOSE);
}

void SearchDialog::OnInputText(wxCommandEvent&)
{
    search_line->SetInsertionPointEnd();

    wxString input_string = search_line->GetValue();
    if (input_string == default_string)
        input_string.Clear();

    searcher->search(into_u8(input_string));

    update_list();
}

void SearchDialog::OnLeftUpInTextCtrl(wxEvent& event)
{
    if (search_line->GetValue() == default_string)
        search_line->SetValue("");

    event.Skip();
}

void SearchDialog::OnMouseMove(wxMouseEvent& event)
{
    wxPoint pt = wxGetMousePosition() - search_list->GetScreenPosition();
    int selection = search_list->HitTest(pt);
    search_list->Select(selection);
}

void SearchDialog::OnMouseClick(wxMouseEvent&)
{
    int selection = search_list->GetSelection();
    search_list->SetSelection(wxNOT_FOUND);

    wxCommandEvent event(wxEVT_LISTBOX, search_list->GetId());
    event.SetInt(selection);
    event.SetEventObject(search_list);
    ProcessEvent(event);
}

void SearchDialog::OnSelect(wxCommandEvent& event)
{
    int selection = event.GetSelection();
    ProcessSelection(selection);
}

void SearchDialog::update_list()
{
    search_list->Clear();

    const std::vector<FoundOption>& filters = searcher->found_options();
    for (const FoundOption& item : filters)
        search_list->Append(from_u8(item.label));
}

void SearchDialog::OnKeyDown(wxKeyEvent& event)
{
    int key = event.GetKeyCode();

    // change selected item in the list
    if (key == WXK_UP || key == WXK_DOWN)
    {
        int selection = search_list->GetSelection();

        if (key == WXK_UP && selection > 0)
            selection--;
        if (key == WXK_DOWN && selection < int(search_list->GetCount() - 1))
            selection++;

        search_list->Select(selection);
        // This function could be called from search_line,
        // So, for the next correct navigation, set focus on the search_list
        search_list->SetFocus();
    }
    // process "Enter" pressed
    else if (key == WXK_NUMPAD_ENTER || key == WXK_RETURN)
        ProcessSelection(search_list->GetSelection());
    else
        event.Skip(); // !Needed to have EVT_CHAR generated as well
}

void SearchDialog::OnCheck(wxCommandEvent& event)
{
    OptionViewParameters& params = searcher->view_params;
    params.type     = check_type->GetValue();
    params.category = check_category->GetValue();
    params.group    = check_group->GetValue();

    searcher->search();
    update_list();
}

void SearchDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int& em = em_unit();

    msw_buttons_rescale(this, em, { wxID_CANCEL });

    const wxSize& size = wxSize(40 * em, 30 * em);
    SetMinSize(size);

    Fit();
    Refresh();
}


}

}    // namespace Slic3r::GUI
