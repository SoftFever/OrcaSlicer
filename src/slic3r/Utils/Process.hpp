#ifndef GUI_PROCESS_HPP
#define GUI_PROCESS_HPP

class wxWindow;
class wxString;

namespace Slic3r {
namespace GUI {

// Start a new slicer instance, optionally with a file to open.
void start_new_slicer(const wxString *path_to_open = nullptr);
// Start a new G-code viewer instance, optionally with a file to open.
void start_new_gcodeviewer(const wxString *path_to_open = nullptr);
// Open a file dialog, ask the user to select a new G-code to open, start a new G-code viewer.
void start_new_gcodeviewer_open_file(wxWindow *parent = nullptr);

} // namespace GUI
} // namespace Slic3r

#endif // GUI_PROCESS_HPP
