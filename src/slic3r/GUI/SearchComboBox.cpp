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

using boost::optional;

namespace Slic3r {
namespace GUI {

bool SearchOptions::Option::containes(const wxString& search_) const
{
    char const* search_pattern = search_.utf8_str();
    char const* opt_key_str    = opt_key.c_str();
    char const* label_str      = label.utf8_str();

    return  fts::fuzzy_match_simple(search_pattern, label_str   )   ||
            fts::fuzzy_match_simple(search_pattern, opt_key_str )   ; 
}

bool SearchOptions::Option::is_matched_option(const wxString& search, int& outScore)
{
    char const* search_pattern = search.utf8_str();
    char const* opt_key_str    = opt_key.c_str();
    char const* label_str      = label.utf8_str();

    return (fts::fuzzy_match(search_pattern, label_str   , outScore)   ||
            fts::fuzzy_match(search_pattern, opt_key_str , outScore)   ); 
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

        options.emplace_back(Option{ label, opt_key, opt.category, type });
    }
}

void SearchOptions::apply_filters(const wxString& search)
{
    clear_filters();
    for (auto option : options) {
        int score;
        if (option.is_matched_option(search, score))
            filters.emplace_back(Filter{ option.label, score });
    }
    sort_filters();
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
        if (option.containes(search))
            append(option.label, (void*)&option);

    SuppressUpdate su(this);
    this->SetValue(search);
    this->SetInsertionPointEnd();
}

}}    // namespace Slic3r::GUI
