#ifndef slic3r_MainFrame_hpp_
#define slic3r_MainFrame_hpp_

#include "PrintConfig.hpp"

#include <wx/frame.h>

#include <string>
#include <map>

#include "Plater.hpp"
#include "Event.hpp"

class wxMenuBar;
class wxNotebook;
class wxPanel;
class wxMenu;
class wxProgressDialog;

namespace Slic3r {

class ProgressStatusBar;
class AppController;

// #define _(s)    Slic3r::GUI::I18N::translate((s))

namespace GUI
{
class Tab;

enum QuickSlice
{
    qsUndef,
    qsReslice,
    qsSaveAs,
    qsExportSVG,
    qsExportPNG
};

struct PresetTab {
    std::string       name;
    Tab*              panel;
    PrinterTechnology technology;
};

class MainFrame : public wxFrame
{
    bool        m_no_plater;
    bool        m_loaded;
    int         m_lang_ch_event;
    int         m_preferences_event;

    wxString    m_qs_last_input_file = wxEmptyString;
    wxString    m_qs_last_output_file = wxEmptyString;
    wxString    m_last_config = wxEmptyString;

    AppController*                  m_appController { nullptr };
    std::map<std::string, Tab*>     m_options_tabs;

    wxMenuItem* append_menu_item(wxMenu* menu,
                                 int id,
                                 const wxString& string,
                                 const wxString& description,
                                 std::function<void(wxCommandEvent& event)> cb,
                                 const std::string& icon = "");

    wxMenuItem* m_menu_item_reslice_now { nullptr };
    wxMenu*     m_plater_menu { nullptr };
    wxMenu*     m_object_menu { nullptr };
    wxMenu*     m_viewMenu { nullptr };

    std::string     get_base_name(const wxString full_name) const ;
    std::string     get_dir_name(const wxString full_name) const ;

    void on_presets_changed(SimpleEvent&);
    Tab*         get_tab(const std::string& name);

public:
    MainFrame() {}
    MainFrame(const bool no_plater, const bool loaded);
    ~MainFrame() {}


    void        init_tabpanel();
    const std::map<std::string, Tab*>& options_tabs() const { return m_options_tabs; }
    Tab*        get_preset_tab(const std::string& name);
    void        create_preset_tabs();
    void        add_created_tab(Tab* panel);
    void        init_menubar();

    void        update_ui_from_settings();
    bool        is_loaded() const { return m_loaded; }
    bool        is_last_input_file() const  { return !m_qs_last_input_file.IsEmpty(); }

    void        on_plater_selection_changed(const bool have_selection);
    void        slice_to_png();
    void        quick_slice(const int qs = qsUndef);
    void        reslice_now();
    void        repair_stl();
    void        export_config();
    void        load_config_file(wxString file = wxEmptyString);
    void        export_configbundle();
    void        load_configbundle(wxString file = wxEmptyString);
    void        load_config(const DynamicPrintConfig& config);
    void        select_tab(size_t tab) const;
    void        select_view(const std::string& direction);

    std::vector<PresetTab>& get_preset_tabs();

    Plater*             m_plater { nullptr };
    wxNotebook*         m_tabpanel { nullptr };
    wxProgressDialog*   m_progress_dialog { nullptr };
    ProgressStatusBar*  m_statusbar { nullptr };
};

} // GUI
} //Slic3r

#endif // slic3r_MainFrame_hpp_