#ifndef slic3r_GUI_KBShortcutsDialog_hpp_
#define slic3r_GUI_KBShortcutsDialog_hpp_

#include <wx/wx.h>
#include <map>
#include <vector>

namespace Slic3r { 
namespace GUI {

class KBShortcutsDialog : public wxDialog
{
    typedef std::pair<std::string, std::string> Shortcut;
    typedef std::vector< Shortcut >             Shortcuts;
    typedef std::vector< std::pair<wxString, std::pair<Shortcuts, int>> >   ShortcutsVec;

    wxString text_info {wxEmptyString};

    ShortcutsVec m_full_shortcuts;

public:
    KBShortcutsDialog();
    
    void fill_shortcuts();

private:
    void onCloseDialog(wxEvent &);
};

} // namespace GUI
} // namespace Slic3r

#endif
