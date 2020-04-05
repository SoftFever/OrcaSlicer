#ifndef slic3r_SearchComboBox_hpp_
#define slic3r_SearchComboBox_hpp_

#include <vector>

#include <wx/panel.h>
#include <wx/sizer.h>
//#include <wx/bmpcbox.h>
#include <wx/popupwin.h>
#include <wx/listctrl.h>

#include <wx/combo.h>

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

        bool fuzzy_match_simple(char const *search_pattern) const;
        bool fuzzy_match_simple(const wxString& search) const;
        bool fuzzy_match_simple(const std::string &search) const;
        bool fuzzy_match(char const *search_pattern, int &outScore);
        bool fuzzy_match(const wxString &search, int &outScore);
        bool fuzzy_match(const std::string &search, int &outScore);
    };
    std::vector<Option> options {};

    struct Filter {
        wxString        label;
        wxString        marked_label;
        size_t          option_idx {0};
        int             outScore {0};

        void get_label(const char** out_text) const;
        void get_marked_label(const char** out_text) const;
    };
    std::vector<Filter> filters {};

    void clear_options() { options.clear(); }
    void clear_filters() { filters.clear(); }

    void init(std::vector<SearchInput> input_values);

    void append_options(DynamicPrintConfig* config, Preset::Type type, ConfigOptionMode mode);
    void apply_filters(const std::string& search);

    void sort_options() {
        std::sort(options.begin(), options.end(), [](const Option& o1, const Option& o2) {
            return o1.label < o2.label; });
    }
    void sort_filters() {
        std::sort(filters.begin(), filters.end(), [](const Filter& f1, const Filter& f2) {
            return f1.outScore > f2.outScore; });
    };

    size_t options_size() const { return options.size(); }
    size_t filters_size() const { return filters.size(); }
    size_t size() const         { return filters_size(); }

    const Filter& operator[](const size_t pos) const noexcept { return filters[pos]; }
    const Option& get_option(size_t pos_in_filter) const;
};
/*
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
    SearchComboBox(wxWindow *parent, SearchOptions& search_list);
    ~SearchComboBox();

    int     append(const wxString& item)                            { return Append(item, bmp.bmp()); }
    int     append(const wxString& item, void* clientData)          { return Append(item, bmp.bmp(), clientData); }
    int     append(const wxString& item, wxClientData* clientData)  { return Append(item, bmp.bmp(), clientData); }
    
    void    append_all_items();
    void    append_items(const wxString& search);

    void    msw_rescale();

    void	init(DynamicPrintConfig* config, Preset::Type type, ConfigOptionMode mode);
    void    init(std::vector<SearchInput> input_values);
    void    init(const SearchOptions& new_search_list);
    void    update_combobox();


private:
    SearchOptions&		search_list;
    wxString            default_search_line;
    wxString            search_line;

    int                 em_unit;
    bool                prevent_update {false};

    ScalableBitmap      bmp;
};
*/
class PopupSearchList : public wxPopupTransientWindow
{
public:
    PopupSearchList(wxWindow* parent);
    ~PopupSearchList() {}

    // wxPopupTransientWindow virtual methods are all overridden to log them
    void Popup(wxWindow* focus = NULL) wxOVERRIDE;
    void OnDismiss() wxOVERRIDE;
    bool ProcessLeftDown(wxMouseEvent& event) wxOVERRIDE;
    bool Show(bool show = true) wxOVERRIDE;

    void update_list(std::vector<SearchOptions::Filter>& filters);

private:
    wxWindow*   panel;
    wxListCtrl* search_ctrl{ nullptr };

    void OnSize(wxSizeEvent& event);
    void OnSetFocus(wxFocusEvent& event);
    void OnKillFocus(wxFocusEvent& event);
    void OnSelect(wxListEvent& event);
};





class SearchComboPopup : public wxListBox, public wxComboPopup
{
public:
    // Initialize member variables
    virtual void Init(){}

    // Create popup control
    virtual bool Create(wxWindow* parent)
    {
        return wxListBox::Create(parent, 1, wxPoint(0, 0), wxDefaultSize);
    }
    // Return pointer to the created control
    virtual wxWindow* GetControl() { return this; }
    // Translate string into a list selection
    virtual void SetStringValue(const wxString& s)
    {
        int n = wxListBox::FindString(s);
        if (n >= 0 && n < wxListBox::GetCount())
            wxListBox::Select(n);

        // save a combo control's string
        m_input_string = s;
    }
    // Get list selection as a string
    virtual wxString GetStringValue() const
    {
        // we shouldn't change a combo control's string
        return m_input_string;
    }
    // Do mouse hot-tracking (which is typical in list popups)
    void OnMouseMove(wxMouseEvent& event)
    {
        // TODO: Move selection to cursor
    }
    // On mouse left up, set the value and close the popup
    void OnMouseClick(wxMouseEvent& WXUNUSED(event))
    {
         // TODO: Send event as well
        Dismiss();
    }
protected:
    wxString m_input_string;
};



class SearchComboPopup_ : public wxListCtrl, public wxComboPopup
{
public:
    // Initialize member variables
    virtual void Init() {}

    // Create popup control
    virtual bool Create(wxWindow* parent)
    {
        return wxListCtrl::Create(parent, 1, wxPoint(0, 0), wxDefaultSize, wxLC_LIST | wxLC_NO_HEADER | wxLC_SINGLE_SEL);
    }
    // Return pointer to the created control
    virtual wxWindow* GetControl() { return this; }
    // Translate string into a list selection
    virtual void SetStringValue(const wxString& s)
    {
        int n = wxListCtrl::FindItem(0, s);
        if (n >= 0 && n < wxListCtrl::GetItemCount())
            wxListCtrl::SetItemState(n, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);

        // save a combo control's string
        m_input_string = s;
    }
    // Get list selection as a string
    virtual wxString GetStringValue() const
    {
        // we shouldn't change a combo control's string
        return m_input_string;
    }
    // Do mouse hot-tracking (which is typical in list popups)
    void OnMouseMove(wxMouseEvent& event)
    {
        // TODO: Move selection to cursor
    }
    // On mouse left up, set the value and close the popup
    void OnMouseClick(wxMouseEvent& WXUNUSED(event))
    {
        // TODO: Send event as well
        Dismiss();
    }
protected:
    wxString m_input_string;
};


class SearchCtrl
{
    wxBoxSizer*         box_sizer   {nullptr};
    wxTextCtrl*         search_line {nullptr};
    ScalableButton*     search_btn  {nullptr};
    PopupSearchList*    popup_win   {nullptr};


    bool                prevent_update{ false };
    wxString            default_string;

    void PopupList(wxCommandEvent& event);
    void OnInputText(wxCommandEvent& event);

    wxComboCtrl*        comboCtrl {nullptr};
    SearchComboPopup*   popupListBox {nullptr};
    SearchComboPopup_*   popupListCtrl {nullptr};

    void OnSelect(wxCommandEvent& event);
    void OnLeftDown(wxEvent& event);
    void OnSelectCtrl(wxListEvent& event);

public:
    SearchCtrl(wxWindow* parent);
    ~SearchCtrl();

    wxBoxSizer* sizer() const { return box_sizer; }

    void		set_search_line(const std::string& search_line);
    void        msw_rescale();

    void        update_list(std::vector<SearchOptions::Filter>& filters);
};



}}

#endif //slic3r_SearchComboBox_hpp_
