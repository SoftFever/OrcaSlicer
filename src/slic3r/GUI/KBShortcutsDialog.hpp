#ifndef slic3r_GUI_KBShortcutsDialog_hpp_
#define slic3r_GUI_KBShortcutsDialog_hpp_

#include <wx/wx.h>
#include <map>
#include <vector>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"

namespace Slic3r { 
namespace GUI {

class KBShortcutsDialog : public DPIDialog
{
    typedef std::pair<std::string, std::string> Shortcut;
    typedef std::vector<Shortcut> Shortcuts;
    typedef std::vector<std::pair<wxString, Shortcuts>> ShortcutsVec;

    ShortcutsVec    m_full_shortcuts;
    ScalableBitmap  m_logo_bmp;
    wxStaticBitmap* m_header_bitmap;

public:
    KBShortcutsDialog();
    
protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    void fill_shortcuts();

    wxPanel* create_header(wxWindow* parent, const wxFont& bold_font);
    wxPanel* create_page(wxWindow* parent, const std::pair<wxString, Shortcuts>& shortcuts, const wxFont& font, const wxFont& bold_font);
};

} // namespace GUI
} // namespace Slic3r

#endif
