#include "CalibrationPanel.hpp"
#include "I18N.hpp"

namespace Slic3r { namespace GUI {
CalibrationPanel::CalibrationPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
{
    SetBackgroundColour(*wxWHITE);

    //init_bitmaps();
    init_tabpanel();

    wxBoxSizer* sizer_main = new wxBoxSizer(wxVERTICAL);
    sizer_main->Add(m_tabpanel, 1, wxEXPAND, 0);

    SetSizerAndFit(sizer_main);
    Layout();
}

void CalibrationPanel::init_tabpanel() {
    //m_side_tools = new SideTools(this, wxID_ANY);
    wxBoxSizer* sizer_side_tools = new wxBoxSizer(wxVERTICAL);
    //sizer_side_tools->Add(m_side_tools, 1, wxEXPAND, 0);

    m_tabpanel = new Tabbook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, sizer_side_tools, wxNB_LEFT | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME);
    m_tabpanel->SetBackgroundColour(*wxWHITE);

    //m_pa_panel = new CalibrationWizard(m_tabpanel);
    //m_tabpanel->AddPage(m_pa_panel, _L("Pressure Adavance"), "", true);

    m_flow_panel = new FlowRateWizard(m_tabpanel);
    m_tabpanel->AddPage(m_flow_panel, _L("Flow Rate"), "", false);

    m_volumetric_panel = new MaxVolumetricSpeedWizard(m_tabpanel);
    m_tabpanel->AddPage(m_volumetric_panel, _L("Max Volumetric Speed"), "", false);

    m_temp_panel = new TemperatureWizard(m_tabpanel);
    m_tabpanel->AddPage(m_temp_panel, _L("Temperature"), "", true);

    //m_vfa_panel = new CalibrationWizard(m_tabpanel);
    //m_tabpanel->AddPage(m_vfa_panel, _L("VFA"), "", false);

    //m_tabpanel->Bind(wxEVT_BOOKCTRL_PAGE_CHANGED, [this](wxBookCtrlEvent& e) {
    //    CalibrationWizard* page = static_cast<CalibrationWizard*>(m_tabpanel->GetCurrentPage());
    //    if (page->get_frist_page()) {
    //        page->update_comboboxes();
    //    }
    //    }, m_tabpanel->GetId());
}

void CalibrationPanel::update_obj(MachineObject* obj) {
    if (obj) {
        if (m_pa_panel)
            m_pa_panel->update_obj(obj);
        if (m_flow_panel) {
            m_flow_panel->update_obj(obj);
            m_flow_panel->update_ams(obj);
        }
        if (m_volumetric_panel) {
            m_volumetric_panel->update_obj(obj);
            m_volumetric_panel->update_ams(obj);
        }
        if (m_temp_panel) {
            m_temp_panel->update_obj(obj);
            m_temp_panel->update_ams(obj);
            m_temp_panel->update_progress();
        }
        if (m_vfa_panel) {
            m_vfa_panel->update_obj(obj);
        }
    }
}

}}