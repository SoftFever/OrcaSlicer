#ifdef __linux__
#include "DesktopIntegrationDialog.hpp"
#include "GUI_App.hpp"
#include "format.hpp"
#include "I18N.hpp"
#include "NotificationManager.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Platform.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <wx/filename.h>
#include <wx/stattext.h>

namespace Slic3r {
namespace GUI {

namespace integrate_desktop_internal{
// Disects path strings stored in system variable divided by ':' and adds into vector
static void resolve_path_from_var(const std::string& var, std::vector<std::string>& paths)
{
    wxString wxdirs;
    if (! wxGetEnv(boost::nowide::widen(var), &wxdirs) || wxdirs.empty() )
        return;
    std::string dirs = boost::nowide::narrow(wxdirs);
    for (size_t i = dirs.find(':'); i != std::string::npos; i = dirs.find(':'))
    {
        paths.push_back(dirs.substr(0, i));
        if (dirs.size() > i+1)
            dirs = dirs.substr(i+1);
    }
    paths.push_back(dirs);
}
// Return true if directory in path p+dir_name exists
static bool contains_path_dir(const std::string& p, const std::string& dir_name)
{
    if (p.empty() || dir_name.empty()) 
       return false;
    boost::filesystem::path path(p + (p[p.size()-1] == '/' ? "" : "/") + dir_name);
    if (boost::filesystem::exists(path) && boost::filesystem::is_directory(path)) {
        //BOOST_LOG_TRIVIAL(debug) << path.string() << " " << std::oct << boost::filesystem::status(path).permissions();
        return true; //boost::filesystem::status(path).permissions() & boost::filesystem::owner_write;
    } else
        BOOST_LOG_TRIVIAL(debug) << path.string() << " doesnt exists";
    return false;
}
// Creates directory in path if not exists yet
static void create_dir(const boost::filesystem::path& path)
{
    if (boost::filesystem::exists(path))
        return;
    BOOST_LOG_TRIVIAL(debug)<< "creating " << path.string();
    boost::system::error_code ec;
    boost::filesystem::create_directory(path, ec);
    if (ec)
        BOOST_LOG_TRIVIAL(error)<< "create directory failed: " << ec.message();
}
// Starts at basic_path (excluded) and creates all directories in dir_path
static void create_path(const std::string& basic_path, const std::string& dir_path)
{
    if (basic_path.empty() || dir_path.empty())
       return;

    boost::filesystem::path path(basic_path);
    std::string dirs = dir_path;
    for (size_t i = dirs.find('/'); i != std::string::npos; i = dirs.find('/'))
    {
        std::string dir = dirs.substr(0, i);
        path = boost::filesystem::path(path.string() +"/"+ dir);
        create_dir(path);
        dirs = dirs.substr(i+1);
    }
    path = boost::filesystem::path(path.string() +"/"+ dirs);
    create_dir(path);
}
// Calls our internal copy_file function to copy file at icon_path to dest_path
static bool copy_icon(const std::string& icon_path, const std::string& dest_path)
{
    BOOST_LOG_TRIVIAL(debug) <<"icon from "<< icon_path;
    BOOST_LOG_TRIVIAL(debug) <<"icon to "<< dest_path;
    std::string error_message;
    auto cfr = copy_file(icon_path, dest_path, error_message, false);
    if (cfr) {
        BOOST_LOG_TRIVIAL(debug) << "Copy icon fail(" << cfr << "): " << error_message;
        return false;
    }
    BOOST_LOG_TRIVIAL(debug) << "Copy icon success.";
    return true;
}
// Creates new file filled with data.
static bool create_desktop_file(const std::string& path, const std::string& data)
{
    BOOST_LOG_TRIVIAL(debug) <<".desktop to "<< path;
    std::ofstream output(path);
    output << data;
    struct stat buffer;
    if (stat(path.c_str(), &buffer) == 0)
    {
        BOOST_LOG_TRIVIAL(debug) << "Desktop file created.";
        return true;
    }
    BOOST_LOG_TRIVIAL(debug) << "Desktop file NOT created.";
    return false;
}
} // namespace integratec_desktop_internal

// methods that actually do / undo desktop integration. Static to be accesible from anywhere.
bool DesktopIntegrationDialog::is_integrated()
{
	const char *appimage_env = std::getenv("APPIMAGE");
    if (!appimage_env) 
        return false;
 
    const AppConfig *app_config = wxGetApp().app_config;
    std::string path(app_config->get("desktop_integration_app_path"));
    BOOST_LOG_TRIVIAL(debug) << "Desktop integration desktop file path: " << path;

    if (path.empty())
        return false;

    // confirmation that PrusaSlicer.desktop exists
    struct stat buffer;   
    return (stat (path.c_str(), &buffer) == 0);
}
bool DesktopIntegrationDialog::integration_possible()
{

	const char *appimage_env = std::getenv("APPIMAGE");
    if (!appimage_env)
        return false;
    return true;
}
void DesktopIntegrationDialog::perform_desktop_integration()
{
	BOOST_LOG_TRIVIAL(debug) << "performing desktop integration";

    // Path to appimage
    const char *appimage_env = std::getenv("APPIMAGE");
    std::string appimage_path;
    if (appimage_env) {
        try {
            appimage_path = boost::filesystem::canonical(boost::filesystem::path(appimage_env)).string();
        } catch (std::exception &) {            
        }
    } else {
        // not appimage - not performing
        BOOST_LOG_TRIVIAL(error) << "Performing desktop integration failed - not Appimage executable.";
        wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::DesktopIntegrationFail);
        return;
    }

