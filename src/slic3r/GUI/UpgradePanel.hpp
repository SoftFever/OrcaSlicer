#ifndef slic3r_UpgradePanel_hpp_
#define slic3r_UpgradePanel_hpp_

#include <wx/panel.h>
#include <slic3r/GUI/Widgets/Button.hpp>
#include "Widgets/ProgressBar.hpp"
#include <slic3r/GUI/DeviceManager.hpp>
#include <slic3r/GUI/Widgets/ScrolledWindow.hpp>
#include "ReleaseNote.hpp"


namespace Slic3r {
namespace GUI {

class AmsPanel : public wxPanel
{
public:
    wxStaticText *m_staticText_ams;
    wxStaticText *m_staticText_ams_sn_val;
    wxStaticText *m_staticText_ams_ver_val;
    wxStaticBitmap *m_ams_new_version_img;

    AmsPanel(wxWindow *      parent,
                     wxWindowID      id    = wxID_ANY,
                     const wxPoint & pos   = wxDefaultPosition,
                     const wxSize &  size  = wxDefaultSize,
                     long            style = wxTAB_TRAVERSAL,
                     const wxString &name  = wxEmptyString);
    ~AmsPanel();
};


WX_DEFINE_ARRAY(AmsPanel*, AmsPanelHash);


class MachineInfoPanel : public wxPanel
{
protected:
    wxPanel *       m_panel_caption;
    wxStaticBitmap *m_upgrade_status_img;
    wxStaticText *  m_caption_text;
    wxStaticBitmap *m_printer_img;
    wxStaticText *  m_staticText_model_id;
    wxStaticText *  m_staticText_model_id_val;
    wxStaticText *  m_staticText_sn;
    wxStaticText *  m_staticText_sn_val;
    wxStaticBitmap *m_ota_new_version_img;
    wxStaticText *  m_staticText_ver;
    wxStaticText *  m_staticText_ver_val;
    wxStaticLine *  m_staticline;
    wxStaticBitmap *m_ams_img;
    AmsPanel*       m_ahb_panel;
   

    wxGridSizer *   m_ams_info_sizer;

    /* ams info */
    bool           m_last_ams_show = true;
    wxBoxSizer*    m_ams_sizer;

    /* upgrade widgets */
    wxBoxSizer*     m_upgrading_sizer;
    wxStaticText *  m_staticText_upgrading_info;
    ProgressBar *   m_upgrade_progress;
    wxStaticText *  m_staticText_upgrading_percent;
    wxStaticBitmap *m_upgrade_retry_img;
    wxStaticText *  m_staticText_release_note;
    Button *        m_button_upgrade_firmware;

    wxPanel* create_caption_panel(wxWindow *parent);
    AmsPanelHash     m_amspanel_list;

    wxBitmap m_img_monitor_ams;
    wxBitmap m_img_printer;
    wxBitmap upgrade_gray_icon;
    wxBitmap upgrade_green_icon;
    wxBitmap upgrade_yellow_icon;
    int last_status = -1;
    std::string last_status_str = "";

    SecondaryCheckDialog* confirm_dlg = nullptr;

    void upgrade_firmware_internal();
    void on_show_release_note(wxMouseEvent &event);

public:
    MachineInfoPanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL, const wxString& name = wxEmptyString);
    ~MachineInfoPanel();

    void init_bitmaps();

    Button* get_btn() {
        return m_button_upgrade_firmware;
    }

    void msw_rescale();
    void update(MachineObject *obj);
    void update_version_text(MachineObject *obj);
    void update_ams(MachineObject *obj);
    void show_status(int status, std::string upgrade_status_str = "");
    void show_ams(bool show = false, bool force_update = false);

    void on_upgrade_firmware(wxCommandEvent &event);
    void on_consisitency_upgrade_firmware(wxCommandEvent &event);

    MachineObject *m_obj{nullptr};
    FirmwareInfo  m_ota_info;
    FirmwareInfo  m_ams_info;

    bool is_upgrading = false;

    enum PanelType {
        ptUndef,
        ptPushPanel,
        ptOtaPanel,
        ptAmsPanel,
    }panel_type;
};

enum UpgradeMode {
    umPushUpgrading,
    umSelectOtaVerUpgrading,
    umSelectAmsVerUpgrading,
};
static UpgradeMode upgrade_mode;

class UpgradePanel : public wxPanel
{
protected:
    wxScrolledWindow* m_scrolledWindow;
    wxBoxSizer* m_machine_list_sizer;
    MachineInfoPanel *m_push_upgrade_panel{nullptr};

    //enable_select_firmware only in debug mode
    bool enable_select_firmware = false;
    bool m_need_update = false;
    //hint of force upgrade or consistency upgrade
    int last_forced_hint_status = -1;
    int last_consistency_hint_status = -1;
    bool m_show_forced_hint = true;
    bool m_show_consistency_hint = true;
    SecondaryCheckDialog* force_dlg{ nullptr };
    SecondaryCheckDialog* consistency_dlg{ nullptr };

public:
    UpgradePanel(wxWindow *parent, wxWindowID id = wxID_ANY, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~UpgradePanel();
    void clean_push_upgrade_panel();
    void msw_rescale();
    bool Show(bool show = true) override;

    void refresh_version_and_firmware(MachineObject* obj);
    void update(MachineObject *obj);

    MachineObject *m_obj { nullptr };
};

}
}

#endif
