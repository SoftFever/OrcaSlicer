#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_ObjectManipulation.hpp"
#include "I18N.hpp"

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
#include <wx/dir.h>
#include <wx/wupdlock.h>
#include <wx/filefn.h>

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
#include "ConfigWizard_private.hpp"
#include "slic3r/Config/Snapshot.hpp"
#include "ConfigSnapshotDialog.hpp"
#include "FirmwareDialog.hpp"
#include "Preferences.hpp"
#include "Tab.hpp"
#include "SysInfoDialog.hpp"
#include "KBShortcutsDialog.hpp"

namespace Slic3r {
namespace GUI {


wxString file_wildcards(FileType file_type, const std::string &custom_extension)
{
    static const std::string defaults[FT_SIZE] = {
        /* FT_STL */   "STL files (*.stl)|*.stl;*.STL",
        /* FT_OBJ */   "OBJ files (*.obj)|*.obj;*.OBJ",
        /* FT_AMF */   "AMF files (*.amf)|*.zip.amf;*.amf;*.AMF;*.xml;*.XML",
        /* FT_3MF */   "3MF files (*.3mf)|*.3mf;*.3MF;",
        /* FT_PRUSA */ "Prusa Control files (*.prusa)|*.prusa;*.PRUSA",
        /* FT_GCODE */ "G-code files (*.gcode, *.gco, *.g, *.ngc)|*.gcode;*.GCODE;*.gco;*.GCO;*.g;*.G;*.ngc;*.NGC",
        /* FT_MODEL */ "Known files (*.stl, *.obj, *.amf, *.xml, *.3mf, *.prusa)|*.stl;*.STL;*.obj;*.OBJ;*.amf;*.AMF;*.xml;*.XML;*.3mf;*.3MF;*.prusa;*.PRUSA",

        /* FT_INI */   "INI files (*.ini)|*.ini;*.INI",
        /* FT_SVG */   "SVG files (*.svg)|*.svg;*.SVG",
        /* FT_PNGZIP */"Masked SLA files (*.sl1)|*.sl1;*.SL1",
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

IMPLEMENT_APP(GUI_App)

GUI_App::GUI_App()
    : wxApp()
    , m_em_unit(10)
#if ENABLE_IMGUI
    , m_imgui(new ImGuiWrapper())
#endif // ENABLE_IMGUI
{}

bool GUI_App::OnInit()
{
    // Verify resources path
    const wxString resources_dir = from_u8(Slic3r::resources_dir());
    wxCHECK_MSG(wxDirExists(resources_dir), false,
        wxString::Format("Resources path does not exist or is not a directory: %s", resources_dir));

#if ENABLE_IMGUI
    wxCHECK_MSG(m_imgui->init(), false, "Failed to initialize ImGui");
#endif // ENABLE_IMGUI

    SetAppName("Slic3rPE-beta");
    SetAppDisplayName("Slic3r Prusa Edition");

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
    try { 
        preset_bundle->setup_directories();
    } catch (const std::exception &ex) {
        show_error(nullptr, ex.what());
        // Exit the application.
        return false;
    }

    app_conf_exists = app_config->exists();
    // load settings
    if (app_conf_exists)
        app_config->load();
    app_config->set("version", SLIC3R_VERSION);
    app_config->save();

    preset_updater = new PresetUpdater();
    Bind(EVT_SLIC3R_VERSION_ONLINE, [this](const wxCommandEvent &evt) {
        app_config->set("version_online", into_u8(evt.GetString()));
        app_config->save();
    });

    load_language();

    // Suppress the '- default -' presets.
    preset_bundle->set_default_suppressed(app_config->get("no_defaults") == "1");
	try {
		preset_bundle->load_presets(*app_config);
	} catch (const std::exception &ex) {
        show_error(nullptr, ex.what());
	}

    // Let the libslic3r know the callback, which will translate messages on demand.
    Slic3r::I18N::set_translate_callback(libslic3r_translate_callback);
    // initialize label colors and fonts
    init_label_colours();
    init_fonts();

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
        // This is ugly but I honestly found not better way to do it.
        // Neither wxShowEvent nor wxWindowCreateEvent work reliably.
        static bool once = true;
        if (once) {
            once = false;

            try {
                if (!preset_updater->config_update()) {
                    mainframe->Close();
                }
            } catch (const std::exception &ex) {
                show_error(nullptr, ex.what());
            }

            CallAfter([this] {
                if (!config_wizard_startup(app_conf_exists)) {
                    // Only notify if there was not wizard so as not to bother too much ...
                    preset_updater->slic3r_update_notify();
                }
                preset_updater->sync(preset_bundle);
            });
        }
    });

    load_current_presets();

    mainframe->Show(true);
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

void GUI_App::init_label_colours()
{
    auto luma = get_colour_approx_luma(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    if (luma >= 128) {
        m_color_label_modified = wxColour(252, 77, 1);
        m_color_label_sys = wxColour(26, 132, 57);
    }
    else {
        m_color_label_modified = wxColour(253, 111, 40);
        m_color_label_sys = wxColour(115, 220, 103);
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

#ifdef __WXMAC__
    m_small_font.SetPointSize(11);
    m_bold_font.SetPointSize(13);
#endif /*__WXMAC__*/
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

void GUI_App::recreate_GUI()
{
    // to make sure nobody accesses data from the soon-to-be-destroyed widgets:
    tabs_list.clear();
    plater_ = nullptr;

    MainFrame* topwindow = mainframe;
    mainframe = new MainFrame();
    sidebar().obj_list()->init_objects(); // propagate model objects to object list

    if (topwindow) {
        SetTopWindow(mainframe);
        topwindow->Destroy();
    }

    m_printhost_job_queue.reset(new PrintHostJobQueue(mainframe->printhost_queue_dlg()));

    load_current_presets();

    mainframe->Show(true);

    // On OSX the UI was not initialized correctly if the wizard was called
    // before the UI was up and running.
    CallAfter([]() {
        // Run the config wizard, don't offer the "reset user profile" checkbox.
        config_wizard_startup(true);
    });
}

void GUI_App::system_info()
{
    SysInfoDialog dlg;
    dlg.ShowModal();
    dlg.Destroy();
}

void GUI_App::keyboard_shortcuts()
{
    KBShortcutsDialog dlg;
    dlg.ShowModal();
    dlg.Destroy();
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

void GUI_App::persist_window_geometry(wxTopLevelWindow *window)
{
    const std::string name = into_u8(window->GetName());

    window->Bind(wxEVT_CLOSE_WINDOW, [=](wxCloseEvent &event) {
        window_pos_save(window, name);
        event.Skip();
    });

    window_pos_restore(window, name);

    on_window_geometry(window, [=]() {
        window_pos_sanitize(window);
    });
}

void GUI_App::load_project(wxWindow *parent, wxString& input_file)
{
    input_file.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
        _(L("Choose one file (3MF):")),
        app_config->get_last_dir(), "",
        file_wildcards(FT_3MF), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        input_file = dialog.GetPath();
}

void GUI_App::import_model(wxWindow *parent, wxArrayString& input_files)
{
    input_files.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
        _(L("Choose one or more files (STL/OBJ/AMF/3MF/PRUSA):")),
        from_u8(app_config->get_last_dir()), "",
        file_wildcards(FT_MODEL), wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        dialog.GetPaths(input_files);
}

// select language from the list of installed languages
bool GUI_App::select_language(  wxArrayString & names,
                                wxArrayLong & identifiers)
{
    wxCHECK_MSG(names.Count() == identifiers.Count(), false,
        _(L("Array of language names and identifiers should have the same size.")));
    int init_selection = 0;
    long current_language = m_wxLocale ? m_wxLocale->GetLanguage() : wxLANGUAGE_UNKNOWN;
    for (auto lang : identifiers) {
        if (lang == current_language)
            break;
        ++init_selection;
    }
    if (init_selection == identifiers.size())
        init_selection = 0;
    long index = wxGetSingleChoiceIndex(_(L("Select the language")), _(L("Language")),
        names, init_selection);
    if (index != -1)
    {
        m_wxLocale = new wxLocale;
        m_wxLocale->Init(identifiers[index]);
		m_wxLocale->AddCatalogLookupPathPrefix(from_u8(localization_dir()));
        m_wxLocale->AddCatalog(/*GetAppName()*/"Slic3rPE");
		//FIXME This is a temporary workaround, the correct solution is to switch to "C" locale during file import / export only.
		wxSetlocale(LC_NUMERIC, "C");
        Preset::update_suffix_modified();
		m_imgui->set_language(m_wxLocale->GetCanonicalName().ToUTF8().data());
        return true;
    }
    return false;
}

// load language saved at application config
bool GUI_App::load_language()
{
    wxString language = wxEmptyString;
    if (app_config->has("translation_language"))
        language = app_config->get("translation_language");

    if (language.IsEmpty())
        return false;
    wxArrayString	names;
    wxArrayLong		identifiers;
    get_installed_languages(names, identifiers);
    for (size_t i = 0; i < identifiers.Count(); i++)
    {
        if (wxLocale::GetLanguageCanonicalName(identifiers[i]) == language)
        {
            m_wxLocale = new wxLocale;
            m_wxLocale->Init(identifiers[i]);
			m_wxLocale->AddCatalogLookupPathPrefix(from_u8(localization_dir()));
            m_wxLocale->AddCatalog(/*GetAppName()*/"Slic3rPE");
			//FIXME This is a temporary workaround, the correct solution is to switch to "C" locale during file import / export only.
            wxSetlocale(LC_NUMERIC, "C");
			Preset::update_suffix_modified();
			m_imgui->set_language(m_wxLocale->GetCanonicalName().ToUTF8().data());
            return true;
        }
    }
    return false;
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

// get list of installed languages 
void GUI_App::get_installed_languages(wxArrayString & names, wxArrayLong & identifiers)
{
    names.Clear();
    identifiers.Clear();

	wxDir dir(from_u8(localization_dir()));
    wxString filename;
    const wxLanguageInfo * langinfo;
    wxString name = wxLocale::GetLanguageName(wxLANGUAGE_DEFAULT);
    if (!name.IsEmpty())
    {
        names.Add(_(L("Default")));
        identifiers.Add(wxLANGUAGE_DEFAULT);
    }
    for (bool cont = dir.GetFirst(&filename, wxEmptyString, wxDIR_DIRS);
        cont; cont = dir.GetNext(&filename))
    {
        langinfo = wxLocale::FindLanguageInfo(filename);
        if (langinfo != NULL)
        {
            auto full_file_name = dir.GetName() + wxFileName::GetPathSeparator() +
                filename + wxFileName::GetPathSeparator() +
                /*GetAppName()*/"Slic3rPE" + 
                wxT(".mo");
            if (wxFileExists(full_file_name))
            {
                names.Add(langinfo->Description);
                identifiers.Add(langinfo->Language);
            }
        }
    }
}

Tab* GUI_App::get_tab(Preset::Type type)
{
    for (Tab* tab: tabs_list)
        if (tab->type() == type)
            return tab;
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
        tab->update_visibility();

    plater()->update_object_menu();
}

void GUI_App::add_config_menu(wxMenuBar *menu)
{
    auto local_menu = new wxMenu();
    wxWindowID config_id_base = wxWindow::NewControlId((int)ConfigMenuCnt);

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
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { evt.Check(get_mode() == comSimple); }, config_id_base + ConfigMenuModeSimple);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { evt.Check(get_mode() == comAdvanced); }, config_id_base + ConfigMenuModeAdvanced);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { evt.Check(get_mode() == comExpert); }, config_id_base + ConfigMenuModeExpert);

    local_menu->AppendSubMenu(mode_menu, _(L("Mode")), _(L("Slic3r View Mode")));
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
            wxArrayString names;
            wxArrayLong identifiers;
            get_installed_languages(names, identifiers);
            if (select_language(names, identifiers)) {
                save_language();
                show_info(mainframe->m_tabpanel, _(L("Application will be restarted")), _(L("Attention!")));
                _3DScene::remove_all_canvases();// remove all canvas before recreate GUI
                recreate_GUI();
            }
            break;
        }
        case ConfigMenuFlashFirmware:
            FirmwareDialog::run(mainframe);
            break;
        default:
            break;
        }
    });
    mode_menu->Bind(wxEVT_MENU, [this, config_id_base](wxEvent& event) {
        int id_mode = event.GetId() - config_id_base;
        save_mode(id_mode - ConfigMenuModeSimple);
    });
    menu->Append(local_menu, _(L("&Configuration")));
}

// This is called when closing the application, when loading a config file or when starting the config wizard
// to notify the user whether he is aware that some preset changes will be lost.
bool GUI_App::check_unsaved_changes()
{
    std::string dirty;
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (Tab *tab : tabs_list)
        if (tab->supports_printer_technology(printer_technology) && tab->current_preset_is_dirty())
            if (dirty.empty())
                dirty = tab->name();
            else
                dirty += std::string(", ") + tab->name();
    if (dirty.empty())
        // No changes, the application may close or reload presets.
        return true;
    // Ask the user.
    auto dialog = new wxMessageDialog(mainframe,
        _(L("You have unsaved changes ")) + dirty + _(L(". Discard changes and continue anyway?")),
        _(L("Unsaved Presets")),
        wxICON_QUESTION | wxYES_NO | wxNO_DEFAULT);
    return dialog->ShowModal() == wxID_YES;
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
			if (tab->name() == "printer")
				static_cast<TabPrinter*>(tab)->update_pages();
			tab->load_current_preset();
		}
}

