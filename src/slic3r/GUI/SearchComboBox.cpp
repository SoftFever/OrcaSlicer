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

using boost::optional;

namespace Slic3r {
namespace GUI {

bool SearchOptions::Option::containes(const wxString& search_) const
{
    wxString search = search_.Lower();
    wxString label_ = label.Lower();
    wxString category_ = category.Lower();

    return (opt_key.find(into_u8(search)) != std::string::npos ||
            label_.Find(search) != wxNOT_FOUND ||
            category_.Find(search) != wxNOT_FOUND);
/*    */

    auto search_str = into_u8(search);
    auto pos = opt_key.find(into_u8(search));
    bool in_opt_key = pos != std::string::npos;
    bool in_label = label_.Find(search) != wxNOT_FOUND;
    bool in_category = category_.Find(search) != wxNOT_FOUND;

    if (in_opt_key || in_label || in_category)
        return true;
    return false;
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

        options.emplace(Option{ opt_key, label, opt.category, type });
    }
}


SearchComboBox::SearchComboBox(wxWindow *parent) :
wxBitmapComboBox(parent, wxID_ANY, "", wxDefaultPosition, wxSize(25 * wxGetApp().em_unit(), -1)),
    em_unit(wxGetApp().em_unit())
{
    SetFont(wxGetApp().normal_font());
    default_search_line = search_line = _(L("Search through options")) + dots;
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
    }); 

    Bind(wxEVT_KILL_FOCUS, [this](wxEvent & e) {
        e.Skip();

        SuppressUpdate su(this);        
        this->SetValue(search_line.IsEmpty() ? default_search_line : search_line);
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
    search_list.clear();
    search_list.append_options(config, type, mode);

    update_combobox();
}

void SearchComboBox::init(std::vector<SearchInput> input_values)
{
    search_list.clear();

    for (auto i : input_values)
        search_list.append_options(i.config, i.type, i.mode);

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

    auto cmp = [](SearchOptions::Option* o1, SearchOptions::Option* o2) { return o1->label > o2->label; };
    std::set<SearchOptions::Option*, decltype(cmp)> ret(cmp);

    for (const SearchOptions::Option& option : search_list.options)
        if (option.containes(search))
            append(option.label, (void*)&option);

    this->Popup();
    SuppressUpdate su(this);
    this->SetValue(search);
    this->SetInsertionPointEnd();
}

}}    // namespace Slic3r::GUI
