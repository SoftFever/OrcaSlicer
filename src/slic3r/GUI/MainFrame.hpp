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

// #define _(s)    Slic3r::GUI::I18N::translate((s))

namespace GUI
{
class Tab;

enum QuickSlice
{
    qsUndef = 0,
    qsReslice = 1,
    qsSaveAs = 2,
    qsExportSVG = 4,
    qsExportPNG = 8
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

    wxMenuItem* m_menu_item_repeat { nullptr };
    wxMenuItem* m_menu_item_reslice_now { nullptr };
#if !ENABLE_NEW_MENU_LAYOUT
    wxMenu*     m_plater_menu{ nullptr };
    wxMenu*     m_viewMenu{ nullptr };
#endif // !ENABLE_NEW_MENU_LAYOUT

    std::string     get_base_name(const wxString full_name) const ;
    std::string     get_dir_name(const wxString full_name) const ;

    void on_presets_changed(SimpleEvent&);
    void on_value_changed(wxCommandEvent&);

#if ENABLE_NEW_MENU_LAYOUT
    bool can_save() const;
    bool can_export_model() const;
    bool can_export_gcode() const;
    bool can_change_view() const;
    bool can_select() const;
#endif // ENABLE_NEW_MENU_LAYOUT

public:
    MainFrame() {}
    MainFrame(const bool no_plater, const bool loaded);
    ~MainFrame() {}

    Plater*     plater() { return m_plater; }

    void        init_tabpanel();
    void        create_preset_tabs();
    void        add_created_tab(Tab* panel);
    void        init_menubar();

    void        update_ui_from_settings();
    bool        is_loaded() const { return m_loaded; }
    bool        is_last_input_file() const  { return !m_qs_last_input_file.IsEmpty(); }

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

    Plater*             m_plater { nullptr };
    wxNotebook*         m_tabpanel { nullptr };
    wxProgressDialog*   m_progress_dialog { nullptr };
    ProgressStatusBar*  m_statusbar { nullptr };
};

} // GUI
} //Slic3r

#endif // slic3r_MainFrame_hpp_
