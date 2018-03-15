#ifndef _WIPE_TOWER_DIALOG_H_
#define _WIPE_TOWER_DIALOG_H_

#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/choicdlg.h>
#include <wx/notebook.h>
#include <wx/msgdlg.h>

#include "../../libslic3r/GCode/WipeTowerPrusaMM.hpp"
#include "RammingChart.hpp"


std::ostream& operator<<(std::ostream& str,Slic3r::WipeTowerParameters& par);

class RammingPanel : public wxPanel {
public:
    RammingPanel(wxWindow* parent);
    RammingPanel(wxWindow* parent,const std::string& data);
    std::string get_parameters();

private:
    Chart* m_chart = nullptr;
    wxSpinCtrl* m_widget_volume = nullptr;
    wxSpinCtrl* m_widget_ramming_line_width_multiplicator = nullptr;
    wxSpinCtrl* m_widget_ramming_step_multiplicator = nullptr;
    wxSpinCtrlDouble* m_widget_time = nullptr;
    int m_ramming_step_multiplicator;
    int m_ramming_line_width_multiplicator;
      
    void line_parameters_changed();
};


class RammingDialog : public wxDialog {
public:
    RammingDialog(wxWindow* parent,const std::string& parameters);    
    std::string get_parameters() { return m_output_data; }
private:
    RammingPanel* m_panel_ramming = nullptr;
    std::string m_output_data;
};




class WipingPanel : public wxPanel {
public:
    WipingPanel(wxWindow* parent,const Slic3r::WipeTowerParameters& p);
    void fill_parameters(Slic3r::WipeTowerParameters& p);
        
private:
    void fill_in_matrix();
        
    std::vector<wxSpinCtrl*> m_old;
    std::vector<wxSpinCtrl*> m_new;
    std::vector<std::vector<wxTextCtrl*>> edit_boxes;
    wxButton* m_widget_button=nullptr;    
};





class WipingDialog : public wxDialog {
public:
    WipingDialog(wxWindow* parent,const std::string& init_data);
    
    std::string GetValue() const { return m_output_data; }
    
    
private:
    std::string m_file_name="config_wipe_tower";
    WipingPanel*  m_panel_wiping  = nullptr;
    std::string m_output_data = "";
            
    std::string read_dialog_values() {
        Slic3r::WipeTowerParameters p;
        m_panel_wiping ->fill_parameters(p);
        //return p.to_string();
    }
};

#endif  // _WIPE_TOWER_DIALOG_H_