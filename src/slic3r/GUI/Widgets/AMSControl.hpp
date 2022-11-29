#ifndef slic3r_GUI_AMXCONTROL_hpp_
#define slic3r_GUI_AMXCONTROL_hpp_

#include "../wxExtensions.hpp"
#include "StaticBox.hpp"
#include "StepCtrl.hpp"
#include "Button.hpp"
#include "../DeviceManager.hpp"
#include "slic3r/GUI/Event.hpp"
#include <wx/simplebook.h>
#include <wx/hyperlink.h>
#include <wx/animate.h>
#include <wx/dynarray.h>

#define AMS_CONTROL_BRAND_COLOUR wxColour(0, 174, 66)
#define AMS_CONTROL_GRAY700 wxColour(107, 107, 107)
#define AMS_CONTROL_GRAY800 wxColour(50, 58, 61)
#define AMS_CONTROL_GRAY500 wxColour(172, 172, 172)
#define AMS_CONTROL_DISABLE_COLOUR wxColour(206, 206, 206)
#define AMS_CONTROL_DISABLE_TEXT_COLOUR wxColour(144, 144, 144)
#define AMS_CONTROL_WHITE_COLOUR wxColour(255, 255, 255)
#define AMS_CONTROL_BLACK_COLOUR wxColour(0, 0, 0)
#define AMS_CONTROL_DEF_BLOCK_BK_COLOUR wxColour(238, 238, 238)
#define AMS_CONTROL_DEF_LIB_BK_COLOUR wxColour(248, 248, 248)
#define AMS_EXTRUDER_DEF_COLOUR wxColour(234, 234, 234)
#define AMS_CONTROL_MAX_COUNT 4
#define AMS_CONTRO_CALIBRATION_BUTTON_SIZE wxSize(FromDIP(150), FromDIP(28))

// enum AMSRoadMode{
//    AMS_ROAD_MODE_LEFT,
//    AMS_ROAD_MODE_LEFT_RIGHT,
//    AMS_ROAD_MODE_END,
//};

namespace Slic3r { namespace GUI {

enum class AMSRoadMode : int {
    AMS_ROAD_MODE_LEFT,
    AMS_ROAD_MODE_LEFT_RIGHT,
    AMS_ROAD_MODE_END,
    AMS_ROAD_MODE_END_ONLY,
    AMS_ROAD_MODE_NONE,
};

enum class AMSPassRoadMode : int {
    AMS_ROAD_MODE_NONE,
    AMS_ROAD_MODE_LEFT,
    AMS_ROAD_MODE_LEFT_RIGHT,
    AMS_ROAD_MODE_END_TOP,
    AMS_ROAD_MODE_END_RIGHT,
    AMS_ROAD_MODE_END_BOTTOM,
};

enum class AMSAction : int {
    AMS_ACTION_NONE,
    AMS_ACTION_LOAD,
    AMS_ACTION_UNLOAD,
    AMS_ACTION_PRINTING,
    AMS_ACTION_NORMAL,
};

enum class AMSPassRoadSTEP : int {
    AMS_ROAD_STEP_NONE,
    AMS_ROAD_STEP_1, // lib -> extrusion
    AMS_ROAD_STEP_2, // extrusion->buffer
    AMS_ROAD_STEP_3, // extrusion

