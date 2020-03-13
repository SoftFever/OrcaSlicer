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

        std::string     opt_key;
        wxString        label;
        wxString        category;
        Preset::Type    type {Preset::TYPE_INVALID};
        // wxString     grope;

        bool containes(const wxString& search) const;
    };

    std::set<Option> options {};

    void clear() { options. clear(); }
    void append_options(DynamicPrintConfig* config, Preset::Type type, ConfigOptionMode mode);
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
