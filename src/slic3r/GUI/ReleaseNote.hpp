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
#include <wx/event.h>
#include <wx/hyperlink.h>
#include <wx/richtext/richtextctrl.h>

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

#include "Jobs/Worker.hpp"

namespace Slic3r { namespace GUI {

wxDECLARE_EVENT(EVT_SECONDARY_CHECK_CONFIRM, wxCommandEvent);
wxDECLARE_EVENT(EVT_SECONDARY_CHECK_CANCEL, wxCommandEvent);
wxDECLARE_EVENT(EVT_SECONDARY_CHECK_RETRY, wxCommandEvent);
wxDECLARE_EVENT(EVT_SECONDARY_CHECK_DONE, wxCommandEvent);
wxDECLARE_EVENT(EVT_UPDATE_NOZZLE, wxCommandEvent);

class ReleaseNoteDialog : public DPIDialog
{
public:
    ReleaseNoteDialog(Plater *plater = nullptr);
    ~ReleaseNoteDialog();

    void on_dpi_changed(const wxRect &suggested_rect) override;
    void update_release_note(wxString release_note, std::string version);

    Label *    m_text_up_info{nullptr};
    wxScrolledWindow *m_vebview_release_note {nullptr};
};

class UpdatePluginDialog : public DPIDialog
{
public:
    UpdatePluginDialog(wxWindow* parent = nullptr);
    ~UpdatePluginDialog();

    void on_dpi_changed(const wxRect& suggested_rect) override;
    void update_info(std::string json_path);

    Label* m_text_up_info{ nullptr };
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
    std::vector<std::string> splitWithStl(std::string str, std::string pattern);

    wxStaticBitmap*   m_brand{nullptr};
    Label *           m_text_up_info{nullptr};
    wxWebView*        m_vebview_release_note{nullptr};
    wxSimplebook*     m_simplebook_release_note{nullptr};
    wxScrolledWindow* m_scrollwindows_release_note{nullptr};
    wxBoxSizer *      sizer_text_release_note{nullptr};
    Label *           m_staticText_release_note{nullptr};
    wxStaticBitmap*   m_bitmap_open_in_browser;
    Button*           m_button_skip_version;
    CheckBox*         m_cb_stable_only;
    Button*           m_button_download;
    Button*           m_button_cancel;
    std::string       url_line;
};

class SecondaryCheckDialog : public DPIFrame
{
private:
    wxWindow* event_parent { nullptr };
public:
    enum ButtonStyle {
        ONLY_CONFIRM        = 0,
        CONFIRM_AND_CANCEL  = 1,
        CONFIRM_AND_DONE    = 2,
        CONFIRM_AND_RETRY   = 3,
        DONE_AND_RETRY      = 4,
        MAX_STYLE_NUM       = 5
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
    void update_title_style(wxString title, SecondaryCheckDialog::ButtonStyle style, wxWindow* parent = nullptr);
    void post_event(wxCommandEvent&& event);
    void rescale();
    ~SecondaryCheckDialog();
    void on_dpi_changed(const wxRect& suggested_rect);
    void msw_rescale();


    StateColor btn_bg_green;
    StateColor btn_bg_white;
    Label* m_staticText_release_note {nullptr};
    wxBoxSizer* m_sizer_main;
    wxScrolledWindow *m_vebview_release_note {nullptr};
    Button* m_button_ok { nullptr };
    Button* m_button_retry { nullptr };
    Button* m_button_cancel { nullptr };
    Button* m_button_fn { nullptr };
    wxCheckBox* m_show_again_checkbox;
    ButtonStyle m_button_style;
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
    void rescale();
    void on_dpi_changed(const wxRect& suggested_rect);
    void show_update_nozzle_button();
    wxString format_text(wxString str, int warp);

    ~ConfirmBeforeSendDialog();

    wxBoxSizer* m_sizer_main;
    wxScrolledWindow* m_vebview_release_note{ nullptr };
    Label* m_staticText_release_note{ nullptr };
    Button* m_button_ok;
    Button* m_button_cancel;
    Button* m_button_update_nozzle;
    wxCheckBox* m_show_again_checkbox;
    bool not_show_again = false;
    std::string show_again_config_text = "";
};

class InputIpAddressDialog : public DPIDialog
{
public:
    wxString comfirm_before_enter_text;
    wxString comfirm_after_enter_text;

    std::string m_ip;
    Label* m_tip1{ nullptr };
    Label* m_tip2{ nullptr };
    Label* m_tip3{ nullptr };
    Label* m_tip4{ nullptr };
    InputIpAddressDialog(wxWindow* parent = nullptr);
    ~InputIpAddressDialog();

    MachineObject* m_obj{nullptr};
    Button* m_button_ok{ nullptr };
    Label* m_tips_ip{ nullptr };
    Label* m_tips_access_code{ nullptr };
    Label* m_test_right_msg{ nullptr };
    Label* m_test_wrong_msg{ nullptr };
    TextInput* m_input_ip{ nullptr };
    TextInput* m_input_access_code{ nullptr };
    wxStaticBitmap* m_img_help{ nullptr };
    wxStaticBitmap* m_img_step1{ nullptr };
    wxStaticBitmap* m_img_step2{ nullptr };
    wxStaticBitmap* m_img_step3{ nullptr };
    wxHyperlinkCtrl* m_trouble_shoot{ nullptr };
    bool   m_show_access_code{ false };
    int    m_result;
    std::shared_ptr<BBLStatusBarSend>  m_status_bar;
    std::unique_ptr<Worker> m_worker;

    void on_cancel();
    void update_title(wxString title);
    void set_machine_obj(MachineObject* obj);
    void update_test_msg(wxString msg, bool connected);
    bool isIp(std::string ipstr);
    void check_ip_address_failed(int result);
    void on_check_ip_address_failed(wxCommandEvent& evt);
    void on_ok(wxMouseEvent& evt);
    void on_text(wxCommandEvent& evt);
    void on_dpi_changed(const wxRect& suggested_rect) override;
};


wxDECLARE_EVENT(EVT_CLOSE_IPADDRESS_DLG, wxCommandEvent);
wxDECLARE_EVENT(EVT_CHECKBOX_CHANGE, wxCommandEvent);
wxDECLARE_EVENT(EVT_ENTER_IP_ADDRESS, wxCommandEvent);
wxDECLARE_EVENT(EVT_CHECK_IP_ADDRESS_FAILED, wxCommandEvent);


}} // namespace Slic3r::GUI

#endif
