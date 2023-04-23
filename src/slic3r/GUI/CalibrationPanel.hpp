#ifndef slic3r_GUI_CalibrationPanel_hpp_
#define slic3r_GUI_CalibrationPanel_hpp_

#include "CalibrationWizard.hpp"
#include "Tabbook.hpp"
//#include "Widgets/SideTools.hpp"

namespace Slic3r { namespace GUI {
class CalibrationPanel : public wxPanel
{
public:
    CalibrationPanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~CalibrationPanel() {};
    Tabbook* get_tabpanel() { return m_tabpanel; };
    void update_obj(MachineObject* obj);

protected:
    void init_bitmaps();
    void init_tabpanel();

    //void show_wizard();
    //CalibrationWizard* get_current_wizard();

private:
    Tabbook*    m_tabpanel{ nullptr };
    //SideTools* m_side_tools{ nullptr };

    CalibrationWizard* m_pa_panel{ nullptr };
    CalibrationWizard* m_flow_panel{ nullptr };
    CalibrationWizard* m_volumetric_panel{ nullptr };
    TemperatureWizard* m_temp_panel{ nullptr };
    CalibrationWizard* m_vfa_panel{ nullptr };
};
}} // namespace Slic3r::GUI

#endif