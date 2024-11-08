#ifndef _STEP_MESH_DIALOG_H_
#define _STEP_MESH_DIALOG_H_

#include <thread>
#include "GUI_App.hpp"
#include "GUI_Utils.hpp"
#include "libslic3r/Format/STEP.hpp"
#include "Widgets/Button.hpp"
class Button;

class StepMeshDialog : public Slic3r::GUI::DPIDialog
{
public:
    StepMeshDialog(wxWindow* parent, Slic3r::Step& file);
    void on_dpi_changed(const wxRect& suggested_rect) override;
    inline double get_linear_init() {
        return std::stod(Slic3r::GUI::wxGetApp().app_config->get("linear_defletion"));
    }
    inline double get_angle_init() {
        return std::stod(Slic3r::GUI::wxGetApp().app_config->get("angle_defletion"));
    }
    inline double get_linear_defletion() {
        double value;
        if (m_linear_last.ToDouble(&value)) {
            return value;
        }else {
            return get_linear_init();
        }
    }
    inline double get_angle_defletion() {
        double value;
        if (m_angle_last.ToDouble(&value)) {
            return value;
        } else {
            return get_angle_init();
        }
    }
private:
    Slic3r::Step& m_file;
    Button* m_button_ok = nullptr;
    Button* m_button_cancel = nullptr;
    wxCheckBox* m_checkbox = nullptr;
    wxString m_linear_last = wxString::Format("%.3f", get_linear_init());
    wxString m_angle_last = wxString::Format("%.2f", get_angle_init());
    wxStaticText* mesh_face_number_text;
    double m_last_linear;
    double m_last_angle;
    std::future<unsigned int> task;
    bool validate_number_range(const wxString& value, double min, double max);
    void update_mesh_number_text();
    void on_task_done(wxCommandEvent& event);
    void stop_task();
};

#endif  // _STEP_MESH_DIALOG_H_