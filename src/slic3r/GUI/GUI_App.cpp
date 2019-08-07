#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_ObjectManipulation.hpp"
#include "I18N.hpp"

#include <algorithm>
#include <iterator>
#include <exception>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>

#include <wx/stdpaths.h>
#include <wx/imagpng.h>
#include <wx/display.h>
#include <wx/menu.h>
#include <wx/menuitem.h>
#include <wx/filedlg.h>
#include <wx/progdlg.h>
#include <wx/dir.h>
#include <wx/wupdlock.h>
#include <wx/filefn.h>
#include <wx/sysopt.h>
#include <wx/msgdlg.h>
#include <wx/log.h>
#include <wx/intl.h>

#include "libslic3r/Utils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/I18N.hpp"

#include "GUI.hpp"
#include "GUI_Utils.hpp"
#include "AppConfig.hpp"
#include "PresetBundle.hpp"
#include "3DScene.hpp"

#include "../Utils/PresetUpdater.hpp"
#include "../Utils/PrintHost.hpp"
#include "../Utils/MacDarkMode.hpp"
#include "ConfigWizard.hpp"
#include "slic3r/Config/Snapshot.hpp"
#include "ConfigSnapshotDialog.hpp"
#include "FirmwareDialog.hpp"
#include "Preferences.hpp"
#include "Tab.hpp"
#include "SysInfoDialog.hpp"
#include "KBShortcutsDialog.hpp"

#ifdef __WXMSW__
#include <Shlobj.h>
#endif // __WXMSW__