    // Find directories icons and applications
    // $XDG_DATA_HOME defines the base directory relative to which user specific data files should be stored. 
    // If $XDG_DATA_HOME is either not set or empty, a default equal to $HOME/.local/share should be used. 
    // $XDG_DATA_DIRS defines the preference-ordered set of base directories to search for data files in addition to the $XDG_DATA_HOME base directory.
    // The directories in $XDG_DATA_DIRS should be seperated with a colon ':'.
    // If $XDG_DATA_DIRS is either not set or empty, a value equal to /usr/local/share/:/usr/share/ should be used. 
    std::vector<std::string>target_candidates;
    integrate_desktop_internal::resolve_path_from_var("XDG_DATA_HOME", target_candidates);
    integrate_desktop_internal::resolve_path_from_var("XDG_DATA_DIRS", target_candidates);

    AppConfig *app_config = wxGetApp().app_config;
    // suffix string to create different desktop file for alpha, beta.
    
    std::string version_suffix;
    std::string name_suffix;
    std::string version(SLIC3R_VERSION);
    if (version.find("alpha") != std::string::npos)
    {
        version_suffix = "-alpha";
        name_suffix = " - alpha";
    }else if (version.find("beta") != std::string::npos)
    {
        version_suffix = "-beta";
        name_suffix = " - beta";
    }

    // theme path to icon destination    
    std::string icon_theme_path;
    std::string icon_theme_dirs;

    if (platform_flavor() == PlatformFlavor::LinuxOnChromium) {
        icon_theme_path = "hicolor/96x96/apps/";
        icon_theme_dirs = "/hicolor/96x96/apps";
    }
    
    
    std::string target_dir_icons;
    std::string target_dir_desktop;
    
    // slicer icon
    // iterate thru target_candidates to find icons folder
    for (size_t i = 0; i < target_candidates.size(); ++i) {
        // Copy icon PrusaSlicer.png from resources_dir()/icons to target_dir_icons/icons/
        if (integrate_desktop_internal::contains_path_dir(target_candidates[i], "icons")) {
            target_dir_icons = target_candidates[i];
            std::string icon_path = GUI::format("%1%/icons/PrusaSlicer.png",resources_dir());
            std::string dest_path = GUI::format("%1%/icons/%2%PrusaSlicer%3%.png", target_dir_icons, icon_theme_path, version_suffix);
            if (integrate_desktop_internal::copy_icon(icon_path, dest_path))
                break; // success
            else
                target_dir_icons.clear(); // copying failed
            // if all failed - try creating default home folder
            if (i == target_candidates.size() - 1) {
                // create $HOME/.local/share
                integrate_desktop_internal::create_path(boost::nowide::narrow(wxFileName::GetHomeDir()), ".local/share/icons" + icon_theme_dirs);
                // copy icon
                target_dir_icons = GUI::format("%1%/.local/share",wxFileName::GetHomeDir());
                std::string icon_path = GUI::format("%1%/icons/PrusaSlicer.png",resources_dir());
                std::string dest_path = GUI::format("%1%/icons/%2%PrusaSlicer%3%.png", target_dir_icons, icon_theme_path, version_suffix);
                if (!integrate_desktop_internal::contains_path_dir(target_dir_icons, "icons") 
                    || !integrate_desktop_internal::copy_icon(icon_path, dest_path)) {
                	// every attempt failed - icon wont be present
                    target_dir_icons.clear(); 
                }
            }
        }
    }
    if(target_dir_icons.empty()) {
        BOOST_LOG_TRIVIAL(error) << "Copying PrusaSlicer icon to icons directory failed.";
    } else 
    	// save path to icon
        app_config->set("desktop_integration_icon_slicer_path", GUI::format("%1%/icons/%2%PrusaSlicer%3%.png", target_dir_icons, icon_theme_path, version_suffix));

