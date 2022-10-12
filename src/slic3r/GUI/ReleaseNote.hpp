#ifndef slic3r_GUI_ReleaseNote_hpp_
#define slic3r_GUI_ReleaseNote_hpp_

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/collpane.h>
#include <wx/dataview.h>
#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/dataview.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/hyperlink.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/popupwin.h>
#include <wx/spinctrl.h>
#include <wx/artprov.h>
#include <wx/wrapsizer.h>

#include "AmsMappingPopup.hpp"
#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "DeviceManager.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/ScrolledWindow.hpp"
#include <wx/hashmap.h>
#include <wx/webview.h>

namespace Slic3r { namespace GUI {

class ReleaseNoteDialog : public DPIDialog
{
public:
    ReleaseNoteDialog(Plater *plater = nullptr);
    ~ReleaseNoteDialog();

    void on_dpi_changed(const wxRect &suggested_rect) override;
    void update_release_note(wxString release_note, std::string version);

    wxStaticText *    m_text_up_info{nullptr};
    wxScrolledWindow *m_vebview_release_note {nullptr};
};

class UpdateVersionDialog : public DPIDialog
{
public:
    UpdateVersionDialog(wxWindow *parent = nullptr);
    ~UpdateVersionDialog();

    wxWebView* CreateTipView(wxWindow* parent);
    void OnLoaded(wxWebViewEvent& event);
    void OnTitleChanged(wxWebViewEvent& event);
    void OnError(wxWebViewEvent& event);
    bool ShowReleaseNote(std::string content);
    void RunScript(std::string script);
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void update_version_info(wxString release_note, wxString version);
    void alter_choice(wxCommandEvent& event);
    std::vector<std::string> splitWithStl(std::string str, std::string pattern);

    wxStaticText *    m_text_up_info{nullptr};
    wxWebView*        m_vebview_release_note{nullptr};
    wxSimplebook*     m_simplebook_release_note{nullptr};
    wxScrolledWindow* m_scrollwindows_release_note{nullptr};
    wxBoxSizer *      sizer_text_release_note{nullptr};
    wxStaticText *    m_staticText_release_note{nullptr};
    wxCheckBox*       m_remind_choice;
    Button*           m_button_ok;
    Button*           m_button_cancel;
};

}} // namespace Slic3r::GUI

#endif
