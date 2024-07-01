#ifndef slic3r_GUI_SelectMachine_hpp_
#define slic3r_GUI_SelectMachine_hpp_

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/collpane.h>
#include <wx/dataview.h>
#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/dataview.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/hyperlink.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/popupwin.h>
#include <wx/spinctrl.h>
#include <wx/artprov.h>
#include <wx/wrapsizer.h>
#include <wx/srchctrl.h>

#include "AmsMappingPopup.hpp"
#include "ReleaseNote.hpp"
#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "DeviceManager.hpp"
#include "Plater.hpp"
#include "BBLStatusBar.hpp"
#include "BBLStatusBarSend.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/ScrolledWindow.hpp"
#include "Widgets/PopupWindow.hpp"
#include <wx/simplebook.h>
#include <wx/hashmap.h>

#include "Jobs/Worker.hpp"

namespace Slic3r { namespace GUI {

enum PrinterState {
    OFFLINE,
    IDLE,
    BUSY,
    LOCK,
    IN_LAN
};

enum PrinterBindState {
    NONE,
    ALLOW_BIND,
    ALLOW_UNBIND
};

enum PrintFromType {
    FROM_NORMAL,
    FROM_SDCARD_VIEW,
};

static int get_brightness_value(wxImage image) {

    wxImage grayImage = image.ConvertToGreyscale();

    int width = grayImage.GetWidth();
    int height = grayImage.GetHeight();

    int totalLuminance = 0;
    unsigned char alpha;
    int num_none_transparent = 0;
    for (int y = 0; y < height; y += 2) {

        for (int x = 0; x < width; x += 2) {

            alpha = image.GetAlpha(x, y);
            if (alpha != 0) {
                wxColour pixelColor = grayImage.GetRed(x, y);
                totalLuminance += pixelColor.Red();
                num_none_transparent = num_none_transparent + 1;
            }
        }
    }
    if (totalLuminance <= 0 || num_none_transparent <= 0) {
        return 0;
    }
    return totalLuminance / num_none_transparent;
}

class Material
{
public:
    int             id;
    MaterialItem    *item;
};

WX_DECLARE_HASH_MAP(int, Material *, wxIntegerHash, wxIntegerEqual, MaterialHash);

// move to seperate file
class MachineListModel : public wxDataViewVirtualListModel
{
public:
    enum {
        Col_MachineName           = 0,
        Col_MachineSN             = 1,
        Col_MachineBind           = 2,
        Col_MachinePrintingStatus = 3,
        Col_MachineIPAddress      = 4,
        Col_MachineConnection     = 5,
        Col_MachineTaskName       = 6,
        Col_Max                   = 7
    };
    MachineListModel();

    virtual unsigned int GetColumnCount() const wxOVERRIDE { return Col_Max; }

    virtual wxString GetColumnType(unsigned int col) const wxOVERRIDE { return "string"; }

    virtual void GetValueByRow(wxVariant &variant, unsigned int row, unsigned int col) const wxOVERRIDE;
    virtual bool GetAttrByRow(unsigned int row, unsigned int col, wxDataViewItemAttr &attr) const wxOVERRIDE;
    virtual bool SetValueByRow(const wxVariant &variant, unsigned int row, unsigned int col) wxOVERRIDE;

    void display_machines(std::map<std::string, MachineObject *> list);
    void add_machine(MachineObject *obj, bool reset = true);
    int  find_row_by_sn(wxString sn);

private:
    wxArrayString m_values[Col_Max];

    wxArrayString m_nameColValues;
    wxArrayString m_snColValues;
    wxArrayString m_bindColValues;
    wxArrayString m_connectionColValues;
    wxArrayString m_printingStatusValues;
    wxArrayString m_ipAddressValues;
};

class MachineObjectPanel : public wxPanel
{
private:
    bool        m_is_my_devices {false};
    bool        m_show_edit{false};
    bool        m_show_bind{false};
    bool        m_hover {false};
    bool        m_is_macos_special_version{false};


    PrinterBindState   m_bind_state;
    PrinterState       m_state;

    ScalableBitmap m_unbind_img;
    ScalableBitmap m_edit_name_img;
    ScalableBitmap m_select_unbind_img;

