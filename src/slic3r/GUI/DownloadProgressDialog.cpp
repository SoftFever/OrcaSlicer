#include "DownloadProgressDialog.hpp"

#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/statbmp.h>
#include <wx/scrolwin.h>
#include <wx/clipbrd.h>
#include <wx/checkbox.h>
#include <wx/html/htmlwin.h>

#include <boost/algorithm/string/replace.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
//#include "ConfigWizard.hpp"
#include "wxExtensions.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "GUI_App.hpp"

#define DESIGN_INPUT_SIZE wxSize(FromDIP(100), -1)

namespace Slic3r {
namespace GUI {



DownloadProgressDialog::DownloadProgressDialog(wxString title)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top   = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_status_bar    = std::make_shared<BBLStatusBarSend>(this);
    m_panel_download = m_status_bar->get_panel();
    m_panel_download->SetSize(wxSize(FromDIP(340), -1));
    m_panel_download->SetMinSize(wxSize(FromDIP(340), -1));
    m_panel_download->SetMaxSize(wxSize(FromDIP(340), -1));
    m_sizer_main->Add(m_panel_download, 0, wxALL, FromDIP(20));
    m_sizer_main->Add(0, 0, 1, wxBOTTOM, 10);

    SetSizer(m_sizer_main);
    Layout();
    Fit();
    CentreOnParent();
}

bool DownloadProgressDialog::Show(bool show)
{
    if (show) {
        m_upgrade_job = std::make_shared<UpgradeNetworkJob>(m_status_bar);
        m_upgrade_job->set_event_handle(this);
        m_status_bar->set_progress(0);
        Bind(EVT_UPGRADE_NETWORK_SUCCESS, [this](wxCommandEvent& evt) {
            m_status_bar->change_button_label(_L("Finish"));
            wxGetApp().restart_networking();
            m_status_bar->set_cancel_callback_fina(
                [this]() {
                    this->Close();
                }
            );
        });

        Bind(EVT_UPGRADE_NETWORK_FAILED, [this](wxCommandEvent& evt) {
            m_status_bar->change_button_label(_L("Close"));
            m_status_bar->set_progress(0);
            m_status_bar->set_cancel_callback_fina(
                [this]() {
                    this->Close();
                }
            );
        });

        m_status_bar->set_cancel_callback_fina([this]() {
            if (m_upgrade_job) {
                m_upgrade_job->cancel();
                //EndModal(wxID_CLOSE);
            }
                
        });
        m_upgrade_job->start();
    }
    return DPIDialog::Show(show);
}

 DownloadProgressDialog::~DownloadProgressDialog() {}

void DownloadProgressDialog::on_dpi_changed(const wxRect &suggested_rect) {}

void DownloadProgressDialog::update_release_note(std::string release_note, std::string version) {}

}} // namespace Slic3r::GUI