namespace Slic3r {
namespace GUI {


wxString file_wildcards(FileType file_type, const std::string &custom_extension)
{
    static const std::string defaults[FT_SIZE] = {
        /* FT_STL */     "STL files (*.stl)|*.stl;*.STL",
        /* FT_OBJ */     "OBJ files (*.obj)|*.obj;*.OBJ",
        /* FT_AMF */     "AMF files (*.amf)|*.zip.amf;*.amf;*.AMF;*.xml;*.XML",
        /* FT_3MF */     "3MF files (*.3mf)|*.3mf;*.3MF;",
        /* FT_PRUSA */   "Prusa Control files (*.prusa)|*.prusa;*.PRUSA",
        /* FT_GCODE */   "G-code files (*.gcode, *.gco, *.g, *.ngc)|*.gcode;*.GCODE;*.gco;*.GCO;*.g;*.G;*.ngc;*.NGC",
        /* FT_MODEL */   "Known files (*.stl, *.obj, *.amf, *.xml, *.3mf, *.prusa)|*.stl;*.STL;*.obj;*.OBJ;*.amf;*.AMF;*.xml;*.XML;*.3mf;*.3MF;*.prusa;*.PRUSA",
        /* FT_PROJECT */ "Project files (*.3mf, *.amf)|*.3mf;*.3MF;*.amf;*.AMF",

        /* FT_INI */     "INI files (*.ini)|*.ini;*.INI",
        /* FT_SVG */     "SVG files (*.svg)|*.svg;*.SVG",

        /* FT_TEX */     "Texture (*.png, *.svg)|*.png;*.PNG;*.svg;*.SVG",

        /* FT_PNGZIP */  "Masked SLA files (*.sl1)|*.sl1;*.SL1",
    };

	std::string out = defaults[file_type];
    if (! custom_extension.empty()) {
        // Find the custom extension in the template.
        if (out.find(std::string("*") + custom_extension + ",") == std::string::npos && out.find(std::string("*") + custom_extension + ")") == std::string::npos) {
            // The custom extension was not found in the template.
            // Append the custom extension to the wildcards, so that the file dialog would not add the default extension to it.
			boost::replace_first(out, ")|", std::string(", *") + custom_extension + ")|");
			out += std::string(";*") + custom_extension;
        }
    }
    return from_u8(out);
}

static std::string libslic3r_translate_callback(const char *s) { return wxGetTranslation(wxString(s, wxConvUTF8)).utf8_str().data(); }

static void register_dpi_event()
{
#ifdef WIN32
    enum { WM_DPICHANGED_ = 0x02e0 };

    wxWindow::MSWRegisterMessageHandler(WM_DPICHANGED_, [](wxWindow *win, WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) {
        const int dpi = wParam & 0xffff;
        const auto rect = reinterpret_cast<PRECT>(lParam);
        const wxRect wxrect(wxPoint(rect->top, rect->left), wxPoint(rect->bottom, rect->right));

        DpiChangedEvent evt(EVT_DPI_CHANGED, dpi, wxrect);
        win->GetEventHandler()->AddPendingEvent(evt);

        return true;
    });
#endif
}


static void generic_exception_handle()
{
    // Note: Some wxWidgets APIs use wxLogError() to report errors, eg. wxImage
    // - see https://docs.wxwidgets.org/3.1/classwx_image.html#aa249e657259fe6518d68a5208b9043d0
    //
    // wxLogError typically goes around exception handling and display an error dialog some time
    // after an error is logged even if exception handling and OnExceptionInMainLoop() take place.
    // This is why we use wxLogError() here as well instead of a custom dialog, because it accumulates
    // errors if multiple have been collected and displays just one error message for all of them.
    // Otherwise we would get multiple error messages for one missing png, for example.
    //
    // If a custom error message window (or some other solution) were to be used, it would be necessary
    // to turn off wxLogError() usage in wx APIs, most notably in wxImage
    // - see https://docs.wxwidgets.org/trunk/classwx_image.html#aa32e5d3507cc0f8c3330135bc0befc6a

    try {
        throw;
    } catch (const std::exception &ex) {
        wxLogError("Internal error: %s", ex.what());
        BOOST_LOG_TRIVIAL(error) << boost::format("Uncaught exception: %1%") % ex.what();
        throw;
    } catch (...) {
        wxLogError("Unknown internal error");
        BOOST_LOG_TRIVIAL(error) << "Uncaught exception: Unknown error";
    }
}

IMPLEMENT_APP(GUI_App)

GUI_App::GUI_App()
    : wxApp()
    , m_em_unit(10)
    , m_imgui(new ImGuiWrapper())
{}

GUI_App::~GUI_App()
{
    if (app_config != nullptr)
        delete app_config;

    if (preset_bundle != nullptr)
        delete preset_bundle;

    if (preset_updater != nullptr)
        delete preset_updater;
}

bool GUI_App::OnInit()
{
    try {
        return on_init_inner();
    } catch (...) {
        generic_exception_handle();
        return false;
    }
}

bool GUI_App::on_init_inner()
{
    // Verify resources path
    const wxString resources_dir = from_u8(Slic3r::resources_dir());
    wxCHECK_MSG(wxDirExists(resources_dir), false,
        wxString::Format("Resources path does not exist or is not a directory: %s", resources_dir));

    SetAppName(SLIC3R_APP_KEY);
    SetAppDisplayName(SLIC3R_APP_NAME);

// Enable this to get the default Win32 COMCTRL32 behavior of static boxes.
//    wxSystemOptions::SetOption("msw.staticbox.optimized-paint", 0);
// Enable this to disable Windows Vista themes for all wxNotebooks. The themes seem to lead to terrible
// performance when working on high resolution multi-display setups.
//    wxSystemOptions::SetOption("msw.notebook.themed-background", 0);

//     Slic3r::debugf "wxWidgets version %s, Wx version %s\n", wxVERSION_STRING, wxVERSION;

    // Set the Slic3r data directory at the Slic3r XS module.
    // Unix: ~/ .Slic3r
    // Windows : "C:\Users\username\AppData\Roaming\Slic3r" or "C:\Documents and Settings\username\Application Data\Slic3r"
    // Mac : "~/Library/Application Support/Slic3r"
    if (data_dir().empty())
        set_data_dir(wxStandardPaths::Get().GetUserDataDir().ToUTF8().data());

    app_config = new AppConfig();
    preset_bundle = new PresetBundle();

    // just checking for existence of Slic3r::data_dir is not enough : it may be an empty directory
    // supplied as argument to --datadir; in that case we should still run the wizard
    preset_bundle->setup_directories();

    app_conf_exists = app_config->exists();
    // load settings
    app_conf_exists = app_config->exists();
    if (app_conf_exists) {
        app_config->load();
    }

    app_config->set("version", SLIC3R_VERSION);
    app_config->save();

#ifdef __WXMSW__
    associate_3mf_files();
#endif // __WXMSW__

    preset_updater = new PresetUpdater();
    Bind(EVT_SLIC3R_VERSION_ONLINE, [this](const wxCommandEvent &evt) {
        app_config->set("version_online", into_u8(evt.GetString()));
        app_config->save();
    });

    // initialize label colors and fonts
    init_label_colours();
    init_fonts();

    load_language();

    // Suppress the '- default -' presets.
    preset_bundle->set_default_suppressed(app_config->get("no_defaults") == "1");
    try {
        preset_bundle->load_presets(*app_config);
    } catch (const std::exception &ex) {
        show_error(nullptr, from_u8(ex.what()));
    }

    register_dpi_event();

    // Let the libslic3r know the callback, which will translate messages on demand.
    Slic3r::I18N::set_translate_callback(libslic3r_translate_callback);

    // application frame
    if (wxImage::FindHandler(wxBITMAP_TYPE_PNG) == nullptr)
        wxImage::AddHandler(new wxPNGHandler());
    mainframe = new MainFrame();
    sidebar().obj_list()->init_objects(); // propagate model objects to object list
//     update_mode(); // !!! do that later
    SetTopWindow(mainframe);

    m_printhost_job_queue.reset(new PrintHostJobQueue(mainframe->printhost_queue_dlg()));

    Bind(wxEVT_IDLE, [this](wxIdleEvent& event)
    {
        if (! plater_)
            return;

        if (app_config->dirty() && app_config->get("autosave") == "1")
            app_config->save();

        this->obj_manipul()->update_if_dirty();

        // Preset updating & Configwizard are done after the above initializations,
        // and after MainFrame is created & shown.
        // The extra CallAfter() is needed because of Mac, where this is the only way
        // to popup a modal dialog on start without screwing combo boxes.
        // This is ugly but I honestly found no better way to do it.
        // Neither wxShowEvent nor wxWindowCreateEvent work reliably.
        static bool once = true;
        if (once) {
            once = false;

            PresetUpdater::UpdateResult updater_result;
            try {
                updater_result = preset_updater->config_update();
                if (updater_result == PresetUpdater::R_INCOMPAT_EXIT) {
                    mainframe->Close();
                } else if (updater_result == PresetUpdater::R_INCOMPAT_CONFIGURED) {
                    app_conf_exists = true;
                }
            } catch (const std::exception &ex) {
                show_error(nullptr, from_u8(ex.what()));
            }

            CallAfter([this] {
                config_wizard_startup(app_conf_exists);
                preset_updater->slic3r_update_notify();
                preset_updater->sync(preset_bundle);
            });
        }
    });

    load_current_presets();

    mainframe->Show(true);

    /* Temporary workaround for the correct behavior of the Scrolled sidebar panel:
     * change min hight of object list to the normal min value (15 * wxGetApp().em_unit()) 
     * after first whole Mainframe updating/layouting
     */
    if (obj_list()->GetMinSize().GetY() > 15 * em_unit())
        obj_list()->SetMinSize(wxSize(-1, 15 * em_unit()));

    update_mode(); // update view mode after fix of the object_list size

    m_initialized = true;
    return true;
}

unsigned GUI_App::get_colour_approx_luma(const wxColour &colour)
{
    double r = colour.Red();
    double g = colour.Green();
    double b = colour.Blue();

    return std::round(std::sqrt(
        r * r * .241 +
        g * g * .691 +
        b * b * .068
        ));
}

bool GUI_App::dark_mode()
{
    const unsigned luma = get_colour_approx_luma(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    return luma < 128;
}

bool GUI_App::dark_mode_menus()
{
#if __APPLE__
    return mac_dark_mode();
#else
    return dark_mode();
#endif
}

void GUI_App::init_label_colours()
{
    if (dark_mode()) {
        m_color_label_modified = wxColour(253, 111, 40);
        m_color_label_sys = wxColour(115, 220, 103);
    }
    else {
        m_color_label_modified = wxColour(252, 77, 1);
        m_color_label_sys = wxColour(26, 132, 57);
    }
    m_color_label_default = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
}

void GUI_App::update_label_colours_from_appconfig()
{
    if (app_config->has("label_clr_sys")) {
        auto str = app_config->get("label_clr_sys");
        if (str != "")
            m_color_label_sys = wxColour(str);
    }

    if (app_config->has("label_clr_modified")) {
        auto str = app_config->get("label_clr_modified");
        if (str != "")
            m_color_label_modified = wxColour(str);
    }
}

void GUI_App::init_fonts()
{
    m_small_font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    m_bold_font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Bold();
    m_normal_font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);

#ifdef __WXMAC__
    m_small_font.SetPointSize(11);
    m_bold_font.SetPointSize(13);
#endif /*__WXMAC__*/
}

void GUI_App::update_fonts(const MainFrame *main_frame)
{
    /* Only normal and bold fonts are used for an application rescale,
     * because of under MSW small and normal fonts are the same.
     * To avoid same rescaling twice, just fill this values
     * from rescaled MainFrame
     */
	if (main_frame == nullptr)
		main_frame = this->mainframe;
    m_normal_font   = main_frame->normal_font();
    m_small_font    = m_normal_font;
    m_bold_font     = main_frame->normal_font().Bold();
    m_em_unit       = main_frame->em_unit();
}

void GUI_App::set_label_clr_modified(const wxColour& clr) {
    m_color_label_modified = clr;
    auto clr_str = wxString::Format(wxT("#%02X%02X%02X"), clr.Red(), clr.Green(), clr.Blue());
    std::string str = clr_str.ToStdString();
    app_config->set("label_clr_modified", str);
    app_config->save();
}

void GUI_App::set_label_clr_sys(const wxColour& clr) {
    m_color_label_sys = clr;
    auto clr_str = wxString::Format(wxT("#%02X%02X%02X"), clr.Red(), clr.Green(), clr.Blue());
    std::string str = clr_str.ToStdString();
    app_config->set("label_clr_sys", str);
    app_config->save();
}

float GUI_App::toolbar_icon_scale(const bool is_limited/* = false*/) const
{
#ifdef __APPLE__
    const float icon_sc = 1.0f; // for Retina display will be used its own scale
#else
    const float icon_sc = m_em_unit*0.1f;
#endif // __APPLE__

    const std::string& use_val  = app_config->get("use_custom_toolbar_size");
    const std::string& val      = app_config->get("custom_toolbar_size");

    if (val.empty() || use_val.empty() || use_val == "0")
        return icon_sc;

    int int_val = atoi(val.c_str());
    if (is_limited && int_val < 50)
        int_val = 50;

    return 0.01f * int_val * icon_sc;
}

void GUI_App::recreate_GUI()
{
    // Weird things happen as the Paint messages are floating around the windows being destructed.
    // Avoid the Paint messages by hiding the main window.
    // Also the application closes much faster without these unnecessary screen refreshes.
    // In addition, there were some crashes due to the Paint events sent to already destructed windows.
    mainframe->Show(false);

    const auto msg_name = _(L("Changing of an application language")) + dots;
    wxProgressDialog dlg(msg_name, msg_name);
    dlg.Pulse();

    // to make sure nobody accesses data from the soon-to-be-destroyed widgets:
    tabs_list.clear();
    plater_ = nullptr;

    dlg.Update(10, _(L("Recreating")) + dots);

    MainFrame* topwindow = mainframe;
    mainframe = new MainFrame();
    sidebar().obj_list()->init_objects(); // propagate model objects to object list

    if (topwindow) {
        SetTopWindow(mainframe);

        dlg.Update(30, _(L("Recreating")) + dots);
        topwindow->Destroy();
    }

    dlg.Update(80, _(L("Loading of current presets")) + dots);

    m_printhost_job_queue.reset(new PrintHostJobQueue(mainframe->printhost_queue_dlg()));

    load_current_presets();

    mainframe->Show(true);

    dlg.Update(90, _(L("Loading of a mode view")) + dots);

    /* Temporary workaround for the correct behavior of the Scrolled sidebar panel:
    * change min hight of object list to the normal min value (15 * wxGetApp().em_unit())
    * after first whole Mainframe updating/layouting
    */
    if (obj_list()->GetMinSize().GetY() > 15 * em_unit())
        obj_list()->SetMinSize(wxSize(-1, 15 * em_unit()));

    update_mode();

    // #ys_FIXME_delete_after_testing  Do we still need this  ?
//     CallAfter([]() {
//         // Run the config wizard, don't offer the "reset user profile" checkbox.
//         config_wizard_startup(true);
//     });
}

void GUI_App::system_info()
{
    SysInfoDialog dlg;
    dlg.ShowModal();
}

void GUI_App::keyboard_shortcuts()
{
    KBShortcutsDialog dlg;
    dlg.ShowModal();
}

// static method accepting a wxWindow object as first parameter
bool GUI_App::catch_error(std::function<void()> cb,
    //                       wxMessageDialog* message_dialog,
    const std::string& err /*= ""*/)
{
    if (!err.empty()) {
        if (cb)
            cb();
        //         if (message_dialog)
        //             message_dialog->(err, "Error", wxOK | wxICON_ERROR);
        show_error(/*this*/nullptr, err);
        return true;
    }
    return false;
}

// static method accepting a wxWindow object as first parameter
void fatal_error(wxWindow* parent)
{
    show_error(parent, "");
    //     exit 1; // #ys_FIXME
}

// Called after the Preferences dialog is closed and the program settings are saved.
// Update the UI based on the current preferences.
void GUI_App::update_ui_from_settings()
{
    mainframe->update_ui_from_settings();
}

void GUI_App::persist_window_geometry(wxTopLevelWindow *window, bool default_maximized)
{
    const std::string name = into_u8(window->GetName());

    window->Bind(wxEVT_CLOSE_WINDOW, [=](wxCloseEvent &event) {
        window_pos_save(window, name);
        event.Skip();
    });

    window_pos_restore(window, name, default_maximized);

    on_window_geometry(window, [=]() {
        window_pos_sanitize(window);
    });
}

void GUI_App::load_project(wxWindow *parent, wxString& input_file) const
{
    input_file.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
        _(L("Choose one file (3MF/AMF):")),
        app_config->get_last_dir(), "",
        file_wildcards(FT_PROJECT), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        input_file = dialog.GetPath();
}

void GUI_App::import_model(wxWindow *parent, wxArrayString& input_files) const
{
    input_files.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
        _(L("Choose one or more files (STL/OBJ/AMF/3MF/PRUSA):")),
        from_u8(app_config->get_last_dir()), "",
        file_wildcards(FT_MODEL), wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        dialog.GetPaths(input_files);
}

bool GUI_App::switch_language()
{
    if (select_language()) {
        save_language();
        _3DScene::remove_all_canvases();
        recreate_GUI();
        return true;
    } else {
        return false;
    }
}

// select language from the list of installed languages
bool GUI_App::select_language()
{
    const auto langs = get_installed_languages();
    wxArrayString names;
    names.Alloc(langs.size());

    int init_selection = -1;
    const auto current_language = m_wxLocale ? m_wxLocale->GetLanguage() : wxLocale::GetSystemLanguage();

    for (size_t i = 0; i < langs.size(); i++) {
        const auto lang = langs[i]->Language;
        const bool is_english = lang >= wxLANGUAGE_ENGLISH && lang <= wxLANGUAGE_ENGLISH_ZIMBABWE;

        if (lang == current_language || (current_language == wxLANGUAGE_UNKNOWN && is_english)) {
            init_selection = i;
        }

        names.Add(langs[i]->Description);
    }

    const long index = wxGetSingleChoiceIndex(
        _(L("Select the language")),
        _(L("Language")), names, init_selection >= 0 ? init_selection : 0);

    if (index != -1) {
        const wxLanguageInfo *lang = langs[index];
        if (lang->Language == current_language) {
            // There was no change
            return false;
        }

        m_wxLocale = new wxLocale;    // FIXME: leak?
        m_wxLocale->Init(lang->Language);
		m_wxLocale->AddCatalogLookupPathPrefix(from_u8(localization_dir()));
        m_wxLocale->AddCatalog(SLIC3R_APP_KEY);
		//FIXME This is a temporary workaround, the correct solution is to switch to "C" locale during file import / export only.
		wxSetlocale(LC_NUMERIC, "C");
        Preset::update_suffix_modified();
        m_imgui->set_language(into_u8(lang->CanonicalName));
        return true;
    }

    return false;
}

// Load gettext translation files and activate them at the start of the application,
// based on the "translation_language" key stored in the application config.
bool GUI_App::load_language()
{
    wxString language = wxEmptyString;
    if (app_config->has("translation_language"))
        language = app_config->get("translation_language");

    if (language.IsEmpty()) {
        int lang = wxLocale::GetSystemLanguage();
        if (lang != wxLANGUAGE_UNKNOWN) {
			const wxLanguageInfo *info = wxLocale::GetLanguageInfo(lang);
            if (info != nullptr)
                language = info->CanonicalName;
        }
    }

    const wxLanguageInfo *info = nullptr;
    if (! language.IsEmpty()) {
        const auto langs = get_installed_languages();
        for (const wxLanguageInfo *this_info : langs)
            if (this_info->CanonicalName == language) {
                info = this_info;
                break;
            }
    }

    m_wxLocale = new wxLocale;
    if (info == nullptr) {
        m_wxLocale->Init(wxLANGUAGE_DEFAULT);
        m_imgui->set_language("en");
    } else {
        m_wxLocale->Init(info->Language);
        m_wxLocale->AddCatalogLookupPathPrefix(from_u8(localization_dir()));
        m_wxLocale->AddCatalog(SLIC3R_APP_KEY);
        m_imgui->set_language(into_u8(info->CanonicalName));
    }
	//FIXME This is a temporary workaround, the correct solution is to switch to "C" locale during file import / export only.
    wxSetlocale(LC_NUMERIC, "C");
    Preset::update_suffix_modified();
    return true;
}

// save language at application config
void GUI_App::save_language()
{
    wxString language = wxEmptyString;
    if (m_wxLocale)
        language = m_wxLocale->GetCanonicalName();

    app_config->set("translation_language", language.ToUTF8().data());
    app_config->save();
}

// Get a list of installed languages
std::vector<const wxLanguageInfo*> GUI_App::get_installed_languages()
{
    std::vector<const wxLanguageInfo*> res;

	wxDir dir(from_u8(localization_dir()));
    wxString filename;
    const wxLanguageInfo * langinfo;
    wxString name = wxLocale::GetLanguageName(wxLANGUAGE_DEFAULT);
    if (!name.IsEmpty()) {
        res.push_back(wxLocale::GetLanguageInfo(wxLANGUAGE_DEFAULT));
    }

    for (bool cont = dir.GetFirst(&filename, wxEmptyString, wxDIR_DIRS); cont; cont = dir.GetNext(&filename)) {
        langinfo = wxLocale::FindLanguageInfo(filename);
        if (langinfo != NULL) {
            auto full_file_name = dir.GetName() + wxFileName::GetPathSeparator() +
                filename + wxFileName::GetPathSeparator() + SLIC3R_APP_KEY + wxT(".mo");
            if (wxFileExists(full_file_name)) {
                res.push_back(langinfo);
            }
        }
    }

    return res;
}

Tab* GUI_App::get_tab(Preset::Type type)
{
    for (Tab* tab: tabs_list)
        if (tab->type() == type)
            return tab->complited() ? tab : nullptr; // To avoid actions with no-completed Tab
    return nullptr;
}

ConfigOptionMode GUI_App::get_mode()
{
    if (!app_config->has("view_mode"))
        return comSimple;

    const auto mode = app_config->get("view_mode");
    return mode == "expert" ? comExpert : 
           mode == "simple" ? comSimple : comAdvanced;
}

void GUI_App::save_mode(const /*ConfigOptionMode*/int mode) 
{
    const std::string mode_str = mode == comExpert ? "expert" :
                                 mode == comSimple ? "simple" : "advanced";
    app_config->set("view_mode", mode_str);
    app_config->save(); 
    update_mode();
}

// Update view mode according to selected menu
void GUI_App::update_mode()
{
    sidebar().update_mode();

    for (auto tab : tabs_list)
        tab->update_mode();

    plater()->update_object_menu();
}

void GUI_App::add_config_menu(wxMenuBar *menu)
{
    auto local_menu = new wxMenu();
    wxWindowID config_id_base = wxWindow::NewControlId(int(ConfigMenuCnt));

    const auto config_wizard_name = _(ConfigWizard::name(true).wx_str());
    const auto config_wizard_tooltip = wxString::Format(_(L("Run %s")), config_wizard_name);
    // Cmd+, is standard on OS X - what about other operating systems?
    local_menu->Append(config_id_base + ConfigMenuWizard, config_wizard_name + dots, config_wizard_tooltip);
    local_menu->Append(config_id_base + ConfigMenuSnapshots, _(L("&Configuration Snapshots")) + dots, _(L("Inspect / activate configuration snapshots")));
    local_menu->Append(config_id_base + ConfigMenuTakeSnapshot, _(L("Take Configuration &Snapshot")), _(L("Capture a configuration snapshot")));
    // 	local_menu->Append(config_id_base + ConfigMenuUpdate, 		_(L("Check for updates")), 					_(L("Check for configuration updates")));
    local_menu->AppendSeparator();
    local_menu->Append(config_id_base + ConfigMenuPreferences, _(L("&Preferences")) + dots + 
#ifdef __APPLE__
        "\tCtrl+,",
#else
        "\tCtrl+P",
#endif
        _(L("Application preferences")));
    local_menu->AppendSeparator();
    auto mode_menu = new wxMenu();
    mode_menu->AppendRadioItem(config_id_base + ConfigMenuModeSimple, _(L("Simple")), _(L("Simple View Mode")));
    mode_menu->AppendRadioItem(config_id_base + ConfigMenuModeAdvanced, _(L("Advanced")), _(L("Advanced View Mode")));
    mode_menu->AppendRadioItem(config_id_base + ConfigMenuModeExpert, _(L("Expert")), _(L("Expert View Mode")));
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { if(get_mode() == comSimple) evt.Check(true); }, config_id_base + ConfigMenuModeSimple);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { if(get_mode() == comAdvanced) evt.Check(true); }, config_id_base + ConfigMenuModeAdvanced);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { if(get_mode() == comExpert) evt.Check(true); }, config_id_base + ConfigMenuModeExpert);

    local_menu->AppendSubMenu(mode_menu, _(L("Mode")), wxString::Format(_(L("%s View Mode")), SLIC3R_APP_NAME));
    local_menu->AppendSeparator();
    local_menu->Append(config_id_base + ConfigMenuLanguage, _(L("Change Application &Language")));
    local_menu->AppendSeparator();
    local_menu->Append(config_id_base + ConfigMenuFlashFirmware, _(L("Flash printer &firmware")), _(L("Upload a firmware image into an Arduino based printer")));
    // TODO: for when we're able to flash dictionaries
    // local_menu->Append(config_id_base + FirmwareMenuDict,  _(L("Flash language file")),    _(L("Upload a language dictionary file into a Prusa printer")));

    local_menu->Bind(wxEVT_MENU, [this, config_id_base](wxEvent &event) {
        switch (event.GetId() - config_id_base) {
        case ConfigMenuWizard:
            config_wizard(ConfigWizard::RR_USER);
            break;
        case ConfigMenuTakeSnapshot:
            // Take a configuration snapshot.
            if (check_unsaved_changes()) {
                wxTextEntryDialog dlg(nullptr, _(L("Taking configuration snapshot")), _(L("Snapshot name")));
                
                // set current normal font for dialog children, 
                // because of just dlg.SetFont(normal_font()) has no result;
                for (auto child : dlg.GetChildren())
                    child->SetFont(normal_font());

                if (dlg.ShowModal() == wxID_OK)
                    app_config->set("on_snapshot",
                    Slic3r::GUI::Config::SnapshotDB::singleton().take_snapshot(
                    *app_config, Slic3r::GUI::Config::Snapshot::SNAPSHOT_USER, dlg.GetValue().ToUTF8().data()).id);
            }
            break;
        case ConfigMenuSnapshots:
            if (check_unsaved_changes()) {
                std::string on_snapshot;
                if (Config::SnapshotDB::singleton().is_on_snapshot(*app_config))
                    on_snapshot = app_config->get("on_snapshot");
                ConfigSnapshotDialog dlg(Slic3r::GUI::Config::SnapshotDB::singleton(), on_snapshot);
                dlg.ShowModal();
                if (!dlg.snapshot_to_activate().empty()) {
                    if (!Config::SnapshotDB::singleton().is_on_snapshot(*app_config))
                        Config::SnapshotDB::singleton().take_snapshot(*app_config, Config::Snapshot::SNAPSHOT_BEFORE_ROLLBACK);
                    app_config->set("on_snapshot",
                        Config::SnapshotDB::singleton().restore_snapshot(dlg.snapshot_to_activate(), *app_config).id);
                    preset_bundle->load_presets(*app_config);
                    // Load the currently selected preset into the GUI, update the preset selection box.
                    load_current_presets();
                }
            }
            break;
        case ConfigMenuPreferences:
        {
            PreferencesDialog dlg(mainframe);
            dlg.ShowModal();
            break;
        }
        case ConfigMenuLanguage:
        {
            /* Before change application language, let's check unsaved changes on 3D-Scene
             * and draw user's attention to the application restarting after a language change
             */
            wxMessageDialog dialog(nullptr,
                _(L("Switching the language will trigger application restart.\n"
                    "You will lose content of the plater.")) + "\n\n" +
                _(L("Do you want to proceed?")),
                wxString(SLIC3R_APP_NAME) + " - " + _(L("Language selection")),
                wxICON_QUESTION | wxOK | wxCANCEL);
            if ( dialog.ShowModal() == wxID_CANCEL)
                return;

            switch_language();
            break;
        }
        case ConfigMenuFlashFirmware:
            FirmwareDialog::run(mainframe);
            break;
        default:
            break;
        }
    });
    
    using std::placeholders::_1;
    
    auto modfn = [this](int mode, wxCommandEvent&) { if(get_mode() != mode) save_mode(mode); };
    mode_menu->Bind(wxEVT_MENU, std::bind(modfn, comSimple, _1),   config_id_base + ConfigMenuModeSimple);
    mode_menu->Bind(wxEVT_MENU, std::bind(modfn, comAdvanced, _1), config_id_base + ConfigMenuModeAdvanced);
    mode_menu->Bind(wxEVT_MENU, std::bind(modfn, comExpert, _1),   config_id_base + ConfigMenuModeExpert);

    menu->Append(local_menu, _(L("&Configuration")));
}