    ScalableBitmap m_printer_status_offline;
    ScalableBitmap m_printer_status_busy;
    ScalableBitmap m_printer_status_idle;
    ScalableBitmap m_printer_status_lock;
    ScalableBitmap m_printer_in_lan;

    MachineObject *m_info;

protected:
    wxStaticBitmap *m_bitmap_info;
    wxStaticBitmap *m_bitmap_bind;

public:
    MachineObjectPanel(wxWindow *      parent,
                       wxWindowID      id    = wxID_ANY,
                       const wxPoint & pos   = wxDefaultPosition,
                       const wxSize &  size  = wxDefaultSize,
                       long            style = wxTAB_TRAVERSAL,
                       const wxString &name  = wxEmptyString);
    
    ~MachineObjectPanel();

    void show_bind_dialog();
    void set_printer_state(PrinterState state);
    void show_printer_bind(bool show, PrinterBindState state);
    void show_edit_printer_name(bool show);
    void update_machine_info(MachineObject *info, bool is_my_devices = false);
protected:
    void OnPaint(wxPaintEvent &event);
    void render(wxDC &dc);
    void doRender(wxDC &dc);
    void on_mouse_enter(wxMouseEvent &evt);
    void on_mouse_leave(wxMouseEvent &evt);
    void on_mouse_left_up(wxMouseEvent &evt);
};

#define SELECT_MACHINE_POPUP_SIZE wxSize(FromDIP(216), FromDIP(364))
#define SELECT_MACHINE_LIST_SIZE wxSize(FromDIP(212), FromDIP(360))  
#define SELECT_MACHINE_ITEM_SIZE wxSize(FromDIP(182), FromDIP(35))
#define SELECT_MACHINE_GREY900 wxColour(38, 46, 48)
#define SELECT_MACHINE_GREY600 wxColour(144,144,144)
#define SELECT_MACHINE_GREY400 wxColour(206, 206, 206)
#define SELECT_MACHINE_BRAND wxColour(0, 150, 136)
#define SELECT_MACHINE_REMIND wxColour(255,111,0)
#define SELECT_MACHINE_LIGHT_GREEN wxColour(219, 253, 231)

class MachinePanel
{
public:
    wxString mIndex;
    MachineObjectPanel *mPanel;
};

class PinCodePanel : public wxPanel
{
public:
    PinCodePanel(wxWindow* parent,
        wxWindowID      winid = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize);
    ~PinCodePanel() {};

    ScalableBitmap       m_bitmap;
    bool           m_hover{false};

    void OnPaint(wxPaintEvent& event);
    void render(wxDC& dc);
    void doRender(wxDC& dc);

    void on_mouse_enter(wxMouseEvent& evt);
    void on_mouse_leave(wxMouseEvent& evt);
    void on_mouse_left_up(wxMouseEvent& evt);
};


class ThumbnailPanel;

class SelectMachinePopup : public PopupWindow
{
public:
    SelectMachinePopup(wxWindow *parent);
    ~SelectMachinePopup();

    // PopupWindow virtual methods are all overridden to log them
    virtual void Popup(wxWindow *focus = NULL) wxOVERRIDE;
    virtual void OnDismiss() wxOVERRIDE;
    virtual bool ProcessLeftDown(wxMouseEvent &event) wxOVERRIDE;
    virtual bool Show(bool show = true) wxOVERRIDE;

    void update_machine_list(wxCommandEvent &event);
    void start_ssdp(bool on_off);
    bool was_dismiss() { return m_dismiss; }

private:
    int                               m_my_devices_count{0};
    int                               m_other_devices_count{0};
    PinCodePanel*                     m_panel_ping_code{nullptr};
    wxWindow*                         m_placeholder_panel{nullptr};
    wxHyperlinkCtrl*                  m_hyperlink{nullptr};
    Label*                            m_ping_code_text{nullptr};
    wxStaticBitmap*                   m_img_ping_code{nullptr};
    wxBoxSizer *                      m_sizer_body{nullptr};
    wxBoxSizer *                      m_sizer_my_devices{nullptr};
    wxBoxSizer *                      m_sizer_other_devices{nullptr};
    wxBoxSizer *                      m_sizer_search_bar{nullptr};
    wxSearchCtrl*                     m_search_bar{nullptr};
    wxScrolledWindow *                m_scrolledWindow{nullptr};
    wxWindow *                        m_panel_body{nullptr};
    wxTimer *                         m_refresh_timer{nullptr};
    std::vector<MachinePanel*>        m_user_list_machine_panel;
    std::vector<MachinePanel*>        m_other_list_machine_panel;
    boost::thread*                    get_print_info_thread{ nullptr };
    std::shared_ptr<int>              m_token = std::make_shared<int>(0);
    std::string                       m_print_info = "";
    bool                              m_dismiss { false };

