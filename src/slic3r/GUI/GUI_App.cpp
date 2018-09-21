#include "GUI_App.hpp"

#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

#include <wx/stdpaths.h>
#include <wx/imagpng.h>
#include <wx/display.h>
#include <wx/menu.h>
#include <wx/menuitem.h>
#include <wx/filedlg.h>

#include "Utils.hpp"
#include "GUI.hpp"
#include "MainFrame.hpp"
#include "AppConfig.hpp"
#include "PresetBundle.hpp"
#include "3DScene.hpp"

#include "../Utils/PresetUpdater.hpp"
#include "ConfigWizard_private.hpp"
#include "slic3r/Config/Snapshot.hpp"
#include "ConfigSnapshotDialog.hpp"
#include "FirmwareDialog.hpp"
#include "Preferences.hpp"
#include "Tab.hpp"

namespace Slic3r {
namespace GUI {

IMPLEMENT_APP(GUI_App)
bool GUI_App::OnInit()
{
    SetAppName("Slic3rPE");
    SetAppDisplayName("Slic3r Prusa Edition");

//     Slic3r::debugf "wxWidgets version %s, Wx version %s\n", wxVERSION_STRING, wxVERSION;

    // Set the Slic3r data directory at the Slic3r XS module.
    // Unix: ~/ .Slic3r
    // Windows : "C:\Users\username\AppData\Roaming\Slic3r" or "C:\Documents and Settings\username\Application Data\Slic3r"
    // Mac : "~/Library/Application Support/Slic3r"
    datadir.empty() ?
        Slic3r::set_data_dir(wxStandardPaths::Get().GetUserDataDir().ToStdString()) :
        Slic3r::set_data_dir(datadir);
    //     set_wxapp(this); // #ys_FIXME

    // #ys_FIXME temporary workaround 
    if (var_dir().empty())
        set_var_dir("c:\\src\\Slic3r_TMP\\resources\\icons");
    if (localization_dir().empty())
        set_local_dir("c:\\src\\Slic3r_TMP\\resources\\localization");

    app_config = new AppConfig();
    //     set_app_config(app_config);// #ys_FIXME
    preset_bundle = new PresetBundle();
    //     set_preset_bundle(preset_bundle);// #ys_FIXME

    // just checking for existence of Slic3r::data_dir is not enough : it may be an empty directory
    // supplied as argument to --datadir; in that case we should still run the wizard
    //     eval{ 
    preset_bundle->setup_directories();
    //     };
    //     if ($@) {
    //         warn $@ . "\n";
    //         fatal_error(undef, $@);
    //     }
    app_conf_exists = app_config->exists();
    // load settings
    if (app_conf_exists) app_config->load();
    app_config->set("version", "Slic3r_VERSION"/*Slic3r::VERSION*/);
    app_config->save();

//     preset_updater = new PresetUpdater();
//     set_preset_updater(preset_updater); // #ys_FIXME

    load_language();

    // Suppress the '- default -' presets.
    preset_bundle->set_default_suppressed(app_config->get("no_defaults").empty() ? false : true);
    //     eval{ 
    preset_bundle->load_presets(*app_config);
    //     };
    //     if ($@) {
    //         warn $@ . "\n";
    //         show_error(undef, $@);
    //     }

    // application frame
    //     print STDERR "Creating main frame...\n";
    //     wxImage::FindHandlerType(wxBITMAP_TYPE_PNG) ||
    wxImage::AddHandler(new wxPNGHandler());
    mainframe = new Slic3r::GUI::MainFrame(no_plater, false);
    SetTopWindow(mainframe);

    // This makes CallAfter() work
    //     /*mainframe->*/Bind(wxEVT_IDLE, 
//     [this](wxIdleEvent& event)
//     {
//         std::function<void()> cur_cb{ nullptr };
//         // try to get the mutex. If we can't, just skip this idle event and get the next one.
//         if (!callback_register.try_lock()) return;
//         // pop callback
//         if (m_cb.size() != 0){
//             cur_cb = m_cb.top();
//             m_cb.pop();
//         }
//         // unlock mutex
//         this->callback_register.unlock();
// 
//         try { // call the function if it's not nullptr;
//             if (cur_cb != nullptr) cur_cb();
//         }
//         catch (std::exception& e) {
//             //             Slic3r::Log::error(LogChannel, LOG_WSTRING("Exception thrown: " << e.what())); // #ys_FIXME
//         }
// 
//         if (app_config->dirty())
//             app_config->save();
//     }
    ;// #ys_FIXME
    //     );

    // On OS X the UI tends to freeze in weird ways if modal dialogs(config wizard, update notifications, ...)
    // are shown before or in the same event callback with the main frame creation.
    // Therefore we schedule them for later using CallAfter.
//     CallAfter([this](){
//         //         eval{
//         if (!preset_updater->config_update())
//             mainframe->Close();
//         //         };
//         //         if ($@) {
//         //             show_error(undef, $@);
//         //             mainframe->Close();
//         //         }
//     });
// 
//     CallAfter([this](){
//         if (!Slic3r::GUI::config_wizard_startup(app_conf_exists)) {
//             // Only notify if there was not wizard so as not to bother too much ...
//             preset_updater->slic3r_update_notify();
//         }
//         preset_updater->sync(preset_bundle);
//     });
// 

    // #ys_FIXME All of this should to be removed
    //     # The following event is emited by the C++ menu implementation of preferences change.
    //     EVT_COMMAND($self, -1, $PREFERENCES_EVENT, sub{
    //         $self->update_ui_from_settings;
    //     });
    // 
    //     # The following event is emited by PresetUpdater(C++) to inform about
    //     # the newer Slic3r application version avaiable online.
    //     EVT_COMMAND($self, -1, $VERSION_ONLINE_EVENT, sub {
    //         my($self, $event) = @_;
    //         my $version = $event->GetString;
    //         $self->{app_config}->set('version_online', $version);
    //         $self->{app_config}->save;
    //     });

    mainframe->Show(true);
    return true;
}

void GUI_App::recreate_GUI()
{
//     print STDERR "recreate_GUI\n";

    auto topwindow = GetTopWindow();
    mainframe = new Slic3r::GUI::MainFrame(no_plater,false);

    if (topwindow) {
        SetTopWindow(mainframe);
        topwindow->Destroy();
    }

    // On OSX the UI was not initialized correctly if the wizard was called
    // before the UI was up and running.
    CallAfter([](){
        // Run the config wizard, don't offer the "reset user profile" checkbox.
        Slic3r::GUI::config_wizard_startup(true);
    });
}

void GUI_App::system_info()
{
//     auto slic3r_info = Slic3r::slic3r_info(format = > 'html');
//     auto copyright_info = Slic3r::copyright_info(format = > 'html');
//     auto system_info = Slic3r::system_info(format = > 'html');
    std::string opengl_info = "";
    std::string opengl_info_txt = "";
    if (mainframe && mainframe->m_plater /*&& mainframe->m_plater->canvas3D*/) {
        opengl_info = _3DScene::get_gl_info(true, true);
        opengl_info_txt = _3DScene::get_gl_info(false, true);
    }
//     auto about = new SystemInfo(nullptr, slic3r_info, /*copyright_info,*/system_info, opengl_info,
//         text_info = > Slic3r::slic3r_info.Slic3r::system_info.$opengl_info_txt,
//         );
//     about->ShowModal();
//     about->Destroy();
}

// static method accepting a wxWindow object as first parameter
bool GUI_App::catch_error(std::function<void()> cb,
    //                       wxMessageDialog* message_dialog,
    const std::string& err /*= ""*/){
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
void fatal_error(wxWindow* parent){
    show_error(parent, "");
    //     exit 1; // #ys_FIXME
}

// Called after the Preferences dialog is closed and the program settings are saved.
// Update the UI based on the current preferences.
void GUI_App::update_ui_from_settings(){
    mainframe->update_ui_from_settings();
}


void GUI_App::open_model(wxWindow *parent, wxArrayString& input_files)
{
    auto dialog = new wxFileDialog(parent ? parent : GetTopWindow(),
        _(L("Choose one or more files (STL/OBJ/AMF/3MF/PRUSA):")),
        app_config->get_last_dir(), "",
        MODEL_WILDCARD, wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
    if (dialog->ShowModal() != wxID_OK) {
        dialog->Destroy();
        return;
    }

    dialog->GetPaths(input_files);
    dialog->Destroy();
}

void GUI_App::CallAfter(std::function<void()> cb)
{
    // set mutex
    callback_register.lock();
    // push function onto stack
    m_cb.emplace(cb);
    // unset mutex
    callback_register.unlock();
}

wxMenuItem* GUI_App::append_menu_item(  wxMenu* menu,
                                        int id,
                                        const wxString& string,
                                        const wxString& description,
                                        const std::string& icon,
                                        std::function<void(wxCommandEvent& event)> cb,
                                        wxItemKind kind/* = wxITEM_NORMAL*/)
{
    if (id == wxID_ANY)
        id = wxNewId();
    auto item = new wxMenuItem(menu, id, string, description, kind);
    if (!icon.empty())
        item->SetBitmap(wxBitmap(Slic3r::var(icon), wxBITMAP_TYPE_PNG));
    menu->Append(item);

    menu->Bind(wxEVT_MENU, /*[cb](wxCommandEvent& event){cb; }*/cb);
    return item;
}

wxMenuItem* GUI_App::append_submenu(wxMenu* menu,
    wxMenu* sub_menu,
    int id,
    const wxString& string,
    const wxString& description,
    const std::string& icon)
{
    if (id == wxID_ANY)
        id = wxNewId();
    auto item = new wxMenuItem(menu, id, string, description);
    if (!icon.empty())
        item->SetBitmap(wxBitmap(Slic3r::var(icon), wxBITMAP_TYPE_PNG));
    item->SetSubMenu(sub_menu);
    menu->Append(item);

    return item;
}

void GUI_App::save_window_pos(wxTopLevelWindow* window, const std::string& name){
    int x, y;
    window->GetScreenPosition(&x, &y);
    app_config->set(name + "_pos", wxString::Format("%d,%d", x, y).ToStdString());

    window->GetSize(&x, &y);
    app_config->set(name + "_size", wxString::Format("%d,%d", x, y).ToStdString());

    app_config->set(name + "_maximized", window->IsMaximized() ? "1" : "0");

    app_config->save();
}

void GUI_App::restore_window_pos(wxTopLevelWindow* window, const std::string& name){
    if (!app_config->has(name + "_pos"))
        return;

    std::string str = app_config->get(name + "_size");
    std::vector<std::string> values;
    boost::split(values, str, boost::is_any_of(","));
    wxSize size = wxSize(atoi(values[0].c_str()), atoi(values[1].c_str()));
    window->SetSize(size);

    auto display = (new wxDisplay())->GetClientArea();
    str = app_config->get(name + "_pos");
    values.resize(0);
    boost::split(values, str, boost::is_any_of(","));
    wxPoint pos = wxPoint(atoi(values[0].c_str()), atoi(values[1].c_str()));
    if (pos.x + 0.5*size.GetWidth() < display.GetRight() &&
        pos.y + 0.5*size.GetHeight() < display.GetBottom())
        window->Move(pos);

    if (app_config->get(name + "_maximized") == "1")
        window->Maximize();
}

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
            auto locale = get_locale();
            locale = new wxLocale;
            locale->Init(identifiers[i]);
            locale->AddCatalogLookupPathPrefix(wxPathOnly(localization_dir()));
            locale->AddCatalog(GetAppName());
            wxSetlocale(LC_NUMERIC, "C");
            Preset::update_suffix_modified();
            return true;
        }
    }
    return false;
}

ConfigMenuIDs GUI_App::get_view_mode()
{
    if (!app_config->has("view_mode"))
        return ConfigMenuModeSimple;

    const auto mode = app_config->get("view_mode");
    return mode == "expert" ? ConfigMenuModeExpert : ConfigMenuModeSimple;
}

static wxString dots("…", wxConvUTF8);

void GUI_App::add_config_menu(wxMenuBar *menu)
{
    auto local_menu = new wxMenu();
    wxWindowID config_id_base = wxWindow::NewControlId((int)ConfigMenuCnt);

    const auto config_wizard_name = _(ConfigWizard::name().wx_str());
    const auto config_wizard_tooltip = wxString::Format(_(L("Run %s")), config_wizard_name);
    // Cmd+, is standard on OS X - what about other operating systems?
    local_menu->Append(config_id_base + ConfigMenuWizard, config_wizard_name + dots, config_wizard_tooltip);
    local_menu->Append(config_id_base + ConfigMenuSnapshots, _(L("Configuration Snapshots")) + dots, _(L("Inspect / activate configuration snapshots")));
    local_menu->Append(config_id_base + ConfigMenuTakeSnapshot, _(L("Take Configuration Snapshot")), _(L("Capture a configuration snapshot")));
    // 	local_menu->Append(config_id_base + ConfigMenuUpdate, 		_(L("Check for updates")), 					_(L("Check for configuration updates")));
    local_menu->AppendSeparator();
    local_menu->Append(config_id_base + ConfigMenuPreferences, _(L("Preferences")) + dots + "\tCtrl+,", _(L("Application preferences")));
    local_menu->AppendSeparator();
    auto mode_menu = new wxMenu();
    mode_menu->AppendRadioItem(config_id_base + ConfigMenuModeSimple, _(L("&Simple")), _(L("Simple View Mode")));
    mode_menu->AppendRadioItem(config_id_base + ConfigMenuModeExpert, _(L("&Expert")), _(L("Expert View Mode")));
    mode_menu->Check(config_id_base + get_view_mode(), true);
    local_menu->AppendSubMenu(mode_menu, _(L("&Mode")), _(L("Slic3r View Mode")));
    local_menu->AppendSeparator();
    local_menu->Append(config_id_base + ConfigMenuLanguage, _(L("Change Application Language")));
    local_menu->AppendSeparator();
    local_menu->Append(config_id_base + ConfigMenuFlashFirmware, _(L("Flash printer firmware")), _(L("Upload a firmware image into an Arduino based printer")));
    // TODO: for when we're able to flash dictionaries
    // local_menu->Append(config_id_base + FirmwareMenuDict,  _(L("Flash language file")),    _(L("Upload a language dictionary file into a Prusa printer")));

    local_menu->Bind(wxEVT_MENU, [this, config_id_base](wxEvent &event){
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
//             PreferencesDialog dlg(mainframe, event_preferences_changed);
//             dlg.ShowModal();
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
        std::string mode = event.GetId() - config_id_base == ConfigMenuModeExpert ?
            "expert" : "simple";
        app_config->set("view_mode", mode);
        app_config->save();
        update_mode();
    });
    menu->Append(local_menu, _(L("&Configuration")));
}

// This is called when closing the application, when loading a config file or when starting the config wizard
// to notify the user whether he is aware that some preset changes will be lost.
bool GUI_App::check_unsaved_changes()
{
    std::string dirty;
    for (Tab *tab : tabs_list)
        if (tab->current_preset_is_dirty())
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

wxNotebook* GUI_App::tab_panel() const 
{
    return mainframe->m_tabpanel;
}

// std::vector<PresetTab>  preset_tabs = {
//     { "print", nullptr, ptFFF },
//     { "filament", nullptr, ptFFF },
//     { "sla_material", nullptr, ptSLA }
// };
// 
// Tab* GUI_App::get_tab(const std::string& name)
// {
//     std::vector<PresetTab>::iterator it = std::find_if(preset_tabs.begin(), preset_tabs.end(),
//         [name](PresetTab& tab){ return name == tab.name; });
//     return it != preset_tabs.end() ? it->panel : nullptr;
// }


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
// void GUI_App::notify(message){
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