// This is called when closing the application, when loading a config file or when starting the config wizard
// to notify the user whether he is aware that some preset changes will be lost.
bool GUI_App::check_unsaved_changes(const wxString &header)
{
    wxString dirty;
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (Tab *tab : tabs_list)
        if (tab->supports_printer_technology(printer_technology) && tab->current_preset_is_dirty())
            if (dirty.empty())
                dirty = tab->title();
            else
                dirty += wxString(", ") + tab->title();
    if (dirty.empty())
        // No changes, the application may close or reload presets.
        return true;
    // Ask the user.
    wxString message;
    if (! header.empty())
    	message = header + "\n\n";
    message += _(L("The presets on the following tabs were modified")) + ": " + dirty + "\n\n" + _(L("Discard changes and continue anyway?"));
    wxMessageDialog dialog(mainframe,
        message,
        wxString(SLIC3R_APP_NAME) + " - " + _(L("Unsaved Presets")),
        wxICON_QUESTION | wxYES_NO | wxNO_DEFAULT);
    return dialog.ShowModal() == wxID_YES;
}

bool GUI_App::checked_tab(Tab* tab)
{
    bool ret = true;
    if (find(tabs_list.begin(), tabs_list.end(), tab) == tabs_list.end())
        ret = false;
    return ret;
}

