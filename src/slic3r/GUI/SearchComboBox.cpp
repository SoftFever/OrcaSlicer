#include "SearchComboBox.hpp"

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

#include <wx/sizer.h>
#include <wx/bmpcbox.h>
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
    if (str.Replace(delete_string, ImGui::ColorMarkerStart, true) != 0) {
        // If there were several ColorMarkerStart in a row, it means there should be a several ColorMarkerStop in a row,
        // replace them to only one
        delete_string = wxString::Format("%c%c", ImGui::ColorMarkerEnd, ImGui::ColorMarkerEnd);
        str.Replace(delete_string, ImGui::ColorMarkerEnd, true);
    }

    // And we should to remove redundant ColorMarkers, if they are in "End, Start" sequence in a row
    delete_string = wxString::Format("%c%c", ImGui::ColorMarkerEnd, ImGui::ColorMarkerStart);
    str.Replace(delete_string, wxEmptyString, true);
}

void SearchOptions::apply_filters(const std::string& search)
{
    clear_filters();

    bool full_list = search.empty();

    for (size_t i=0; i < options.size(); i++)
    {
        if (full_list) {
            filters.emplace_back(Filter{ options[i].label, i, 0 });
            continue;
        }

        int score = 0;
        if (options[i].fuzzy_match_simple(search)/*fuzzy_match(search, score)*/)
        {
            wxString label = options[i].label;
            mark_string(label, from_u8(search));
            clear_marked_string(label);

            filters.emplace_back(Filter{ label, i, score });
        }
    }

    if (!full_list)
        sort_filters();
}

void SearchOptions::init(std::vector<SearchInput> input_values)
{
    clear_options();
    for (auto i : input_values)
        append_options(i.config, i.type, i.mode);
    sort_options();

    apply_filters("");
}

const SearchOptions::Option& SearchOptions::get_option(size_t pos_in_filter) const
{
    assert(pos_in_filter != size_t(-1) && filters[pos_in_filter].option_idx != size_t(-1));
    return options[filters[pos_in_filter].option_idx];
}

SearchComboBox::SearchComboBox(wxWindow *parent) :
wxBitmapComboBox(parent, wxID_ANY, _(L("Type here to search")) + dots, wxDefaultPosition, wxSize(25 * wxGetApp().em_unit(), -1)),
    em_unit(wxGetApp().em_unit())
{
    SetFont(wxGetApp().normal_font());
    default_search_line = search_line = _(L("Type here to search")) + dots;
    bmp = ScalableBitmap(this, "search");

#ifdef _WIN32
    // Workaround for ignoring CBN_EDITCHANGE events, which are processed after the content of the combo box changes, so that
    // the index of the item inside CBN_EDITCHANGE may no more be valid.
//    EnableTextChangedEvents(false);
#endif /* _WIN32 */

    Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &evt) {
        auto selected_item = this->GetSelection();
        SearchOptions::Option* opt = reinterpret_cast<SearchOptions::Option*>(this->GetClientData(selected_item));
        wxGetApp().get_tab(opt->type)->activate_option(opt->opt_key, opt->category);

        evt.StopPropagation();

        SuppressUpdate su(this);
        this->SetValue(search_line);
    });

    Bind(wxEVT_TEXT, [this](wxCommandEvent &e) {
        if (prevent_update)
            return;

        if (this->IsTextEmpty())
        {
            return;
        }

        if (search_line != this->GetValue()) {
            update_combobox();
            search_line = this->GetValue();
        }

        e.Skip();
    });
}

SearchComboBox::~SearchComboBox()
{
}

void SearchComboBox::msw_rescale()
{
    em_unit = wxGetApp().em_unit();

    wxSize size = wxSize(25 * em_unit, -1);

    // Set rescaled min height to correct layout
    this->SetMinSize(size);
    // Set rescaled size
    this->SetSize(size);

    update_combobox();
}

void SearchComboBox::init(DynamicPrintConfig* config, Preset::Type type, ConfigOptionMode mode)
{
    search_list.clear_options();
    search_list.append_options(config, type, mode);
    search_list.sort_options();

    update_combobox();
}

void SearchComboBox::init(std::vector<SearchInput> input_values)
{
    search_list.clear_options();
    for (auto i : input_values)
        search_list.append_options(i.config, i.type, i.mode);
    search_list.sort_options();

    update_combobox();
}

void SearchComboBox::init(const SearchOptions& new_search_list)
{
    search_list = new_search_list;

    update_combobox();
}

void SearchComboBox::update_combobox()
{
    wxString search_str = this->GetValue();
    if (search_str.IsEmpty() || search_str == default_search_line)
        // add whole options list to the controll
        append_all_items();
    else
        append_items(search_str);
}

void SearchComboBox::append_all_items()
{
    this->Clear();
    for (const SearchOptions::Option& item : search_list.options)
        if (!item.label.IsEmpty())
            append(item.label, (void*)&item);

    SuppressUpdate su(this);
    this->SetValue(default_search_line);
}

void SearchComboBox::append_items(const wxString& search)
{
    this->Clear();
/*
    search_list.apply_filters(search);
    for (auto filter : search_list.filters) {
        auto it = std::lower_bound(search_list.options.begin(), search_list.options.end(), SearchOptions::Option{filter.label});
        if (it != search_list.options.end())
            append(it->label, (void*)(&(*it)));
    }
*/

    for (const SearchOptions::Option& option : search_list.options)
        if (option.fuzzy_match_simple(search))
            append(option.label, (void*)&option);

    SuppressUpdate su(this);
    this->SetValue(search);
    this->SetInsertionPointEnd();
}

}}    // namespace Slic3r::GUI
