#ifndef slic3r_GUI_App_hpp_
#define slic3r_GUI_App_hpp_

#include <memory>
#include <string>
#include "Preset.hpp"
#include "ImGuiWrapper.hpp"
#include "ConfigWizard.hpp"
#include "OpenGLManager.hpp"

#include <wx/app.h>
#include <wx/colour.h>
#include <wx/font.h>
#include <wx/string.h>
#include <wx/snglinst.h>

#include <mutex>
#include <stack>

class wxMenuItem;
class wxMenuBar;
class wxTopLevelWindow;
class wxNotebook;
struct wxLanguageInfo;

namespace Slic3r {
class AppConfig;
class PresetBundle;
class PresetUpdater;
class ModelObject;
class PrintHostJobQueue;
class Model;

namespace GUI{
class RemovableDriveManager;
class OtherInstanceMessageHandler;
class MainFrame;
class Sidebar;
class ObjectManipulation;
class ObjectSettings;
class ObjectList;
class ObjectLayers;
class Plater;



enum FileType
{
    FT_STL,
    FT_OBJ,
    FT_AMF,
    FT_3MF,
    FT_PRUSA,
    FT_GCODE,
    FT_MODEL,
    FT_PROJECT,

    FT_INI,
    FT_SVG,

    FT_TEX,

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
class ConfigWizard;

static wxString dots("…", wxConvUTF8);

class GUI_App : public wxApp
{
    bool            m_initialized { false };
    bool            app_conf_exists{ false };

    wxColour        m_color_label_modified;
    wxColour        m_color_label_sys;
    wxColour        m_color_label_default;

    wxFont		    m_small_font;
    wxFont		    m_bold_font;
	wxFont			m_normal_font;

    int          m_em_unit; // width of a "m"-symbol in pixels for current system font
                               // Note: for 100% Scale m_em_unit = 10 -> it's a good enough coefficient for a size setting of controls

    std::unique_ptr<wxLocale> 	  m_wxLocale;
    // System language, from locales, owned by wxWidgets.
    const wxLanguageInfo		 *m_language_info_system = nullptr;
    // Best translation language, provided by Windows or OSX, owned by wxWidgets.
    const wxLanguageInfo		 *m_language_info_best   = nullptr;

    OpenGLManager m_opengl_mgr;

    std::unique_ptr<RemovableDriveManager> m_removable_drive_manager;

    std::unique_ptr<ImGuiWrapper> m_imgui;
    std::unique_ptr<PrintHostJobQueue> m_printhost_job_queue;
    ConfigWizard* m_wizard;    // Managed by wxWindow tree
	std::unique_ptr <OtherInstanceMessageHandler> m_other_instance_message_handler;
    std::unique_ptr <wxSingleInstanceChecker> m_single_instance_checker;
    std::string m_instance_hash_string;
	size_t m_instance_hash_int;
public:
    bool            OnInit() override;
    bool            initialized() const { return m_initialized; }

    GUI_App();
    ~GUI_App() override;

    static std::string get_gl_info(bool format_as_html, bool extensions);
    wxGLContext* init_glcontext(wxGLCanvas& canvas);
    bool init_opengl();

    static unsigned get_colour_approx_luma(const wxColour &colour);
    static bool     dark_mode();
    void            init_label_colours();
    void            update_label_colours_from_appconfig();
    void            init_fonts();
	void            update_fonts(const MainFrame *main_frame = nullptr);
    void            set_label_clr_modified(const wxColour& clr);
    void            set_label_clr_sys(const wxColour& clr);

    const wxColour& get_label_clr_modified(){ return m_color_label_modified; }
    const wxColour& get_label_clr_sys()     { return m_color_label_sys; }
    const wxColour& get_label_clr_default() { return m_color_label_default; }

    const wxFont&   small_font()            { return m_small_font; }
    const wxFont&   bold_font()             { return m_bold_font; }
    const wxFont&   normal_font()           { return m_normal_font; }
    int             em_unit() const         { return m_em_unit; }
    wxSize          get_min_size() const;
    float           toolbar_icon_scale(const bool is_limited = false) const;
    void            set_auto_toolbar_icon_scale(float scale) const;

