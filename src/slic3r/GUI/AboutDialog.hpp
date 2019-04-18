#ifndef slic3r_GUI_AboutDialog_hpp_
#define slic3r_GUI_AboutDialog_hpp_

#include "GUI.hpp"

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/html/htmlwin.h>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"

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

class AboutDialog : public DPIDialog
{
    PrusaBitmap     m_logo_bitmap;
    wxHtmlWindow*   m_html;
    wxStaticBitmap* m_logo;
public:
    AboutDialog();

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;
    
private:
    void onLinkClicked(wxHtmlLinkEvent &event);
    void onCloseDialog(wxEvent &);
};

} // namespace GUI
} // namespace Slic3r

#endif
