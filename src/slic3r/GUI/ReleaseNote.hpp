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

wxDECLARE_EVENT(EVT_SECONDARY_CHECK_CONFIRM, wxCommandEvent);
wxDECLARE_EVENT(EVT_SECONDARY_CHECK_CANCEL, wxCommandEvent);

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

class UpdatePluginDialog : public DPIDialog
{
public:
    UpdatePluginDialog(wxWindow* parent = nullptr);
    ~UpdatePluginDialog();

    void on_dpi_changed(const wxRect& suggested_rect) override;
    void update_info(std::string json_path);

    wxStaticText* m_text_up_info{ nullptr };
    Label* operation_tips{ nullptr };
    wxScrolledWindow* m_vebview_release_note{ nullptr };
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

class SecondaryCheckDialog : public DPIFrame
{
public:
    enum ButtonStyle {
        ONLY_CONFIRM = 0,
        CONFIRM_AND_CANCEL = 1,
        MAX_STYLE_NUM = 2
    };
    SecondaryCheckDialog(
        wxWindow* parent,
        wxWindowID      id = wxID_ANY,
        const wxString& title = wxEmptyString,
        enum ButtonStyle btn_style = CONFIRM_AND_CANCEL,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long            style = wxCLOSE_BOX | wxCAPTION,
        bool not_show_again_check = false
    );
    void update_text(wxString text);
    void on_show();
    void on_hide();
    void update_btn_label(wxString ok_btn_text, wxString cancel_btn_text);
    void rescale();
    ~SecondaryCheckDialog();
    void on_dpi_changed(const wxRect& suggested_rect);

    wxBoxSizer* m_sizer_main;
    wxScrolledWindow *m_vebview_release_note {nullptr};
    Button* m_button_ok;
    Button* m_button_cancel;
    wxCheckBox* m_show_again_checkbox;
    bool not_show_again = false;
    std::string show_again_config_text = "";
};

class ConfirmBeforeSendDialog : public DPIDialog
{
public:
    enum ButtonStyle {
        ONLY_CONFIRM = 0,
        CONFIRM_AND_CANCEL = 1,
        MAX_STYLE_NUM = 2
    };
    ConfirmBeforeSendDialog(
        wxWindow* parent,
        wxWindowID      id = wxID_ANY,
        const wxString& title = wxEmptyString,
        enum ButtonStyle btn_style = CONFIRM_AND_CANCEL,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long            style = wxCLOSE_BOX | wxCAPTION,
        bool not_show_again_check = false
    );
    void update_text(wxString text);
    void on_show();
    void on_hide();
    void update_btn_label(wxString ok_btn_text, wxString cancel_btn_text);
    wxString format_text(wxString str, int warp);
    void rescale();
    ~ConfirmBeforeSendDialog();
    void on_dpi_changed(const wxRect& suggested_rect);

    wxBoxSizer* m_sizer_main;
    wxScrolledWindow* m_vebview_release_note{ nullptr };
    Button* m_button_ok;
    Button* m_button_cancel;
    wxCheckBox* m_show_again_checkbox;
    bool not_show_again = false;
    std::string show_again_config_text = "";
};

}} // namespace Slic3r::GUI

#endif
