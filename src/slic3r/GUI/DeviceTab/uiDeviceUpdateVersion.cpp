//**********************************************************/
/* File: uiDeviceUpdateVersion.cpp
*  Description: The panel with firmware info
* 
* \n class uiDeviceUpdateVersion
//**********************************************************/

#include "uiDeviceUpdateVersion.h"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <wx/stattext.h>

#define SERIAL_STR  L("Serial:")
#define VERSION_STR L("Version:")

using namespace Slic3r::GUI;


uiDeviceUpdateVersion::uiDeviceUpdateVersion(wxWindow* parent,
                                             wxWindowID id /*= wxID_ANY*/,
                                             const wxPoint& pos /*= wxDefaultPosition*/,
                                             const wxSize& size /*= wxDefaultSize*/,
                                             long style /*= wxTAB_TRAVERSAL*/)
     : wxPanel(parent, id, pos, size, style)
{
    CreateWidgets();
}

void uiDeviceUpdateVersion::UpdateInfo(const MachineObject::ModuleVersionInfo& info)
{
    SetName(I18N::translate(info.product_name));
    SetSerial(info.sn);
    SetVersion(info.sw_ver, info.sw_new_ver);
}

void uiDeviceUpdateVersion::SetVersion(const wxString& cur_version, const wxString& latest_version)
{
    if (cur_version.empty())
    {
        return;
    }

    if (!latest_version.empty() && (cur_version != latest_version))
    {
        const wxString& shown_ver = wxString::Format("%s->%s", cur_version, latest_version);
        m_dev_version->SetLabel(shown_ver);
        if (!m_dev_upgrade_indicator->IsShown())
        {
            m_dev_upgrade_indicator->Show(true);
        }
    }
    else
    {
        const wxString& shown_ver = wxString::Format("%s(%s)", cur_version, _L("Latest version"));
        m_dev_version->SetLabel(shown_ver);
        if (m_dev_upgrade_indicator->IsShown())
        {
            m_dev_upgrade_indicator->Hide();
        }
    }
}

void uiDeviceUpdateVersion::CreateWidgets()
{
    m_dev_name = new wxStaticText(this, wxID_ANY, "_");
    m_dev_snl = new wxStaticText(this, wxID_ANY, "_");
    m_dev_version = new wxStaticText(this, wxID_ANY, "_");

    wxStaticText* serial_text = new wxStaticText(this, wxID_ANY, _L(SERIAL_STR));
    wxStaticText* version_text = new wxStaticText(this, wxID_ANY, _L(VERSION_STR));

    // The main sizer
    wxFlexGridSizer* main_sizer = new wxFlexGridSizer(3, 3, 0, 0);
    main_sizer->AddGrowableCol(1);
    main_sizer->SetFlexibleDirection(wxHORIZONTAL);
    main_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    main_sizer->Add(m_dev_name, 0, wxALIGN_RIGHT | wxALL, FromDIP(5));
    main_sizer->Add(0, 0, wxALL, wxEXPAND);
    main_sizer->Add(0, 0, wxALL, wxEXPAND);

    main_sizer->Add(serial_text, 0, wxALIGN_RIGHT | wxALL, FromDIP(5));
    main_sizer->Add(m_dev_snl, 0, wxALIGN_LEFT | wxALL, FromDIP(5));
    main_sizer->Add(0, 0, wxALL, wxEXPAND);

    m_dev_upgrade_indicator = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(FromDIP(5), FromDIP(5)));
    m_dev_upgrade_indicator->SetBitmap(ScalableBitmap(this, "monitor_upgrade_online", 5).bmp());

    wxBoxSizer* version_hsizer = new wxBoxSizer(wxHORIZONTAL);
    version_hsizer->Add(m_dev_upgrade_indicator, 0, wxALIGN_CENTER_VERTICAL);
    version_hsizer->AddSpacer(FromDIP(5));
    version_hsizer->Add(version_text, 0);

    main_sizer->Add(version_hsizer, 0, wxALIGN_RIGHT | wxALL, FromDIP(5));
    main_sizer->Add(m_dev_version, 0, wxALIGN_LEFT | wxALL, FromDIP(5));
    main_sizer->Add(0, 0, wxALL, wxEXPAND);

    // Updating
    SetSizer(main_sizer);
    Layout();
}