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
    WipingPanel(wxWindow* parent, const std::vector<float>& matrix, const std::vector<float>& extruders);
    std::vector<float> read_matrix_values();
    std::vector<float> read_extruders_values();
        
private:
    void fill_in_matrix();
    void toggle_advanced(bool user_button = false);
    bool advanced_matches_simple();
        
    std::vector<wxSpinCtrl*> m_old;
    std::vector<wxSpinCtrl*> m_new;
    std::vector<wxWindow*>   m_advanced_widgets;
    std::vector<wxWindow*>   m_notadvanced_widgets;
    std::vector<std::vector<wxTextCtrl*>> edit_boxes;
    wxButton* m_widget_button           = nullptr;
    unsigned int m_number_of_extruders  = 0;
    bool m_advanced                     = false;
};





class WipingDialog : public wxDialog {
public:
    WipingDialog(wxWindow* parent,const std::vector<float>& matrix, const std::vector<float>& extruders);
    std::vector<float> get_matrix() const    { return m_output_matrix; }
    std::vector<float> get_extruders() const { return m_output_extruders; }


private:
    WipingPanel*  m_panel_wiping  = nullptr;
    std::vector<float> m_output_matrix;
    std::vector<float> m_output_extruders;
};

#endif  // _WIPE_TOWER_DIALOG_H_