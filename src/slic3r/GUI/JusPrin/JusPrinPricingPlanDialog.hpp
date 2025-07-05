#ifndef slic3r_GUI_JusPrinPricingPlanDialog_hpp_
#define slic3r_GUI_JusPrinPricingPlanDialog_hpp_

#include <wx/dialog.h>
#include <wx/webview.h>

namespace Slic3r { namespace GUI {

class JusPrinPricingPlanDialog : public wxDialog
{
public:
    JusPrinPricingPlanDialog();
    bool run();

private:
    wxWebView* m_browser {nullptr};
    void OnNavigationRequest(wxWebViewEvent& evt);
    void OnNavigationComplete(wxWebViewEvent& evt);
    void OnDocumentLoaded(wxWebViewEvent& evt);
    void OnNewWindow(wxWebViewEvent& evt);
    void OnError(wxWebViewEvent& evt);
    void OnTitleChanged(wxWebViewEvent& evt);
    void OnFullScreenChanged(wxWebViewEvent& evt);

    void load_url(wxString& url);
    void UpdateState();
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_JusPrinPricingPlanDialog_hpp_