// Update UI / Tabs to reflect changes in the currently loaded presets
void GUI_App::load_current_presets()
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
	this->plater()->set_printer_technology(printer_technology);
    for (Tab *tab : tabs_list)
		if (tab->supports_printer_technology(printer_technology)) {
			if (tab->type() == Preset::TYPE_PRINTER)
				static_cast<TabPrinter*>(tab)->update_pages();
			tab->load_current_preset();
		}
}

bool GUI_App::OnExceptionInMainLoop()
{
    generic_exception_handle();
    return false;
}

#ifdef __APPLE__
// wxWidgets override to get an event on open files.
void GUI_App::MacOpenFiles(const wxArrayString &fileNames)
{
    std::vector<std::string> files;
    for (size_t i = 0; i < fileNames.GetCount(); ++ i)
        files.emplace_back(fileNames[i].ToUTF8().data());
    this->plater()->load_files(files, true, true);
}
#endif /* __APPLE */

Sidebar& GUI_App::sidebar()
{
    return plater_->sidebar();
}

ObjectManipulation* GUI_App::obj_manipul()
{
    // If this method is called before plater_ has been initialized, return nullptr (to avoid a crash)
    return (plater_ != nullptr) ? sidebar().obj_manipul() : nullptr;
}

ObjectSettings* GUI_App::obj_settings()
{
    return sidebar().obj_settings();
}

