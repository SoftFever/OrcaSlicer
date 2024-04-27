#ifndef slic3r_MultiMachinePage_hpp_
#define slic3r_MultiMachinePage_hpp_

#include "libslic3r/libslic3r.h"
#include "GUI_App.hpp"
#include "GUI_Utils.hpp"
#include "MultiTaskManagerPage.hpp"
#include "MultiMachineManagerPage.hpp"
#include "Tabbook.hpp"

#include "wx/button.h"

namespace Slic3r { 
namespace GUI {
    
class MultiMachinePage : public wxPanel
{
private:
    wxTimer*                    m_refresh_timer      = nullptr;
    wxSizer*                    m_main_sizer{ nullptr };
    LocalTaskManagerPage*       m_local_task_manager{ nullptr };
    CloudTaskManagerPage*       m_cloud_task_manager{ nullptr };
    MultiMachineManagerPage*    m_machine_manager{ nullptr };
    Tabbook*                    m_tabpanel{ nullptr };

public:
    MultiMachinePage(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~MultiMachinePage();

    void jump_to_send_page();

    void on_sys_color_changed();
    void msw_rescale();
    bool Show(bool show);

    void init_tabpanel();
    void init_timer();
    void on_timer(wxTimerEvent& event);

    void clear_page();
};
} // namespace GUI
} // namespace Slic3r

#endif
