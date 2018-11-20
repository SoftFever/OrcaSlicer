#ifndef slic3r_GUI_SysInfoDialog_hpp_
#define slic3r_GUI_SysInfoDialog_hpp_

#include <wx/wx.h>
#include <wx/html/htmlwin.h>

namespace Slic3r { 
namespace GUI {

class SysInfoDialog : public wxDialog
{
    wxString text_info {wxEmptyString};
public:
    SysInfoDialog();
    
private:
    void onCopyToClipboard(wxEvent &);
    void onCloseDialog(wxEvent &);
};

} // namespace GUI
} // namespace Slic3r

#endif
