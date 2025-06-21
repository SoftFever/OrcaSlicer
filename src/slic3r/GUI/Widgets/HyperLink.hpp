#ifndef slic3r_GUI_HyperLink_hpp_
#define slic3r_GUI_HyperLink_hpp_

#include <wx/wx.h>
#include <wx/window.h>
#include "Label.hpp"

#include "slic3r/GUI/GUI_App.hpp"

namespace Slic3r {
namespace GUI {

enum class HyperLinkType{
    Releases,

    Wiki_STL_Transformation,
    Wiki_Calib_Temp,
    Wiki_Calib_FlowRate,
    Wiki_Calib_PA,
    Wiki_Calib_Retraction,
    Wiki_Calib_InputShaping,
    Wiki_Calib_JunctionDeviation,
    Wiki_Calib_MaxVolumetricSpeed,

    BBL_NetworkPlugin,
    BBL_NetworkCheck,
    BBL_Privacy,
    BBL_PinCode,
    BBL_FailedConnect,
    BBL_LANonly,
    BBL_CustomFilamentIssue
};

class HyperLink : public wxStaticText
{
public:
    HyperLink(wxWindow* parent, const wxString& label = wxEmptyString, const wxString& url = wxEmptyString);

    void     SetURL(const wxString& url) { m_url = url; }
    wxString GetURL() const { return m_url; }

    void     SetFont(wxFont& font);

private:
    wxString m_url;
    wxColour m_normalColor;
    wxColour m_hoverColor;

public:
    static wxString For(HyperLinkType link)
    {
        wxString url;
        wxString country = get_app_config()->get_country_code();

        // HOMEPAGE / RELEASE ETC
        if      (link == HyperLinkType::Releases)
            url = "https://github.com/SoftFever/OrcaSlicer/releases";

        // WIKI
        else if (link == HyperLinkType::Wiki_STL_Transformation)
            url = "https://github.com/SoftFever/OrcaSlicer/wiki/stl-transformation";
        else if (link == HyperLinkType::Wiki_Calib_Temp)
            url = "https://github.com/SoftFever/OrcaSlicer/wiki/temp-calib";
        else if (link == HyperLinkType::Wiki_Calib_FlowRate)
            url = "https://github.com/SoftFever/OrcaSlicer/wiki/flow-rate-calib";
        else if (link == HyperLinkType::Wiki_Calib_PA)
            url = "https://github.com/SoftFever/OrcaSlicer/wiki/pressure-advance-calib";
        else if (link == HyperLinkType::Wiki_Calib_Retraction)
            url = "https://github.com/SoftFever/OrcaSlicer/wiki/retraction-calib";
        else if (link == HyperLinkType::Wiki_Calib_InputShaping)
            url = "https://github.com/SoftFever/OrcaSlicer/wiki/input-shaping-calib";
        else if (link == HyperLinkType::Wiki_Calib_JunctionDeviation)
            url = "https://github.com/SoftFever/OrcaSlicer/wiki/cornering-calib";
        else if (link == HyperLinkType::Wiki_Calib_MaxVolumetricSpeed)
            url = "https://github.com/SoftFever/OrcaSlicer/wiki/volumetric-speed-calib";

        // BBL
        else if (link == HyperLinkType::BBL_NetworkCheck){
            if (country == "CN")
                url = "https://status.bambulab.cn";
            else
                url = "https://status.bambulab.com";
        }
        else if (link == HyperLinkType::BBL_NetworkPlugin)
            url = "https://wiki.bambulab.com/en/software/bambu-studio/failed-to-get-network-plugin";
        else if (link == HyperLinkType::BBL_Privacy){
            if (country == "CN")
                url = "https://www.bambulab.cn/policies/privacy";
            else
                url = "https://www.bambulab.com/policies/privacy";
        }
        else if (link == HyperLinkType::BBL_PinCode)
            url = "https://wiki.bambulab.com/en/bambu-studio/manual/pin-code";
        else if (link == HyperLinkType::BBL_FailedConnect)
            url = "https://wiki.bambulab.com/en/software/bambu-studio/failed-to-connect-printer";
        else if (link == HyperLinkType::BBL_LANonly){
            if (country == "CN")
                url = "https://wiki.bambulab.com/zh/knowledge-sharing/enable-lan-mode";
            else
                url = "https://wiki.bambulab.com/en/knowledge-sharing/enable-lan-mode";
        }
        else if (link == HyperLinkType::BBL_CustomFilamentIssue)
            url = "https://wiki.bambulab.com/en/software/bambu-studio/custom-filament-issue";

        return url;
    };
};

}
}
#endif // !slic3r_GUI_HyperLink_hpp_