    std::map<std::string, MachineObject*> m_bind_machine_list; 
    std::map<std::string, MachineObject*> m_free_machine_list;

private:
    void OnLeftUp(wxMouseEvent &event);
    void on_timer(wxTimerEvent &event);

	void      update_other_devices();
    void      update_user_devices();
    bool      search_for_printer(MachineObject* obj);
    void      on_dissmiss_win(wxCommandEvent &event);
    wxWindow *create_title_panel(wxString text);
};

#define SELECT_MACHINE_DIALOG_BUTTON_SIZE wxSize(FromDIP(68), FromDIP(23))
#define SELECT_MACHINE_DIALOG_SIMBOOK_SIZE wxSize(FromDIP(370), FromDIP(64))


enum PrintPageMode {
    PrintPageModePrepare = 0,
    PrintPageModeSending,
    PrintPageModeFinish
};

enum PrintDialogStatus {
    PrintStatusInit = 0,
    PrintStatusNoUserLogin,
    PrintStatusInvalidPrinter,
    PrintStatusConnectingServer,
    PrintStatusReading,
    PrintStatusReadingFinished,
    PrintStatusReadingTimeout,
    PrintStatusInUpgrading,
    PrintStatusNeedUpgradingAms,
    PrintStatusInSystemPrinting,
    PrintStatusInPrinting,
    PrintStatusDisableAms,
    PrintStatusAmsMappingSuccess,
    PrintStatusAmsMappingInvalid,
    PrintStatusAmsMappingU0Invalid,
    PrintStatusAmsMappingValid,
    PrintStatusAmsMappingByOrder,
    PrintStatusRefreshingMachineList,
    PrintStatusSending,
    PrintStatusSendingCanceled,
    PrintStatusLanModeNoSdcard,
    PrintStatusNoSdcard,
    PrintStatusTimelapseNoSdcard,
    PrintStatusNotOnTheSameLAN,
    PrintStatusNeedForceUpgrading,
    PrintStatusNeedConsistencyUpgrading,
    PrintStatusNotSupportedSendToSDCard,
    PrintStatusNotSupportedPrintAll,
    PrintStatusBlankPlate,
    PrintStatusUnsupportedPrinter,
    PrintStatusTimelapseWarning
};

std::string get_print_status_info(PrintDialogStatus status);

class SelectMachineDialog : public DPIDialog
{
private:
    int                                 m_current_filament_id{0};
    int                                 m_print_plate_idx{0};
    int                                 m_print_plate_total{0};
    int                                 m_timeout_count{0};
    int                                 m_print_error_code{0};
    bool                                m_is_in_sending_mode{ false };
    bool                                m_ams_mapping_res{ false };
    bool                                m_ams_mapping_valid{ false };
    bool                                m_need_adaptation_screen{ false };
    bool                                m_export_3mf_cancel{ false };
    bool                                m_is_canceled{ false };
    bool                                m_is_rename_mode{ false };
    PrintPageMode                       m_print_page_mode{PrintPageMode::PrintPageModePrepare};
    std::string                         m_print_error_msg;
    std::string                         m_print_error_extra;
    std::string                         m_printer_last_select;
    std::string                         m_print_info;
    wxString                            m_current_project_name;
    PrintDialogStatus                   m_print_status { PrintStatusInit };
    wxColour                            m_colour_def_color{wxColour(255, 255, 255)};
    wxColour                            m_colour_bold_color{wxColour(38, 46, 48)};
    StateColor                          m_btn_bg_enable;
    
