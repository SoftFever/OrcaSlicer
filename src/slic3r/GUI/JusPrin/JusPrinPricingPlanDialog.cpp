#include "JusPrinPricingPlanDialog.hpp"
#include "JusPrinUtils.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "slic3r/GUI/Widgets/WebView.hpp"
#include <wx/sizer.h>
#include <wx/settings.h>

namespace Slic3r { namespace GUI {

JusPrinPricingPlanDialog::JusPrinPricingPlanDialog()
    : wxDialog((wxWindow*)(wxGetApp().mainframe), wxID_ANY, "JusPrin Pricing Plan")
{
    SetBackgroundColour(*wxWHITE);

    m_browser = WebView::CreateWebView(this, "");
    if (m_browser == nullptr) {
        wxLogError("Could not create JusPrin pricing plan webview");
        return;
    }
    m_browser->Hide();
    m_browser->SetSize(0, 0);

    Bind(wxEVT_WEBVIEW_NAVIGATING, &JusPrinPricingPlanDialog::OnNavigationRequest, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NAVIGATED, &JusPrinPricingPlanDialog::OnNavigationComplete, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_LOADED, &JusPrinPricingPlanDialog::OnDocumentLoaded, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_ERROR, &JusPrinPricingPlanDialog::OnError, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NEWWINDOW, &JusPrinPricingPlanDialog::OnNewWindow, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_TITLE_CHANGED, &JusPrinPricingPlanDialog::OnTitleChanged, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_FULLSCREEN_CHANGED, &JusPrinPricingPlanDialog::OnFullScreenChanged, this, m_browser->GetId());

    SetTitle(_L("JusPrin Pricing Plan"));
    wxSize pSize = FromDIP(wxSize(820, 660));
    SetSize(pSize);

    int screenheight = wxSystemSettings::GetMetric(wxSYS_SCREEN_Y, NULL);
    int screenwidth = wxSystemSettings::GetMetric(wxSYS_SCREEN_X, NULL);
    int MaxY = (screenheight - pSize.y) > 0 ? (screenheight - pSize.y) / 2 : 0;
    wxPoint tmpPT((screenwidth - pSize.x) / 2, MaxY);
    Move(tmpPT);

    // Enable dev tools if developer mode is enabled
    m_browser->EnableAccessToDevTools(wxGetApp().app_config->get_bool("developer_mode"));

    // Construct the pricing URL using the base URL utility
    wxString pricing_url = JusPrinUtils::GetJusPrinBaseUrl() + "/ent/jusprin/pricing/";
    m_browser->LoadURL(pricing_url);
    wxGetApp().UpdateDlgDarkUI(this);
}

bool JusPrinPricingPlanDialog::run()
{
    return (ShowModal() == wxID_OK);
}

void JusPrinPricingPlanDialog::load_url(wxString& url)
{
    m_browser->LoadURL(url);
    m_browser->SetFocus();
    UpdateState();
}

void JusPrinPricingPlanDialog::UpdateState()
{
    // Update UI state if needed
}

void JusPrinPricingPlanDialog::OnNavigationRequest(wxWebViewEvent& evt)
{
    UpdateState();
}

void JusPrinPricingPlanDialog::OnNavigationComplete(wxWebViewEvent& evt)
{
    m_browser->Show();
    Layout();
    UpdateState();
}

void JusPrinPricingPlanDialog::OnDocumentLoaded(wxWebViewEvent& evt)
{
    UpdateState();
}

void JusPrinPricingPlanDialog::OnNewWindow(wxWebViewEvent& evt)
{
    m_browser->LoadURL(evt.GetURL());
    UpdateState();
}

void JusPrinPricingPlanDialog::OnTitleChanged(wxWebViewEvent& evt)
{
    // Optionally update dialog title
}

void JusPrinPricingPlanDialog::OnFullScreenChanged(wxWebViewEvent& evt)
{
    ShowFullScreen(evt.GetInt() != 0);
}

void JusPrinPricingPlanDialog::OnError(wxWebViewEvent& evt)
{
    UpdateState();
}

}} // namespace Slic3r::GUI