    // desktop file
    // iterate thru target_candidates to find applications folder
    for (size_t i = 0; i < target_candidates.size(); ++i)
    {
        if (integrate_desktop_internal::contains_path_dir(target_candidates[i], "applications")) {
            target_dir_desktop = target_candidates[i];
            // Write slicer desktop file
            std::string desktop_file = GUI::format(
                "[Desktop Entry]\n"
                "Name=PrusaSlicer%1%\n"
                "GenericName=3D Printing Software\n"
                "Icon=PrusaSlicer%2%\n"
                "Exec=%3% %%F\n"
                "Terminal=false\n"
                "Type=Application\n"
                "MimeType=model/stl;application/vnd.ms-3mfdocument;application/prs.wavefront-obj;application/x-amf;\n"
                "Categories=Graphics;3DGraphics;Engineering;\n"
                "Keywords=3D;Printing;Slicer;slice;3D;printer;convert;gcode;stl;obj;amf;SLA\n"
                "StartupNotify=false\n"
                "StartupWMClass=prusa-slicer", name_suffix, version_suffix, appimage_path);

            std::string path = GUI::format("%1%/applications/PrusaSlicer%2%.desktop", target_dir_desktop, version_suffix);
            if (integrate_desktop_internal::create_desktop_file(path, desktop_file)){
                BOOST_LOG_TRIVIAL(debug) << "PrusaSlicer.desktop file installation success.";
                break;
            } else {
            	// write failed - try another path
                BOOST_LOG_TRIVIAL(error) << "PrusaSlicer.desktop file installation failed.";
                target_dir_desktop.clear(); 
            }
            // if all failed - try creating default home folder
            if (i == target_candidates.size() - 1) {
                // create $HOME/.local/share
                integrate_desktop_internal::create_path(boost::nowide::narrow(wxFileName::GetHomeDir()), ".local/share/applications");
                // create desktop file
                target_dir_desktop = GUI::format("%1%/.local/share",wxFileName::GetHomeDir());
                std::string path = GUI::format("%1%/applications/PrusaSlicer%2%.desktop", target_dir_desktop, version_suffix);
                if (integrate_desktop_internal::contains_path_dir(target_dir_desktop, "applications")) {
                    if (!integrate_desktop_internal::create_desktop_file(path, desktop_file)) {    
                        // Desktop file not written - end desktop integration
                        BOOST_LOG_TRIVIAL(error) << "Performing desktop integration failed - could not create desktop file";
                        return;
                    }
                } else {
                	// Desktop file not written - end desktop integration
                    BOOST_LOG_TRIVIAL(error) << "Performing desktop integration failed - could not find applications directory";
                    return;
                }
            }
        }
    }
    if(target_dir_desktop.empty()) {
    	// Desktop file not written - end desktop integration
        BOOST_LOG_TRIVIAL(error) << "Performing desktop integration failed - could not find applications directory";
        wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::DesktopIntegrationFail);
        return;
    }
    // save path to desktop file
    app_config->set("desktop_integration_app_path", GUI::format("%1%/applications/PrusaSlicer%2%.desktop", target_dir_desktop, version_suffix));

    // Repeat for Gcode viewer - use same paths as for slicer files
    // Icon
    if (!target_dir_icons.empty())
    {
    	std::string icon_path = GUI::format("%1%/icons/PrusaSlicer-gcodeviewer_192px.png",resources_dir());
	    std::string dest_path = GUI::format("%1%/icons/%2%PrusaSlicer-gcodeviewer%3%.png", target_dir_icons, icon_theme_path, version_suffix);
	    if (integrate_desktop_internal::copy_icon(icon_path, dest_path))
	    	// save path to icon
	        app_config->set("desktop_integration_icon_viewer_path", dest_path);
	    else
	        BOOST_LOG_TRIVIAL(error) << "Copying Gcode Viewer icon to icons directory failed.";
    }

