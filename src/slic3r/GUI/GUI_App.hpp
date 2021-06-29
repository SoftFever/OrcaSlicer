#ifndef slic3r_GUI_App_hpp_
#define slic3r_GUI_App_hpp_

#include <memory>
#include <string>
#include "ImGuiWrapper.hpp"
#include "ConfigWizard.hpp"
#include "OpenGLManager.hpp"
#include "libslic3r/Preset.hpp"

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
class wxDataViewCtrl;
class wxBookCtrlBase;
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
class NotificationManager;
struct GUI_InitParams;



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
    ConfigMenuDesktopIntegration,
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

// Does our wxWidgets version support markup?
// https://github.com/prusa3d/PrusaSlicer/issues/4282#issuecomment-634676371
#if wxUSE_MARKUP && wxCHECK_VERSION(3, 1, 1)
    #define SUPPORTS_MARKUP
#endif

class GUI_App : public wxApp
{
public:
    enum class EAppMode : unsigned char
    {
        Editor,
        GCodeViewer
    };

private:
    bool            m_initialized { false };
    bool            m_app_conf_exists{ false };
    EAppMode        m_app_mode{ EAppMode::Editor };
    bool            m_is_recreating_gui{ false };
#ifdef __linux__
    bool            m_opengl_initialized{ false };
#endif

    wxColour        m_color_label_modified;
    wxColour        m_color_label_sys;
    wxColour        m_color_label_default;
    wxColour        m_color_window_default;
#ifdef _WIN32
    wxColour        m_color_highlight_label_default;
    wxColour        m_color_hovered_btn_label;
    wxColour        m_color_highlight_default;
    wxColour        m_color_selected_btn_bg;
    bool            m_force_colors_update { false };
#endif

    wxFont		    m_small_font;
    wxFont		    m_bold_font;
	wxFont			m_normal_font;
	wxFont			m_code_font;

    int             m_em_unit; // width of a "m"-symbol in pixels for current system font
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

    explicit GUI_App(EAppMode mode = EAppMode::Editor);
    ~GUI_App() override;

    EAppMode get_app_mode() const { return m_app_mode; }
    bool is_editor() const { return m_app_mode == EAppMode::Editor; }
    bool is_gcode_viewer() const { return m_app_mode == EAppMode::GCodeViewer; }
    bool is_recreating_gui() const { return m_is_recreating_gui; }

    // To be called after the GUI is fully built up.
    // Process command line parameters cached in this->init_params,
    // load configs, STLs etc.
    void            post_init();
    static std::string get_gl_info(bool format_as_html, bool extensions);
    wxGLContext*    init_glcontext(wxGLCanvas& canvas);
    bool            init_opengl();

    static unsigned get_colour_approx_luma(const wxColour &colour);
    static bool     dark_mode();
    void            init_label_colours();
    void            update_label_colours_from_appconfig();
    void            update_label_colours();
    // update color mode for window
    void            UpdateDarkUI(wxWindow *window, bool highlited = false, bool just_font = false);
    // update color mode for whole dialog including all children
    void            UpdateDlgDarkUI(wxDialog* dlg);
    // update color mode for DataViewControl
    void            UpdateDVCDarkUI(wxDataViewCtrl* dvc, bool highlited = false);
    // update color mode for panel including all static texts controls
    void            UpdateAllStaticTextDarkUI(wxWindow* parent);
    void            init_fonts();
	void            update_fonts(const MainFrame *main_frame = nullptr);
    void            set_label_clr_modified(const wxColour& clr);
    void            set_label_clr_sys(const wxColour& clr);

    const wxColour& get_label_clr_modified(){ return m_color_label_modified; }
    const wxColour& get_label_clr_sys()     { return m_color_label_sys; }
    const wxColour& get_label_clr_default() { return m_color_label_default; }
    const wxColour& get_window_default_clr(){ return m_color_window_default; }


#ifdef _WIN32
    const wxColour& get_label_highlight_clr()   { return m_color_highlight_label_default; }
    const wxColour& get_highlight_default_clr() { return m_color_highlight_default; }
    const wxColour& get_color_hovered_btn_label() { return m_color_hovered_btn_label; }
    const wxColour& get_color_selected_btn_bg() { return m_color_selected_btn_bg; }
    void            force_colors_update();
#endif

