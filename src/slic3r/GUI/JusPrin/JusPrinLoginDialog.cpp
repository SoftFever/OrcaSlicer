#include "JusPrinLoginDialog.hpp"

#include "../I18N.hpp"
#include "../GUI_App.hpp"
#include "../../libslic3r/AppConfig.hpp"
#include "libslic3r_version.h"
#include "../wxExtensions.hpp"
#include <wx/sizer.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>
#include <boost/lexical_cast.hpp>
#include <nlohmann/json.hpp>
#include <slic3r/GUI/Widgets/WebView.hpp>

using namespace nlohmann;

namespace Slic3r { namespace GUI {

#define JUSPRINT_LOGIN_TIMER_ID 10003

BEGIN_EVENT_TABLE(JusPrinLoginDialog, wxDialog)
    EVT_TIMER(JUSPRINT_LOGIN_TIMER_ID, JusPrinLoginDialog::OnTimer)
END_EVENT_TABLE()

JusPrinLoginDialog::JusPrinLoginDialog()
    : wxDialog((wxWindow*)(wxGetApp().mainframe), wxID_ANY, "JusPrin Login")
{
    SetBackgroundColour(*wxWHITE);

    // Set up the URL for JusPrin login
    m_jusprint_url = "https://app.obico.io/accounts/login/?hide_navbar=true&next=/o/authorize/%3Fresponse_type%3Dtoken%26client_id%3DJusPrin";
    m_networkOk = false;

    // Create the webview
    m_browser = WebView::CreateWebView(this, m_jusprint_url);
    if (m_browser == nullptr) {
        wxLogError("Could not create JusPrin login webview");
        return;
    }
    m_browser->Hide();
    m_browser->SetSize(0, 0);

    // Connect the webview events
    Bind(wxEVT_WEBVIEW_NAVIGATING, &JusPrinLoginDialog::OnNavigationRequest, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NAVIGATED, &JusPrinLoginDialog::OnNavigationComplete, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_LOADED, &JusPrinLoginDialog::OnDocumentLoaded, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_ERROR, &JusPrinLoginDialog::OnError, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NEWWINDOW, &JusPrinLoginDialog::OnNewWindow, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_TITLE_CHANGED, &JusPrinLoginDialog::OnTitleChanged, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_FULLSCREEN_CHANGED, &JusPrinLoginDialog::OnFullScreenChanged, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &JusPrinLoginDialog::OnScriptMessage, this, m_browser->GetId());

    // Set up the dialog
    SetTitle(_L("JusPrin Login"));
    wxSize pSize = FromDIP(wxSize(650, 840));
    SetSize(pSize);

    int screenheight = wxSystemSettings::GetMetric(wxSYS_SCREEN_Y, NULL);
    int screenwidth = wxSystemSettings::GetMetric(wxSYS_SCREEN_X, NULL);
    int MaxY = (screenheight - pSize.y) > 0 ? (screenheight - pSize.y) / 2 : 0;
    wxPoint tmpPT((screenwidth - pSize.x) / 2, MaxY);
    Move(tmpPT);

    wxGetApp().UpdateDlgDarkUI(this);
}

JusPrinLoginDialog::~JusPrinLoginDialog()
{
    if (m_timer) {
        m_timer->Stop();
        delete m_timer;
        m_timer = nullptr;
    }
}

bool JusPrinLoginDialog::run()
{
    m_timer = new wxTimer(this, JUSPRINT_LOGIN_TIMER_ID);
    m_timer->Start(8000);

    return (ShowModal() == wxID_OK);
}

void JusPrinLoginDialog::OnTimer(wxTimerEvent& event)
{
    m_timer->Stop();
    if (!m_networkOk) {
        ShowErrorPage();
    }
}

void JusPrinLoginDialog::load_url(wxString& url)
{
    m_browser->LoadURL(url);
    m_browser->SetFocus();
    UpdateState();
}

void JusPrinLoginDialog::UpdateState()
{
    // Update UI state if needed
}

void JusPrinLoginDialog::OnNavigationRequest(wxWebViewEvent& evt)
{
    wxString tmpUrl = evt.GetURL();
    if (tmpUrl.Contains("authorized/") && tmpUrl.Contains("access_token=")) {
        wxString access_token;
        int start = tmpUrl.Find("access_token=");
        if (start != wxNOT_FOUND) {
            start += 13; // length of "access_token="
            int end = tmpUrl.find('&', start);
            if (end != wxNOT_FOUND) {
                access_token = tmpUrl.SubString(start, end - 1);
            } else {
                access_token = tmpUrl.SubString(start, tmpUrl.Length() - 1);
            }
            std::string oauth_token = access_token.ToStdString();

            // Check if access_token is not null and not empty
            if (!oauth_token.empty()) {
                // Set the access_token in the jusprin_server section of the AppConfig
                wxGetApp().app_config->set("jusprin_server", "access_token", oauth_token);
                wxGetApp().app_config->save();

                // End the modal with wxID_OK
                EndModal(wxID_OK);
            } else {
                // End the modal with a "not okay" value
                EndModal(wxID_CANCEL);
            }
        } else {
            // No access_token found in the URL
            EndModal(wxID_CANCEL);
        }
    }

    UpdateState();
}

void JusPrinLoginDialog::OnNavigationComplete(wxWebViewEvent& evt)
{
    m_browser->Show();
    Layout();
    UpdateState();
}

void JusPrinLoginDialog::OnDocumentLoaded(wxWebViewEvent& evt)
{
    wxString tmpUrl = evt.GetURL();
    if (tmpUrl.Contains("jusprint.com")) {
        m_networkOk = true;
    }
    UpdateState();
}

void JusPrinLoginDialog::OnNewWindow(wxWebViewEvent& evt)
{
    wxString flag = evt.GetNavigationAction() == wxWEBVIEW_NAV_ACTION_USER ? " (user)" : " (other)";
    m_browser->LoadURL(evt.GetURL());
    UpdateState();
}

void JusPrinLoginDialog::OnTitleChanged(wxWebViewEvent& evt)
{
    // SetTitle(evt.GetString());
}

void JusPrinLoginDialog::OnFullScreenChanged(wxWebViewEvent& evt)
{
    ShowFullScreen(evt.GetInt() != 0);
}

void JusPrinLoginDialog::OnError(wxWebViewEvent& evt)
{
    if (evt.GetInt() == wxWEBVIEW_NAV_ERR_CONNECTION && !m_networkOk) {
        if (m_timer) {
            m_timer->Stop();
        }
        ShowErrorPage();
    }
    UpdateState();
}

void JusPrinLoginDialog::OnScriptMessage(wxWebViewEvent& evt)
{
    wxString str_input = evt.GetString();
    try {
        json j = json::parse(into_u8(str_input));
        wxString strCmd = j["command"];

        if (strCmd == "jusprint_login") {
            // Handle JusPrin login
            // wxGetApp().handle_jusprint_login(j.dump());
            Close();
        }
        // Add other JusPrin-specific commands as needed
    } catch (std::exception& e) {
        wxMessageBox(e.what(), "Parse JSON failed", wxICON_WARNING);
        Close();
    }
}

void JusPrinLoginDialog::RunScript(const wxString& javascript)
{
    m_javascript = javascript;
    if (m_browser) {
        WebView::RunScript(m_browser, javascript);
    }
}

bool JusPrinLoginDialog::ShowErrorPage()
{
    wxString ErrorUrl = from_u8((boost::filesystem::path(resources_dir()) / "web/login/error.html").make_preferred().string());
    load_url(ErrorUrl);
    return true;
}

}} // namespace Slic3r::GUI
