#ifndef slic3r_AMSMaterialsSetting_hpp_
#define slic3r_AMSMaterialsSetting_hpp_

#include "libslic3r/Preset.hpp"
#include "wxExtensions.hpp"
#include "GUI_Utils.hpp"
#include "DeviceManager.hpp"
#include "wx/clrpicker.h"
#include "Widgets/RadioBox.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/TextInput.hpp"

#define AMS_MATERIALS_SETTING_DEF_COLOUR wxColour(255, 255, 255)
#define AMS_MATERIALS_SETTING_GREY900 wxColour(38, 46, 48)
#define AMS_MATERIALS_SETTING_GREY800 wxColour(50, 58, 61)
#define AMS_MATERIALS_SETTING_GREY700 wxColour(107, 107, 107)
#define AMS_MATERIALS_SETTING_GREY300 wxColour(174,174,174)
#define AMS_MATERIALS_SETTING_GREY200 wxColour(248, 248, 248)
#define AMS_MATERIALS_SETTING_BODY_WIDTH FromDIP(380)
#define AMS_MATERIALS_SETTING_LABEL_WIDTH FromDIP(80)
#define AMS_MATERIALS_SETTING_COMBOX_WIDTH wxSize(FromDIP(250), FromDIP(30))
#define AMS_MATERIALS_SETTING_BUTTON_SIZE wxSize(FromDIP(90), FromDIP(24))
#define AMS_MATERIALS_SETTING_INPUT_SIZE wxSize(FromDIP(90), FromDIP(24))

namespace Slic3r { namespace GUI {

class AMSMaterialsSetting : public DPIDialog
{
public:
    AMSMaterialsSetting(wxWindow *parent, wxWindowID id);
    ~AMSMaterialsSetting();
    void create();

	void paintEvent(wxPaintEvent &evt);
    void input_min_finish();
    void input_max_finish();
    void update();
    void enable_confirm_button(bool en);
    bool Show(bool show) override;
    void Popup(wxString filament = wxEmptyString, wxString sn = wxEmptyString, wxString temp_min = wxEmptyString, wxString temp_max = wxEmptyString);

	void post_select_event();

    void set_color(wxColour color);

    MachineObject *obj{nullptr};
    int            ams_id { 0 };        /* 0 ~ 3 */
    int            tray_id { 0 };       /* 0 ~ 3 */

    std::string    ams_filament_id;

    bool           m_is_third;
    wxString       m_brand_filament;
    wxString       m_brand_sn;
    wxString       m_brand_tmp;
    wxColour       m_brand_colour;
    std::string    m_filament_type;

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;
    void on_select_filament(wxCommandEvent& evt);
    void on_select_ok(wxCommandEvent &event);
    void on_select_close(wxCommandEvent &event);
    void on_clr_picker(wxCommandEvent &event);

protected:
    StateColor          m_btn_bg_green;
    StateColor          m_btn_bg_gray;
    wxPanel *           m_panel_SN;
    wxStaticText *      m_sn_number;
    wxStaticText *      warning_text;
    //wxPanel *           m_panel_body;
    wxStaticText *      m_title_filament;
    wxStaticText *      m_title_colour;
    wxStaticText *      m_title_temperature;
    wxStaticText *      m_label_other;
    TextInput *         m_input_nozzle_min;
    TextInput*          m_input_nozzle_max;
    Button *            m_button_confirm;
    wxStaticText*       m_tip_readonly;
    Button *            m_button_close;
    Button *          m_clr_picker;
    wxColourData *      m_clrData;
#ifdef __APPLE__
    wxComboBox *m_comboBox_filament_mac;
#else
    ComboBox *m_comboBox_filament;
#endif
    TextInput*          m_readonly_filament;
};

}} // namespace Slic3r::GUI

#endif
