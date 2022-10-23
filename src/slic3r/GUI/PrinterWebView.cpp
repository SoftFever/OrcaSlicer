#include "PrinterWebView.hpp"

#include "I18N.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "libslic3r_version.h"

#include <wx/sizer.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>

#include <slic3r/GUI/Widgets/WebView.hpp>

namespace pt = boost::property_tree;

namespace Slic3r {
namespace GUI {

PrinterWebView::PrinterWebView(wxWindow *parent)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
 {

    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);

    // Create the button
    bSizer_toolbar = new wxBoxSizer(wxHORIZONTAL);

    //m_button_reload = new wxButton(this, wxID_ANY, wxT("Reload"), wxDefaultPosition, wxDefaultSize, 0);
    //bSizer_toolbar->Add(m_button_reload, 0, wxALL, 5);

    m_url = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    bSizer_toolbar->Add(m_url, 1, wxALL | wxEXPAND, 5);

  
    // Create the webview
    m_browser = WebView::CreateWebView(this, "");
    if (m_browser == nullptr) {
        wxLogError("Could not init m_browser");
        return;
    }

    SetSizer(topsizer);

    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));

    // Log backend information
    if (wxGetApp().get_mode() == comDevelop) {
        wxLogMessage(wxWebView::GetBackendVersionInfo().ToString());
        wxLogMessage("Backend: %s Version: %s", m_browser->GetClassInfo()->GetClassName(),
            wxWebView::GetBackendVersionInfo().ToString());
        wxLogMessage("User Agent: %s", m_browser->GetUserAgent());
    }

    //Zoom
    m_zoomFactor = 100;

    // Connect the button events
    //Bind(wxEVT_BUTTON, &PrinterWebView::OnReload, this, m_button_reload->GetId());
    Bind(wxEVT_TEXT_ENTER, &PrinterWebView::OnUrl, this, m_url->GetId());
    //Connect the idle events
    Bind(wxEVT_CLOSE_WINDOW, &PrinterWebView::OnClose, this);

 }

PrinterWebView::~PrinterWebView()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Start";
    SetEvtHandlerEnabled(false);

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " End";
}


void PrinterWebView::load_url(wxString& url)
{
    //this->Show();
    //this->Raise();
    m_url->SetLabelText(url);

    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage(m_url->GetValue());
    m_browser->LoadURL(url);
    //m_browser->SetFocus();
    UpdateState();
}
/**
 * Method that retrieves the current state from the web control and updates the
 * GUI the reflect this current state.
 */
void PrinterWebView::UpdateState() {
  // SetTitle(m_browser->GetCurrentTitle());
  m_url->SetValue(m_browser->GetCurrentURL());
}

/**
    * Callback invoked when user entered an URL and pressed enter
    */
void PrinterWebView::OnUrl(wxCommandEvent& WXUNUSED(evt))
{
    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage(m_url->GetValue());
    m_browser->LoadURL(m_url->GetValue());
    m_browser->SetFocus();
    UpdateState();
}

/**
    * Callback invoked when user pressed the "reload" button
    */
void PrinterWebView::OnReload(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->Reload();
    UpdateState();
}

void PrinterWebView::OnClose(wxCloseEvent& evt)
{
    this->Hide();
}



} // GUI
} // Slic3r
