#pragma once

#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>

namespace Slic3r {

class MachineObject;

class ThermalPreconditioningDialog : public wxDialog
{
public:
    ThermalPreconditioningDialog(wxWindow *parent, std::string dev_id, const wxString &remaining_time);
    ~ThermalPreconditioningDialog() = default;

    // Allow external updates of remaining time text
    void set_remaining_time_text(const wxString& text) { if (m_remaining_time_label) m_remaining_time_label->SetLabelText(text); }

    void update_thermal_remaining_time();

private:
    void create_ui();
    void on_ok_clicked(wxCommandEvent& event);
    void            on_timer(wxTimerEvent &event);

    std::string   m_dev_id;
    wxTimer         m_refresh_timer;
    wxStaticText* m_remaining_time_label;
    wxStaticText* m_explanation_label;
    wxButton* m_ok_button;
    wxStaticBitmap* m_title_bitmap;

    DECLARE_EVENT_TABLE()
};

} // namespace Slic3r