    const wxFont&   small_font()            { return m_small_font; }
    const wxFont&   bold_font()             { return m_bold_font; }
    const wxFont&   normal_font()           { return m_normal_font; }
    const wxFont&   code_font()             { return m_code_font; }
    int             em_unit() const         { return m_em_unit; }
    bool            tabs_as_menu() const;
    wxSize          get_min_size() const;
    float           toolbar_icon_scale(const bool is_limited = false) const;
    void            set_auto_toolbar_icon_scale(float scale) const;
    void            check_printer_presets();

    void            recreate_GUI(const wxString& message);
    void            system_info();
    void            keyboard_shortcuts();
    void            load_project(wxWindow *parent, wxString& input_file) const;
    void            import_model(wxWindow *parent, wxArrayString& input_files) const;
    void            load_gcode(wxWindow* parent, wxString& input_file) const;

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
#if ENABLE_PROJECT_DIRTY_STATE
    bool            has_unsaved_preset_changes() const;
    bool            has_current_preset_changes() const;
    void            update_saved_preset_from_current_preset();
    std::vector<std::pair<unsigned int, std::string>> get_selected_presets() const;
    bool            check_and_save_current_preset_changes(const wxString& header = wxString());
#else
    bool            check_unsaved_changes(const wxString& header = wxString());
#endif // ENABLE_PROJECT_DIRTY_STATE
    bool            check_print_host_queue();
    bool            checked_tab(Tab* tab);
    void            load_current_presets(bool check_printer_presets = true);

    wxString        current_language_code() const { return m_wxLocale->GetCanonicalName(); }
	// Translate the language code to a code, for which Prusa Research maintains translations. Defaults to "en_US".
    wxString 		current_language_code_safe() const;
    bool            is_localized() const { return m_wxLocale->GetLocale() != "English"; }

    virtual bool OnExceptionInMainLoop() override;

#ifdef __APPLE__
    void            OSXStoreOpenFiles(const wxArrayString &files) override;
    // wxWidgets override to get an event on open files.
    void            MacOpenFiles(const wxArrayString &fileNames) override;
#endif /* __APPLE */

    Sidebar&             sidebar();
    ObjectManipulation*  obj_manipul();
    ObjectSettings*      obj_settings();
    ObjectList*          obj_list();
    ObjectLayers*        obj_layers();
    Plater*              plater();
    Model&      		 model();
    NotificationManager* notification_manager();

    // Parameters extracted from the command line to be passed to GUI after initialization.
    GUI_InitParams* init_params { nullptr };

    AppConfig*      app_config{ nullptr };
    PresetBundle*   preset_bundle{ nullptr };
    PresetUpdater*  preset_updater{ nullptr };
    MainFrame*      mainframe{ nullptr };
    Plater*         plater_{ nullptr };

	PresetUpdater*  get_preset_updater() { return preset_updater; }

    wxBookCtrlBase* tab_panel() const ;
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
    void            show_desktop_integration_dialog();

#if ENABLE_THUMBNAIL_GENERATOR_DEBUG
    // temporary and debug only -> extract thumbnails from selected gcode and save them as png files
    void            gcode_thumbnails_debug();
#endif // ENABLE_THUMBNAIL_GENERATOR_DEBUG

    GLShaderProgram* get_shader(const std::string& shader_name) { return m_opengl_mgr.get_shader(shader_name); }
    GLShaderProgram* get_current_shader() { return m_opengl_mgr.get_current_shader(); }

    bool is_gl_version_greater_or_equal_to(unsigned int major, unsigned int minor) const { return m_opengl_mgr.get_gl_info().is_version_greater_or_equal_to(major, minor); }
    bool is_glsl_version_greater_or_equal_to(unsigned int major, unsigned int minor) const { return m_opengl_mgr.get_gl_info().is_glsl_version_greater_or_equal_to(major, minor); }

#ifdef __WXMSW__
    void            associate_3mf_files();
    void            associate_stl_files();
    void            associate_gcode_files();
#endif // __WXMSW__

private:
    bool            on_init_inner();
	void            init_app_config();
    void            window_pos_save(wxTopLevelWindow* window, const std::string &name);
    void            window_pos_restore(wxTopLevelWindow* window, const std::string &name, bool default_maximized = false);
    void            window_pos_sanitize(wxTopLevelWindow* window);
    bool            select_language();

    bool            config_wizard_startup();
	void            check_updates(const bool verbose);
};

DECLARE_APP(GUI_App)

} // GUI
} // Slic3r

#endif // slic3r_GUI_App_hpp_
