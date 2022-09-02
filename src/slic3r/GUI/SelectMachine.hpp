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

#include "AmsMappingPopup.hpp"
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
#include <wx/simplebook.h>
#include <wx/hashmap.h>

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

#define SELECT_MACHINE_POPUP_SIZE wxSize(FromDIP(218), FromDIP(364))
#define SELECT_MACHINE_LIST_SIZE wxSize(FromDIP(214), FromDIP(360))  
#define SELECT_MACHINE_ITEM_SIZE wxSize(FromDIP(180), FromDIP(35))
#define SELECT_MACHINE_GREY900 wxColour(38, 46, 48)
#define SELECT_MACHINE_GREY600 wxColour(144,144,144)
#define SELECT_MACHINE_GREY400 wxColour(206, 206, 206)
#define SELECT_MACHINE_BRAND wxColour(0, 174, 66)
#define SELECT_MACHINE_REMIND wxColour(255,111,0)
#define SELECT_MACHINE_LIGHT_GREEN wxColour(219, 253, 231)

class MachinePanel
{
public:
    wxString mIndex;
    MachineObjectPanel *mPanel;
};


class ThumbnailPanel;

class SelectMachinePopup : public wxPopupTransientWindow
{
public:
    SelectMachinePopup(wxWindow *parent);
    ~SelectMachinePopup();

    // wxPopupTransientWindow virtual methods are all overridden to log them
    virtual void Popup(wxWindow *focus = NULL) wxOVERRIDE;
    virtual void OnDismiss() wxOVERRIDE;
    virtual bool ProcessLeftDown(wxMouseEvent &event) wxOVERRIDE;
    virtual bool Show(bool show = true) wxOVERRIDE;

    void update_machine_list(wxCommandEvent &event);
    void start_ssdp(bool on_off);
    bool was_dismiss() { return m_dismiss; }

private:
    wxBoxSizer *                      m_sizer_body{nullptr};
    wxBoxSizer *                      m_sizer_my_devices{nullptr};
    wxBoxSizer *                      m_sizer_other_devices{nullptr};
    wxScrolledWindow *                m_scrolledWindow{nullptr};
    wxWindow *                        m_panel_body{nullptr};
    wxTimer *                         m_refresh_timer{nullptr};
    std::vector<MachinePanel*>        m_user_list_machine_panel;
    std::vector<MachinePanel*>        m_other_list_machine_panel;
    boost::thread*                    get_print_info_thread{ nullptr };
    std::string                       m_print_info;
    bool                              m_dismiss { false };

    std::map<std::string, MachineObject*> m_bind_machine_list; 
    std::map<std::string, MachineObject*> m_free_machine_list;

private:
    void OnLeftUp(wxMouseEvent &event);
    void on_timer(wxTimerEvent &event);

	void      update_other_devices();
    void      update_user_devices();
    void      on_dissmiss_win(wxCommandEvent &event);
    wxWindow *create_title_panel(wxString text);
};

#define SELECT_MACHINE_DIALOG_BUTTON_SIZE wxSize(FromDIP(68), FromDIP(24))
#define SELECT_MACHINE_DIALOG_SIMBOOK_SIZE wxSize(FromDIP(350), FromDIP(70))


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
    PrintStatusAmsMappingSuccess,
    PrintStatusAmsMappingInvalid,
    PrintStatusAmsMappingU0Invalid,
    PrintStatusAmsMappingValid,
    PrintStatusAmsMappingByOrder,
    PrintStatusRefreshingMachineList,
    PrintStatusSending,
    PrintStatusSendingCanceled,
    PrintStatusNoSdcard
};

class SelectMachineDialog : public DPIDialog
{
private:
    void init_model();
    void init_bind();
    void init_timer();

    int m_print_plate_idx;

    std::string m_printer_last_select;
    PrintDialogStatus m_print_status { PrintStatusInit };

    std::vector<wxString>               m_bedtype_list;
    std::map<std::string, ::CheckBox *> m_checkbox_list;

    wxColour m_colour_def_color{wxColour(255, 255, 255)};
    wxColour m_colour_bold_color{wxColour(38, 46, 48)};

protected:
    MaterialHash  m_materialList;
    std::vector<FilamentInfo> m_filaments;
    std::vector<FilamentInfo> m_ams_mapping_result;     /* ams mapping data  */
    bool m_ams_mapping_res { false };                   /* ams mapping result */
    bool ams_mapping_valid { false };
    Plater *      m_plater{nullptr};
    wxPanel *     m_line_top{nullptr};
    wxPanel *     m_panel_image{nullptr};
    wxStaticText *m_stext_time{nullptr};
    wxStaticText *m_stext_weight{nullptr};
    wxPanel *     m_line_materia{nullptr};
    wxStaticText *m_stext_printer_title{nullptr};

    wxStaticText *m_statictext_ams_msg{nullptr};
    wxStaticText * m_statictext_printer_msg{nullptr};
    wxStaticBitmap* m_staticbitmap {nullptr};
    ThumbnailPanel *m_thumbnailPanel {nullptr};

