#ifndef slic3r_SearchComboBox_hpp_
#define slic3r_SearchComboBox_hpp_

#include <memory>
#include <vector>
#include <boost/filesystem/path.hpp>

#include <wx/bmpcbox.h>

#include "Preset.hpp"
#include "wxExtensions.hpp"


namespace Slic3r {

namespace GUI {

struct SearchInput
{
    DynamicPrintConfig* config  {nullptr};
    Preset::Type        type    {Preset::TYPE_INVALID};
    ConfigOptionMode    mode    {comSimple};
};

class SearchOptions
{
public:
    struct Option {
        bool operator<(const Option& other) const { return other.label > this->label; }
        bool operator>(const Option& other) const { return other.label < this->label; }

        wxString        label;
        std::string     opt_key;
        wxString        category;
        Preset::Type    type {Preset::TYPE_INVALID};
        // wxString     grope;

        bool containes(const wxString& search) const;
        bool is_matched_option(const wxString &search, int &outScore);
    };
    std::vector<Option> options {};

    struct Filter {
        wxString        label;
        int             outScore {0};
    };
    std::vector<Filter> filters {};

    void clear_options() { options.clear(); }
    void clear_filters() { filters.clear(); }
    void append_options(DynamicPrintConfig* config, Preset::Type type, ConfigOptionMode mode);
    void apply_filters(const wxString& search);

    void sort_options() {
        std::sort(options.begin(), options.end(), [](const Option& o1, const Option& o2) {
            return o1.label < o2.label; });
    }
    void sort_filters() {
        std::sort(filters.begin(), filters.end(), [](const Filter& f1, const Filter& f2) {
            return f1.outScore > f2.outScore; });
    };
};

class SearchComboBox : public wxBitmapComboBox
{
    class SuppressUpdate
    {
        SearchComboBox* m_cb;
    public:
        SuppressUpdate(SearchComboBox* cb) : 
                m_cb(cb)    { m_cb->prevent_update = true ; }
        ~SuppressUpdate()   { m_cb->prevent_update = false; }
    };                                                 

public:
    SearchComboBox(wxWindow *parent);
    ~SearchComboBox();

    int     append(const wxString& item, void* clientData)          { return Append(item, bmp.bmp(), clientData); }
    int     append(const wxString& item, wxClientData* clientData)  { return Append(item, bmp.bmp(), clientData); }
    
    void    append_all_items();
    void    append_items(const wxString& search);

    void    msw_rescale();

    void	init(DynamicPrintConfig* config, Preset::Type type, ConfigOptionMode mode);
    void    init(std::vector<SearchInput> input_values);
    void    update_combobox();

private:
    SearchOptions		search_list;
    wxString            default_search_line;
    wxString            search_line;

    int                 em_unit;
    bool                prevent_update {false};

    ScalableBitmap      bmp;
};

}}

#endif //slic3r_SearchComboBox_hpp_
