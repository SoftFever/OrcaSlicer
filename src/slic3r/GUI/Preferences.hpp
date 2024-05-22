#ifndef slic3r_Preferences_hpp_
#define slic3r_Preferences_hpp_

#include "GUI.hpp"
#include "GUI_Utils.hpp"

#include <wx/simplebook.h>
#include <wx/dialog.h>
#include <wx/timer.h>
#include <vector>
#include <list>
#include <map>
#include "Widgets/ComboBox.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/TextInput.hpp"

namespace Slic3r { namespace GUI {


#define DESIGN_SELECTOR_NOMORE_COLOR wxColour(248, 248, 248)
#define DESIGN_GRAY900_COLOR wxColour(38, 46, 48)
#define DESIGN_GRAY800_COLOR wxColour(50, 58, 61)
#define DESIGN_GRAY600_COLOR wxColour(144, 144, 144)
#define DESIGN_GRAY400_COLOR wxColour(166, 169, 170)

class Selector
{
public:
    int       m_index;
    wxWindow *m_tab_button;
    wxWindow *m_tab_text;
};
WX_DECLARE_HASH_MAP(int, Selector *, wxIntegerHash, wxIntegerEqual, SelectorHash);

class RadioBox;
class RadioSelector
{
public:
    wxString  m_param_name;
    int       m_groupid;
    RadioBox *m_radiobox;
    bool      m_selected = false;
};

WX_DECLARE_LIST(RadioSelector, RadioSelectorList);
class CheckBox;
class TextInput;



#define DESIGN_RESOUTION_PREFERENCES wxSize(FromDIP(540), -1)
#define DESIGN_TITLE_SIZE wxSize(FromDIP(100), -1)
#define DESIGN_COMBOBOX_SIZE wxSize(FromDIP(140), -1)
#define DESIGN_LARGE_COMBOBOX_SIZE wxSize(FromDIP(160), -1)
#define DESIGN_INPUT_SIZE wxSize(FromDIP(100), -1)


class PreferencesDialog : public DPIDialog
{
private:
    AppConfig *app_config;

protected:
    wxBoxSizer *  m_sizer_body;
    wxScrolledWindow* m_scrolledWindow;

    // bool								m_settings_layout_changed {false};
    bool m_seq_top_layer_only_changed{false};
    bool m_recreate_GUI{false};

public:
    bool seq_top_layer_only_changed() const { return m_seq_top_layer_only_changed; }
    bool recreate_GUI() const { return m_recreate_GUI; }
    void on_dpi_changed(const wxRect &suggested_rect) override;

public:
    PreferencesDialog(wxWindow *      parent,
                      wxWindowID      id    = wxID_ANY,
                      const wxString &title = wxT(""),
                      const wxPoint & pos   = wxDefaultPosition,
                      const wxSize &  size  = wxDefaultSize,
                      long            style = wxSYSTEM_MENU | wxCAPTION | wxCLOSE_BOX);

    ~PreferencesDialog();

    wxString m_backup_interval_time;

    void      create();
    wxWindow *create_tab_button(int id, wxString text);

    // debug mode
    ::CheckBox * m_developer_mode_ckeckbox   = {nullptr};
    ::CheckBox * m_internal_developer_mode_ckeckbox = {nullptr};
    ::CheckBox * m_dark_mode_ckeckbox        = {nullptr};
    ::TextInput *m_backup_interval_textinput = {nullptr};

    wxString m_developer_mode_def;
    wxString m_internal_developer_mode_def;
    wxString m_backup_interval_def;
    wxString m_iot_environment_def;

    SelectorHash      m_hash_selector;
    RadioSelectorList m_radio_group;
    // ComboBoxSelectorList    m_comxbo_group;

    wxBoxSizer *create_item_title(wxString title, wxWindow *parent, wxString tooltip);
    wxBoxSizer *create_item_combobox(wxString title, wxWindow *parent, wxString tooltip, std::string param, std::vector<wxString> vlist);
    wxBoxSizer *create_item_region_combobox(wxString title, wxWindow *parent, wxString tooltip, std::vector<wxString> vlist);
    wxBoxSizer *create_item_language_combobox(wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param, std::vector<const wxLanguageInfo *> vlist);
    wxBoxSizer *create_item_loglevel_combobox(wxString title, wxWindow *parent, wxString tooltip, std::vector<wxString> vlist);
    wxBoxSizer *create_item_checkbox(wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param);
    wxBoxSizer *create_item_darkmode_checkbox(wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param);
    void set_dark_mode();
    wxBoxSizer *create_item_button(wxString title, wxString title2, wxWindow *parent, wxString tooltip, wxString tooltip2, std::function<void()> onclick);
    wxWindow* create_item_downloads(wxWindow* parent, int padding_left, std::string param);
    wxBoxSizer *create_item_input(wxString title, wxString title2, wxWindow *parent, wxString tooltip, std::string param, std::function<void(wxString)> onchange = {});
    wxBoxSizer *create_item_backup_input(wxString title, wxWindow *parent, wxString tooltip, std::string param);
    wxBoxSizer *create_item_multiple_combobox(
        wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string parama, std::vector<wxString> vlista, std::vector<wxString> vlistb);
    wxBoxSizer *create_item_switch(wxString title, wxWindow *parent, wxString tooltip, std::string param);
    wxWindow *  create_item_radiobox(wxString title, wxWindow *parent, wxString tooltip, int padding_left, int groupid, std::string param);

    wxWindow* create_general_page();
    void create_gui_page();
    void create_sync_page();
    void create_shortcuts_page();
    wxWindow* create_debug_page();

    void     on_select_radio(std::string param);
    wxString get_select_radio(int groupid);
    // BBS
    void create_select_domain_widget();

    void Split(const std::string &src, const std::string &separator, std::vector<wxString> &dest);
    int m_current_language_selected = {0};

protected:
    void OnSelectTabel(wxCommandEvent &event);
    void OnSelectRadio(wxMouseEvent &event);
};

wxDECLARE_EVENT(EVT_PREFERENCES_SELECT_TAB, wxCommandEvent);

}} // namespace Slic3r::GUI

#endif /* slic3r_Preferences_hpp_ */
