#include "ChatConfigPanel.hpp"

#include <wx/sizer.h>
#include "slic3r/GUI/GUI_App.hpp"
#include <slic3r/GUI/Widgets/WebView.hpp>
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/Tab.hpp"

namespace Slic3r { namespace GUI {

ChatConfigPanel::ChatConfigPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);

    // Create the webview
    m_browser = WebView::CreateWebView(this, "");
    if (m_browser == nullptr) {
        wxLogError("Could not init m_browser");
        return;
    }

    m_browser->Bind(wxEVT_WEBVIEW_ERROR, &ChatConfigPanel::OnError, this);
    m_browser->Bind(wxEVT_WEBVIEW_LOADED, &ChatConfigPanel::OnLoaded, this);
    m_browser->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &ChatConfigPanel::OnScriptMessageReceived, this);

    topsizer->Add(m_browser, 1, wxEXPAND);
    SetSizer(topsizer);

    update_mode();

    // Zoom
    m_zoomFactor = 100;

    // Connect the idle events
    Bind(wxEVT_CLOSE_WINDOW, &ChatConfigPanel::OnClose, this);

    load_url();
}

ChatConfigPanel::~ChatConfigPanel()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Start";
    SetEvtHandlerEnabled(false);

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " End";
}

void ChatConfigPanel::load_url()
{
    wxString url = wxString::Format("file://%s/web/ai/chat_config_test.html", from_u8(resources_dir()));
    if (m_browser == nullptr)
        return;


    m_browser->LoadURL(url);
    // m_browser->SetFocus();
    //UpdateState();
}

void ChatConfigPanel::reload() { m_browser->Reload(); }

void ChatConfigPanel::update_mode() { m_browser->EnableAccessToDevTools(wxGetApp().app_config->get_bool("developer_mode")); }

void ChatConfigPanel::UpdateState()
{
    // SetTitle(m_browser->GetCurrentTitle());
}

void ChatConfigPanel::OnClose(wxCloseEvent& evt) { this->Hide(); }

void ChatConfigPanel::SendMessage(wxString  message)
{
    wxString script = wxString::Format(R"(
    // Check if window.fetch exists before overriding
    if (window.onGUIMessage) {
        window.onGUIMessage('%s');
    }
)",
                                       message);
    WebView::RunScript(m_browser, script);
}

void ChatConfigPanel::OnError(wxWebViewEvent& evt)
{
    auto e = "unknown error";
    switch (evt.GetInt()) {
    case wxWEBVIEW_NAV_ERR_CONNECTION: e = "wxWEBVIEW_NAV_ERR_CONNECTION"; break;
    case wxWEBVIEW_NAV_ERR_CERTIFICATE: e = "wxWEBVIEW_NAV_ERR_CERTIFICATE"; break;
    case wxWEBVIEW_NAV_ERR_AUTH: e = "wxWEBVIEW_NAV_ERR_AUTH"; break;
    case wxWEBVIEW_NAV_ERR_SECURITY: e = "wxWEBVIEW_NAV_ERR_SECURITY"; break;
    case wxWEBVIEW_NAV_ERR_NOT_FOUND: e = "wxWEBVIEW_NAV_ERR_NOT_FOUND"; break;
    case wxWEBVIEW_NAV_ERR_REQUEST: e = "wxWEBVIEW_NAV_ERR_REQUEST"; break;
    case wxWEBVIEW_NAV_ERR_USER_CANCELLED: e = "wxWEBVIEW_NAV_ERR_USER_CANCELLED"; break;
    case wxWEBVIEW_NAV_ERR_OTHER: e = "wxWEBVIEW_NAV_ERR_OTHER"; break;
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                            << boost::format(": error loading page %1% %2% %3% %4%") % evt.GetURL() % evt.GetTarget() % e % evt.GetString();
}

void ChatConfigPanel::OnLoaded(wxWebViewEvent& evt)
{
    if (evt.GetURL().IsEmpty())
        return;

}

void ChatConfigPanel::OnScriptMessageReceived(wxWebViewEvent& event)
{
    wxString message = event.GetString();

    //wxLogMessage("Received message from HTML: %s", message);

    Tab* tab = Slic3r::GUI::wxGetApp().get_plate_tab();
    if (tab)
        tab->on_value_change("test", 1);

    SendMessage("Hello from C++");


}
}} // namespace Slic3r::GUI