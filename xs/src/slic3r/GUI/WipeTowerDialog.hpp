#ifndef _WIPE_TOWER_DIALOG_H_
#define _WIPE_TOWER_DIALOG_H_

#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/msgdlg.h>

#include "RammingChart.hpp"


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
    WipingPanel(wxWindow* parent, const std::vector<float>& matrix, const std::vector<float>& extruders, wxButton* widget_button);
    std::vector<float> read_matrix_values();
    std::vector<float> read_extruders_values();
    void toggle_advanced(bool user_action = false);
	void format_sizer(wxSizer* sizer, wxPanel* page, wxGridSizer* grid_sizer, const wxString& info, const wxString& table_title, int table_lshift=0);
        
private:
    void fill_in_matrix();
    bool advanced_matches_simple();
        
    std::vector<wxSpinCtrl*> m_old;
    std::vector<wxSpinCtrl*> m_new;
    std::vector<std::vector<wxTextCtrl*>> edit_boxes;
    unsigned int m_number_of_extruders  = 0;
    bool m_advanced                     = false;
	wxPanel*	m_page_simple = nullptr;
	wxPanel*	m_page_advanced = nullptr;
    wxBoxSizer*	m_sizer = nullptr;
    wxBoxSizer* m_sizer_simple = nullptr;
    wxBoxSizer* m_sizer_advanced = nullptr;
    wxGridSizer* m_gridsizer_advanced = nullptr;
    wxButton* m_widget_button     = nullptr;
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