    std::shared_ptr<int>                m_token = std::make_shared<int>(0);
    std::map<std::string, CheckBox *>   m_checkbox_list;
    //std::map<std::string, bool>         m_checkbox_state_list;
    std::vector<wxString>               m_bedtype_list;
    std::vector<MachineObject*>         m_list;
    std::vector<FilamentInfo>           m_filaments;
    std::vector<FilamentInfo>           m_ams_mapping_result;
    std::shared_ptr<BBLStatusBarSend>   m_status_bar;
    std::unique_ptr<Worker>             m_worker;

    Slic3r::DynamicPrintConfig          m_required_data_config;
    Slic3r::Model                       m_required_data_model; 
    Slic3r::PlateDataPtrs               m_required_data_plate_data_list;
    std::string                         m_required_data_file_name;
    std::string                         m_required_data_file_path;

protected:
    PrintFromType                       m_print_type{FROM_NORMAL};
    AmsMapingPopup                      m_mapping_popup{ nullptr };
    AmsMapingTipPopup                   m_mapping_tip_popup{ nullptr };
    AmsTutorialPopup                    m_mapping_tutorial_popup{ nullptr };
    MaterialHash                        m_materialList;
    Plater *                            m_plater{nullptr};
    wxWrapSizer*                        m_sizer_select{ nullptr };
    wxBoxSizer*                         m_sizer_thumbnail{ nullptr };
    wxGridSizer*                        m_sizer_material{ nullptr };
    wxBoxSizer*                         m_sizer_main{ nullptr };
    wxBoxSizer*                         m_sizer_scrollable_view{ nullptr };
    wxBoxSizer*                         m_sizer_scrollable_region{ nullptr };
    wxBoxSizer*                         rename_sizer_v{ nullptr };
    wxBoxSizer*                         rename_sizer_h{ nullptr };
    wxBoxSizer*                         m_sizer_backup{ nullptr };
    Button*                             m_button_refresh{ nullptr };
    Button*                             m_button_ensure{ nullptr };
    ScalableButton *                    m_rename_button{nullptr};
    ComboBox*                           m_comboBox_printer{ nullptr };
    wxStaticBitmap*                     m_staticbitmap{ nullptr };
    wxStaticBitmap*                     m_bitmap_last_plate{ nullptr };
    wxStaticBitmap*                     m_bitmap_next_plate{ nullptr };
    wxStaticBitmap*                     img_amsmapping_tip{nullptr};
    ThumbnailPanel*                     m_thumbnailPanel{ nullptr };
    wxWindow*                           select_bed{ nullptr };
    wxWindow*                           select_flow{ nullptr };
    wxWindow*                           select_timelapse{ nullptr };
    wxWindow*                           select_use_ams{ nullptr };
    wxPanel*                            m_panel_status{ nullptr };
    wxPanel*                            m_scrollable_region;
    wxPanel*                            m_rename_normal_panel{nullptr};
    wxPanel*                            m_line_schedule{nullptr};
    wxPanel*                            m_panel_sending{nullptr};
    wxPanel*                            m_panel_prepare{nullptr};
    wxPanel*                            m_panel_finish{nullptr};
    wxPanel*                            m_line_top{ nullptr };
    wxPanel*                            m_panel_image{ nullptr };
    wxPanel*                            m_line_materia{ nullptr };
    Label*                              m_st_txt_error_code{nullptr};
    Label*                              m_st_txt_error_desc{nullptr};
    Label*                              m_st_txt_extra_info{nullptr};
    Label*                              m_ams_backup_tip{nullptr};
    wxHyperlinkCtrl*                    m_link_network_state{ nullptr };
    wxSimplebook*                       m_rename_switch_panel{nullptr};
    wxSimplebook*                       m_simplebook{nullptr};
    wxStaticText*                       m_rename_text{nullptr};
    wxStaticText*                       m_stext_printer_title{nullptr};
    wxStaticText*                       m_stext_time{ nullptr };
    wxStaticText*                       m_stext_weight{ nullptr };
    wxStaticText*                       m_statictext_ams_msg{ nullptr };
    wxStaticText*                       m_statictext_printer_msg{ nullptr };
    wxStaticText*                       m_staticText_bed_title{ nullptr };
    wxStaticText*                       m_stext_sending{ nullptr };
    wxStaticText*                       m_statictext_finish{nullptr};
    TextInput*                          m_rename_input{nullptr};
    wxTimer*                            m_refresh_timer{ nullptr };
    wxScrolledWindow*                   m_scrollable_view;
    wxScrolledWindow*                   m_sw_print_failed_info{nullptr};
    wxHyperlinkCtrl*                    m_hyperlink{nullptr};
    ScalableBitmap *                    ams_editable{nullptr};
    ScalableBitmap *                    ams_editable_light{nullptr};
    wxStaticBitmap *                    timeimg{nullptr};
    ScalableBitmap *                    print_time{nullptr};
    wxStaticBitmap *                    weightimg{nullptr};
    ScalableBitmap *                    print_weight{nullptr};
    ScalableBitmap *                    enable_ams_mapping{nullptr};
    wxStaticBitmap *                    img_use_ams_tip{nullptr};
    wxStaticBitmap *                    img_ams_backup{nullptr};
    ScalableBitmap *                    enable_ams{nullptr};
    ThumbnailData                       m_cur_input_thumbnail_data;
    ThumbnailData                       m_cur_no_light_thumbnail_data;
    ThumbnailData                       m_preview_thumbnail_data;//when ams map change
    std::vector<wxColour>               m_preview_colors_in_thumbnail;
    std::vector<wxColour>               m_cur_colors_in_thumbnail;
    std::vector<bool>                   m_edge_pixels;

public:
    SelectMachineDialog(Plater *plater = nullptr);
    ~SelectMachineDialog();

