#ifndef _WIPE_TOWER_DIALOG_H_
#define _WIPE_TOWER_DIALOG_H_

#include "GUI_Utils.hpp"

#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/msgdlg.h>

#include "RammingChart.hpp"
class Button;
class Label;


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
    // BBS
    WipingPanel(wxWindow* parent, const std::vector<float>& matrix, const std::vector<float>& extruders, const std::vector<std::string>& extruder_colours, Button* calc_button,
        int extra_flush_volume, float flush_multiplier);
    std::vector<float> read_matrix_values();
    void create_panels(wxWindow* parent, const int num);
    void calc_flushing_volumes();
    void msw_rescale();

    float get_flush_multiplier()
    {
        if (m_flush_multiplier_ebox == nullptr)
            return 1.f;

        return std::atof(m_flush_multiplier_ebox->GetValue().c_str());
    }

private:
    int calc_flushing_volume(const wxColour& from, const wxColour& to);
    void update_warning_texts();

    std::vector<std::vector<wxTextCtrl*>> edit_boxes;
    std::vector<wxColour> m_colours;
    unsigned int m_number_of_extruders  = 0;
    wxPanel* header_line_panel = nullptr;
    wxBoxSizer* m_sizer        = nullptr;
    Label* m_tip_message_label = nullptr;

    std::vector<wxButton *> icon_list1;
    std::vector<wxButton *> icon_list2;

    const int m_min_flush_volume;
    const int m_max_flush_volume;

    wxTextCtrl* m_flush_multiplier_ebox = nullptr;
    wxStaticText* m_min_flush_label = nullptr;

    std::vector<float> m_matrix;
};





class WipingDialog : public Slic3r::GUI::DPIDialog
{
public:
    WipingDialog(wxWindow* parent, const std::vector<float>& matrix, const std::vector<float>& extruders, const std::vector<std::string>& extruder_colours,
        int extra_flush_volume, float flush_multiplier);
    std::vector<float> get_matrix() const    { return m_output_matrix; }
    wxBoxSizer* create_btn_sizer(long flags);

    float get_flush_multiplier()
    {
        if (m_panel_wiping == nullptr)
            return 1.f;

        return m_panel_wiping->get_flush_multiplier();
    }

    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    WipingPanel*  m_panel_wiping  = nullptr;
    std::vector<float> m_output_matrix;
    std::unordered_map<int, Button *> m_button_list;
};

#endif  // _WIPE_TOWER_DIALOG_H_