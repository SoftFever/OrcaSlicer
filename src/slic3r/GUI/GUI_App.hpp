#ifndef slic3r_GUI_App_hpp_
#define slic3r_GUI_App_hpp_

#include <string>
// #include <vector>
// #include "PrintConfig.hpp"
// #include "../../libslic3r/Utils.hpp"
// #include "GUI.hpp"

#include <wx/app.h>

#include <mutex>
#include <stack>

class wxMenuItem;
class wxTopLevelWindow;

namespace Slic3r {
class AppConfig;
class PresetBundle;
class PresetUpdater;

namespace GUI
{
class MainFrame;

class GUI_App : public wxApp
{
    // Datadir provided on the command line.
    std::string     datadir = "";
    bool            no_plater{ true };
    bool            app_conf_exists{ false };

    // Lock to guard the callback stack
    std::mutex      callback_register;
    // callbacks registered to run during idle event.
    std::stack<std::function<void()>>    m_cb{};

public:
    bool            OnInit() override;
    GUI_App() : wxApp() {}

    void            recreate_GUI();
    void            system_info();
    void            open_model(wxWindow *parent, wxArrayString& input_files);
    static bool     catch_error(std::function<void()> cb,
//                                 wxMessageDialog* message_dialog,
                                const std::string& err);
//     void            notify(/*message*/);
    void            update_ui_from_settings();
    void            CallAfter(std::function<void()> cb);
    wxMenuItem*     append_menu_item(   wxMenu* menu,
                                        int id,
                                        const wxString& string,
                                        const wxString& description,
                                        const std::string& icon,
                                        std::function<void(wxCommandEvent& event)> cb,
                                        wxItemKind kind = wxITEM_NORMAL);
    wxMenuItem*     append_submenu( wxMenu* menu,
                                    wxMenu* sub_menu,
                                    int id,
                                    const wxString& string,
                                    const wxString& description,
                                    const std::string& icon);
    void            save_window_pos(wxTopLevelWindow* window, const std::string& name);
    void            restore_window_pos(wxTopLevelWindow* window, const std::string& name);

    AppConfig*      app_config{ nullptr };
    PresetBundle*   preset_bundle{ nullptr };
    PresetUpdater*  preset_updater{ nullptr };
    MainFrame*      mainframe{ nullptr };
};
// DECLARE_APP(GUI_App)

} // GUI
} //Slic3r

#endif // slic3r_GUI_App_hpp_