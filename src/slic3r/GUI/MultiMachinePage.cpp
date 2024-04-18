#include "MultiMachinePage.hpp"

namespace Slic3r {
namespace GUI {

    
MultiMachinePage::MultiMachinePage(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
{
    init_tabpanel();
    m_main_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_main_sizer->Add(m_tabpanel, 1, wxEXPAND | wxLEFT, 0);
    SetSizerAndFit(m_main_sizer);
    Layout();
    Fit();
    
    wxGetApp().UpdateDarkUIWin(this);

    init_timer();
    Bind(wxEVT_TIMER, &MultiMachinePage::on_timer, this);
}

MultiMachinePage::~MultiMachinePage()
{
    if (m_refresh_timer)
        m_refresh_timer->Stop();
    delete m_refresh_timer;
}

void MultiMachinePage::jump_to_send_page()
{
    m_tabpanel->SetSelection(1);
}

void MultiMachinePage::on_sys_color_changed()
{
}

void MultiMachinePage::msw_rescale()
{
}

bool MultiMachinePage::Show(bool show)
{
    if (show) {
        m_refresh_timer->Stop();
        m_refresh_timer->SetOwner(this);
        m_refresh_timer->Start(2000);
        wxPostEvent(this, wxTimerEvent());
    }
    else {
        m_refresh_timer->Stop();
    }

    auto page = m_tabpanel->GetCurrentPage();
    if (page)
        page->Show(show);
    return wxPanel::Show(show);
}

void MultiMachinePage::init_tabpanel()
{
    auto m_side_tools = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(220), FromDIP(18)));
    wxBoxSizer* sizer_side_tools = new wxBoxSizer(wxHORIZONTAL);
    sizer_side_tools->Add(m_side_tools, 1, wxEXPAND, 0);
    m_tabpanel = new Tabbook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, sizer_side_tools, wxNB_LEFT | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME);
    m_tabpanel->SetBackgroundColour(wxColour("#FEFFFF"));
    m_tabpanel->Bind(wxEVT_BOOKCTRL_PAGE_CHANGED, [this](wxBookCtrlEvent& e) {; });

    m_local_task_manager = new LocalTaskManagerPage(m_tabpanel);
    m_cloud_task_manager = new CloudTaskManagerPage(m_tabpanel);
    m_machine_manager = new MultiMachineManagerPage(m_tabpanel);

    m_tabpanel->AddPage(m_machine_manager, _L("Device"), "", true);
    m_tabpanel->AddPage(m_local_task_manager, _L("Task Sending"), "", false);
    m_tabpanel->AddPage(m_cloud_task_manager, _L("Task Sent"), "", false);
}


void MultiMachinePage::init_timer()
{
    m_refresh_timer = new wxTimer();
    //m_refresh_timer->SetOwner(this);
    //m_refresh_timer->Start(8000);
    //wxPostEvent(this, wxTimerEvent());
}

void MultiMachinePage::on_timer(wxTimerEvent& event)
{
    m_local_task_manager->update_page();
    m_cloud_task_manager->update_page();
    m_machine_manager->update_page();
}

void MultiMachinePage::clear_page()
{
    m_local_task_manager->refresh_user_device(true);
    m_cloud_task_manager->refresh_user_device(true);
    m_machine_manager->refresh_user_device(true);
}

} // namespace GUI
} // namespace Slic3r
