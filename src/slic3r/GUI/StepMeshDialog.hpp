#ifndef _STEP_MESH_DIALOG_H_
#define _STEP_MESH_DIALOG_H_

#include "GUI_Utils.hpp"
#include <boost/filesystem.hpp>
#include <wx/sizer.h>
class Button;
namespace fs = boost::filesystem;
class StepMeshDialog : public Slic3r::GUI::DPIDialog
{
public:
    StepMeshDialog(wxWindow* parent, fs::path file);
    void on_dpi_changed(const wxRect& suggested_rect) override;
    inline double get_linear_defletion() {
        double value;
        if (m_linear_last.ToDouble(&value)) {
            return value;
        }else {
            return 0.003;
        }
    }
    inline double get_angle_defletion() {
        double value;
        if (m_angle_last.ToDouble(&value)) {
            return value;
        } else {
            return 0.5;
        }
    }
    long get_mesh_number();
private:
    fs::path m_file;
    Button* m_button_ok = nullptr;
    Button* m_button_cancel = nullptr;
    wxString m_linear_last = wxString::Format("%.3f", 0.003);
    wxString m_angle_last = wxString::Format("%.2f", 0.5);
    wxStaticText* mesh_face_number_text;
    bool validate_number_range(const wxString& value, double min, double max);
    void update_mesh_number_text();
};

#endif  // _STEP_MESH_DIALOG_H_