    AMS_ROAD_STEP_COMBO_LOAD_STEP1,
    AMS_ROAD_STEP_COMBO_LOAD_STEP2,
    AMS_ROAD_STEP_COMBO_LOAD_STEP3,
};

enum class AMSPassRoadType : int {
    AMS_ROAD_TYPE_NONE,
    AMS_ROAD_TYPE_LOAD,
    AMS_ROAD_TYPE_UNLOAD,
};

enum class AMSCanType : int {
    AMS_CAN_TYPE_NONE,
    AMS_CAN_TYPE_BRAND,
    AMS_CAN_TYPE_THIRDBRAND,
    AMS_CAN_TYPE_EMPTY,
};

enum FilamentStep {
    STEP_IDLE,
    STEP_HEAT_NOZZLE,
    STEP_CUT_FILAMENT,
    STEP_PULL_CURR_FILAMENT,
    STEP_PUSH_NEW_FILAMENT,
    STEP_PURGE_OLD_FILAMENT,
    STEP_COUNT,
};

#define AMS_ITEM_CUBE_SIZE wxSize(FromDIP(14), FromDIP(14))
#define AMS_ITEM_SIZE wxSize(FromDIP(82), FromDIP(27))
#define AMS_ITEM_HUMIDITY_SIZE wxSize(FromDIP(120), FromDIP(27))
#define AMS_CAN_LIB_SIZE wxSize(FromDIP(58), FromDIP(80))
#define AMS_CAN_ROAD_SIZE wxSize(FromDIP(66), FromDIP(60))
#define AMS_CAN_ITEM_HEIGHT_SIZE FromDIP(27)
#define AMS_CANS_SIZE wxSize(FromDIP(284), FromDIP(186))
#define AMS_CANS_WINDOW_SIZE wxSize(FromDIP(264), FromDIP(186))
#define AMS_STEP_SIZE wxSize(FromDIP(172), FromDIP(180))
#define AMS_REFRESH_SIZE wxSize(FromDIP(30), FromDIP(30))
#define AMS_EXTRUDER_SIZE wxSize(FromDIP(66), FromDIP(55))
#define AMS_EXTRUDER_BITMAP_SIZE wxSize(FromDIP(36), FromDIP(55))

struct Caninfo
{
    std::string     can_id;
    wxString        material_name;
    wxColour        material_colour = {*wxWHITE};
    AMSCanType      material_state;
    int             material_remain = 100;
};

struct AMSinfo
{
public:
    std::string             ams_id;
    std::vector<Caninfo>    cans;
    std::string             current_can_id;
    AMSPassRoadSTEP         current_step;
    AMSAction               current_action;
    int                     curreent_filamentstep;
    int                     ams_humidity = 0;

    bool parse_ams_info(Ams *ams, bool remain_flag = false);
};

/*************************************************
Description:AMSrefresh
**************************************************/
#define AMS_REFRESH_PLAY_LOADING_TIMER 100
class AMSrefresh : public wxWindow
{
public:
    AMSrefresh();
    AMSrefresh(wxWindow *parent, wxWindowID id, wxString number, Caninfo info, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize);
    AMSrefresh(wxWindow *parent, wxWindowID id, int number, Caninfo info, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize);
    ~AMSrefresh();
    void    PlayLoading();
    void    StopLoading();
    void    create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size);
    void    on_timer(wxTimerEvent &event);
    void    OnEnterWindow(wxMouseEvent &evt);
    void    OnLeaveWindow(wxMouseEvent &evt);
    void    OnClick(wxMouseEvent &evt);
    void    post_event(wxCommandEvent &&event);
    void    paintEvent(wxPaintEvent &evt);
    void    Update(Caninfo info);
    void    msw_rescale();
    Caninfo m_info;

protected:
    wxTimer *m_playing_timer= {nullptr};
    int      m_rotation_angle = 0;

    bool             m_play_loading = {false};
    bool             m_selected      = {false};

    ScalableBitmap   m_bitmap_normal;
    ScalableBitmap   m_bitmap_selected;
    ScalableBitmap   m_bitmap_ams_rfid_0;
    ScalableBitmap   m_bitmap_ams_rfid_1;
    ScalableBitmap   m_bitmap_ams_rfid_2;
    ScalableBitmap   m_bitmap_ams_rfid_3;
    ScalableBitmap   m_bitmap_ams_rfid_4;
    ScalableBitmap   m_bitmap_ams_rfid_5;
    ScalableBitmap   m_bitmap_ams_rfid_6;
    ScalableBitmap   m_bitmap_ams_rfid_7;
    std::vector<ScalableBitmap> m_rfid_bitmap_list;

    wxString         m_text;
    wxBoxSizer *     m_size_body;
    virtual void     DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO);
};

/*************************************************
Description:AMSextruder
**************************************************/
class AMSextruderImage: public wxWindow
{
public:
    void TurnOn(wxColour col);
    void TurnOff();
    void msw_rescale();
    void paintEvent(wxPaintEvent &evt);

	void            render(wxDC &dc);
    bool            m_turn_on = {false};
    wxColour        m_colour;
    ScalableBitmap  m_ams_extruder;
    void            doRender(wxDC &dc);
    AMSextruderImage(wxWindow *parent, wxWindowID id, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize);
    ~AMSextruderImage();
};


