#include "CalibrationPanel.hpp"
#include "I18N.hpp"

namespace Slic3r { namespace GUI {

#define REFRESH_INTERVAL       1000

CalibrationPanel::CalibrationPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
{
    SetBackgroundColour(*wxWHITE);

    init_tabpanel();

    wxBoxSizer* sizer_main = new wxBoxSizer(wxVERTICAL);
    sizer_main->Add(m_tabpanel, 1, wxEXPAND, 0);

    SetSizerAndFit(sizer_main);
    Layout();

    init_timer();
    Bind(wxEVT_TIMER, &CalibrationPanel::on_timer, this);
}

void CalibrationPanel::init_tabpanel() {
    //m_side_tools = new SideTools(this, wxID_ANY);
    wxBoxSizer* sizer_side_tools = new wxBoxSizer(wxVERTICAL);
    //sizer_side_tools->Add(m_side_tools, 1, wxEXPAND, 0);

    m_tabpanel = new Tabbook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, sizer_side_tools, wxNB_LEFT | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME);
    m_tabpanel->SetBackgroundColour(*wxWHITE);

    m_pa_panel = new PressureAdvanceWizard(m_tabpanel);
    m_tabpanel->AddPage(m_pa_panel, _L("Pressure Adavance"), "", true);

    m_flow_panel = new FlowRateWizard(m_tabpanel);
    m_tabpanel->AddPage(m_flow_panel, _L("Flow Rate"), "", false);

    m_volumetric_panel = new MaxVolumetricSpeedWizard(m_tabpanel);
    m_tabpanel->AddPage(m_volumetric_panel, _L("Max Volumetric Speed"), "", false);

    m_temp_panel = new TemperatureWizard(m_tabpanel);
    m_tabpanel->AddPage(m_temp_panel, _L("Temperature"), "", false);

    for (int i = 0; i < 4; i++)
        m_tabpanel->SetPageImage(i, "");

    m_tabpanel->Bind(wxEVT_BOOKCTRL_PAGE_CHANGED, [this](wxBookCtrlEvent&) {
        wxCommandEvent e (EVT_CALIBRATION_TAB_CHANGED);
        e.SetEventObject(m_tabpanel->GetCurrentPage());
        wxPostEvent(m_tabpanel->GetCurrentPage(), e);
        }, m_tabpanel->GetId());
}

void CalibrationPanel::init_timer()
{
    m_refresh_timer = new wxTimer();
    m_refresh_timer->SetOwner(this);
    m_refresh_timer->Start(REFRESH_INTERVAL);
    wxPostEvent(this, wxTimerEvent());
}

void CalibrationPanel::on_timer(wxTimerEvent& event) {
    update_all();
}

void CalibrationPanel::update_all() {
    if (m_pa_panel && m_pa_panel->IsShown()) {
        m_pa_panel->update_printer_selections();
        m_pa_panel->update_print_progress();
    }
    if (m_flow_panel && m_flow_panel->IsShown()) {
        m_flow_panel->update_printer_selections();
        m_flow_panel->update_print_progress();
    }
    if (m_volumetric_panel && m_volumetric_panel->IsShown()) {
        m_volumetric_panel->update_printer_selections();
        m_volumetric_panel->update_print_progress();
    }
    if (m_temp_panel && m_temp_panel->IsShown()) {
        m_temp_panel->update_printer_selections();
        m_temp_panel->update_print_progress();
    }
}

bool CalibrationPanel::Show(bool show) {
    if (show) {
        m_refresh_timer->Stop();
        m_refresh_timer->SetOwner(this);
        m_refresh_timer->Start(REFRESH_INTERVAL);
        wxPostEvent(this, wxTimerEvent());
    }
    else {
        m_refresh_timer->Stop();
    }
    return wxPanel::Show(show);
}

CalibrationPanel::~CalibrationPanel() {
    if (m_refresh_timer)
        m_refresh_timer->Stop();
    delete m_refresh_timer;
}

}}