bool GUI_App::OnExceptionInMainLoop()
{
    try {
        throw;
    } catch (const std::exception &ex) {
        const std::string error = (boost::format("Uncaught exception: %1%") % ex.what()).str();
        BOOST_LOG_TRIVIAL(error) << error;
        show_error(nullptr, from_u8(error));
    } catch (...) {
        const char *error = "Uncaught exception: Unknown error";
        BOOST_LOG_TRIVIAL(error) << error;
        show_error(nullptr, from_u8(error));
    }

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

Plater* GUI_App::plater()
{
    return plater_;
}

ModelObjectPtrs* GUI_App::model_objects()
{
    return &plater_->model().objects;
}

wxNotebook* GUI_App::tab_panel() const
{
    return mainframe->m_tabpanel;
}

int GUI_App::extruders_cnt() const
{
    const Preset& preset = preset_bundle->printers.get_selected_preset();
    return preset.printer_technology() == ptSLA ? 1 :
           preset.config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();
}

void GUI_App::window_pos_save(wxTopLevelWindow* window, const std::string &name)
{
    if (name.empty()) { return; }
    const auto config_key = (boost::format("window_%1%") % name).str();

    WindowMetrics metrics = WindowMetrics::from_window(window);
    app_config->set(config_key, metrics.serialize());
    app_config->save();
}

void GUI_App::window_pos_restore(wxTopLevelWindow* window, const std::string &name)
{
    if (name.empty()) { return; }
    const auto config_key = (boost::format("window_%1%") % name).str();

    if (! app_config->has(config_key)) { return; }

    auto metrics = WindowMetrics::deserialize(app_config->get(config_key));
    if (! metrics) { return; }

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


} // GUI
} //Slic3r