    ::ComboBox *  m_comboBox_printer{nullptr};
    ::ComboBox *  m_comboBox_bed{nullptr};
    wxStaticText *m_staticText_bed_title{nullptr};
    wxPanel *     m_line_schedule{nullptr};
    wxPanel *     m_panel_sending{nullptr};
    wxStaticText *m_stext_sending{nullptr};
    wxPanel *     m_panel_prepare{nullptr};
    Button *      m_button_refresh{nullptr};
    Button *      m_button_ensure{nullptr};
    wxPanel *     m_panel_finish{nullptr};
    wxSimplebook *m_simplebook{nullptr};
    wxStaticText *m_statictext_finish{nullptr};

    StateColor btn_bg_enable;
    int        m_current_filament_id;
    bool       m_is_in_sending_mode { false };

    wxGridSizer *m_sizer_select;
    wxBoxSizer * sizer_thumbnail;
    wxGridSizer *m_sizer_material;
    wxBoxSizer * m_sizer_main;
    wxBoxSizer * m_sizer_bottom;

    bool              m_need_adaptation_screen {false};
    wxScrolledWindow *   m_scrollable_view;
    wxBoxSizer *      m_sizer_scrollable_view; 

    wxPanel* m_scrollable_region;
    wxBoxSizer* m_sizer_scrollable_region; 

    wxWindow *select_bed{nullptr};
    wxWindow *select_flow{nullptr};


    void stripWhiteSpace(std::string& str);
    wxString format_text(wxString &m_msg);
    void        update_ams_status_msg(wxString msg, bool is_warning = false);
    void update_priner_status_msg(wxString msg, bool is_warning = false);
    void update_print_status_msg(wxString msg, bool is_warning = false, bool is_printer = true);

public:
    SelectMachineDialog(Plater *plater = nullptr);
    ~SelectMachineDialog();

    wxWindow *create_item_checkbox(wxString title, wxWindow *parent, wxString tooltip, std::string param);
    void      update_select_layout(MachineObject *obj);
    void      prepare_mode();
    void      sending_mode();
    void      finish_mode();

	void      sync_ams_mapping_result(std::vector<FilamentInfo>& result);
    bool      do_ams_mapping(MachineObject *obj_);
    bool      get_ams_mapping_result(std::string &mapping_array_str);
    void      prepare(int print_plate_idx);
    void      show_status(PrintDialogStatus status, std::vector<wxString> params = std::vector<wxString>());
    PrintDialogStatus  get_status() { return m_print_status; }

    bool Show(bool show);

    /* model */
    wxObjectDataPtr<MachineListModel> machine_model;
    std::shared_ptr<BBLStatusBarSend> m_status_bar;
    bool                              m_export_3mf_cancel{false};
    bool                              m_is_canceled { false };

protected:
    std::vector<MachineObject *> m_list;
    wxDataViewCtrl *             m_dataViewListCtrl_machines{nullptr};
    wxStaticText *               m_staticText_left{nullptr};
    wxHyperlinkCtrl *            m_hyperlink_add_machine{nullptr};
    wxGauge *                    m_gauge_job_progress{nullptr};
    wxPanel *                    m_panel_status{nullptr};
    wxButton *                   m_button_cancel{nullptr};
    AmsMapingPopup               m_mapping_popup{nullptr};

    std::string                  m_print_info;
    int                          timeout_count = 0;
    bool                         is_timeout();
    void                         reset_timeout();
    void                         update_user_printer();
    void                         reset_ams_material();
    void                         update_show_status();

    wxTimer *m_refresh_timer { nullptr };

    std::shared_ptr<PrintJob> m_print_job;

    // Virtual event handlers, overide them in your derived class
    void                     update_printer_combobox(wxCommandEvent &event);
    void                     on_cancel(wxCloseEvent &event);
    void                     on_ok(wxCommandEvent &event);
    void                     on_refresh(wxCommandEvent &event);
    void                     on_set_finish_mapping(wxCommandEvent &evt);
    void                     on_print_job_cancel(wxCommandEvent &evt);
    void                     set_default();
    void                     on_timer(wxTimerEvent &event);
    void                     on_selection_changed(wxCommandEvent &event);
    void                     Enable_Refresh_Button(bool en);
    void                     Enable_Send_Button(bool en);
    void                     on_dpi_changed(const wxRect &suggested_rect) override;
    void                     update_user_machine_list();
    wxImage *                LoadImageFromBlob(const unsigned char *data, int size);
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

    MachineObject *m_info {nullptr};
    wxStaticText* m_static_valid {nullptr};
    ::TextInput* m_textCtr   {nullptr};
    Button* m_button_confirm {nullptr};
};


class ThumbnailPanel : public wxPanel
{
public:
    wxBitmap *      m_bitmap{nullptr};
    wxStaticBitmap *m_staticbitmap{nullptr};

    ThumbnailPanel(wxWindow *      parent,
                   wxWindowID      winid = wxID_ANY,
                   const wxPoint & pos   = wxDefaultPosition,
                   const wxSize &  size  = wxDefaultSize);
    void OnPaint(wxPaintEvent &event);
    void PaintBackground(wxDC &dc);
    void OnEraseBackground(wxEraseEvent &event);
    void set_thumbnail(wxImage img);
    ~ThumbnailPanel();
};

}} // namespace Slic3r::GUI

#endif