ObjectList* GUI_App::obj_list()
{
    return sidebar().obj_list();
}

ObjectLayers* GUI_App::obj_layers()
{
    return sidebar().obj_layers();
}

Plater* GUI_App::plater()
{
    return plater_;
}

Model& GUI_App::model()
{
    return plater_->model();
}

wxNotebook* GUI_App::tab_panel() const
{
    return mainframe->m_tabpanel;
}

// extruders count from selected printer preset
int GUI_App::extruders_cnt() const
{
    const Preset& preset = preset_bundle->printers.get_selected_preset();
    return preset.printer_technology() == ptSLA ? 1 :
           preset.config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();
}

// extruders count from edited printer preset
int GUI_App::extruders_edited_cnt() const
{
    const Preset& preset = preset_bundle->printers.get_edited_preset();
    return preset.printer_technology() == ptSLA ? 1 :
           preset.config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();
}

wxString GUI_App::current_language_code_safe() const
{
	// Translate the language code to a code, for which Prusa Research maintains translations.
	wxString language_code = this->current_language_code();
	size_t   idx_underscore = language_code.find(language_code);
	if (idx_underscore != wxString::npos)
		language_code = language_code.substr(0, idx_underscore);
	const std::map<wxString, wxString> mapping {
		{ "cs", 	"cs_CZ", },
		{ "sk", 	"cs_CZ", },
		{ "de", 	"de_DE", },
		{ "es", 	"es_ES", },
		{ "fr", 	"fr_FR", },
		{ "it", 	"it_IT", },
		{ "ja", 	"ja_JP", },
		{ "ko", 	"ko_KR", },
		{ "pl", 	"pl_PL", },
		{ "uk", 	"uk_UA", },
		{ "zh", 	"zh_CN", },
	};
	auto it = mapping.find(language_code);
	if (it != mapping.end())
		language_code = it->second;
	else
		language_code = "en_US";
	return language_code;
}

