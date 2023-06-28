#ifndef slic3r_EditCalibrationHistoryDialog_hpp_
#define slic3r_EditCalibrationHistoryDialog_hpp_

#include "GUI_Utils.hpp"

namespace Slic3r { namespace GUI {

class EditCalibrationHistoryDialog : public DPIDialog
{
public:
    EditCalibrationHistoryDialog(wxWindow* parent, wxString k, wxString n, wxString material_name, wxString nozzle_dia);
    ~EditCalibrationHistoryDialog();
    void on_dpi_changed(const wxRect& suggested_rect) override;
    float get_k_value();
    float get_n_value();
    wxString get_material_name_value();

protected:
	void create(const wxString& k, const wxString& n, const wxString& material_name, const wxString& nozzle_dia);
	virtual void on_save(wxCommandEvent& event);
	virtual void on_cancel(wxCommandEvent& event);

protected:
    float m_k_value;
    float m_n_value;
    std::string m_material_name;
};

}} // namespace Slic3r::GUI

#endif
