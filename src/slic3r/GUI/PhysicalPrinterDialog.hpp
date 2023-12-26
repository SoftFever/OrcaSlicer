#ifndef slic3r_PhysicalPrinterDialog_hpp_
#define slic3r_PhysicalPrinterDialog_hpp_

#include <vector>

#include <wx/gdicmn.h>

#include "libslic3r/Preset.hpp"
#include "GUI_Utils.hpp"
#include "Widgets/RoundedRectangle.hpp"

class wxTextCtrl;
class wxStaticText;
class ScalableButton;
class wxBoxSizer;

namespace Slic3r {

namespace GUI {

//------------------------------------------
//          PhysicalPrinterDialog
//------------------------------------------

class ConfigOptionsGroup;
class PhysicalPrinterDialog : public DPIDialog
{
    DynamicPrintConfig* m_config            { nullptr };
    ConfigOptionsGroup* m_optgroup          { nullptr };

    ScalableButton*     m_printhost_browse_btn              {nullptr};
    ScalableButton*     m_printhost_test_btn                {nullptr};
    ScalableButton*     m_printhost_cafile_browse_btn       {nullptr};
    ScalableButton*     m_printhost_client_cert_browse_btn  {nullptr};
    ScalableButton*     m_printhost_port_browse_btn         {nullptr};

    RoundedRectangle*   m_input_area                        {nullptr};
    wxStaticText*       m_valid_label                       {nullptr};
    wxTextCtrl*         m_input_ctrl                        {nullptr};
    wxButton*           btnOK                               {nullptr};

    void build_printhost_settings(ConfigOptionsGroup* optgroup);
    void OnOK(wxEvent& event);

public:
    PhysicalPrinterDialog(wxWindow* parent);
    ~PhysicalPrinterDialog();

    enum ValidationType
    {
        Valid,
        NoValid,
        Warning
    };
    PresetCollection* m_presets {nullptr};
    ValidationType  m_valid_type;
    std::string     m_preset_name;

    void        update(bool printer_change = false);
    void        update_host_type(bool printer_change);
    void        update_preset_input();
    void        update_printhost_buttons();
    void        update_printers();

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override {};
};


} // namespace GUI
} // namespace Slic3r

#endif