    void            recreate_GUI(const wxString& message);
    void            system_info();
    void            keyboard_shortcuts();
    void            load_project(wxWindow *parent, wxString& input_file) const;
    void            import_model(wxWindow *parent, wxArrayString& input_files) const;
    static bool     catch_error(std::function<void()> cb, const std::string& err);

    void            persist_window_geometry(wxTopLevelWindow *window, bool default_maximized = false);
    void            update_ui_from_settings();

    bool            switch_language();
    bool            load_language(wxString language, bool initial);

    Tab*            get_tab(Preset::Type type);
    ConfigOptionMode get_mode();
    void            save_mode(const /*ConfigOptionMode*/int mode) ;
    void            update_mode();

    void            add_config_menu(wxMenuBar *menu);
    bool            check_unsaved_changes(const wxString &header = wxString());
    bool            checked_tab(Tab* tab);
    void            load_current_presets();

    wxString        current_language_code() const { return m_wxLocale->GetCanonicalName(); }
	// Translate the language code to a code, for which Prusa Research maintains translations. Defaults to "en_US".
    wxString 		current_language_code_safe() const;
    bool            is_localized() const { return m_wxLocale->GetLocale() != "English"; }

    virtual bool OnExceptionInMainLoop() override;

#ifdef __APPLE__
    // wxWidgets override to get an event on open files.
    void            MacOpenFiles(const wxArrayString &fileNames) override;
#endif /* __APPLE */

    Sidebar&            sidebar();
    ObjectManipulation* obj_manipul();
    ObjectSettings*     obj_settings();
    ObjectList*         obj_list();
    ObjectLayers*       obj_layers();
    Plater*             plater();
    Model&      		model();


    AppConfig*      app_config{ nullptr };
    PresetBundle*   preset_bundle{ nullptr };
    PresetUpdater*  preset_updater{ nullptr };
    MainFrame*      mainframe{ nullptr };
    Plater*         plater_{ nullptr };

	PresetUpdater* get_preset_updater() { return preset_updater; }

    wxNotebook*     tab_panel() const ;
    int             extruders_cnt() const;
    int             extruders_edited_cnt() const;

    std::vector<Tab *>      tabs_list;

	RemovableDriveManager* removable_drive_manager() { return m_removable_drive_manager.get(); }
	OtherInstanceMessageHandler* other_instance_message_handler() { return m_other_instance_message_handler.get(); }
    wxSingleInstanceChecker* single_instance_checker() {return m_single_instance_checker.get();}
    
	void        init_single_instance_checker(const std::string &name, const std::string &path);
	void        set_instance_hash (const size_t hash) { m_instance_hash_int = hash; m_instance_hash_string = std::to_string(hash); }
    std::string get_instance_hash_string ()           { return m_instance_hash_string; }
	size_t      get_instance_hash_int ()              { return m_instance_hash_int; }

    ImGuiWrapper* imgui() { return m_imgui.get(); }

    PrintHostJobQueue& printhost_job_queue() { return *m_printhost_job_queue.get(); }

    void            open_web_page_localized(const std::string &http_address);
    bool            run_wizard(ConfigWizard::RunReason reason, ConfigWizard::StartPage start_page = ConfigWizard::SP_WELCOME);

#if ENABLE_THUMBNAIL_GENERATOR_DEBUG
    // temporary and debug only -> extract thumbnails from selected gcode and save them as png files
    void            gcode_thumbnails_debug();
#endif // ENABLE_THUMBNAIL_GENERATOR_DEBUG

private:
    bool            on_init_inner();
	void            init_app_config();
    void            window_pos_save(wxTopLevelWindow* window, const std::string &name);
    void            window_pos_restore(wxTopLevelWindow* window, const std::string &name, bool default_maximized = false);
    void            window_pos_sanitize(wxTopLevelWindow* window);
    bool            select_language();

    bool            config_wizard_startup();
	void            check_updates(const bool verbose);

#ifdef __WXMSW__
    void            associate_3mf_files();
#endif // __WXMSW__
};
DECLARE_APP(GUI_App)

} // GUI
} //Slic3r

#endif // slic3r_GUI_App_hpp_
