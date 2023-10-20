#include "wxMediaCtrl2.h"
#include "I18N.hpp"
#include "GUI_App.hpp"
#ifdef __WIN32__
#include <versionhelpers.h>
#include <wx/msw/registry.h>
#include <shellapi.h>
#endif

#ifdef __LINUX__
#include "Printer/gstbambusrc.h"
#include <gst/gst.h> // main gstreamer header
class WXDLLIMPEXP_MEDIA
    wxGStreamerMediaBackend : public wxMediaBackendCommonBase
{
public:
    GstElement *m_playbin; // GStreamer media element
};
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
#ifdef __LINUX__
    /* Register only after we have created the wxMediaCtrl, since only then are we guaranteed to have fired up Gstreamer's plugin registry. */
    auto playbin = reinterpret_cast<wxGStreamerMediaBackend *>(m_imp)->m_playbin;
    g_object_set (G_OBJECT (playbin),
                  "audio-sink", NULL,
                   NULL);
    gstbambusrc_register();
    Bind(wxEVT_MEDIA_LOADED, [this](auto & e) {
        m_loaded = true;
    });
#endif
}

#define CLSID_BAMBU_SOURCE L"{233E64FB-2041-4A6C-AFAB-FF9BCF83E7AA}"

void wxMediaCtrl2::Load(wxURI url)
{
#ifdef __WIN32__
    if (m_imp == nullptr) {
        CallAfter([] {
            auto res = wxMessageBox(_L("Windows Media Player is required for this task! Do you want to enable 'Windows Media Player' for your operation system?"), _L("Error"), wxOK | wxCANCEL);
            if (res == wxOK) {
                wxString url = IsWindows10OrGreater() 
                        ? "ms-settings:optionalfeatures?activationSource=SMC-Article-14209" 
                        : "https://support.microsoft.com/en-au/windows/get-windows-media-player-81718e0d-cfce-25b1-aee3-94596b658287";
                wxExecute("cmd /c start " + url, wxEXEC_HIDE_CONSOLE);
            }
        });
        m_error = 100;
        wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
        event.SetId(GetId());
        event.SetEventObject(this);
        wxPostEvent(this, event);
        return;
    }
    {
        wxRegKey key11(wxRegKey::HKCU, L"SOFTWARE\\Classes\\CLSID\\" CLSID_BAMBU_SOURCE L"\\InProcServer32");
        wxRegKey key12(wxRegKey::HKCR, L"CLSID\\" CLSID_BAMBU_SOURCE L"\\InProcServer32");
        wxString path = key11.Exists() ? key11.QueryDefaultValue() 
                                       : key12.Exists() ? key12.QueryDefaultValue() : wxString{};
        wxRegKey key2(wxRegKey::HKCR, "bambu");
        wxString clsid;
        if (key2.Exists())
            key2.QueryRawValue("Source Filter", clsid);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": clsid %1% path %2%") % clsid % path;

        std::string             data_dir_str = Slic3r::data_dir();
        boost::filesystem::path data_dir_path(data_dir_str);
        auto                    dll_path = data_dir_path / "plugins" / "BambuSource.dll";

        if (path.empty() || !wxFile::Exists(path) || clsid != CLSID_BAMBU_SOURCE) {
            if (boost::filesystem::exists(dll_path)) {
                CallAfter(
                    [dll_path] {
                    int res = wxMessageBox(_L("BambuSource has not correctly been registered for media playing! Press Yes to re-register it."), _L("Error"), wxYES_NO | wxICON_ERROR);
                    if (res == wxYES) {
                        SHELLEXECUTEINFO info{sizeof(info), 0, NULL, L"open", L"regsvr32", dll_path.wstring().c_str(), SW_HIDE };
                        ::ShellExecuteEx(&info);
                    }
                });
            } else {
                CallAfter([] {
                    wxMessageBox(_L("Missing BambuSource component registered for media playing! Please re-install BambuStutio or seek after-sales help."), _L("Error"), wxOK | wxICON_ERROR);
                });
            }
            m_error = clsid != CLSID_BAMBU_SOURCE ? 101 : path.empty() ? 102 : 103;
            wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
            event.SetId(GetId());
            event.SetEventObject(this);
            wxPostEvent(this, event);
            return;
        }
        if (path != dll_path) {
            static bool notified = false;
            if (!notified) CallAfter([dll_path] {
                int res = wxMessageBox(_L("Using a BambuSource from a different install, video play may not work correctly! Press Yes to fix it."), _L("Warning"), wxYES_NO | wxICON_WARNING);
                if (res == wxYES) {
                    SHELLEXECUTEINFO info{sizeof(info), 0, NULL, L"open", L"regsvr32", dll_path.wstring().c_str(), SW_HIDE};
                    ::ShellExecuteEx(&info);
                }
            });
            notified = true;
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
    url = wxURI(url.BuildURI().append("&hwnd=").append(boost::lexical_cast<std::string>(GetHandle())).append("&tid=").append(
        boost::lexical_cast<std::string>(GetCurrentThreadId())));
#endif
#ifdef __WXGTK3__
    GstElementFactory *factory;
    int hasplugins = 1;
    
    factory = gst_element_factory_find("h264parse");
    if (!factory) {
        hasplugins = 0;
    } else {
        gst_object_unref(factory);
    }
    
    factory = gst_element_factory_find("openh264dec");
    if (!factory) {
        factory = gst_element_factory_find("avdec_h264");
    }
    if (!factory) {
        factory = gst_element_factory_find("vaapih264dec");
    }
    if (!factory) {
        hasplugins = 0;
    } else {
        gst_object_unref(factory);
    }
    
    if (!hasplugins) {
        CallAfter([] {
            wxMessageBox(_L("Your system is missing H.264 codecs for GStreamer, which are required to play video.  (Try installing the gstreamer1.0-plugins-bad or gstreamer1.0-libav packages, then restart Bambu Studio?)"), _L("Error"), wxOK);
        });
        m_error = 101;
        wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
        event.SetId(GetId());
        event.SetEventObject(this);
        wxPostEvent(this, event);
        return;
    }
    wxLog::EnableLogging(false);
#endif
    m_error = 0;
    m_loaded = false;
    wxMediaCtrl::Load(url);

#ifdef __WXGTK3__
        wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
        event.SetId(GetId());
        event.SetEventObject(this);
        wxPostEvent(this, event);
#endif
}

void wxMediaCtrl2::Play() { wxMediaCtrl::Play(); }

void wxMediaCtrl2::Stop()
{
    wxMediaCtrl::Stop();
}

#ifdef __LINUX__
extern "C" int gst_bambu_last_error;
#endif

int wxMediaCtrl2::GetLastError() const
{
#ifdef __LINUX__
    return gst_bambu_last_error;
#else
    return m_error;
#endif
}

wxSize wxMediaCtrl2::GetVideoSize() const
{
#ifdef __LINUX__
    // Gstreamer doesn't give us a VideoSize until we're playing, which
    // confuses the MediaPlayCtrl into claiming that it is stuck
    // "Loading...".  Fake it out for now.
    return m_loaded ? wxSize(1280, 720) : wxSize{};
#else
    return m_imp ? m_imp->GetVideoSize() : wxSize(0, 0);
#endif
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
