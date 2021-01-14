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
    typedef std::pair<std::pair<wxString, wxString>, Shortcuts> ShortcutsItem;
    typedef std::vector<ShortcutsItem> ShortcutsVec;

    ShortcutsVec    m_full_shortcuts;
    ScalableBitmap  m_logo_bmp;
    wxStaticBitmap* m_header_bitmap;
    std::vector<wxPanel*> m_pages;

public:
    KBShortcutsDialog();
    
protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    void fill_shortcuts();

    wxPanel* create_header(wxWindow* parent, const wxFont& bold_font);
    wxPanel* create_page(wxWindow* parent, const ShortcutsItem& shortcuts, const wxFont& font, const wxFont& bold_font);
};

} // namespace GUI
} // namespace Slic3r

#endif
