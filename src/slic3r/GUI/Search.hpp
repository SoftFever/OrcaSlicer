#ifndef slic3r_SearchComboBox_hpp_
#define slic3r_SearchComboBox_hpp_

#include <vector>
#include <map>

#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/listctrl.h>

#include <wx/combo.h>

#include <wx/popupwin.h>
#include <wx/checkbox.h>

#include "Preset.hpp"
#include "wxExtensions.hpp"


namespace Slic3r {

namespace Search{

struct InputInfo
{
    DynamicPrintConfig* config  {nullptr};
    Preset::Type        type    {Preset::TYPE_INVALID};
    ConfigOptionMode    mode    {comSimple};
};

struct GroupAndCategory {
    wxString        group;
    wxString        category;
};

// fuzzy_match flag
enum FMFlag
{
    fmUndef = 0, // didn't find 
    fmOptKey,
    fmLabel,
    fmLabelLocal,
    fmGroup,
    fmGroupLocal,
    fmCategory,
    fmCategoryLocal
};

struct Option {
    bool operator<(const Option& other) const { return other.label > this->label; }
    bool operator>(const Option& other) const { return other.label < this->label; }

    std::string     opt_key;
    Preset::Type    type {Preset::TYPE_INVALID};
    wxString        label;
    wxString        label_local;
    wxString        group;
    wxString        group_local;
    wxString        category;
    wxString        category_local;

    FMFlag fuzzy_match_simple(char const *search_pattern) const;
    FMFlag fuzzy_match_simple(const wxString& search) const;
    FMFlag fuzzy_match_simple(const std::string &search) const;
    FMFlag fuzzy_match(char const *search_pattern, int &outScore) const;
    FMFlag fuzzy_match(const wxString &search, int &outScore) const ;
    FMFlag fuzzy_match(const std::string &search, int &outScore) const ;
};

struct FoundOption {
    wxString        label;
    wxString        marked_label;
    wxString        tooltip;
    size_t          option_idx {0};
    int             outScore {0};

    void get_label(const char** out_text) const;
    void get_marked_label_and_tooltip(const char** label, const char** tooltip) const;
};

struct OptionViewParameters
{
    bool type       {false};
    bool category   {false};
    bool group      {true };

    int  hovered_id {-1};
};

class OptionsSearcher
{
    std::string                             search_line;
    std::map<std::string, GroupAndCategory> groups_and_categories;

    std::vector<Option>                     options {};
    std::vector<FoundOption>                found {};

    void append_options(DynamicPrintConfig* config, Preset::Type type, ConfigOptionMode mode);

    void sort_options() {
        std::sort(options.begin(), options.end(), [](const Option& o1, const Option& o2) {
            return o1.label < o2.label; });
    }
    void sort_found() {
        std::sort(found.begin(), found.end(), [](const FoundOption& f1, const FoundOption& f2) {
            return f1.outScore > f2.outScore; });
    };

    size_t options_size() const { return options.size(); }
    size_t found_size()   const { return found.size(); }

public:
    OptionViewParameters                    view_params;

    void init(std::vector<InputInfo> input_values);
    void apply(DynamicPrintConfig *config,
               Preset::Type        type,
               ConfigOptionMode    mode);
    bool search(const std::string& search, bool force = false);

    void add_key(const std::string& opt_key, const wxString& group, const wxString& category);

    size_t size() const         { return found_size(); }

    const FoundOption& operator[](const size_t pos) const noexcept { return found[pos]; }
    const Option& get_option(size_t pos_in_filter) const;

    const std::vector<FoundOption>& found_options() { return found; }
    const GroupAndCategory&         get_group_and_category (const std::string& opt_key) { return groups_and_categories[opt_key]; }
};


class SearchComboPopup : public wxListBox, public wxComboPopup
{
public:
    // Initialize member variables
    void Init();

    // Create popup control
    virtual bool Create(wxWindow* parent);
    // Return pointer to the created control
    virtual wxWindow* GetControl() { return this; }

    // Translate string into a list selection
    virtual void SetStringValue(const wxString& s);
    // Get list selection as a string
    virtual wxString GetStringValue() const {
        // we shouldn't change a combo control's string
        return m_input_string;
    }

    void ProcessSelection(int selection);

    // Do mouse hot-tracking (which is typical in list popups)
    void OnMouseMove(wxMouseEvent& event);
    // On mouse left up, set the value and close the popup
    void OnMouseClick(wxMouseEvent& WXUNUSED(event));
    // process Up/Down arrows and Enter press
    void OnKeyDown(wxKeyEvent& event);

protected:
    wxString m_input_string;
};

class SearchCtrl : public wxComboCtrl
{
    SearchComboPopup*   popupListBox {nullptr};

    bool                prevent_update { false };
    wxString            default_string;
    bool                editing {false};

    void PopupList(wxCommandEvent& event);
    void OnInputText(wxCommandEvent& event);

    void OnSelect(wxCommandEvent& event);
    void OnLeftUpInTextCtrl(wxEvent& event);

public:
    SearchCtrl(wxWindow* parent);
    ~SearchCtrl() {}

    void		set_search_line(const std::string& search_line);
    void        msw_rescale();

    void        update_list(const std::vector<FoundOption>& filters);
};



class PopupSearchList : public wxPopupTransientWindow
{
    wxString search_str; 
public:
    PopupSearchList(wxWindow* parent);
    ~PopupSearchList() {}

    // wxPopupTransientWindow virtual methods are all overridden to log them
    void Popup(wxWindow* focus = NULL) wxOVERRIDE;
    void OnDismiss() wxOVERRIDE;
    bool ProcessLeftDown(wxMouseEvent& event) wxOVERRIDE;
    bool Show(bool show = true) wxOVERRIDE;

private:
    wxWindow* panel;

    wxTextCtrl* text {nullptr};
    wxListBox*  list{ nullptr };
    wxCheckBox* check {nullptr};

    void OnSize(wxSizeEvent& event);
    void OnSetFocus(wxFocusEvent& event);
    void OnKillFocus(wxFocusEvent& event);
};

class SearchButton : public ScalableButton
{
    PopupSearchList* popup_win{ nullptr };

    void PopupSearch(wxCommandEvent& event);
public:
    SearchButton(wxWindow* parent);
    ~SearchButton() {}
};




} // Search namespace
}

#endif //slic3r_SearchComboBox_hpp_
