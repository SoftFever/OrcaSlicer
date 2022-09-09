#include "wxMediaCtrl2.h"
#include "I18N.hpp"
#include "GUI_App.hpp"
#ifdef __WIN32__
#include <versionhelpers.h>
#include <wx/msw/registry.h>
#endif

wxMediaCtrl2::wxMediaCtrl2(wxWindow *parent)
{
#ifdef __WIN32__
    auto hModExe = GetModuleHandle(NULL);
    // BOOST_LOG_TRIVIAL(info) << "wxMediaCtrl2: GetModuleHandle " << hModExe;
    auto NvOptimusEnablement = (DWORD *) GetProcAddress(hModExe, "NvOptimusEnablement");
    auto AmdPowerXpressRequestHighPerformance = (int *) GetProcAddress(hModExe, "AmdPowerXpressRequestHighPerformance");
    if (NvOptimusEnablement) {
        // BOOST_LOG_TRIVIAL(info) << "wxMediaCtrl2: NvOptimusEnablement " << *NvOptimusEnablement;
        *NvOptimusEnablement = 0;
    }
    if (AmdPowerXpressRequestHighPerformance) {
        // BOOST_LOG_TRIVIAL(info) << "wxMediaCtrl2: AmdPowerXpressRequestHighPerformance " << *AmdPowerXpressRequestHighPerformance;
        *AmdPowerXpressRequestHighPerformance = 0;
    }
#endif
    wxMediaCtrl::Create(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxMEDIACTRLPLAYERCONTROLS_NONE);
}

void wxMediaCtrl2::Load(wxURI url)
{
#ifdef __WIN32__
    if (m_imp == nullptr) {
        Slic3r::GUI::wxGetApp().CallAfter([] {
            auto res = wxMessageBox(_L("Windows Media Player is required for this task! Do you want to enable 'Windows Media Player' for your operation system?"), _L("Error"), wxOK | wxCANCEL);
            if (res == wxOK) {
                wxString url = IsWindows10OrGreater() 
                        ? "ms-settings:optionalfeatures?activationSource=SMC-Article-14209" 
                        : "https://support.microsoft.com/en-au/windows/get-windows-media-player-81718e0d-cfce-25b1-aee3-94596b658287";
                wxExecute("cmd /c start " + url, wxEXEC_HIDE_CONSOLE);
            }
        });
        m_error = 2;
        wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
        event.SetId(GetId());
        event.SetEventObject(this);
        wxPostEvent(this, event);
        return;
    }
    {
        wxRegKey key1(wxRegKey::HKCR, "CLSID\\{233E64FB-2041-4A6C-AFAB-FF9BCF83E7AA}\\InProcServer32");
        wxString path = key1.QueryDefaultValue();
        wxRegKey key2(wxRegKey::HKCR, "bambu");
        wxString clsid;
        key2.QueryRawValue("Source Filter", clsid);
        if (!wxFile::Exists(path) || clsid != L"{233E64FB-2041-4A6C-AFAB-FF9BCF83E7AA}") {
            Slic3r::GUI::wxGetApp().CallAfter([] {
                wxMessageBox(_L("Missing BambuSource component registered for media playing! Please re-install BambuStutio or seek after-sales help."), _L("Error"),
                                    wxOK);
            });
            m_error = clsid.IsEmpty() ? 100 : clsid != L"{233E64FB-2041-4A6C-AFAB-FF9BCF83E7AA}" ? 101 : 102;
            wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
            event.SetId(GetId());
            event.SetEventObject(this);
            wxPostEvent(this, event);
            return;
        }
        wxRegKey keyWmp(wxRegKey::HKCU, "SOFTWARE\\Microsoft\\MediaPlayer\\Player\\Extensions\\.");
        keyWmp.Create();
        long permissions = 0;
        if (keyWmp.HasValue("Permissions"))
            keyWmp.QueryValue("Permissions", &permissions);
        if ((permissions & 32) == 0) {
            permissions |= 32;
            keyWmp.SetValue("Permissions", permissions);
        }
    }
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