    void init_bind();
    void init_timer();
    void check_focus(wxWindow* window);
    void show_print_failed_info(bool show, int code = 0, wxString description = wxEmptyString, wxString extra = wxEmptyString);
    void check_fcous_state(wxWindow* window);
    void popup_filament_backup();
    void update_select_layout(MachineObject *obj);
    void prepare_mode(bool refresh_button = true);
    void sending_mode();
    void finish_mode();
	void sync_ams_mapping_result(std::vector<FilamentInfo>& result);
    void prepare(int print_plate_idx);
    void show_status(PrintDialogStatus status, std::vector<wxString> params = std::vector<wxString>());
    void sys_color_changed();
    void reset_timeout();
    void update_user_printer();
    void reset_ams_material();
    void update_show_status();
    void update_ams_check(MachineObject* obj);
    void on_rename_click(wxCommandEvent& event);
    void on_rename_enter();
    void update_printer_combobox(wxCommandEvent& event);
    void on_cancel(wxCloseEvent& event);
    void show_errors(wxString& info);
    void on_ok_btn(wxCommandEvent& event);
    void Enable_Auto_Refill(bool enable);
    void connect_printer_mqtt();
    void on_send_print();
    void clear_ip_address_config(wxCommandEvent& e);
    void on_refresh(wxCommandEvent& event);
    void on_set_finish_mapping(wxCommandEvent& evt);
    void on_print_job_cancel(wxCommandEvent& evt);
    void set_default();
    void reset_and_sync_ams_list();
    void clone_thumbnail_data();
    void record_edge_pixels_data();
    wxColour adjust_color_for_render(const wxColour& color);
    void final_deal_edge_pixels_data(ThumbnailData& data);
    void updata_thumbnail_data_after_connected_printer();
    void unify_deal_thumbnail_data(ThumbnailData &input_data, ThumbnailData &no_light_data);
    void change_default_normal(int old_filament_id, wxColour temp_ams_color);
    void set_default_normal(const ThumbnailData&);
    void set_default_from_sdcard();
    void update_page_turn_state(bool show);
    void on_timer(wxTimerEvent& event);
    void on_selection_changed(wxCommandEvent& event);
    void update_flow_cali_check(MachineObject* obj);
    void Enable_Refresh_Button(bool en);
    void Enable_Send_Button(bool en);
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void update_user_machine_list();
    void update_lan_machine_list();
    void stripWhiteSpace(std::string& str);
    void update_ams_status_msg(wxString msg, bool is_warning = false);
    void update_priner_status_msg(wxString msg, bool is_warning = false);
    void update_print_status_msg(wxString msg, bool is_warning = false, bool is_printer = true);
    void update_print_error_info(int code, std::string msg, std::string extra);
    void set_flow_calibration_state(bool state, bool show_tips = true);
    bool is_show_timelapse();
    bool has_timelapse_warning();
    void update_timelapse_enable_status();
    bool is_same_printer_model();
    bool is_blocking_printing(MachineObject* obj_);
    bool is_same_nozzle_diameters(std::string& tag_nozzle_type, std::string& nozzle_diameter);
    bool is_same_nozzle_type(std::string& filament_type, std::string& tag_nozzle_type);
    bool has_tips(MachineObject* obj);
    bool is_timeout();
    int  update_print_required_data(Slic3r::DynamicPrintConfig config, Slic3r::Model model, Slic3r::PlateDataPtrs plate_data_list, std::string file_name, std::string file_path);
    void set_print_type(PrintFromType type) {m_print_type = type;};
    bool Show(bool show);
    bool do_ams_mapping(MachineObject* obj_);
    bool get_ams_mapping_result(std::string& mapping_array_str, std::string& ams_mapping_info);

