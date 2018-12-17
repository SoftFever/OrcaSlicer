#ifndef slic3r_GUI_App_hpp_
#define slic3r_GUI_App_hpp_

#include <memory>
#include <string>
#include "libslic3r/PrintConfig.hpp"
#include "MainFrame.hpp"
#if ENABLE_IMGUI
#include "ImGuiWrapper.hpp"
#endif // ENABLE_IMGUI

#include <wx/app.h>
#include <wx/colour.h>
#include <wx/font.h>
#include <wx/string.h>

#include <mutex>
#include <stack>

class wxMenuItem;
class wxMenuBar;
class wxTopLevelWindow;
class wxNotebook;

namespace Slic3r {
class AppConfig;
class PresetBundle;
class PresetUpdater;
class ModelObject;
class PrintHostJobQueue;

namespace GUI
{

enum FileType
{
    FT_STL,
    FT_OBJ,
    FT_AMF,
    FT_3MF,
    FT_PRUSA,
    FT_GCODE,
    FT_MODEL,

    FT_INI,
    FT_SVG,
    FT_PNGZIP,

    FT_SIZE,
};

extern wxString file_wildcards(FileType file_type, const std::string &custom_extension = std::string());

enum ConfigMenuIDs {
    ConfigMenuWizard,
    ConfigMenuSnapshots,
    ConfigMenuTakeSnapshot,
    ConfigMenuUpdate,
    ConfigMenuPreferences,
    ConfigMenuModeSimple,
    ConfigMenuModeAdvanced,
    ConfigMenuModeExpert,
    ConfigMenuLanguage,
    ConfigMenuFlashFirmware,
    ConfigMenuCnt,
};

class Tab;

static wxString dots("…", wxConvUTF8);

class GUI_App : public wxApp
{
    bool            no_plater{ false };
    bool            app_conf_exists{ false };

    // Lock to guard the callback stack
    std::mutex      callback_register;
    // callbacks registered to run during idle event.
    std::stack<std::function<void()>>    m_cb{};

    wxColour        m_color_label_modified;
    wxColour        m_color_label_sys;
    wxColour        m_color_label_default;

    wxFont		    m_small_font;
    wxFont		    m_bold_font;

    wxLocale*	    m_wxLocale{ nullptr };

#if ENABLE_IMGUI
    std::unique_ptr<ImGuiWrapper> m_imgui;
#endif // ENABLE_IMGUI

    std::unique_ptr<PrintHostJobQueue> m_printhost_job_queue;

public:
    bool            OnInit() override;

    GUI_App();

    unsigned        get_colour_approx_luma(const wxColour &colour);
    void            init_label_colours();
    void            update_label_colours_from_appconfig();
    void            init_fonts();
    void            set_label_clr_modified(const wxColour& clr);
    void            set_label_clr_sys(const wxColour& clr);

    const wxColour& get_label_clr_modified(){ return m_color_label_modified; }
    const wxColour& get_label_clr_sys()     { return m_color_label_sys; }
    const wxColour& get_label_clr_default() { return m_color_label_default; }

    const wxFont&   small_font()            { return m_small_font; }
    const wxFont&   bold_font()             { return m_bold_font; }

    void            recreate_GUI();
    void            system_info();
    void            load_project(wxWindow *parent, wxString& input_file);
    void            import_model(wxWindow *parent, wxArrayString& input_files);
    static bool     catch_error(std::function<void()> cb,
//                                 wxMessageDialog* message_dialog,
                                const std::string& err);
//     void            notify(/*message*/);
    void            update_ui_from_settings();
    void            CallAfter(std::function<void()> cb);

    void            window_pos_save(wxTopLevelWindow* window, const std::string &name);
    void            window_pos_restore(wxTopLevelWindow* window, const std::string &name);
    void            window_pos_sanitize(wxTopLevelWindow* window);

    bool            select_language(wxArrayString & names, wxArrayLong & identifiers);
    bool            load_language();
    void            save_language();
    void            get_installed_languages(wxArrayString & names, wxArrayLong & identifiers);

    Tab*            get_tab(Preset::Type type);
    ConfigMenuIDs   get_view_mode();
    void            update_mode();

    void            add_config_menu(wxMenuBar *menu);
    bool            check_unsaved_changes();
    bool            checked_tab(Tab* tab);
    void            delete_tab_from_list(Tab* tab);
    void            load_current_presets();

    Sidebar&            sidebar();
    ObjectManipulation* obj_manipul();
    ObjectSettings*     obj_settings();
    ObjectList*         obj_list();
    Plater*             plater();
    std::vector<ModelObject*> *model_objects();

    AppConfig*      app_config{ nullptr };
    PresetBundle*   preset_bundle{ nullptr };
    PresetUpdater*  preset_updater{ nullptr };
    MainFrame*      mainframe{ nullptr };
    Plater*         plater_{ nullptr };

    wxNotebook*     tab_panel() const ;

    std::vector<Tab *>      tabs_list;

#if ENABLE_IMGUI
    ImGuiWrapper* imgui() { return m_imgui.get(); }
#endif // ENABLE_IMGUI

    PrintHostJobQueue& printhost_job_queue() { return *m_printhost_job_queue.get(); }

};
DECLARE_APP(GUI_App)

} // GUI
} //Slic3r

#endif // slic3r_GUI_App_hpp_
