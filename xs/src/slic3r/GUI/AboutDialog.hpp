#ifndef slic3r_GUI_AboutDialog_hpp_
#define slic3r_GUI_AboutDialog_hpp_

#include "GUI.hpp"

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/html/htmlwin.h>

namespace Slic3r { 
namespace GUI {

class AboutDialogLogo : public wxPanel
{
public:
    AboutDialogLogo(wxWindow* parent);
    
private:
    wxBitmap logo;
    void onRepaint(wxEvent &event);
};

class AboutDialog : public wxDialog
{
public:
    AboutDialog();
    
private:
    void onLinkClicked(wxHtmlLinkEvent &event);
    void onCloseDialog(wxEvent &);
};

} // namespace GUI
} // namespace Slic3r

#endif