void GUI_App::open_web_page_localized(const std::string &http_address)
{
    wxLaunchDefaultBrowser(http_address + "&lng=" + this->current_language_code_safe());
}

void GUI_App::window_pos_save(wxTopLevelWindow* window, const std::string &name)
{
    if (name.empty()) { return; }
    const auto config_key = (boost::format("window_%1%") % name).str();

    WindowMetrics metrics = WindowMetrics::from_window(window);
    app_config->set(config_key, metrics.serialize());
    app_config->save();
}

void GUI_App::window_pos_restore(wxTopLevelWindow* window, const std::string &name, bool default_maximized)
{
    if (name.empty()) { return; }
    const auto config_key = (boost::format("window_%1%") % name).str();

    if (! app_config->has(config_key)) {
        window->Maximize(default_maximized);
        return;
    }

    auto metrics = WindowMetrics::deserialize(app_config->get(config_key));
    if (! metrics) {
        window->Maximize(default_maximized);
        return;
    }

    window->SetSize(metrics->get_rect());
    window->Maximize(metrics->get_maximized());
}

void GUI_App::window_pos_sanitize(wxTopLevelWindow* window)
{
    unsigned display_idx = wxDisplay::GetFromWindow(window);
    wxRect display;
    if (display_idx == wxNOT_FOUND) {
        display = wxDisplay(0u).GetClientArea();
        window->Move(display.GetTopLeft());
    } else {
        display = wxDisplay(display_idx).GetClientArea();
    }

    auto metrics = WindowMetrics::from_window(window);
    metrics.sanitize_for_display(display);
    if (window->GetScreenRect() != metrics.get_rect()) {
        window->SetSize(metrics.get_rect());
    }
}

