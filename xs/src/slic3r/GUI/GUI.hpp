#ifndef slic3r_GUI_hpp_
#define slic3r_GUI_hpp_

#include <string>
#include <vector>

class wxApp;
class wxFrame;
class wxMenuBar;
class wxNotebook;

namespace Slic3r { namespace GUI {

void disable_screensaver();
void enable_screensaver();
std::vector<std::string> scan_serial_ports();
bool debugged();
void break_to_debugger();

// Passing the wxWidgets GUI classes instantiated by the Perl part to C++.
void set_wxapp(wxApp *app);
void set_main_frame(wxFrame *main_frame);
void set_tab_panel(wxNotebook *tab_panel);

void add_debug_menu(wxMenuBar *menu);
// Create a new preset tab (print, filament or printer),
// add it at the end of the tab panel.
void create_preset_tab(const char *name);

} }

#endif