    PrintFromType get_print_type() {return m_print_type;};
    wxString    format_steel_name(std::string name);
    wxString    format_text(wxString &m_msg);
    wxWindow*   create_ams_checkbox(wxString title, wxWindow* parent, wxString tooltip);
    wxWindow*   create_item_checkbox(wxString title, wxWindow* parent, wxString tooltip, std::string param);
    wxImage *   LoadImageFromBlob(const unsigned char *data, int size);
    PrintDialogStatus  get_status() { return m_print_status; }
    std::vector<std::string> sort_string(std::vector<std::string> strArray);
};

wxDECLARE_EVENT(EVT_FINISHED_UPDATE_MACHINE_LIST, wxCommandEvent);
wxDECLARE_EVENT(EVT_REQUEST_BIND_LIST, wxCommandEvent);
wxDECLARE_EVENT(EVT_WILL_DISMISS_MACHINE_LIST, wxCommandEvent);
wxDECLARE_EVENT(EVT_UPDATE_WINDOWS_POSITION, wxCommandEvent);
wxDECLARE_EVENT(EVT_DISSMISS_MACHINE_LIST, wxCommandEvent);
wxDECLARE_EVENT(EVT_CONNECT_LAN_PRINT, wxCommandEvent);
wxDECLARE_EVENT(EVT_EDIT_PRINT_NAME, wxCommandEvent);
wxDECLARE_EVENT(EVT_UNBIND_MACHINE, wxCommandEvent);
wxDECLARE_EVENT(EVT_BIND_MACHINE, wxCommandEvent);

class EditDevNameDialog : public DPIDialog
{
public:
    EditDevNameDialog(Plater *plater = nullptr);
    ~EditDevNameDialog();

    void set_machine_obj(MachineObject *obj);
    void on_dpi_changed(const wxRect &suggested_rect) override;
    void on_edit_name(wxCommandEvent &e);

    Button*             m_button_confirm{nullptr};
    TextInput*          m_textCtr{nullptr};
    wxStaticText*       m_static_valid{nullptr};
    MachineObject*      m_info{nullptr};
};


class ThumbnailPanel : public wxPanel
{
public:
    wxBitmap       m_bitmap;
    wxStaticBitmap *m_staticbitmap{nullptr};

    ThumbnailPanel(wxWindow *      parent,
                   wxWindowID      winid = wxID_ANY,
                   const wxPoint & pos   = wxDefaultPosition,
                   const wxSize &  size  = wxDefaultSize);
    ~ThumbnailPanel();

    void OnPaint(wxPaintEvent &event);
    void PaintBackground(wxDC &dc);
    void OnEraseBackground(wxEraseEvent &event);
    void set_thumbnail(wxImage &img);
    void render(wxDC &dc);
private:
    ScalableBitmap m_background_bitmap;
    wxBitmap bitmap_with_background;
    int m_brightness_value{ -1 };
};

}} // namespace Slic3r::GUI

#endif
