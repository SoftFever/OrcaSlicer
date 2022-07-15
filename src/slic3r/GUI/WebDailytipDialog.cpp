#include "WebDailytipDialog.hpp"

#include <string.h>
#include "I18N.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r_version.h"
#include <wx/sizer.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>

#include <wx/wx.h>
#include <wx/fileconf.h>
#include <wx/file.h>
#include <wx/wfstream.h>

#include <boost/cast.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/dll.hpp>
#include <nlohmann/json.hpp>
#include <slic3r/GUI/Widgets/WebView.hpp>

using namespace nlohmann;

namespace Slic3r { namespace GUI {

// BEGIN_EVENT_TABLE(DailytipFrame,wxCheckBox)
// EVT_CHECKBOX(SKIP_CHECKBOX, DailytipFrame::OnSkipClick)
// END_EVENT_TABLE()

DailytipFrame::DailytipFrame(GUI_App *pGUI)
    : wxDialog((wxWindow *) (pGUI->mainframe), wxID_ANY, "BambuStudio")
{
    wxString ExePath = boost::dll::program_location().parent_path().string();
    wxString TargetUrl = "file:///" + ExePath + "\\resources\\dailytip\\index.html";

    m_MainPtr = pGUI;

    wxBoxSizer *topsizer = new wxBoxSizer(wxVERTICAL);

    // Create the webview
    m_browser = WebView::CreateWebView(this, TargetUrl);
    if (m_browser == nullptr) {
        wxLogError("Could not init m_browser");
        return;
    }

    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));
    SetSizer(topsizer);

#ifdef __WXMAC__
    // With WKWebView handlers need to be registered before creation
    m_browser->RegisterHandler(
        wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("wxfs")));
    m_browser->RegisterHandler(
        wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
#endif

    // topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));

    // Set a more sensible size for web browsing
    m_browser->SetSize(FromDIP(wxSize(560, 640)));
    SetSize(FromDIP(wxSize(560, 710)));

    // Connect the webview events
    Bind(wxEVT_WEBVIEW_NAVIGATING, &DailytipFrame::OnNavigationRequest, this,
         m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NAVIGATED, &DailytipFrame::OnNavigationComplete, this,
         m_browser->GetId());
    Bind(wxEVT_WEBVIEW_LOADED, &DailytipFrame::OnDocumentLoaded, this,
         m_browser->GetId());
    Bind(wxEVT_WEBVIEW_ERROR, &DailytipFrame::OnError, this,
         m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NEWWINDOW, &DailytipFrame::OnNewWindow, this,
         m_browser->GetId());
    Bind(wxEVT_WEBVIEW_TITLE_CHANGED, &DailytipFrame::OnTitleChanged, this,
         m_browser->GetId());
    Bind(wxEVT_WEBVIEW_FULLSCREEN_CHANGED,
         &DailytipFrame::OnFullScreenChanged, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED,
         &DailytipFrame::OnScriptMessage, this, m_browser->GetId());

    // Connect the idle events
    // Bind(wxEVT_IDLE, &DailytipFrame::OnIdle, this);
    // Bind(wxEVT_CLOSE_WINDOW, &DailytipFrame::OnClose, this);

    // UI
    SetTitle(L"Daily Tip");
    CenterOnParent();

    // INI
    m_SectionName = "dailytip";

    // Skip CheckBox
    m_SkipBtn = new wxCheckBox(this, wxID_ANY, L"Never Show",
                               FromDIP(wxPoint(10, 640)),
                               FromDIP(wxSize(200, 30)));
    Bind(wxEVT_CHECKBOX, &DailytipFrame::OnSkipClick, this,
         m_SkipBtn->GetId());
}

DailytipFrame::~DailytipFrame() {}

void DailytipFrame::load_url(wxString &url)
{
    this->Show();
    m_browser->LoadURL(url);
    m_browser->SetFocus();
    UpdateState();
}

/**
 * Method that retrieves the current state from the web control and updates
 * the GUI the reflect this current state.
 */
void DailytipFrame::UpdateState()
{
    // SetTitle(m_browser->GetCurrentTitle());
}

void DailytipFrame::OnIdle(wxIdleEvent &WXUNUSED(evt))
{
    if (m_browser->IsBusy()) {
        wxSetCursor(wxCURSOR_ARROWWAIT);
    } else {
        wxSetCursor(wxNullCursor);
    }
}

/**
 * Callback invoked when there is a request to load a new page (for instance
 * when the user clicks a link)
 */
void DailytipFrame::OnNavigationRequest(wxWebViewEvent &evt)
{
    // wxLogMessage("%s", "Navigation request to '" + evt.GetURL() + "'
    // (target='" + evt.GetTarget() + "')");

    UpdateState();
}

/**
 * Callback invoked when a navigation request was accepted
 */
void DailytipFrame::OnNavigationComplete(wxWebViewEvent &evt)
{
    // wxLogMessage("%s", "Navigation complete; url='" + evt.GetURL() + "'");
    UpdateState();
}

/**
 * Callback invoked when a page is finished loading
 */
void DailytipFrame::OnDocumentLoaded(wxWebViewEvent &evt)
{
    UpdateShowTime();

    // Only notify if the document is the main frame, not a subframe
    wxString tmpUrl = evt.GetURL();
    wxString NowUrl = m_browser->GetCurrentURL();

    if (evt.GetURL() == m_browser->GetCurrentURL()) {
        // wxLogMessage("%s", "Document loaded; url='" + evt.GetURL() + "'");
    }
    UpdateState();

    // wxCommandEvent *event = new
    // wxCommandEvent(EVT_WEB_RESPONSE_MESSAGE,this->GetId());
    // wxQueueEvent(this, event);
}

