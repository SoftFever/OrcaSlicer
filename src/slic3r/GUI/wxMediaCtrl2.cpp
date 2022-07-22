#include "wxMediaCtrl2.h"
#include "I18N.hpp"

wxMediaCtrl2::wxMediaCtrl2(wxWindow *parent)
    : wxMediaCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxMEDIACTRLPLAYERCONTROLS_NONE)
{
}

void wxMediaCtrl2::Load(wxURI url)
{
#ifdef __WIN32__
    if (m_imp == nullptr) {
        auto res = wxMessageBox(_L("Windows Media Player is required for this task! Shall I take you to the guide page of 'Get Windows Media Player'?"), _L("Error"), wxOK | wxCANCEL);
        if (res == wxOK) {
            wxString url = "https://support.microsoft.com/en-au/windows/get-windows-media-player-81718e0d-cfce-25b1-aee3-94596b658287";
            wxExecute("cmd /c start " + url, wxEXEC_HIDE_CONSOLE);
        }
        m_error = 2;
        wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
        event.SetId(GetId());
        event.SetEventObject(this);
        wxPostEvent(this, event);
        return;
    }

    auto hModExe = LoadLibrary(NULL);
    auto NvOptimusEnablement = (DWORD *) GetProcAddress(hModExe, "NvOptimusEnablement");
    auto AmdPowerXpressRequestHighPerformance = (int *) GetProcAddress(hModExe, "AmdPowerXpressRequestHighPerformance");
    if (NvOptimusEnablement) *NvOptimusEnablement = 0;
    if (AmdPowerXpressRequestHighPerformance) *AmdPowerXpressRequestHighPerformance = 0;

    url = wxURI(url.BuildURI().append("&hwnd=").append(
        boost::lexical_cast<std::string>(GetHandle())));
#endif
    m_error = 0;
    wxMediaCtrl::Load(url);
}

void wxMediaCtrl2::Play() { wxMediaCtrl::Play(); }

void wxMediaCtrl2::Stop() { wxMediaCtrl::Stop(); }

wxSize wxMediaCtrl2::GetVideoSize() const
{
    return m_imp ? m_imp->GetVideoSize() : wxSize(0, 0);
}

wxSize wxMediaCtrl2::DoGetBestSize() const
{
    return {-1, -1};
}

#ifdef __WIN32__

WXLRESULT wxMediaCtrl2::MSWWindowProc(WXUINT   nMsg,
                                   WXWPARAM wParam,
                                   WXLPARAM lParam)
{
    if (nMsg == WM_USER + 1000) {
        wxString msg((wchar_t const *) lParam);
        if (wParam == 1) {
            if (msg.EndsWith("]")) {
                int n = msg.find_last_of('[');
                if (n != wxString::npos) {
                    long val = 0;
                    if (msg.SubString(n + 1, msg.Length() - 2).ToLong(&val))
                        m_error = (int) val;
                }
            }
        }
        BOOST_LOG_TRIVIAL(info) << msg.ToUTF8().data();
        return 0;
    }
    return wxMediaCtrl::MSWWindowProc(nMsg, wParam, lParam);
}

#endif
