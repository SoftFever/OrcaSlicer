#include "GUI_Init.hpp"

#include "libslic3r/AppConfig.hpp" 

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/InstanceCheck.hpp" 
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"

// To show a message box if GUI initialization ends up with an exception thrown.
#include <wx/msgdlg.h>

#include <boost/nowide/iostream.hpp>
#include <boost/nowide/convert.hpp>

namespace Slic3r {
namespace GUI {

int GUI_Run(GUI_InitParams &params)
{
    try {
        GUI::GUI_App* gui = new GUI::GUI_App(params.start_as_gcodeviewer ? GUI::GUI_App::EAppMode::GCodeViewer : GUI::GUI_App::EAppMode::Editor);
        if (gui->get_app_mode() != GUI::GUI_App::EAppMode::GCodeViewer) {
            // G-code viewer is currently not performing instance check, a new G-code viewer is started every time.
            bool gui_single_instance_setting = gui->app_config->get("single_instance") == "1";
            if (Slic3r::instance_check(params.argc, params.argv, gui_single_instance_setting)) {
                //TODO: do we have delete gui and other stuff?
                return -1;
            }
        }

//      gui->autosave = m_config.opt_string("autosave");
        GUI::GUI_App::SetInstance(gui);
        gui->init_params = &params;
/*
        gui->CallAfter([gui, this, &load_configs, params.start_as_gcodeviewer] {
            if (!gui->initialized()) {
                return;
            }

            if (params.start_as_gcodeviewer) {
                if (!m_input_files.empty())
                    gui->plater()->load_gcode(wxString::FromUTF8(m_input_files[0].c_str()));
            } else {
#if 0
                // Load the cummulative config over the currently active profiles.
                //FIXME if multiple configs are loaded, only the last one will have an effect.
                // We need to decide what to do about loading of separate presets (just print preset, just filament preset etc).
                // As of now only the full configs are supported here.
                if (!m_print_config.empty())
                    gui->mainframe->load_config(m_print_config);
#endif
                if (!load_configs.empty())
                    // Load the last config to give it a name at the UI. The name of the preset may be later
                    // changed by loading an AMF or 3MF.
                    //FIXME this is not strictly correct, as one may pass a print/filament/printer profile here instead of a full config.
                    gui->mainframe->load_config_file(load_configs.back());
                // If loading a 3MF file, the config is loaded from the last one.
                if (!m_input_files.empty())
                    gui->plater()->load_files(m_input_files, true, true);
                if (!m_extra_config.empty())
                    gui->mainframe->load_config(m_extra_config);
            }
        });
*/
        int result = wxEntry(params.argc, params.argv);
        return result;
    } catch (const Slic3r::Exception &ex) {
        boost::nowide::cerr << ex.what() << std::endl;
        wxMessageBox(boost::nowide::widen(ex.what()), _L("PrusaSlicer GUI initialization failed"), wxICON_STOP);
    } catch (const std::exception &ex) {
        boost::nowide::cerr << "PrusaSlicer GUI initialization failed: " << ex.what() << std::endl;
        wxMessageBox(format_wxstr(_L("Fatal error, exception catched: %1%"), ex.what()), _L("PrusaSlicer GUI initialization failed"), wxICON_STOP);
    }

    // error
    return 1;
}
    
}
}
