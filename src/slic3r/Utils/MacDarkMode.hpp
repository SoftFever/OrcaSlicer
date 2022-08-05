#ifndef slic3r_MacDarkMode_hpp_
#define slic3r_MacDarkMode_hpp_

#include <wx/event.h>

namespace Slic3r {
namespace GUI {

#if __APPLE__
extern bool mac_dark_mode();
extern double mac_max_scaling_factor();
extern void set_miniaturizable(void * window);
void WKWebView_evaluateJavaScript(void * web, wxString const & script, void (*callback)(wxString const &));
void set_title_colour_after_set_title();
void initGestures(void * view,  wxEvtHandler * handler);
#endif


} // namespace GUI
} // namespace Slic3r

#endif // MacDarkMode_h
