#ifndef ORCASLICER_SPOOLMANDIALOG_HPP
#define ORCASLICER_SPOOLMANDIALOG_HPP
#include "GUI_Utils.hpp"
#include "OptionsGroup.hpp"
#include "Widgets/DialogButtons.hpp"
#include "Widgets/Label.hpp"

namespace Slic3r::GUI {
class SpoolInfoWidget : public wxPanel
{
public:
    SpoolInfoWidget(wxWindow* parent, const Preset* preset);

    void rescale();

private:
    wxStaticBitmap* m_spool_bitmap;
    Label*          m_preset_name_label;
    Label*          m_remaining_weight_label;
    const Preset*   m_preset;
};

class SpoolmanDialog : DPIDialog
{
public:
    SpoolmanDialog(wxWindow* parent);
    void build_options_group() const;
    void build_spool_info();
    void save_spoolman_settings();
    void OnRefresh(wxCommandEvent& e);
    void OnOK(wxCommandEvent& e);

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;

    bool           m_dirty_settings{false};
    OptionsGroup*  m_optgroup;
    wxBoxSizer*    m_spoolman_info_sizer;
    Label*         m_spoolman_error_label;
    wxGridSizer*   m_info_widgets_sizer;
    DialogButtons* m_buttons;
};
} // namespace Slic3r::GUI

#endif // ORCASLICER_SPOOLMANDIALOG_HPP