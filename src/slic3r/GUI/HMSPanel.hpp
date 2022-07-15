#ifndef slic3r_HMSPanel_hpp_
#define slic3r_HMSPanel_hpp_

#include <wx/panel.h>
#include <wx/textctrl.h>
#include <slic3r/GUI/Widgets/Button.hpp>
#include <slic3r/GUI/DeviceManager.hpp>
#include <slic3r/GUI/Widgets/ScrolledWindow.hpp>

namespace Slic3r {
namespace GUI {


class HMSPanel : public wxPanel
{
protected:
    wxScrolledWindow* m_scrolledWindow;
    wxBoxSizer* m_top_sizer;
    wxTextCtrl* m_hms_content;

public:
    HMSPanel(wxWindow *parent, wxWindowID id = wxID_ANY, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~HMSPanel();

    void msw_rescale() {}

    bool Show(bool show = true) override;

    void update(MachineObject *obj_);

    MachineObject *obj { nullptr };
};

}
}

#endif
