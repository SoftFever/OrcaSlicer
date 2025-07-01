#include "JusPrinAccountDialog.hpp"
#include "../GUI_App.hpp"
#include "slic3r/GUI/Widgets/WebView.hpp"
#include <wx/sizer.h>
#include <wx/systemsettings.h>

namespace Slic3r { namespace GUI {

JusPrinAccountDialog::JusPrinAccountDialog(const wxString& url)
    : wxDialog((wxWindow*)(wxGetApp().mainframe), wxID_ANY, "JusPrin Account"), m_url(url)
{
    SetBackgroundColour(*wxWHITE);

    m_browser = WebView::CreateWebView(this, "");
    if (m_browser == nullptr) {
        wxLogError("Could not create JusPrin account webview");
        return;
    }
    m_browser->Hide();
    m_browser->SetSize(0, 0);

    Bind(wxEVT_WEBVIEW_NAVIGATING, &JusPrinAccountDialog::OnNavigationRequest, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NAVIGATED, &JusPrinAccountDialog::OnNavigationComplete, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_LOADED, &JusPrinAccountDialog::OnDocumentLoaded, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_ERROR, &JusPrinAccountDialog::OnError, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NEWWINDOW, &JusPrinAccountDialog::OnNewWindow, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_TITLE_CHANGED, &JusPrinAccountDialog::OnTitleChanged, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_FULLSCREEN_CHANGED, &JusPrinAccountDialog::OnFullScreenChanged, this, m_browser->GetId());

    SetTitle("JusPrin Account");
    wxSize pSize = wxSize(650, 840);
    SetSize(pSize);

    int screenheight = wxSystemSettings::GetMetric(wxSYS_SCREEN_Y, NULL);
    int screenwidth = wxSystemSettings::GetMetric(wxSYS_SCREEN_X, NULL);
    int MaxY = (screenheight - pSize.y) > 0 ? (screenheight - pSize.y) / 2 : 0;
    wxPoint tmpPT((screenwidth - pSize.x) / 2, MaxY);
    Move(tmpPT);

    m_browser->LoadURL(m_url);
}

bool JusPrinAccountDialog::run()
{
    return (ShowModal() == wxID_OK);
}

void JusPrinAccountDialog::load_url(wxString& url)
{
    m_browser->LoadURL(url);
    m_browser->SetFocus();
    UpdateState();
}

void JusPrinAccountDialog::UpdateState()
{
    // Update UI state if needed
}

void JusPrinAccountDialog::OnNavigationRequest(wxWebViewEvent& evt)
{
    UpdateState();
}

void JusPrinAccountDialog::OnNavigationComplete(wxWebViewEvent& evt)
{
    m_browser->Show();
    Layout();
    UpdateState();
}

void JusPrinAccountDialog::OnDocumentLoaded(wxWebViewEvent& evt)
{
    UpdateState();
}

void JusPrinAccountDialog::OnNewWindow(wxWebViewEvent& evt)
{
    m_browser->LoadURL(evt.GetURL());
    UpdateState();
}

void JusPrinAccountDialog::OnTitleChanged(wxWebViewEvent& evt)
{
    // Optionally update dialog title
}

void JusPrinAccountDialog::OnFullScreenChanged(wxWebViewEvent& evt)
{
    ShowFullScreen(evt.GetInt() != 0);
}

void JusPrinAccountDialog::OnError(wxWebViewEvent& evt)
{
    UpdateState();
}

}} // namespace Slic3r::GUI