class AMSextruder : public wxWindow
{
public:
    void TurnOn(wxColour col);
    void TurnOff();
    void create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size);
    void msw_rescale();

    wxBoxSizer *    m_bitmap_sizer{nullptr};
    wxPanel *       m_bitmap_panel{nullptr};
    AMSextruderImage *m_amsSextruder{nullptr};
    ScalableBitmap        monitor_ams_extruder;
    AMSextruder(wxWindow *parent, wxWindowID id, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize);
    ~AMSextruder();
};

/*************************************************
Description:AMSLib
**************************************************/
class AMSLib : public wxWindow
{
public:
    AMSLib(wxWindow *parent, wxWindowID id, Caninfo info, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize);
    void create(wxWindow *parent, wxWindowID id = wxID_ANY, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize);
public:
    int          m_can_index;
    void         Update(Caninfo info, bool refresh = true);
    void         UnableSelected() { m_unable_selected = true; };
    void         EableSelected() { m_unable_selected = false; };
    wxColour     GetLibColour();
    void         OnSelected();
    void         UnSelected();
    virtual bool Enable(bool enable = true);
    void         post_event(wxCommandEvent &&event);
    Caninfo      m_info;

protected:
    wxStaticBitmap *m_edit_bitmp       = {nullptr};
    wxStaticBitmap *m_edit_bitmp_light = {nullptr};
    ScalableBitmap  m_bitmap_editable;
    ScalableBitmap  m_bitmap_editable_light;
    ScalableBitmap  m_bitmap_readonly;
    ScalableBitmap  m_bitmap_readonly_light;
    bool            m_unable_selected = {false};
    bool            m_enable          = {false};
    bool            m_selected        = {false};
    bool            m_hover           = {false};

    double   m_radius = {4};
    wxColour m_border_color;
    wxColour m_road_def_color;
    wxColour m_lib_color;

    void on_enter_window(wxMouseEvent &evt);
    void on_leave_window(wxMouseEvent &evt);
    void on_left_down(wxMouseEvent &evt);
    void paintEvent(wxPaintEvent &evt);
    void render(wxDC &dc);
    void doRender(wxDC &dc);
};

/*************************************************
Description:AMSRoad
**************************************************/
class AMSRoad : public wxWindow
{
public:
    AMSRoad();
    AMSRoad(wxWindow *parent, wxWindowID id, Caninfo info, int canindex, int maxcan, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize);
    void create(wxWindow *parent, wxWindowID id = wxID_ANY, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize);

public:
    AMSinfo                      m_amsinfo;
    Caninfo                      m_info;
    int                          m_canindex       = {0};
    AMSRoadMode                  m_rode_mode      = {AMSRoadMode::AMS_ROAD_MODE_LEFT_RIGHT};
    std::vector<AMSPassRoadMode> m_pass_rode_mode = {AMSPassRoadMode::AMS_ROAD_MODE_NONE};
    bool                         m_selected       = {false};
    int                          m_passroad_width = {6};
    double                       m_radius         = {4};
    wxColour                     m_road_def_color;
    wxColour                     m_road_color;
    void                         Update(AMSinfo amsinfo, Caninfo info, int canindex, int maxcan);

    ScalableBitmap ams_humidity_0;
    ScalableBitmap ams_humidity_1;
    ScalableBitmap ams_humidity_2;
    ScalableBitmap ams_humidity_3;
    ScalableBitmap ams_humidity_4;
    bool     m_show_humidity = { false };
    int      m_humidity = { 0 };

    void SetPassRoadColour(wxColour col);
    void SetMode(AMSRoadMode mode);
    void OnPassRoad(std::vector<AMSPassRoadMode> prord_list);
    void UpdatePassRoad(int tag_index, AMSPassRoadType type, AMSPassRoadSTEP step);

    void paintEvent(wxPaintEvent &evt);
    void render(wxDC &dc);
    void doRender(wxDC &dc);
};

/*************************************************
Description:AMSItem
**************************************************/

class AMSItem : public wxWindow
{
public:
    AMSItem();
    AMSItem(wxWindow *parent, wxWindowID id, AMSinfo amsinfo, const wxSize cube_size = wxSize(14, 14), const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize);

    bool m_open = {false};
    void Open();
    void Close();