    // Desktop file
    std::string desktop_file = GUI::format(
        "[Desktop Entry]\n"
        "Name=Prusa Gcode Viewer%1%\n"
        "GenericName=3D Printing Software\n"
        "Icon=PrusaSlicer-gcodeviewer%2%\n"
        "Exec=%3% --gcodeviwer %%F\n"
        "Terminal=false\n"
        "Type=Application\n"
        "MimeType=text/x.gcode;\n"
        "Categories=Graphics;3DGraphics;\n"
        "Keywords=3D;Printing;Slicer;\n"
        "StartupNotify=false", name_suffix, version_suffix, appimage_path);

    std::string desktop_path = GUI::format("%1%/applications/PrusaSlicerGcodeViewer%2%.desktop", target_dir_desktop, version_suffix);
    if (integrate_desktop_internal::create_desktop_file(desktop_path, desktop_file))
    	// save path to desktop file
        app_config->set("desktop_integration_app_viewer_path", desktop_path);
    else {
        BOOST_LOG_TRIVIAL(error) << "Performing desktop integration failed - could create gcode viewer desktop file";
         wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::DesktopIntegrationFail);
    }
    wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::DesktopIntegrationSuccess);
}
void DesktopIntegrationDialog::undo_desktop_intgration()
{
	const char *appimage_env = std::getenv("APPIMAGE");
    if (!appimage_env) {
        BOOST_LOG_TRIVIAL(error) << "Undo desktop integration failed - not Appimage executable.";
    	wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::UndoDesktopIntegrationFail);
        return;
    }
    const AppConfig *app_config = wxGetApp().app_config;
    // slicer .desktop
    std::string path = std::string(app_config->get("desktop_integration_app_path"));
    if (!path.empty()) {
    	BOOST_LOG_TRIVIAL(debug) << "removing " << path;
        std::remove(path.c_str());  
    }
    // slicer icon
    path = std::string(app_config->get("desktop_integration_icon_slicer_path"));
    if (!path.empty()) {
    	BOOST_LOG_TRIVIAL(debug) << "removing " << path;
        std::remove(path.c_str());
    }
    // gcode viwer .desktop
    path = std::string(app_config->get("desktop_integration_app_viewer_path"));
    if (!path.empty()) {
    	BOOST_LOG_TRIVIAL(debug) << "removing " << path;
        std::remove(path.c_str());
    }
     // gcode viewer icon
    path = std::string(app_config->get("desktop_integration_icon_viewer_path"));
    if (!path.empty()) {
    	BOOST_LOG_TRIVIAL(debug) << "removing " << path;
        std::remove(path.c_str());
    }
    wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::UndoDesktopIntegrationSuccess);
}

DesktopIntegrationDialog::DesktopIntegrationDialog(wxWindow *parent)
: wxDialog(parent, wxID_ANY, _(L("Desktop Integration")), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER)
{
	bool can_undo = DesktopIntegrationDialog::is_integrated();

	wxBoxSizer *vbox = new wxBoxSizer(wxVERTICAL);


	wxString text = _L("Desktop Integration sets this binary to be searchable by the system.\n\nPress \"Perform\" to proceed.");
	if (can_undo)
		text += "\nPress \"Undo\" to remove previous integration.";

    vbox->Add(
        new wxStaticText( this, wxID_ANY, text),
        //	, wxDefaultPosition, wxSize(100,50), wxTE_MULTILINE),
        1,            // make vertically stretchable
        wxEXPAND |    // make horizontally stretchable
        wxALL,        //   and make border all around
        10 );         // set border width to 10
	

	wxBoxSizer *btn_szr = new wxBoxSizer(wxHORIZONTAL);
	wxButton *btn_perform = new wxButton(this, wxID_ANY, _L("Perform"));
	btn_szr->Add(btn_perform, 0, wxALL, 10);

	btn_perform->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { DesktopIntegrationDialog::perform_desktop_integration(); EndModal(wxID_ANY); });
	
	if (can_undo){
		wxButton *btn_undo = new wxButton(this, wxID_ANY, _L("Undo"));
		btn_szr->Add(btn_undo, 0, wxALL, 10);
		btn_undo->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { DesktopIntegrationDialog::undo_desktop_intgration(); EndModal(wxID_ANY); });
	}
	wxButton *btn_cancel = new wxButton(this, wxID_ANY, _L("Cancel"));
	btn_szr->Add(btn_cancel, 0, wxALL, 10);
	btn_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_ANY); });

	vbox->Add(btn_szr, 0, wxALIGN_CENTER);

    SetSizerAndFit(vbox);
}

DesktopIntegrationDialog::~DesktopIntegrationDialog()
{

}

} // namespace GUI
} // namespace Slic3r
#endif // __linux__