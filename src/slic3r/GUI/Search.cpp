#include "Search.hpp"

#include <cstddef>
#include <algorithm>
#include <numeric>
#include <vector>
#include <string>
#include <regex>
#include <future>
#include <boost/algorithm/string.hpp>
#include <boost/optional.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>

//#include <wx/bmpcbox.h>
#include "libslic3r/PrintConfig.hpp"
#include "GUI_App.hpp"
#include "Tab.hpp"
#include "PresetBundle.hpp"

#define FTS_FUZZY_MATCH_IMPLEMENTATION
#include "fts_fuzzy_match.h"

#include "imgui/imconfig.h"

using boost::optional;

namespace Slic3r {
namespace GUI {

bool SearchOptions::Option::fuzzy_match_simple(char const * search_pattern) const
{
    char const* opt_key_str    = opt_key.c_str();
    char const* label_str      = label.utf8_str();

    return  fts::fuzzy_match_simple(search_pattern, label_str   )   ||
            fts::fuzzy_match_simple(search_pattern, opt_key_str )   ; 
}

bool SearchOptions::Option::fuzzy_match_simple(const wxString& search) const
{
    char const* search_pattern = search.utf8_str();
    return fuzzy_match_simple(search_pattern);
}

bool SearchOptions::Option::fuzzy_match_simple(const std::string& search) const
{
    char const* search_pattern = search.c_str();
    return fuzzy_match_simple(search_pattern);
}

bool SearchOptions::Option::fuzzy_match(char const* search_pattern, int& outScore)
{
    char const* opt_key_str    = opt_key.c_str();
    char const* label_str      = label.utf8_str();

    return (fts::fuzzy_match(search_pattern, label_str   , outScore)   ||
            fts::fuzzy_match(search_pattern, opt_key_str , outScore)   ); 
}

bool SearchOptions::Option::fuzzy_match(const wxString& search, int& outScore)
{
    char const* search_pattern = search.utf8_str();
    return fuzzy_match(search_pattern, outScore); 
}

bool SearchOptions::Option::fuzzy_match(const std::string& search, int& outScore)
{
    char const* search_pattern = search.c_str();
    return fuzzy_match(search_pattern, outScore);
}

void SearchOptions::Filter::get_label(const char** out_text) const
{
    *out_text = label.utf8_str();
}

void SearchOptions::Filter::get_marked_label(const char** out_text) const
{
    *out_text = marked_label.utf8_str();
}

template<class T>
void change_opt_key(std::string& opt_key, DynamicPrintConfig* config)
{
    T* opt_cur = static_cast<T*>(config->option(opt_key));
    if (opt_cur->values.size() > 0)
        opt_key += "#" + std::to_string(0);
}

void SearchOptions::append_options(DynamicPrintConfig* config, Preset::Type type, ConfigOptionMode mode)
{
    for (std::string opt_key : config->keys())
    {
        const ConfigOptionDef& opt = config->def()->options.at(opt_key);
        if (opt.mode > mode)
            continue;

        if (type == Preset::TYPE_SLA_MATERIAL || type == Preset::TYPE_PRINTER)
            switch (config->option(opt_key)->type())
            {
            case coInts:	change_opt_key<ConfigOptionInts		>(opt_key, config);	break;
            case coBools:	change_opt_key<ConfigOptionBools	>(opt_key, config);	break;
            case coFloats:	change_opt_key<ConfigOptionFloats	>(opt_key, config);	break;
            case coStrings:	change_opt_key<ConfigOptionStrings	>(opt_key, config);	break;
            case coPercents:change_opt_key<ConfigOptionPercents	>(opt_key, config);	break;
            case coPoints:	change_opt_key<ConfigOptionPoints	>(opt_key, config);	break;
            default:		break;
            }

        wxString label;
        if (!opt.category.empty())
            label += _(opt.category) + " : ";
        label += _(opt.full_label.empty() ? opt.label : opt.full_label);

        if (!label.IsEmpty())
            options.emplace_back(Option{ label, opt_key, opt.category, type });
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

bool SearchOptions::apply_filters(const std::string& search, bool force/* = false*/)
{
    if (search_line == search && !force)
        return false;

    clear_filters();

    bool full_list = search.empty();

    for (size_t i=0; i < options.size(); i++)
    {
        if (full_list) {
            filters.emplace_back(Filter{ options[i].label, options[i].label, i, 0 });
            continue;
        }

        int score = 0;
        if (options[i].fuzzy_match_simple(search)/*fuzzy_match(search, score)*/)
        {
            wxString marked_label = options[i].label;
            mark_string(marked_label, from_u8(search));
            clear_marked_string(marked_label);

            filters.emplace_back(Filter{ options[i].label, marked_label, i, score });
        }
    }

    if (!full_list)
        sort_filters();

    search_line = search;
    return true;
}

void SearchOptions::init(std::vector<SearchInput> input_values)
{
    clear_options();
    for (auto i : input_values)
        append_options(i.config, i.type, i.mode);
    sort_options();

    apply_filters(search_line, true);
}

const SearchOptions::Option& SearchOptions::get_option(size_t pos_in_filter) const
{
    assert(pos_in_filter != size_t(-1) && filters[pos_in_filter].option_idx != size_t(-1));
    return options[filters[pos_in_filter].option_idx];
}


//------------------------------------------
//          SearchCtrl
//------------------------------------------

SearchCtrl::SearchCtrl(wxWindow* parent) :
    wxComboCtrl(parent, wxID_ANY, _L("Type here to search"), wxDefaultPosition, wxSize(25 * wxGetApp().em_unit(), -1), wxTE_PROCESS_ENTER)
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

    wxGetApp().sidebar().get_search_line() = into_u8(input_string);

    editing = true;
    wxGetApp().sidebar().apply_search_filter();
    editing = false;
}

void SearchCtrl::PopupList(wxCommandEvent& e)
{
    update_list(wxGetApp().sidebar().get_search_list().filters);
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
    wxSize size = wxSize(25 * wxGetApp().em_unit(), -1);
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
    wxGetApp().sidebar().jump_to_option(selection);
    prevent_update = false;
}

void SearchCtrl::update_list(std::vector<SearchOptions::Filter>& filters)
{
    if (popupListBox->GetCount() == filters.size() &&
        popupListBox->GetString(0) == filters[0].label &&
        popupListBox->GetString(popupListBox->GetCount()-1) == filters[filters.size()-1].label)
        return;

    popupListBox->Clear();
    for (const SearchOptions::Filter& item : filters)
        popupListBox->Append(item.label);
}

void SearchCtrl::OnLeftUpInTextCtrl(wxEvent &event)
{
    if (this->GetValue() == default_string)
        this->SetValue("");

    event.Skip();
}

}}    // namespace Slic3r::GUI