    void         Update(AMSinfo amsinfo);
    void         create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size);
    void         OnEnterWindow(wxMouseEvent &evt);
    void         OnLeaveWindow(wxMouseEvent &evt);
    void         OnSelected();
    void         UnSelected();
    void         ShowHumidity();
    void         HideHumidity();
    void         SetHumidity(int humidity);
    virtual bool Enable(bool enable = true);

    AMSinfo      m_amsinfo;
    ScalableBitmap ams_humidity_0;
    ScalableBitmap ams_humidity_1;
    ScalableBitmap ams_humidity_2;
    ScalableBitmap ams_humidity_3;
    ScalableBitmap ams_humidity_4;

protected:
    wxSize   m_cube_size;
    wxColour m_background_colour = {AMS_CONTROL_DEF_BLOCK_BK_COLOUR};
    int      m_padding           = {6};
    int      m_space             = {5};
    bool     m_hover             = {false};
    bool     m_selected          = {false};
    bool     m_show_humidity     = {false};
    int      m_humidity          = {0};

    void         paintEvent(wxPaintEvent &evt);
    void         render(wxDC &dc);
    void         doRender(wxDC &dc);
    virtual void DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO);
};

/*************************************************
Description:AmsCans
**************************************************/
class Canrefreshs
{
public:
    wxString    canID;
    AMSrefresh *canrefresh;
};

class CanLibs
{
public:
    wxString canID;
    AMSLib * canLib;
};

class CanRoads
{
public:
    wxString canID;
    AMSRoad *canRoad;
};

WX_DEFINE_ARRAY(Canrefreshs *, CanrefreshsHash);
WX_DEFINE_ARRAY(CanLibs *, CanLibsHash);
WX_DEFINE_ARRAY(CanRoads *, CansRoadsHash);

class AmsCans : public wxWindow
{
public:
    AmsCans();
    AmsCans(wxWindow *parent, wxWindowID id, AMSinfo info, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize);

    void     Update(AMSinfo info);
    void     create(wxWindow *parent, wxWindowID id, AMSinfo info, const wxPoint &pos, const wxSize &size);
    void     AddCan(Caninfo caninfo, int canindex, int maxcan);
    void     SelectCan(std::string canid);
    void     SetAmsStep(wxString canid, AMSPassRoadType type, AMSPassRoadSTEP step);
    //wxColour GetCanColour(wxString canid);
    void     PlayRridLoading(wxString canid);
    void     StopRridLoading(wxString canid);
    void     msw_rescale();

    std::string GetCurrentCan();

public:
    std::string     m_canlib_id;
    int             m_canlib_selection = {-1};
    int             m_selection        = {0};
    int             m_can_count        = {0};
    CanLibsHash     m_can_lib_list;
    CansRoadsHash   m_can_road_list;
    CanrefreshsHash m_can_refresh_list;
    AMSinfo         m_info;
    wxBoxSizer *    sizer_can = {nullptr};
    AMSPassRoadSTEP m_step    = {AMSPassRoadSTEP ::AMS_ROAD_STEP_NONE};
};

/*************************************************
Description:AMSControl
**************************************************/
class AmsCansWindow
{
public:
    wxString amsIndex;
    AmsCans *amsCans;
};

class AmsItems
{
public:
    wxString amsIndex;
    AMSItem *amsItem;
};

class AMSextruders
{
public:
    wxString     amsIndex;
    AMSextruder *amsextruder;
};

WX_DEFINE_ARRAY(AmsCansWindow *, AmsCansHash);
WX_DEFINE_ARRAY(AmsItems *, AmsItemsHash);
WX_DEFINE_ARRAY(AMSextruders *, AMSextrudersHash);

class AMSControl : public wxSimplebook
{
public:
    AMSControl(wxWindow *parent, wxWindowID id = wxID_ANY, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize);

    void init_scaled_buttons();

protected:
    int m_ams_count = {0};

    std::map<std::string, int> m_ams_selection;
    std::vector<AMSinfo>       m_ams_info;

    std::string  m_current_ams;
    AmsItemsHash m_ams_item_list;
    AmsCansHash  m_ams_cans_list;

    AMSextruder *m_extruder = {nullptr};