// static method accepting a wxWindow object as first parameter
// void warning_catcher{
//     my($self, $message_dialog) = @_;
//     return sub{
//         my $message = shift;
//         return if $message = ~/ GLUquadricObjPtr | Attempt to free unreferenced scalar / ;
//         my @params = ($message, 'Warning', wxOK | wxICON_WARNING);
//         $message_dialog
//             ? $message_dialog->(@params)
//             : Wx::MessageDialog->new($self, @params)->ShowModal;
//     };
// }

// Do we need this function???
// void GUI_App::notify(message) {
//     auto frame = GetTopWindow();
//     // try harder to attract user attention on OS X
//     if (!frame->IsActive())
//         frame->RequestUserAttention(defined(__WXOSX__/*&Wx::wxMAC */)? wxUSER_ATTENTION_ERROR : wxUSER_ATTENTION_INFO);
// 
//     // There used to be notifier using a Growl application for OSX, but Growl is dead.
//     // The notifier also supported the Linux X D - bus notifications, but that support was broken.
//     //TODO use wxNotificationMessage ?
// }


#ifdef __WXMSW__
void GUI_App::associate_3mf_files()
{
    // see as reference: https://stackoverflow.com/questions/20245262/c-program-needs-an-file-association

    auto reg_set = [](HKEY hkeyHive, const wchar_t* pszVar, const wchar_t* pszValue)->bool
    {
        wchar_t szValueCurrent[1000];
        DWORD dwType;
        DWORD dwSize = sizeof(szValueCurrent);

        int iRC = ::RegGetValueW(hkeyHive, pszVar, nullptr, RRF_RT_ANY, &dwType, szValueCurrent, &dwSize);

        bool bDidntExist = iRC == ERROR_FILE_NOT_FOUND;

        if ((iRC != ERROR_SUCCESS) && !bDidntExist)
            // an error occurred
            return false;

        if (!bDidntExist)
        {
            if (dwType != REG_SZ)
                // invalid type
                return false;

            if (::wcscmp(szValueCurrent, pszValue) == 0)
                // value already set
                return false;
        }

        DWORD dwDisposition;
        HKEY hkey;
        iRC = ::RegCreateKeyExW(hkeyHive, pszVar, 0, 0, 0, KEY_ALL_ACCESS, nullptr, &hkey, &dwDisposition);
        bool ret = false;
        if (iRC == ERROR_SUCCESS)
        {
            iRC = ::RegSetValueExW(hkey, L"", 0, REG_SZ, (BYTE*)pszValue, (::wcslen(pszValue) + 1) * sizeof(wchar_t));
            if (iRC == ERROR_SUCCESS)
                ret = true;
        }

        RegCloseKey(hkey);
        return ret;
    };

    wchar_t app_path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, app_path, sizeof(app_path));

    std::wstring prog_path = L"\"" + std::wstring(app_path) + L"\"";
    std::wstring prog_id = L"Prusa.Slicer.1";
    std::wstring prog_desc = L"PrusaSlicer";
    std::wstring prog_command = prog_path + L" \"%1\"";
    std::wstring reg_base = L"Software\\Classes";
    std::wstring reg_extension = reg_base + L"\\.3mf";
    std::wstring reg_prog_id = reg_base + L"\\" + prog_id;
    std::wstring reg_prog_id_command = reg_prog_id + L"\\Shell\\Open\\Command";

    bool is_new = false;
    is_new |= reg_set(HKEY_CURRENT_USER, reg_extension.c_str(), prog_id.c_str());
    is_new |= reg_set(HKEY_CURRENT_USER, reg_prog_id.c_str(), prog_desc.c_str());
    is_new |= reg_set(HKEY_CURRENT_USER, reg_prog_id_command.c_str(), prog_command.c_str());

    if (is_new)
        // notify Windows only when any of the values gets changed
        ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
#endif // __WXMSW__

} // GUI
} //Slic3r