/**
 * On new window, we veto to stop extra windows appearing
 */
void DailytipFrame::OnNewWindow(wxWebViewEvent &evt)
{
    wxString flag = " (other)";

    if (evt.GetNavigationAction() == wxWEBVIEW_NAV_ACTION_USER) {
        flag = " (user)";
    }

    // wxLogMessage("%s", "New window; url='" + evt.GetURL() + "'" + flag);

    // If we handle new window events then just load them in this window as we
    // are a single window browser
    // if (m_tools_handle_new_window->IsChecked())
    //    m_browser->LoadURL(evt.GetURL());

    UpdateState();
}

void DailytipFrame::OnTitleChanged(wxWebViewEvent &evt)
{
    // SetTitle(evt.GetString());
    // wxLogMessage("%s", "Title changed; title='" + evt.GetString() + "'");
}

void DailytipFrame::OnFullScreenChanged(wxWebViewEvent &evt)
{
    // wxLogMessage("Full screen changed; status = %d", evt.GetInt());
    ShowFullScreen(evt.GetInt() != 0);
}

void DailytipFrame::OnScriptMessage(wxWebViewEvent &evt)
{
    ;
}

void DailytipFrame::RunScript(const wxString &javascript)
{
    // Remember the script we run in any case, so the next time the user opens
    // the "Run Script" dialog box, it is shown there for convenient updating.
    m_javascript = javascript;

    // wxLogMessage("Running JavaScript:\n%s\n", javascript);

    if (!m_browser) return;

    WebView::RunScript(m_browser, javascript);
}

#if wxUSE_WEBVIEW_IE
void DailytipFrame::OnRunScriptObjectWithEmulationLevel(
    wxCommandEvent &WXUNUSED(evt))
{
    wxWebViewIE::MSWSetModernEmulationLevel();
    RunScript("function f(){var person = new Object();person.name = 'Foo'; \
    person.lastName = 'Bar';return person;}f();");
    wxWebViewIE::MSWSetModernEmulationLevel(false);
}

void DailytipFrame::OnRunScriptDateWithEmulationLevel(
    wxCommandEvent &WXUNUSED(evt))
{
    wxWebViewIE::MSWSetModernEmulationLevel();
    RunScript("function f(){var d = new Date('10/08/2017 21:30:40'); \
    var tzoffset = d.getTimezoneOffset() * 60000; return \
    new Date(d.getTime() - tzoffset);}f();");
    wxWebViewIE::MSWSetModernEmulationLevel(false);
}

void DailytipFrame::OnRunScriptArrayWithEmulationLevel(
    wxCommandEvent &WXUNUSED(evt))
{
    wxWebViewIE::MSWSetModernEmulationLevel();
    RunScript("function f(){ return [\"foo\", \"bar\"]; }f();");
    wxWebViewIE::MSWSetModernEmulationLevel(false);
}
#endif

/**
 * Callback invoked when a loading error occurs
 */
void DailytipFrame::OnError(wxWebViewEvent &evt)
{
#define WX_ERROR_CASE(type) \
    case type: category = #type; break;

    wxString category;
    switch (evt.GetInt()) {
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CONNECTION);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CERTIFICATE);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_AUTH);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_SECURITY);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_NOT_FOUND);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_REQUEST);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_USER_CANCELLED);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_OTHER);
    }

    // wxLogMessage("%s", "Error; url='" + evt.GetURL() + "', error='" +
    // category + " (" + evt.GetString() + ")'");

    // Show the info bar with an error
    // m_info->ShowMessage(_L("An error occurred loading ") + evt.GetURL() +
    // "\n" + "'" + category + "'", wxICON_ERROR);

    UpdateState();
}

void DailytipFrame::OnScriptResponseMessage(wxCommandEvent &WXUNUSED(evt))
{
    // if (!m_response_js.empty())
    //{
    //    RunScript(m_response_js);
    //}

    // RunScript("This is a message to Web!");
    // RunScript("postMessage(\"AABBCCDD\");");
}

bool DailytipFrame::IsTodayUsed()
{
    std::string strTime =
        wxGetApp().app_config->get(std::string(m_SectionName.mbc_str()),
                                   "lasttime");

    int nLastTime = wxAtol(strTime);

    int NowTime = std::time(0);

    int T1 = NowTime - NowTime % 86400;
    int T2 = T1 + 86400;

    if (nLastTime >= T1 && nLastTime < T2) return true;

    return false;
}

bool DailytipFrame::IsSkip()
{
    wxString strSkip = wxGetApp().app_config->get(std::string(
                                                      m_SectionName.mbc_str()),
                                                  "skip");
    if (strSkip == "1") return true;

    return false;
}

bool DailytipFrame::IsWhetherShow()
{
    if (IsSkip() || IsTodayUsed()) return false;

    return true;
}

void DailytipFrame::UpdateShowTime()
{
    int NowTime = std::time(0);

    wxGetApp().app_config->set(std::string(m_SectionName.mbc_str()),
                               "lasttime", std::to_string(NowTime));
    wxGetApp().app_config->save();
}

void DailytipFrame::OnSkipClick(wxCommandEvent &evt)
{
    bool bSelect = m_SkipBtn->GetValue();

    int nVal = 0;
    if (bSelect) nVal = 1;

    // wxMessageBox(wxString::Format(wxT("%i"), nVal), "Checkbox Value", MB_OK);

    wxGetApp().app_config->set(std::string(m_SectionName.mbc_str()), "skip",
                               std::to_string(nVal));
    wxGetApp().app_config->save();
}

}} // namespace Slic3r::GUI