    wxSimplebook *m_simplebook_right       = {nullptr};
    wxSimplebook *m_simplebook_calibration = {nullptr};
    wxSimplebook *m_simplebook_amsitems    = {nullptr};
    wxSimplebook *m_simplebook_ams         = {nullptr};
    wxSimplebook *m_simplebook_cans        = {nullptr};
    wxSimplebook *m_simplebook_bottom      = {nullptr};

    wxStaticText *m_tip_right_top            = {nullptr};
    wxStaticText *m_tip_load_info            = {nullptr};
    wxStaticText *m_text_calibration_percent = {nullptr};
    wxWindow *    m_none_ams_panel           = {nullptr};
    wxWindow *    m_panel_top                = {nullptr};
    wxWindow *    m_amswin                   = {nullptr}; 

    StaticBox * m_panel_can       = {nullptr};
    wxBoxSizer *m_sizer_top       = {nullptr};
    wxBoxSizer *m_sizer_cans      = {nullptr};
    wxBoxSizer *m_sizer_right_tip = {nullptr};

    ::StepIndicator *m_filament_load_step   = {nullptr};
    ::StepIndicator *m_filament_unload_step = {nullptr};

    Button *m_button_extruder_feed = {nullptr};
    Button *m_button_extruder_back = {nullptr};
    wxStaticBitmap* m_button_ams_setting   = {nullptr};
    ScalableBitmap m_button_ams_setting_normal;
    ScalableBitmap m_button_ams_setting_hover;
    ScalableBitmap m_button_ams_setting_press;
    Button *m_button_guide = {nullptr};
    Button *m_button_retry = {nullptr};

    wxHyperlinkCtrl *m_hyperlink = {nullptr};

public:
    std::string GetCurentAms();
    std::string GetCurrentCan(std::string amsid);
	wxColour GetCanColour(std::string amsid, std::string canid);

	void SetActionState(AMSAction action);
    void EnterNoneAMSMode();
    void ExitNoneAMSMode();

    void EnterCalibrationMode(bool read_to_calibration);
    void ExitcClibrationMode();

    void SetClibrationpercent(int percent);
    void SetClibrationLink(wxString link);

    void PlayRridLoading(wxString amsid, wxString canid);
    void StopRridLoading(wxString amsid, wxString canid);

    void SetFilamentStep(int item_idx, bool isload = true);
    void ShowFilamentTip(bool hasams = true);

    void SetHumidity(std::string amsid, int humidity);
    void UpdateStepCtrl();
    void CreateAms();
    void UpdateAms(std::vector<AMSinfo> info, bool keep_selection = true);
    void AddAms(AMSinfo info, bool refresh = true);
    void SetAmsStep(std::string ams_id, std::string canid, AMSPassRoadType type, AMSPassRoadSTEP step);
    void SwitchAms(std::string ams_id);

    void msw_rescale();
    void on_filament_load(wxCommandEvent &event);
    void on_filament_unload(wxCommandEvent &event);
    void on_ams_setting_click(wxMouseEvent &event);
    void on_clibration_again_click(wxMouseEvent &event);
    void on_clibration_cancel_click(wxMouseEvent &event);
    void Reset();

    void post_event(wxEvent &&event);

    virtual bool Enable(bool enable = true);

public:
    std::string m_current_senect;
};

wxDECLARE_EVENT(EVT_AMS_LOAD, SimpleEvent);
wxDECLARE_EVENT(EVT_AMS_UNLOAD, SimpleEvent);
wxDECLARE_EVENT(EVT_AMS_SETTINGS, SimpleEvent);
wxDECLARE_EVENT(EVT_AMS_REFRESH_RFID, wxCommandEvent);
wxDECLARE_EVENT(EVT_AMS_ON_SELECTED, wxCommandEvent);
wxDECLARE_EVENT(EVT_AMS_ON_FILAMENT_EDIT, wxCommandEvent);
wxDECLARE_EVENT(EVT_AMS_CLIBRATION_AGAIN, wxCommandEvent);
wxDECLARE_EVENT(EVT_AMS_CLIBRATION_CANCEL, wxCommandEvent);
wxDECLARE_EVENT(EVT_AMS_GUIDE_WIKI, wxCommandEvent);
wxDECLARE_EVENT(EVT_AMS_RETRY, wxCommandEvent);

}} // namespace Slic3r::GUI

#endif // !slic3r_GUI_amscontrol_hpp_
