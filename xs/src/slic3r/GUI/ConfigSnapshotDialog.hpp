#ifndef slic3r_GUI_ConfigSnapshotDialog_hpp_
#define slic3r_GUI_ConfigSnapshotDialog_hpp_

#include "GUI.hpp"

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/html/htmlwin.h>

namespace Slic3r { 
namespace GUI {

namespace Config {
	class SnapshotDB;
}

class ConfigSnapshotDialog : public wxDialog
{
public:
    ConfigSnapshotDialog(const Config::SnapshotDB &snapshot_db);
    
private:
    void onLinkClicked(wxHtmlLinkEvent &event);
    void onCloseDialog(wxEvent &);
};

} // namespace GUI
} // namespace Slic3r

#endif /* slic3r_GUI_ConfigSnapshotDialog_hpp_ */
