#ifndef slic3r_SearchComboBox_hpp_
#define slic3r_SearchComboBox_hpp_

#include <vector>
#include <map>

#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/listctrl.h>

#include <wx/combo.h>

#include <wx/checkbox.h>
#include <wx/dialog.h>

#include "GUI_Utils.hpp"
#include "Preset.hpp"
#include "wxExtensions.hpp"


namespace Slic3r {

namespace Search{

class SearchDialog;

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

struct Option {
    bool operator<(const Option& other) const { return other.label > this->label; }
    bool operator>(const Option& other) const { return other.label < this->label; }

    // Fuzzy matching works at a character level. Thus matching with wide characters is a safer bet than with short characters,
    // though for some languages (Chinese?) it may not work correctly.
    std::wstring    opt_key;
    Preset::Type    type {Preset::TYPE_INVALID};
    std::wstring    label;
    std::wstring    label_local;
    std::wstring    group;
    std::wstring    group_local;
    std::wstring    category;
    std::wstring    category_local;
};

struct FoundOption {
	// UTF8 encoding, to be consumed by ImGUI by reference.
    std::string     label;
    std::string     marked_label;
    std::string     tooltip;
    size_t          option_idx {0};
    int             outScore {0};

    // Returning pointers to contents of std::string members, to be used by ImGUI for rendering.
    void get_marked_label_and_tooltip(const char** label, const char** tooltip) const;
};

struct OptionViewParameters
{
    bool category   {false};
    bool group      {true };
    bool english    {false};

    int  hovered_id {-1};
};

class OptionsSearcher
{
    std::string                             search_line;
    std::map<std::string, GroupAndCategory> groups_and_categories;
    PrinterTechnology                       printer_technology;

    std::vector<Option>                     options {};
    std::vector<FoundOption>                found {};

    void append_options(DynamicPrintConfig* config, Preset::Type type, ConfigOptionMode mode);

    void sort_options() {
        std::sort(options.begin(), options.end(), [](const Option& o1, const Option& o2) {
            return o1.label < o2.label; });
    }
    void sort_found() {
        std::sort(found.begin(), found.end(), [](const FoundOption& f1, const FoundOption& f2) {
            return f1.outScore > f2.outScore || (f1.outScore == f2.outScore && f1.label < f2.label); });
    };

    size_t options_size() const { return options.size(); }
    size_t found_size()   const { return found.size(); }

public:
    OptionViewParameters                    view_params;

    SearchDialog*                           search_dialog { nullptr };

    OptionsSearcher();
    ~OptionsSearcher();

    void init(std::vector<InputInfo> input_values);
    void apply(DynamicPrintConfig *config,
               Preset::Type        type,
               ConfigOptionMode    mode);
    bool search();
    bool search(const std::string& search, bool force = false);

    void add_key(const std::string& opt_key, const wxString& group, const wxString& category);

    size_t size() const         { return found_size(); }

    const FoundOption& operator[](const size_t pos) const noexcept { return found[pos]; }
    const Option& get_option(size_t pos_in_filter) const;

    const std::vector<FoundOption>& found_options() { return found; }
    const GroupAndCategory&         get_group_and_category (const std::string& opt_key) { return groups_and_categories[opt_key]; }
    std::string& search_string() { return search_line; }

    void set_printer_technology(PrinterTechnology pt) { printer_technology = pt; }
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


//------------------------------------------
//          SearchDialog
//------------------------------------------

class SearchDialog : public GUI::DPIDialog
{
    wxString search_str;
    wxString default_string;

    wxTextCtrl*     search_line    { nullptr };
    wxListBox*      search_list    { nullptr };
    wxCheckBox*     check_category { nullptr };
    wxCheckBox*     check_group    { nullptr };
    wxCheckBox*     check_english  { nullptr };

    OptionsSearcher* searcher;

    void update_list();

    void OnInputText(wxCommandEvent& event);
    void OnLeftUpInTextCtrl(wxEvent& event);
    
    void OnMouseMove(wxMouseEvent& event); 
    void OnMouseClick(wxMouseEvent& event);
    void OnSelect(wxCommandEvent& event);
    void OnKeyDown(wxKeyEvent& event);

    void OnCheck(wxCommandEvent& event);

public:
    SearchDialog(OptionsSearcher* searcher);
    ~SearchDialog() {}

    void Popup(wxPoint position = wxDefaultPosition);
    void ProcessSelection(int selection);

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
};


} // Search namespace
}

#endif //slic3r_SearchComboBox_hpp_
