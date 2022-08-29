#include "StatusPanel.hpp"

#include "I18N.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/StepCtrl.hpp"
#include "BitmapCache.hpp"
#include "GUI_App.hpp"

#include "slic3r/Utils/Http.hpp"
#include "libslic3r/Thread.hpp"


namespace Slic3r { namespace GUI {

#define TEMP_THRESHOLD_VAL 2

/* const strings */
static const wxString NA_STR         = _L("N/A");
static const wxString TEMP_BLANK_STR = wxString("_");
static const wxFont   SWITCH_FONT    = Label::Body_10;

/* const values */
static const int bed_temp_range[2]    = {20, 120};
static const int nozzle_temp_range[2] = {20, 300};

/* colors */
static const wxColour STATUS_PANEL_BG     = wxColour(238, 238, 238);
static const wxColour STATUS_TITLE_BG     = wxColour(248, 248, 248);
static const wxColour STATIC_BOX_LINE_COL = wxColour(238, 238, 238);

static const wxColour BUTTON_NORMAL1_COL = wxColour(238, 238, 238);
static const wxColour BUTTON_NORMAL2_COL = wxColour(206, 206, 206);
static const wxColour BUTTON_PRESS_COL   = wxColour(172, 172, 172);
static const wxColour BUTTON_HOVER_COL   = wxColour(0, 174, 66);

static const wxColour DISCONNECT_TEXT_COL = wxColour(172, 172, 172);
static const wxColour NORMAL_TEXT_COL     = wxColour(50, 58, 61);
static const wxColour NORMAL_FAN_TEXT_COL = wxColour(107, 107, 107);
static const wxColour WARNING_INFO_BG_COL = wxColour(255, 111, 0);
static const wxColour STAGE_TEXT_COL      = wxColour(0, 174, 66);

static const wxColour GROUP_STATIC_LINE_COL = wxColour(206, 206, 206);

/* font and foreground colors */
static const wxFont PAGE_TITLE_FONT  = Label::Body_14;
static const wxFont GROUP_TITLE_FONT = Label::sysFont(17);

static wxColour PAGE_TITLE_FONT_COL  = wxColour(107, 107, 107);
static wxColour GROUP_TITLE_FONT_COL = wxColour(172, 172, 172);
static wxColour TEXT_LIGHT_FONT_COL  = wxColour(107, 107, 107);

/* size */
#define PAGE_TITLE_HEIGHT FromDIP(36)
#define PAGE_TITLE_LEFT_MARGIN FromDIP(17)
#define GROUP_TITLE_LEFT_MARGIN FromDIP(15)
#define GROUP_TITLE_LINE_MARGIN FromDIP(11)
#define GROUP_TITLE_RIGHT_MARGIN FromDIP(15)

#define NORMAL_SPACING FromDIP(5)
#define PAGE_SPACING FromDIP(10)
#define PAGE_MIN_WIDTH FromDIP(574)
#define PROGRESSBAR_HEIGHT FromDIP(8)

#define SWITCH_BUTTON_SIZE (wxSize(FromDIP(40), -1))
#define TASK_THUMBNAIL_SIZE (wxSize(FromDIP(120), FromDIP(120)))
#define TASK_BUTTON_SIZE (wxSize(FromDIP(48), FromDIP(24)))
#define TASK_BUTTON_SIZE2 (wxSize(-1, FromDIP(24)))
#define Z_BUTTON_SIZE (wxSize(FromDIP(52), FromDIP(52)))
#define MISC_BUTTON_SIZE (wxSize(FromDIP(68), FromDIP(55)))
#define TEMP_CTRL_MIN_SIZE (wxSize(FromDIP(122), FromDIP(52)))
#define AXIS_MIN_SIZE (wxSize(FromDIP(220), FromDIP(220)))
#define EXTRUDER_IMAGE_SIZE (wxSize(FromDIP(48), FromDIP(76)))

StatusBasePanel::StatusBasePanel(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style, const wxString &name)
    : wxScrolledWindow(parent, id, pos, size, wxHSCROLL | wxVSCROLL)
{
    this->SetScrollRate(5, 5);

    init_bitmaps();

    this->SetBackgroundColour(wxColour(0xEE, 0xEE, 0xEE));

    wxBoxSizer *bSizer_status = new wxBoxSizer(wxVERTICAL);

    auto m_panel_separotor_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, PAGE_SPACING), wxTAB_TRAVERSAL);
    m_panel_separotor_top->SetBackgroundColour(STATUS_PANEL_BG);

    bSizer_status->Add(m_panel_separotor_top, 0, wxEXPAND | wxALL, 0);

    wxBoxSizer *bSizer_status_below = new wxBoxSizer(wxHORIZONTAL);

    auto m_panel_separotor_left = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_panel_separotor_left->SetBackgroundColour(STATUS_PANEL_BG);
    m_panel_separotor_left->SetMinSize(wxSize(PAGE_SPACING, -1));

    bSizer_status_below->Add(m_panel_separotor_left, 0, wxEXPAND | wxALL, 0);

    wxBoxSizer *bSizer_left = new wxBoxSizer(wxVERTICAL);

    auto m_monitoring_sizer = create_monitoring_page();
    bSizer_left->Add(m_monitoring_sizer, 1, wxEXPAND | wxALL, 0);

    auto m_panel_separotor1 = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_panel_separotor1->SetBackgroundColour(STATUS_PANEL_BG);
    m_panel_separotor1->SetMinSize(wxSize(-1, PAGE_SPACING));
    m_panel_separotor1->SetMaxSize(wxSize(-1, PAGE_SPACING));
    m_monitoring_sizer->Add(m_panel_separotor1, 0, wxEXPAND, 0);

    m_project_task_panel = new wxPanel(this);
    m_project_task_panel->SetBackgroundColour(*wxWHITE);

    auto m_project_task_sizer = create_project_task_page(m_project_task_panel);
    m_project_task_panel->SetSizer(m_project_task_sizer);
    m_project_task_panel->Layout();
    m_project_task_sizer->Fit(m_project_task_panel);
    m_monitoring_sizer->Add(m_project_task_panel, 0, wxALL | wxEXPAND , 0);

//    auto m_panel_separotor2 = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
//    m_panel_separotor2->SetBackgroundColour(STATUS_PANEL_BG);
//    m_panel_separotor2->SetMinSize(wxSize(-1, PAGE_SPACING));
//    bSizer_left->Add(m_panel_separotor2, 1, wxEXPAND, 0);

    bSizer_status_below->Add(bSizer_left, 1, wxALL | wxEXPAND, 0);

    auto m_panel_separator_middle = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL);
    m_panel_separator_middle->SetBackgroundColour(STATUS_PANEL_BG);
    m_panel_separator_middle->SetMinSize(wxSize(PAGE_SPACING, -1));

    bSizer_status_below->Add(m_panel_separator_middle, 0, wxEXPAND | wxALL, 0);

    m_machine_ctrl_panel = new wxPanel(this);
    m_machine_ctrl_panel->SetBackgroundColour(*wxWHITE);
    m_machine_ctrl_panel->SetDoubleBuffered(true);
    auto m_machine_control = create_machine_control_page(m_machine_ctrl_panel);
    m_machine_ctrl_panel->SetSizer(m_machine_control);
    m_machine_ctrl_panel->Layout();
    m_machine_control->Fit(m_machine_ctrl_panel);

    bSizer_status_below->Add(m_machine_ctrl_panel, 0, wxALL, 0);

    m_panel_separator_right = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(PAGE_SPACING, -1), wxTAB_TRAVERSAL);
    m_panel_separator_right->SetBackgroundColour(STATUS_PANEL_BG);

    bSizer_status_below->Add(m_panel_separator_right, 0, wxEXPAND | wxALL, 0);

    bSizer_status->Add(bSizer_status_below, 1, wxALL | wxEXPAND, 0);

    m_panel_separotor_bottom = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, PAGE_SPACING), wxTAB_TRAVERSAL);
    m_panel_separotor_bottom->SetBackgroundColour(STATUS_PANEL_BG);

    bSizer_status->Add(m_panel_separotor_bottom, 0, wxEXPAND | wxALL, 0);
    this->SetSizerAndFit(bSizer_status);
    this->Layout();
}

StatusBasePanel::~StatusBasePanel() {}

void StatusBasePanel::init_bitmaps()
{
    static Slic3r::GUI::BitmapCache cache;
    m_item_placeholder       = create_scaled_bitmap("monitor_placeholder", nullptr, 60);
    m_bitmap_item_prediction = create_scaled_bitmap("monitor_item_prediction", nullptr, 16);
    m_bitmap_item_cost       = create_scaled_bitmap("monitor_item_cost", nullptr, 16);
    m_bitmap_item_print      = create_scaled_bitmap("monitor_item_print", nullptr, 18);
    m_bitmap_axis_home       = ScalableBitmap(this, "monitor_axis_home", 32);
    m_bitmap_lamp_on         = ScalableBitmap(this, "monitor_lamp_on", 24);
    m_bitmap_lamp_off        = ScalableBitmap(this, "monitor_lamp_off", 24);
    m_bitmap_fan_on          = ScalableBitmap(this, "monitor_fan_on", 24);
    m_bitmap_fan_off         = ScalableBitmap(this, "monitor_fan_off", 24);
    m_bitmap_speed           = ScalableBitmap(this, "monitor_speed", 24);
    m_bitmap_speed_active    = ScalableBitmap(this, "monitor_speed_active", 24);
    m_bitmap_use_time        = ScalableBitmap(this, "print_info_time", 16);
    m_bitmap_use_weight      = ScalableBitmap(this, "print_info_weight", 16);
    m_thumbnail_placeholder  = ScalableBitmap(this, "monitor_placeholder", 120);
    m_thumbnail_sdcard       = ScalableBitmap(this, "monitor_sdcard_thumbnail", 120);
    //m_bitmap_camera          = create_scaled_bitmap("monitor_camera", nullptr, 18);
    m_bitmap_extruder        = *cache.load_png("monitor_extruder", FromDIP(28), FromDIP(70), false, false);
    m_bitmap_sdcard_state_on    = create_scaled_bitmap("sdcard_state_on", nullptr, 20);
    m_bitmap_sdcard_state_off    = create_scaled_bitmap("sdcard_state_off", nullptr, 20);
}

wxBoxSizer *StatusBasePanel::create_monitoring_page()
{
    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

    m_panel_monitoring_title = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, PAGE_TITLE_HEIGHT), wxTAB_TRAVERSAL);
    m_panel_monitoring_title->SetBackgroundColour(STATUS_TITLE_BG);

    wxBoxSizer *bSizer_monitoring_title;
    bSizer_monitoring_title = new wxBoxSizer(wxHORIZONTAL);

    m_staticText_monitoring = new wxStaticText(m_panel_monitoring_title, wxID_ANY, _L("Camera"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    m_staticText_monitoring->Wrap(-1);
    m_staticText_monitoring->SetFont(PAGE_TITLE_FONT);
    m_staticText_monitoring->SetForegroundColour(PAGE_TITLE_FONT_COL);
    bSizer_monitoring_title->Add(m_staticText_monitoring, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, PAGE_TITLE_LEFT_MARGIN);


    bSizer_monitoring_title->Add(FromDIP(13), 0, 0, 0);
    bSizer_monitoring_title->AddStretchSpacer();

    m_staticText_timelapse = new wxStaticText(m_panel_monitoring_title, wxID_ANY, _L("Timelapse"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_timelapse->Wrap(-1);
    m_staticText_timelapse->Hide();
    bSizer_monitoring_title->Add(m_staticText_timelapse, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));

    m_bmToggleBtn_timelapse = new SwitchButton(m_panel_monitoring_title);
    m_bmToggleBtn_timelapse->SetMinSize(SWITCH_BUTTON_SIZE);
    m_bmToggleBtn_timelapse->Hide();
    bSizer_monitoring_title->Add(m_bmToggleBtn_timelapse, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    
    //m_bitmap_camera_img = new wxStaticBitmap(m_panel_monitoring_title, wxID_ANY, m_bitmap_camera , wxDefaultPosition, wxSize(FromDIP(32), FromDIP(18)), 0);
    //m_bitmap_camera_img->SetMinSize(wxSize(FromDIP(32), FromDIP(18)));
    //bSizer_monitoring_title->Add(m_bitmap_camera_img, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));

    m_bitmap_sdcard_off_img = new wxStaticBitmap(m_panel_monitoring_title, wxID_ANY, m_bitmap_sdcard_state_off, wxDefaultPosition, wxSize(FromDIP(38), FromDIP(24)), 0);
    m_bitmap_sdcard_off_img->SetMinSize(wxSize(FromDIP(38), FromDIP(24)));
    m_bitmap_sdcard_on_img = new wxStaticBitmap(m_panel_monitoring_title, wxID_ANY, m_bitmap_sdcard_state_on, wxDefaultPosition, wxSize(FromDIP(38), FromDIP(24)), 0);
    m_bitmap_sdcard_on_img->SetMinSize(wxSize(FromDIP(38), FromDIP(24)));
    m_bitmap_sdcard_on_img->Hide();
   
    m_timelapse_button = new CameraItem(m_panel_monitoring_title, "timelapse_off_normal", "timelapse_on_normal", "timelapse_off_hover", "timelapse_on_hover");
    m_timelapse_button->SetMinSize(wxSize(38, 24));
    m_timelapse_button->SetBackgroundColour(STATUS_TITLE_BG);

    m_recording_button = new CameraItem(m_panel_monitoring_title, "recording_off_normal", "recording_on_normal", "recording_off_hover", "recording_on_hover");
    m_recording_button->SetMinSize(wxSize(38, 24));
    m_recording_button->SetBackgroundColour(STATUS_TITLE_BG);

    m_timelapse_button->SetToolTip(_L("Timelapse"));
    m_recording_button->SetToolTip(_L("Video"));

    bSizer_monitoring_title->Add(m_bitmap_sdcard_off_img, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    bSizer_monitoring_title->Add(m_bitmap_sdcard_on_img, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    bSizer_monitoring_title->Add(m_timelapse_button, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    bSizer_monitoring_title->Add(m_recording_button, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));

    bSizer_monitoring_title->Add(FromDIP(13), 0, 0);

    m_panel_monitoring_title->SetSizer(bSizer_monitoring_title);
    m_panel_monitoring_title->Layout();
    bSizer_monitoring_title->Fit(m_panel_monitoring_title);
    sizer->Add(m_panel_monitoring_title, 0, wxEXPAND | wxALL, 0);

//    media_ctrl_panel              = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
//    media_ctrl_panel->SetBackgroundColour(*wxBLACK);
//    wxBoxSizer *bSizer_monitoring = new wxBoxSizer(wxVERTICAL);
    m_media_ctrl = new wxMediaCtrl2(this);
    m_media_ctrl->SetMinSize(wxSize(PAGE_MIN_WIDTH, FromDIP(288)));

    m_media_play_ctrl = new MediaPlayCtrl(this, m_media_ctrl, wxDefaultPosition, wxSize(-1, FromDIP(40)));

    sizer->Add(m_media_ctrl, 1, wxEXPAND | wxALL, 0);
    sizer->Add(m_media_play_ctrl, 0, wxEXPAND | wxALL, 0);
//    media_ctrl_panel->SetSizer(bSizer_monitoring);
//    media_ctrl_panel->Layout();
//
//    sizer->Add(media_ctrl_panel, 1, wxEXPAND | wxALL, 1);
    return sizer;
}

wxBoxSizer *StatusBasePanel::create_project_task_page(wxWindow *parent)
{
    wxBoxSizer *sizer                 = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *bSizer_printing_title = new wxBoxSizer(wxHORIZONTAL);

    m_panel_printing_title = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, PAGE_TITLE_HEIGHT), wxTAB_TRAVERSAL);
    m_panel_printing_title->SetBackgroundColour(STATUS_TITLE_BG);

    m_staticText_printing = new wxStaticText(m_panel_printing_title, wxID_ANY, _L("Printing Progress"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    m_staticText_printing->Wrap(-1);
    m_staticText_printing->SetFont(PAGE_TITLE_FONT);
    m_staticText_printing->SetForegroundColour(PAGE_TITLE_FONT_COL);
    bSizer_printing_title->Add(m_staticText_printing, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, PAGE_TITLE_LEFT_MARGIN);

    bSizer_printing_title->Add(0, 0, 1, wxEXPAND, 0);

    m_panel_printing_title->SetSizer(bSizer_printing_title);
    m_panel_printing_title->Layout();
    bSizer_printing_title->Fit(m_panel_printing_title);

    sizer->Add(m_panel_printing_title, 0, wxEXPAND | wxALL, 0);
    sizer->Add(0, FromDIP(12), 0);

    m_printing_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_printing_sizer->SetMinSize(wxSize(PAGE_MIN_WIDTH, -1));
    m_bitmap_thumbnail = new wxStaticBitmap(parent, wxID_ANY, m_thumbnail_placeholder.bmp(), wxDefaultPosition, TASK_THUMBNAIL_SIZE, 0);

    m_printing_sizer->Add(m_bitmap_thumbnail, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxLEFT, FromDIP(12));

    m_printing_sizer->Add(FromDIP(8), 0, 0, wxEXPAND, 0);

    wxBoxSizer *bSizer_subtask_info = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *bSizer_task_name = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *bSizer_task_name_hor = new wxBoxSizer(wxHORIZONTAL);
    wxPanel*    task_name_panel      = new wxPanel(parent);

    m_staticText_subtask_value = new wxStaticText(task_name_panel, wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT | wxST_ELLIPSIZE_END);
    m_staticText_subtask_value->Wrap(-1);
    #ifdef __WXOSX_MAC__
    m_staticText_subtask_value->SetFont(::Label::Body_13);
    #else
    m_staticText_subtask_value->SetFont(wxFont(13, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
    #endif
    m_staticText_subtask_value->SetForegroundColour(wxColour(44, 44, 46));

    m_bitmap_static_use_time = new wxStaticBitmap(task_name_panel, wxID_ANY, m_bitmap_use_time.bmp(), wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)));

    m_staticText_consumption_of_time = new wxStaticText(task_name_panel, wxID_ANY, "0m", wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_consumption_of_time->SetFont(::Label::Body_12);
    m_staticText_consumption_of_time->SetForegroundColour(wxColour(0x68, 0x68, 0x68));
    m_staticText_consumption_of_time->Wrap(-1);


    m_bitmap_static_use_weight = new wxStaticBitmap(task_name_panel, wxID_ANY, m_bitmap_use_weight.bmp(), wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)));


    m_staticText_consumption_of_weight = new wxStaticText(task_name_panel, wxID_ANY, "0g", wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_consumption_of_weight->SetFont(::Label::Body_12);
    m_staticText_consumption_of_weight->SetForegroundColour(wxColour(0x68, 0x68, 0x68));
    m_staticText_consumption_of_weight->Wrap(-1);

    bSizer_task_name_hor->Add(m_staticText_subtask_value, 1, wxALL | wxEXPAND, 0);
    bSizer_task_name_hor->Add(0, 0, 1, wxEXPAND, 0);
    bSizer_task_name_hor->Add(m_bitmap_static_use_time, 0, wxALIGN_CENTER_VERTICAL, 0);
    bSizer_task_name_hor->Add(m_staticText_consumption_of_time, 0, wxALIGN_CENTER_VERTICAL|wxLEFT, FromDIP(3));
    bSizer_task_name_hor->Add(0, 0, 0, wxLEFT, FromDIP(20));
    bSizer_task_name_hor->Add(m_bitmap_static_use_weight, 0, wxALIGN_CENTER_VERTICAL, 0);
    bSizer_task_name_hor->Add(m_staticText_consumption_of_weight, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(3));
    bSizer_task_name_hor->Add(0, 0, 0, wxRIGHT, FromDIP(10));


    task_name_panel->SetSizer(bSizer_task_name_hor);
    task_name_panel->Layout();
    task_name_panel->Fit();

    bSizer_task_name->Add(task_name_panel, 0, wxEXPAND, FromDIP(5));
    

   /* wxFlexGridSizer *fgSizer_task = new wxFlexGridSizer(2, 2, 0, 0);
     fgSizer_task->AddGrowableCol(0);
     fgSizer_task->SetFlexibleDirection(wxVERTICAL);
     fgSizer_task->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);*/

    m_printing_stage_value = new wxStaticText(parent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT | wxST_ELLIPSIZE_END);
    m_printing_stage_value->Wrap(-1);
    #ifdef __WXOSX_MAC__
    m_printing_stage_value->SetFont(::Label::Body_11);
    #else 
    m_printing_stage_value->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
    #endif
    
    m_printing_stage_value->SetForegroundColour(STAGE_TEXT_COL);


    auto m_panel_progress = new wxPanel(parent, wxID_ANY);
    m_panel_progress->SetBackgroundColour(*wxWHITE);
    auto m_sizer_progressbar = new wxBoxSizer(wxHORIZONTAL); 
    m_gauge_progress = new ProgressBar(m_panel_progress, wxID_ANY, 100, wxDefaultPosition, wxDefaultSize);
    m_gauge_progress->SetValue(0);
    m_gauge_progress->SetHeight(PROGRESSBAR_HEIGHT);
    m_gauge_progress->SetMaxSize(wxSize(FromDIP(600), -1));
    m_panel_progress->SetSizer(m_sizer_progressbar);
    m_panel_progress->Layout();
    m_panel_progress->SetSize(wxSize(-1, FromDIP(24)));
    m_panel_progress->SetMaxSize(wxSize(-1, FromDIP(24)));

    wxBoxSizer *bSizer_task_btn = new wxBoxSizer(wxHORIZONTAL);

    bSizer_task_btn->Add(FromDIP(10), 0, 0);

   /* m_button_report = new Button(m_panel_progress, _L("Report"));
     StateColor report_bg(std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Disabled), std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
                          std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered), std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Enabled),
                          std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
     m_button_report->SetBackgroundColor(report_bg);
     m_button_report->SetMinSize(TASK_BUTTON_SIZE2);
     StateColor report_bd(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));
     m_button_report->SetBorderColor(report_bd);
     StateColor report_text(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));
     m_button_report->SetTextColor(report_text);
     m_button_report->SetFont(Label::Body_10);
     m_button_report->Hide();
     m_sizer_progressbar->Add(m_button_report, 0, wxALL, FromDIP(5));*/

    m_button_pause_resume = new ScalableButton(m_panel_progress, wxID_ANY, "print_control_pause", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER,true);

    m_button_pause_resume->Bind(wxEVT_ENTER_WINDOW, [this](auto &e) { 
        if (m_button_pause_resume->GetToolTipText() == _L("Pause")) {
            m_button_pause_resume->SetBitmap_("print_control_pause_hover"); 
        }

        if (m_button_pause_resume->GetToolTipText() == _L("Resume")) {
            m_button_pause_resume->SetBitmap_("print_control_resume_hover"); 
        }
    });

    m_button_pause_resume->Bind(wxEVT_LEAVE_WINDOW, [this](auto &e) { 
        auto        buf = m_button_pause_resume->GetClientData();
        if (m_button_pause_resume->GetToolTipText() == _L("Pause")) { 
            m_button_pause_resume->SetBitmap_("print_control_pause");
        }

        if (m_button_pause_resume->GetToolTipText() == _L("Resume")) { 
            m_button_pause_resume->SetBitmap_("print_control_resume"); 
        }
    });

    m_button_abort = new ScalableButton(m_panel_progress, wxID_ANY, "print_control_stop", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true);
    m_button_abort->SetToolTip(_L("Stop"));

    m_button_abort->Bind(wxEVT_ENTER_WINDOW, [this](auto &e) { 
        m_button_abort->SetBitmap_("print_control_stop_hover");
    });

    m_button_abort->Bind(wxEVT_LEAVE_WINDOW, [this](auto &e) { 
        m_button_abort->SetBitmap_("print_control_stop"); }
    );
   
    m_sizer_progressbar->Add(m_gauge_progress, 1, wxALIGN_CENTER_VERTICAL, 0);
    m_sizer_progressbar->Add(0, 0, 0, wxEXPAND|wxLEFT, FromDIP(18));
    m_sizer_progressbar->Add(m_button_pause_resume, 0, wxALL, FromDIP(5));
    m_sizer_progressbar->Add(0, 0, 0, wxEXPAND|wxLEFT, FromDIP(18));
    m_sizer_progressbar->Add(m_button_abort, 0, wxALL, FromDIP(5));

    //fgSizer_task->Add(bSizer_task_btn, 0, wxEXPAND, 0);

    wxBoxSizer *bSizer_buttons = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *bSizer_text = new wxBoxSizer(wxHORIZONTAL);
    wxPanel* penel_bottons = new wxPanel(parent);
    wxPanel* penel_text = new wxPanel(penel_bottons);

    penel_text->SetBackgroundColour(*wxWHITE);
    penel_bottons->SetBackgroundColour(*wxWHITE);

    wxBoxSizer *sizer_percent = new wxBoxSizer(wxVERTICAL);
    sizer_percent->Add(0, 0, 1, wxEXPAND, 0);

    wxBoxSizer *sizer_percent_icon  = new wxBoxSizer(wxVERTICAL);
    sizer_percent_icon->Add(0, 0, 1, wxEXPAND, 0);


    m_staticText_progress_percent = new wxStaticText(penel_text, wxID_ANY, L("0"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_progress_percent->SetFont(::Label::Head_18);
    m_staticText_progress_percent->SetMaxSize(wxSize(-1, FromDIP(20)));
    m_staticText_progress_percent->SetForegroundColour(wxColour(0, 174, 66));

    m_staticText_progress_percent_icon = new wxStaticText(penel_text, wxID_ANY, L("%"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_progress_percent_icon->SetFont(::Label::Body_11);
    m_staticText_progress_percent_icon->SetMaxSize(wxSize(-1, FromDIP(13)));
    m_staticText_progress_percent_icon->SetForegroundColour(wxColour(0, 174, 66));

    sizer_percent->Add(m_staticText_progress_percent, 0, 0, 0);

    #ifdef __WXOSX_MAC__
    sizer_percent_icon->Add(m_staticText_progress_percent_icon, 0, wxBOTTOM, FromDIP(2));
    #else
    sizer_percent_icon->Add(m_staticText_progress_percent_icon, 0, 0, 0);
    #endif
    
   
    m_staticText_progress_left = new wxStaticText(penel_text, wxID_ANY, L("N/A"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_progress_left->Wrap(-1);
    m_staticText_progress_left->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
    m_staticText_progress_left->SetForegroundColour(wxColour(146, 146, 146));

    //fgSizer_task->Add(bSizer_buttons, 0, wxEXPAND, 0);
    //fgSizer_task->Add(0, 0, 0, wxEXPAND, FromDIP(5));

    wxPanel* panel_button_block = new wxPanel(penel_bottons, wxID_ANY);
    panel_button_block->SetMinSize(wxSize(TASK_BUTTON_SIZE.x * 2 + FromDIP(5) * 4, -1));
    panel_button_block->SetMinSize(wxSize(TASK_BUTTON_SIZE.x * 2 + FromDIP(5) * 4, -1));
    panel_button_block->SetSize(wxSize(TASK_BUTTON_SIZE.x * 2 + FromDIP(5) * 2, -1));
    panel_button_block->SetBackgroundColour(*wxWHITE);

    //bSizer_text->Add(m_staticText_progress_percent, 0,  wxALL, 0);
    bSizer_text->Add(sizer_percent, 0, wxEXPAND, 0);
    bSizer_text->Add(sizer_percent_icon, 0, wxEXPAND, 0);
    bSizer_text->Add(0, 0, 1, wxEXPAND, 0);
    bSizer_text->Add(m_staticText_progress_left, 0, wxALIGN_CENTER | wxALL, 0);

    penel_text->SetMaxSize(wxSize(FromDIP(600), -1));
    penel_text->SetSizer(bSizer_text);
    penel_text->Layout();

    bSizer_buttons->Add(penel_text, 1, wxEXPAND | wxALL, 0);
    bSizer_buttons->Add(panel_button_block, 0, wxALIGN_CENTER | wxALL, 0);

    penel_bottons->SetSizer(bSizer_buttons);
    penel_bottons->Layout();

    bSizer_subtask_info->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(14));
    bSizer_subtask_info->Add(bSizer_task_name, 0, wxEXPAND|wxRIGHT, FromDIP(18));
    bSizer_subtask_info->Add(m_printing_stage_value, 0, wxEXPAND | wxTOP, FromDIP(5));
    bSizer_subtask_info->Add(penel_bottons, 0, wxEXPAND | wxTOP, FromDIP(10));
    bSizer_subtask_info->Add(m_panel_progress, 0, wxEXPAND|wxRIGHT, FromDIP(25));

    m_printing_sizer->Add(bSizer_subtask_info, 1, wxALL | wxEXPAND, 0);

    sizer->Add(m_printing_sizer, 0, wxEXPAND | wxALL, 0);

    m_staticline = new wxPanel( parent, wxID_ANY);
    m_staticline->SetBackgroundColour(wxColour(238,238,238));
    m_staticline->Layout();
    m_staticline->Hide();

    sizer->Add(0, 0, 0, wxTOP, FromDIP(15));
    sizer->Add(m_staticline, 0, wxEXPAND|wxALL, FromDIP(10));

    m_panel_error_txt = new wxPanel(parent, wxID_ANY);
    m_panel_error_txt->SetBackgroundColour(*wxWHITE);

    wxBoxSizer *static_text_sizer = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *text_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_error_text = new ErrorMsgStaticText(m_panel_error_txt);
    m_error_text->SetForegroundColour(wxColour(255, 0, 0));
    text_sizer->Add(m_error_text, 1, wxEXPAND|wxLEFT, FromDIP(17));

    m_button_clean = new Button(m_panel_error_txt, _L("Clean"));
    StateColor clean_bg(std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Disabled), std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
                        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered), std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Enabled),
                        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
    StateColor clean_bd(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));
    StateColor clean_text(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));


    m_button_clean->SetBackgroundColor(clean_bg);
    m_button_clean->SetBorderColor(clean_bd);
    m_button_clean->SetTextColor(clean_text);
    m_button_clean->SetFont(Label::Body_10);
    m_button_clean->SetMinSize(TASK_BUTTON_SIZE2);

    static_text_sizer->Add(text_sizer, 1, wxEXPAND, 0);
    static_text_sizer->Add( FromDIP(10), 0, 0, 0, 0 );
    static_text_sizer->Add(m_button_clean, 0, wxALIGN_CENTRE_VERTICAL|wxRIGHT, FromDIP(5));

    m_panel_error_txt->SetSizer(static_text_sizer);
    m_panel_error_txt->Hide();
    sizer->Add(m_panel_error_txt, 0, wxEXPAND | wxALL,0);
    sizer->Add(0, FromDIP(12), 0);

    m_tasklist_sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_tasklist_sizer, 0, wxEXPAND | wxALL, 0);
    return sizer;
}

wxBoxSizer *StatusBasePanel::create_machine_control_page(wxWindow *parent)
{
    wxBoxSizer *bSizer_right = new wxBoxSizer(wxVERTICAL);

    m_panel_control_title = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, PAGE_TITLE_HEIGHT), wxTAB_TRAVERSAL);
    m_panel_control_title->SetBackgroundColour(STATUS_TITLE_BG);

    wxBoxSizer *bSizer_control_title = new wxBoxSizer(wxHORIZONTAL);
    m_staticText_control             = new wxStaticText(m_panel_control_title, wxID_ANY, _L("Control"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    m_staticText_control->Wrap(-1);
    m_staticText_control->SetFont(PAGE_TITLE_FONT);
    m_staticText_control->SetForegroundColour(PAGE_TITLE_FONT_COL);

    StateColor btn_bg_green(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled), std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered), std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
    StateColor btn_bd_green(std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Disabled), std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Enabled));

    m_options_btn = new Button(m_panel_control_title, _L("Print Options"));
    m_options_btn->SetBackgroundColor(btn_bg_green);
    m_options_btn->SetBorderColor(btn_bd_green);
    m_options_btn->SetTextColor(*wxWHITE);
    m_options_btn->SetSize(wxSize(FromDIP(128), FromDIP(26)));
    m_options_btn->SetMinSize(wxSize(-1, FromDIP(26)));


    m_calibration_btn = new Button(m_panel_control_title, _L("Calibration"));
    m_calibration_btn->SetBackgroundColor(btn_bg_green);
    m_calibration_btn->SetBorderColor(btn_bd_green);
    m_calibration_btn->SetTextColor(*wxWHITE);
    m_calibration_btn->SetSize(wxSize(FromDIP(128), FromDIP(26)));
    m_calibration_btn->SetMinSize(wxSize(-1, FromDIP(26)));

    bSizer_control_title->Add(m_staticText_control, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, PAGE_TITLE_LEFT_MARGIN);
    bSizer_control_title->Add(0, 0, 1, wxEXPAND, 0);
    bSizer_control_title->Add(m_options_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
    bSizer_control_title->Add(m_calibration_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

    m_panel_control_title->SetSizer(bSizer_control_title);
    m_panel_control_title->Layout();
    bSizer_control_title->Fit(m_panel_control_title);
    bSizer_right->Add(m_panel_control_title, 0, wxALL | wxEXPAND, 0);

    wxBoxSizer *bSizer_control = new wxBoxSizer(wxVERTICAL);

    auto temp_axis_ctrl_sizer = create_temp_axis_group(parent);
    bSizer_control->Add(temp_axis_ctrl_sizer, 0, wxEXPAND, 0);

    auto m_ams_ctrl_sizer = create_ams_group(parent);
    bSizer_control->Add(m_ams_ctrl_sizer, 0, wxEXPAND|wxBOTTOM, FromDIP(10));

    bSizer_right->Add(bSizer_control, 1, wxEXPAND | wxALL, 0);

    return bSizer_right;
}

wxBoxSizer *StatusBasePanel::create_temp_axis_group(wxWindow *parent)
{
    auto        sizer         = new wxBoxSizer(wxVERTICAL);
    auto        box           = new RoundedRectangle(parent, wxColour(0xEE, 0xEE, 0xEE), wxDefaultPosition, wxSize(FromDIP(510), -1), 5, 1);

    box->SetMinSize(wxSize(FromDIP(530), -1));
    box->SetMaxSize(wxSize(FromDIP(530), -1));

    wxBoxSizer *content_sizer = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_temp_ctrl   = create_temp_control(box);
    content_sizer->Add(m_temp_ctrl, 0, wxEXPAND | wxALL, FromDIP(5));

    m_temp_extruder_line = new StaticLine(box, true);
    m_temp_extruder_line->SetLineColour(STATIC_BOX_LINE_COL);
    content_sizer->Add(m_temp_extruder_line, 0, wxEXPAND, 1);
    content_sizer->Add(FromDIP(9), 0, 0, wxEXPAND, 1);

    auto m_axis_sizer = create_axis_control(box);
    content_sizer->Add(m_axis_sizer, 0, wxALIGN_CENTER_VERTICAL | wxALL, 0);

    wxBoxSizer *bed_sizer = create_bed_control(box);
    content_sizer->Add(bed_sizer, 0, wxEXPAND | wxLEFT | wxTOP| wxBOTTOM, FromDIP(12));

    content_sizer->Add(0, 0, 0, wxLEFT, FromDIP(12));

    wxBoxSizer *extruder_sizer = create_extruder_control(box);
    content_sizer->Add(extruder_sizer, 0, wxEXPAND  | wxTOP | wxBOTTOM, FromDIP(12));

    box->SetSizer(content_sizer);
    sizer->Add(box, 0, wxEXPAND | wxALL, FromDIP(9));

    return sizer;
}

wxBoxSizer *StatusBasePanel::create_temp_control(wxWindow *parent)
{
    auto sizer = new wxBoxSizer(wxVERTICAL);

    wxWindowID nozzle_id = wxWindow::NewControlId();
    m_tempCtrl_nozzle    = new TempInput(parent, nozzle_id, TEMP_BLANK_STR, TEMP_BLANK_STR, wxString("monitor_nozzle_temp"), wxString("monitor_nozzle_temp_active"),
                                      wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    m_tempCtrl_nozzle->SetMinSize(TEMP_CTRL_MIN_SIZE);
    m_tempCtrl_nozzle->SetMinTemp(nozzle_temp_range[0]);
    m_tempCtrl_nozzle->SetMaxTemp(nozzle_temp_range[1]);
    m_tempCtrl_nozzle->SetBorderWidth(FromDIP(2));
    m_tempCtrl_nozzle->SetTextColor(StateColor(std::make_pair(DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(NORMAL_TEXT_COL, (int) StateColor::Normal)));
    m_tempCtrl_nozzle->SetBorderColor(StateColor(std::make_pair(*wxWHITE, (int) StateColor::Disabled), std::make_pair(BUTTON_HOVER_COL, (int) StateColor::Focused),
                                                 std::make_pair(BUTTON_HOVER_COL, (int) StateColor::Hovered), std::make_pair(*wxWHITE, (int) StateColor::Normal)));

    sizer->Add(m_tempCtrl_nozzle, 0, wxEXPAND | wxALL, 1);

    m_line_nozzle = new StaticLine(parent);
    m_line_nozzle->SetLineColour(STATIC_BOX_LINE_COL);
    m_line_nozzle->SetSize(wxSize(FromDIP(1), -1));
    sizer->Add(m_line_nozzle, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

    wxWindowID bed_id = wxWindow::NewControlId();
    m_tempCtrl_bed    = new TempInput(parent, bed_id, TEMP_BLANK_STR, TEMP_BLANK_STR, wxString("monitor_bed_temp"), wxString("monitor_bed_temp_active"), wxDefaultPosition,
                                   wxDefaultSize, wxALIGN_CENTER);
    m_tempCtrl_bed->SetMinTemp(bed_temp_range[0]);
    m_tempCtrl_bed->SetMaxTemp(bed_temp_range[1]);
    m_tempCtrl_bed->SetMinSize(TEMP_CTRL_MIN_SIZE);
    m_tempCtrl_bed->SetBorderWidth(FromDIP(2));
    m_tempCtrl_bed->SetTextColor(StateColor(std::make_pair(DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(NORMAL_TEXT_COL, (int) StateColor::Normal)));
    m_tempCtrl_bed->SetBorderColor(StateColor(std::make_pair(*wxWHITE, (int) StateColor::Disabled), std::make_pair(BUTTON_HOVER_COL, (int) StateColor::Focused),
                                              std::make_pair(BUTTON_HOVER_COL, (int) StateColor::Hovered), std::make_pair(*wxWHITE, (int) StateColor::Normal)));
    sizer->Add(m_tempCtrl_bed, 0, wxEXPAND | wxALL, 1);

    auto line = new StaticLine(parent);
    line->SetLineColour(STATIC_BOX_LINE_COL);
    sizer->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

    wxWindowID frame_id = wxWindow::NewControlId();
    m_tempCtrl_frame    = new TempInput(parent, frame_id, TEMP_BLANK_STR, TEMP_BLANK_STR, wxString("monitor_frame_temp"), wxString("monitor_frame_temp"), wxDefaultPosition,
                                     wxDefaultSize, wxALIGN_CENTER);
    m_tempCtrl_frame->SetReadOnly(true);
    m_tempCtrl_frame->SetMinSize(TEMP_CTRL_MIN_SIZE);
    m_tempCtrl_frame->SetBorderWidth(FromDIP(2));
    m_tempCtrl_frame->SetTextColor(StateColor(std::make_pair(DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(NORMAL_TEXT_COL, (int) StateColor::Normal)));
    m_tempCtrl_frame->SetBorderColor(StateColor(std::make_pair(*wxWHITE, (int) StateColor::Disabled), std::make_pair(BUTTON_HOVER_COL, (int) StateColor::Focused),
                                                std::make_pair(BUTTON_HOVER_COL, (int) StateColor::Hovered), std::make_pair(*wxWHITE, (int) StateColor::Normal)));
    sizer->Add(m_tempCtrl_frame, 0, wxEXPAND | wxALL, 1);
    line = new StaticLine(parent);
    line->SetLineColour(STATIC_BOX_LINE_COL);
    sizer->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

    wxBoxSizer *m_misc_ctrl_sizer = create_misc_control(parent);

    sizer->Add(m_misc_ctrl_sizer, 0, wxEXPAND, 0);
    return sizer;
}

wxBoxSizer *StatusBasePanel::create_misc_control(wxWindow *parent)
{
    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *line_sizer = new wxBoxSizer(wxHORIZONTAL);

    /* create speed control */
    m_switch_speed = new ImageSwitchButton(parent, m_bitmap_speed_active, m_bitmap_speed);
    m_switch_speed->SetLabels(_L("100%"), _L("100%"));
    m_switch_speed->SetMinSize(MISC_BUTTON_SIZE);
    m_switch_speed->SetPadding(FromDIP(3));
    m_switch_speed->SetBorderWidth(FromDIP(2));
    m_switch_speed->SetFont(Label::Head_13);
    m_switch_speed->SetTextColor(StateColor(std::make_pair(DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(NORMAL_TEXT_COL, (int) StateColor::Normal)));
    m_switch_speed->SetValue(false);

    line_sizer->Add(m_switch_speed, 1, wxALIGN_CENTER | wxALL, 0);

    auto line = new StaticLine(parent, true);
    line->SetLineColour(STATIC_BOX_LINE_COL);
    line_sizer->Add(line, 0, wxEXPAND | wxTOP | wxBOTTOM, 4);

    /* create lamp control */
    m_switch_lamp = new ImageSwitchButton(parent, m_bitmap_lamp_on, m_bitmap_lamp_off);
    m_switch_lamp->SetLabels(_L("Lamp"), _L("Lamp"));
    m_switch_lamp->SetMinSize(MISC_BUTTON_SIZE);
    m_switch_lamp->SetPadding(FromDIP(3));
    m_switch_lamp->SetBorderWidth(FromDIP(2));
    m_switch_lamp->SetFont(Label::Head_13);
    m_switch_lamp->SetTextColor(StateColor(std::make_pair(DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(NORMAL_TEXT_COL, (int) StateColor::Normal)));
    line_sizer->Add(m_switch_lamp, 1, wxALIGN_CENTER | wxALL, 0);

    sizer->Add(line_sizer, 0, wxEXPAND, FromDIP(5));
    line = new StaticLine(parent);
    line->SetLineColour(STATIC_BOX_LINE_COL);
    sizer->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

    line_sizer          = new wxBoxSizer(wxHORIZONTAL);
    m_switch_nozzle_fan = new ImageSwitchButton(parent, m_bitmap_fan_on, m_bitmap_fan_off);
    m_switch_nozzle_fan->SetMinSize(MISC_BUTTON_SIZE);
    m_switch_nozzle_fan->SetValue(false);
    m_switch_nozzle_fan->SetLabels(_L("Part Cooling"), _L("Part Cooling"));
    m_switch_nozzle_fan->SetPadding(FromDIP(3));
    m_switch_nozzle_fan->SetBorderWidth(FromDIP(2));
    m_switch_nozzle_fan->SetFont(SWITCH_FONT);
    m_switch_nozzle_fan->SetTextColor(StateColor(std::make_pair(DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(NORMAL_FAN_TEXT_COL, (int) StateColor::Normal)));

    line_sizer->Add(m_switch_nozzle_fan, 1, wxALIGN_CENTER | wxALL, 0);
    line = new StaticLine(parent, true);
    line->SetLineColour(STATIC_BOX_LINE_COL);
    line_sizer->Add(line, 0, wxEXPAND | wxTOP | wxBOTTOM, 4);

    m_switch_printing_fan = new ImageSwitchButton(parent, m_bitmap_fan_on, m_bitmap_fan_off);
    m_switch_printing_fan->SetValue(false);
    m_switch_printing_fan->SetMinSize(MISC_BUTTON_SIZE);
    m_switch_printing_fan->SetPadding(FromDIP(3));
    m_switch_printing_fan->SetBorderWidth(FromDIP(2));
    m_switch_printing_fan->SetFont(SWITCH_FONT);
    m_switch_printing_fan->SetLabels(_L("Aux Cooling"), _L("Aux Cooling"));
    m_switch_printing_fan->SetTextColor(
        StateColor(std::make_pair(DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(NORMAL_FAN_TEXT_COL, (int) StateColor::Normal)));

    line_sizer->Add(m_switch_printing_fan, 1, wxALIGN_CENTER | wxALL, 0);

    sizer->Add(line_sizer, 0, wxEXPAND, FromDIP(5));

    return sizer;
}

void StatusBasePanel::reset_temp_misc_control()
{
    // reset temp string
    m_tempCtrl_nozzle->SetLabel(TEMP_BLANK_STR);
    m_tempCtrl_nozzle->GetTextCtrl()->SetLabel(TEMP_BLANK_STR);
    m_tempCtrl_bed->SetLabel(TEMP_BLANK_STR);
    m_tempCtrl_bed->GetTextCtrl()->SetLabel(TEMP_BLANK_STR);
    m_tempCtrl_frame->SetLabel(TEMP_BLANK_STR);
    m_tempCtrl_frame->GetTextCtrl()->SetLabel(TEMP_BLANK_STR);
    m_button_unload->Show(); 

    m_tempCtrl_nozzle->Enable(true);
    m_tempCtrl_frame->Enable(true);
    m_tempCtrl_bed->Enable(true);

    // reset misc control
    m_switch_speed->SetLabels(_L("100%"), _L("100%"));
    m_switch_speed->SetValue(false);
    m_switch_lamp->SetLabels(_L("Lamp"), _L("Lamp"));
    m_switch_lamp->SetValue(false);
    m_switch_nozzle_fan->SetValue(false);
    m_switch_printing_fan->SetValue(false);
}

wxBoxSizer *StatusBasePanel::create_axis_control(wxWindow *parent)
{
    auto sizer = new wxBoxSizer(wxVERTICAL);
    sizer->AddStretchSpacer();
    m_bpButton_xy = new AxisCtrlButton(parent, m_bitmap_axis_home);
    m_bpButton_xy->SetTextColor(StateColor(std::make_pair(DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(NORMAL_TEXT_COL, (int) StateColor::Normal)));
    m_bpButton_xy->SetMinSize(AXIS_MIN_SIZE);
    m_bpButton_xy->SetSize(AXIS_MIN_SIZE);
    sizer->AddStretchSpacer();
    sizer->Add(m_bpButton_xy, 0, wxALIGN_CENTER | wxALL, 0);
    sizer->AddStretchSpacer();

    /*m_staticText_xy = new wxStaticText(parent, wxID_ANY, _L("X/Y Axis"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_xy->Wrap(-1);

    m_staticText_xy->SetForegroundColour(TEXT_LIGHT_FONT_COL);
    sizer->Add(m_staticText_xy, 0, wxBOTTOM | wxALIGN_CENTER_HORIZONTAL, FromDIP(5));*/
    return sizer;
}

wxBoxSizer *StatusBasePanel::create_bed_control(wxWindow *parent)
{
    wxBoxSizer *sizer         = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *bSizer_z_ctrl = new wxBoxSizer(wxVERTICAL);
    auto        panel         = new wxPanel(parent, wxID_ANY);
    panel->SetBackgroundColour(*wxWHITE);

    panel->SetSize(wxSize(FromDIP(52), -1));
    panel->SetMinSize(wxSize(FromDIP(52), -1));
    panel->SetMaxSize(wxSize(FromDIP(52), -1));



    StateColor z_10_ctrl_bg(std::pair<wxColour, int>(BUTTON_PRESS_COL, StateColor::Pressed), std::pair<wxColour, int>(BUTTON_NORMAL1_COL, StateColor::Normal));
    StateColor z_10_ctrl_bd(std::pair<wxColour, int>(BUTTON_HOVER_COL, StateColor::Hovered), std::pair<wxColour, int>(BUTTON_NORMAL1_COL, StateColor::Normal));

    StateColor z_1_ctrl_bg(std::pair<wxColour, int>(BUTTON_PRESS_COL, StateColor::Pressed), std::pair<wxColour, int>(BUTTON_NORMAL2_COL, StateColor::Normal));
    StateColor z_1_ctrl_bd(std::pair<wxColour, int>(BUTTON_HOVER_COL, StateColor::Hovered), std::pair<wxColour, int>(BUTTON_NORMAL2_COL, StateColor::Normal));

    bSizer_z_ctrl->AddStretchSpacer();
    m_bpButton_z_10 = new Button(panel, wxString("10"), "monitor_bed_up", 0, FromDIP(15));
    m_bpButton_z_10->SetFont(::Label::Body_13);
    m_bpButton_z_10->SetBorderWidth(2);
    m_bpButton_z_10->SetBackgroundColor(z_10_ctrl_bg);
    m_bpButton_z_10->SetBorderColor(z_10_ctrl_bd);
    m_bpButton_z_10->SetTextColor(StateColor(std::make_pair(DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(NORMAL_TEXT_COL, (int) StateColor::Normal)));
    m_bpButton_z_10->SetMinSize(Z_BUTTON_SIZE);
    m_bpButton_z_10->SetCornerRadius(0);

    bSizer_z_ctrl->Add(m_bpButton_z_10, 0, wxEXPAND | wxALL, 0);

    m_bpButton_z_1 = new Button(panel, wxString(" 1"), "monitor_bed_up", 0, FromDIP(15));
    m_bpButton_z_1->SetFont(::Label::Body_13);
    m_bpButton_z_1->SetBorderWidth(2);
    m_bpButton_z_1->SetBackgroundColor(z_1_ctrl_bg);
    m_bpButton_z_1->SetBorderColor(z_1_ctrl_bd);
    m_bpButton_z_1->SetMinSize(Z_BUTTON_SIZE);
    m_bpButton_z_1->SetTextColor(StateColor(std::make_pair(DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(NORMAL_TEXT_COL, (int) StateColor::Normal)));

    bSizer_z_ctrl->Add(m_bpButton_z_1, 0, wxEXPAND | wxALL, 0);

    bSizer_z_ctrl->Add(0, FromDIP(6), 0, wxEXPAND, 0);

    m_bpButton_z_down_1 = new Button(panel, wxString(" 1"), "monitor_bed_down", 0, FromDIP(15));
    m_bpButton_z_down_1->SetFont(::Label::Body_13);
    m_bpButton_z_down_1->SetBorderWidth(2);
    m_bpButton_z_down_1->SetBackgroundColor(z_1_ctrl_bg);
    m_bpButton_z_down_1->SetBorderColor(z_1_ctrl_bd);
    m_bpButton_z_down_1->SetMinSize(Z_BUTTON_SIZE);
    m_bpButton_z_down_1->SetTextColor(StateColor(std::make_pair(DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(NORMAL_TEXT_COL, (int) StateColor::Normal)));

    bSizer_z_ctrl->Add(m_bpButton_z_down_1, 0, wxEXPAND | wxALL, 0);

    m_bpButton_z_down_10 = new Button(panel, wxString("10"), "monitor_bed_down", 0, FromDIP(15));
    m_bpButton_z_down_10->SetFont(::Label::Body_13);
    m_bpButton_z_down_10->SetBorderWidth(2);
    m_bpButton_z_down_10->SetBackgroundColor(z_10_ctrl_bg);
    m_bpButton_z_down_10->SetBorderColor(z_10_ctrl_bd);
    m_bpButton_z_down_10->SetMinSize(Z_BUTTON_SIZE);
    m_bpButton_z_down_10->SetTextColor(StateColor(std::make_pair(DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(NORMAL_TEXT_COL, (int) StateColor::Normal)));

    bSizer_z_ctrl->Add(m_bpButton_z_down_10, 0, wxEXPAND | wxALL, 0);

    bSizer_z_ctrl->Add(0, FromDIP(16), 0, wxEXPAND, 0);

    m_staticText_z_tip = new wxStaticText(panel, wxID_ANY, _L("Bed"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_z_tip->SetFont(::Label::Body_13);
    m_staticText_z_tip->Wrap(-1);
    m_staticText_z_tip->SetForegroundColour(TEXT_LIGHT_FONT_COL);
    bSizer_z_ctrl->Add(m_staticText_z_tip, 0, wxBOTTOM | wxALIGN_CENTER_HORIZONTAL, FromDIP(5));

    panel->SetSizer(bSizer_z_ctrl);
    panel->Layout();
    sizer->Add(panel, 1, wxEXPAND, 0);

    return sizer;
}

wxBoxSizer *StatusBasePanel::create_extruder_control(wxWindow *parent)
{
    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *bSizer_e_ctrl = new wxBoxSizer(wxVERTICAL);
    auto        panel = new wxPanel(parent,wxID_ANY);
    panel->SetBackgroundColour(*wxWHITE);


    panel->SetSize(wxSize(FromDIP(52), -1));
    panel->SetMinSize(wxSize(FromDIP(52), -1));
    panel->SetMaxSize(wxSize(FromDIP(52), -1));

    StateColor e_ctrl_bg(std::pair<wxColour, int>(BUTTON_PRESS_COL, StateColor::Pressed), std::pair<wxColour, int>(BUTTON_NORMAL1_COL, StateColor::Normal));
    StateColor e_ctrl_bd(std::pair<wxColour, int>(BUTTON_HOVER_COL, StateColor::Hovered), std::pair<wxColour, int>(BUTTON_NORMAL1_COL, StateColor::Normal));
    m_bpButton_e_10 = new Button(panel, "", "monitor_extruder_up", 0, FromDIP(22));
    m_bpButton_e_10->SetBorderWidth(2);
    m_bpButton_e_10->SetBackgroundColor(e_ctrl_bg);
    m_bpButton_e_10->SetBorderColor(e_ctrl_bd);
    m_bpButton_e_10->SetMinSize(wxSize(FromDIP(40), FromDIP(40)));

    bSizer_e_ctrl->AddStretchSpacer();
    bSizer_e_ctrl->Add(m_bpButton_e_10, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    bSizer_e_ctrl->Add(0, FromDIP(7), 0, 0, 0);

    m_bitmap_extruder_img = new wxStaticBitmap(panel, wxID_ANY, m_bitmap_extruder, wxDefaultPosition, wxDefaultSize, 0);
    m_bitmap_extruder_img->SetMinSize(EXTRUDER_IMAGE_SIZE);

    bSizer_e_ctrl->Add(m_bitmap_extruder_img, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxBOTTOM, FromDIP(5));
    bSizer_e_ctrl->Add(0, FromDIP(7), 0, 0, 0);
    m_bpButton_e_down_10 = new Button(panel, "", "monitor_extruder_down", 0, FromDIP(22));
    m_bpButton_e_down_10->SetBorderWidth(2);
    m_bpButton_e_down_10->SetBackgroundColor(e_ctrl_bg);
    m_bpButton_e_down_10->SetBorderColor(e_ctrl_bd);
    m_bpButton_e_down_10->SetMinSize(wxSize(FromDIP(40), FromDIP(40)));

    bSizer_e_ctrl->Add(m_bpButton_e_down_10, 0, wxALIGN_CENTER_HORIZONTAL, 0);


    m_button_unload = new Button(panel, _L("Unload"));

    StateColor abort_bg(std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Disabled), std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
                        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered), std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Enabled),
                        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
    m_button_unload->SetBackgroundColor(abort_bg);
    StateColor abort_bd(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));
    m_button_unload->SetBorderColor(abort_bd);
    StateColor abort_text(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));
    m_button_unload->SetTextColor(abort_text);
    m_button_unload->SetFont(Label::Body_10);
    m_button_unload->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_unload->SetCornerRadius(FromDIP(12));
    bSizer_e_ctrl->Add(0, 0, 1, wxEXPAND, 0);
    bSizer_e_ctrl->Add(m_button_unload, 0, wxALIGN_CENTER_HORIZONTAL| wxTOP|wxBOTTOM, FromDIP(5));


    bSizer_e_ctrl->Add(0, FromDIP(9), 0, wxEXPAND, 0);

    m_staticText_e = new wxStaticText(panel, wxID_ANY, _L("Extruder"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_e->SetFont(::Label::Body_13);
    m_staticText_e->Wrap(-1);
    m_staticText_e->SetForegroundColour(TEXT_LIGHT_FONT_COL);
    bSizer_e_ctrl->Add(m_staticText_e, 0, wxBOTTOM | wxALIGN_CENTER_HORIZONTAL, FromDIP(5));

    panel->SetSizer(bSizer_e_ctrl);
    panel->Layout();
    sizer->Add(panel, 1, wxEXPAND, 0);

    return sizer;
}

wxBoxSizer *StatusBasePanel::create_ams_group(wxWindow *parent)
{
    auto sizer     = new wxBoxSizer(wxVERTICAL);
    auto sizer_box = new wxBoxSizer(wxVERTICAL);
    m_ams_control_box = new RoundedRectangle(parent, wxColour(0xEE, 0xEE, 0xEE), wxDefaultPosition, wxDefaultSize, 5, 1);
    m_ams_control_box->SetMinSize(wxSize(FromDIP(530), -1));
    m_ams_control_box->SetBackgroundColour(*wxWHITE);
#if !BBL_RELEASE_TO_PUBLIC
    m_ams_debug = new wxStaticText(m_ams_control_box, wxID_ANY, _L("Debug Info"), wxDefaultPosition, wxDefaultSize, 0);
    sizer_box->Add(m_ams_debug, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    m_ams_debug->Hide();
#endif

    m_ams_control = new AMSControl(m_ams_control_box, wxID_ANY);
    //m_ams_control->SetMinSize(wxSize(FromDIP(510), FromDIP(286)));
    m_ams_control->SetDoubleBuffered(true);
    sizer_box->Add(m_ams_control, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, FromDIP(10));

    m_ams_control_box->SetBackgroundColour(*wxWHITE);
    m_ams_control_box->SetSizer(sizer_box);
    m_ams_control_box->Layout();
    m_ams_control_box->Fit();
    sizer->Add(m_ams_control_box, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, FromDIP(0));
    return sizer;
}

void StatusBasePanel::show_ams_group(bool show)
{
    if (m_show_ams_group != show) {
        m_ams_control->Show(show);
        m_ams_control_box->Show(show);
        Fit();
    }
    m_show_ams_group = show;
}

void StatusBasePanel::upodate_camera_state(bool recording, bool timelapse, bool has_sdcard)
{

    //sdcard
   /* if (has_sdcard && !m_bitmap_sdcard_img->IsShown()) {
        m_bitmap_sdcard_img->Show();
        m_panel_monitoring_title->Layout();
    }
    if (!has_sdcard && m_bitmap_sdcard_img->IsShown()) {
        m_bitmap_sdcard_img->Hide();
        m_panel_monitoring_title->Layout();
    }*/

    if (has_sdcard) {
        if (m_bitmap_sdcard_off_img->IsShown()) {
            m_bitmap_sdcard_on_img->Show();
            m_bitmap_sdcard_off_img->Hide();
            m_panel_monitoring_title->Layout();
        }
    } else {
        if (m_bitmap_sdcard_on_img->IsShown()) {
            m_bitmap_sdcard_on_img->Hide();
            m_bitmap_sdcard_off_img->Show();
            m_panel_monitoring_title->Layout();
        }
    }

     //recording
    m_recording_button->set_switch(recording);

    //timelapse
    m_timelapse_button->set_switch(timelapse);
}

StatusPanel::StatusPanel(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style, const wxString &name)
    : StatusBasePanel(parent, id, pos, size, style)
{
    create_tasklist_info();
    update_tasklist_info();

    init_scaled_buttons();

    //m_buttons.push_back(m_button_report);
    //m_buttons.push_back(m_button_pause_resume);
    //m_buttons.push_back(m_button_abort);
    m_buttons.push_back(m_button_unload);
    m_buttons.push_back(m_button_clean);
    m_buttons.push_back(m_bpButton_z_10);
    m_buttons.push_back(m_bpButton_z_1);
    m_buttons.push_back(m_bpButton_z_down_1);
    m_buttons.push_back(m_bpButton_z_down_10);
    m_buttons.push_back(m_bpButton_e_10);
    m_buttons.push_back(m_bpButton_e_down_10);

    obj = nullptr;
    /* set default values */
    m_switch_lamp->SetValue(false);
    m_switch_printing_fan->SetValue(false);
    m_switch_nozzle_fan->SetValue(false);

    /* set default enable state */
    m_button_pause_resume->Enable(false);
    m_button_pause_resume->SetBitmap_("print_control_resume_disable");

    m_button_abort->Enable(false);
    m_button_abort->SetBitmap_("print_control_stop_disable");

    Bind(wxEVT_WEBREQUEST_STATE, &StatusPanel::on_webrequest_state, this);

    Bind(wxCUSTOMEVT_SET_TEMP_FINISH, [this](wxCommandEvent e) {
        int id = e.GetInt();
        if (id == m_tempCtrl_bed->GetType()) {
            on_set_bed_temp();
        } else if (id == m_tempCtrl_nozzle->GetType()) {
            on_set_nozzle_temp();
        }
    });


    // Connect Events
    //m_bitmap_thumbnail->Connect(wxEVT_ENTER_WINDOW, wxMouseEventHandler(StatusPanel::on_thumbnail_enter), NULL, this);
    //m_bitmap_thumbnail->Connect(wxEVT_LEAVE_WINDOW, wxMouseEventHandler(StatusPanel::on_thumbnail_leave), NULL, this);
    m_recording_button->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(StatusPanel::on_switch_recording), NULL, this);
    m_project_task_panel->Connect(wxEVT_LEAVE_WINDOW, wxMouseEventHandler(StatusPanel::on_thumbnail_leave), NULL, this);

    m_button_pause_resume->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_subtask_pause_resume), NULL, this);
    m_button_abort->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_subtask_abort), NULL, this);
    m_button_clean->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_subtask_clean), NULL, this);
    m_tempCtrl_bed->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(StatusPanel::on_bed_temp_kill_focus), NULL, this);
    m_tempCtrl_bed->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(StatusPanel::on_bed_temp_set_focus), NULL, this);
    m_tempCtrl_nozzle->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(StatusPanel::on_nozzle_temp_kill_focus), NULL, this);
    m_tempCtrl_nozzle->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(StatusPanel::on_nozzle_temp_set_focus), NULL, this);
    m_switch_lamp->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_lamp_switch), NULL, this);
    m_switch_nozzle_fan->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_nozzle_fan_switch), NULL, this); // TODO
    m_switch_printing_fan->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_printing_fan_switch), NULL, this);
    m_bpButton_xy->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_xy), NULL, this); // TODO
    m_bpButton_z_10->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_up_10), NULL, this);
    m_bpButton_z_1->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_up_1), NULL, this);
    m_bpButton_z_down_1->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_down_1), NULL, this);
    m_bpButton_z_down_10->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_down_10), NULL, this);
    m_bpButton_e_10->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_e_up_10), NULL, this);
    m_bpButton_e_down_10->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_e_down_10), NULL, this);
    m_button_unload->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_start_unload), NULL, this);
    Bind(EVT_AMS_LOAD, &StatusPanel::on_ams_load, this);
    Bind(EVT_AMS_UNLOAD, &StatusPanel::on_ams_unload, this);
    Bind(EVT_AMS_SETTINGS, &StatusPanel::on_ams_setting_click, this);
    Bind(EVT_AMS_REFRESH_RFID, &StatusPanel::on_ams_refresh_rfid, this);
    Bind(EVT_AMS_ON_SELECTED, &StatusPanel::on_ams_selected, this);
    Bind(EVT_AMS_ON_FILAMENT_EDIT, &StatusPanel::on_filament_edit, this);

    m_switch_speed->Connect(wxEVT_LEFT_DOWN, wxCommandEventHandler(StatusPanel::on_switch_speed), NULL, this);
    m_calibration_btn->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_start_calibration), NULL, this);
    m_options_btn->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_show_print_options), NULL, this);
}

StatusPanel::~StatusPanel()
{
    // Disconnect Events
    //m_bitmap_thumbnail->Disconnect(wxEVT_ENTER_WINDOW, wxMouseEventHandler(StatusPanel::on_thumbnail_enter), NULL, this);
    //m_bitmap_thumbnail->Disconnect(wxEVT_LEAVE_WINDOW, wxMouseEventHandler(StatusPanel::on_thumbnail_leave), NULL, this);
    m_recording_button->Disconnect(wxEVT_LEFT_DOWN, wxMouseEventHandler(StatusPanel::on_switch_recording), NULL, this);
    m_button_pause_resume->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_subtask_pause_resume), NULL, this);
    m_button_abort->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_subtask_abort), NULL, this);
    m_button_clean->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_subtask_clean), NULL, this);
    m_tempCtrl_bed->Disconnect(wxEVT_KILL_FOCUS, wxFocusEventHandler(StatusPanel::on_bed_temp_kill_focus), NULL, this);
    m_tempCtrl_bed->Disconnect(wxEVT_SET_FOCUS, wxFocusEventHandler(StatusPanel::on_bed_temp_set_focus), NULL, this);
    m_tempCtrl_nozzle->Disconnect(wxEVT_KILL_FOCUS, wxFocusEventHandler(StatusPanel::on_nozzle_temp_kill_focus), NULL, this);
    m_tempCtrl_nozzle->Disconnect(wxEVT_SET_FOCUS, wxFocusEventHandler(StatusPanel::on_nozzle_temp_set_focus), NULL, this);
    m_switch_lamp->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_lamp_switch), NULL, this);
    m_switch_nozzle_fan->Disconnect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_nozzle_fan_switch), NULL, this);
    m_switch_printing_fan->Disconnect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_printing_fan_switch), NULL, this);
    m_bpButton_xy->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_xy), NULL, this);
    m_bpButton_z_10->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_up_10), NULL, this);
    m_bpButton_z_1->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_up_1), NULL, this);
    m_bpButton_z_down_1->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_down_1), NULL, this);
    m_bpButton_z_down_10->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_down_10), NULL, this);
    m_bpButton_e_10->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_e_up_10), NULL, this);
    m_bpButton_e_down_10->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_e_down_10), NULL, this);
    m_switch_speed->Disconnect(wxEVT_LEFT_DOWN, wxCommandEventHandler(StatusPanel::on_switch_speed), NULL, this);
    m_calibration_btn->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_start_calibration), NULL, this);
    m_options_btn->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_show_print_options), NULL, this);
    m_button_unload->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_start_unload), NULL, this);
}

void StatusPanel::init_scaled_buttons()
{
   // m_button_report->SetMinSize(wxSize(FromDIP(48), FromDIP(24)));
   // m_button_report->SetCornerRadius(FromDIP(12));
    //m_button_pause_resume->SetMinSize(wxSize(FromDIP(48), FromDIP(24)));
    //m_button_pause_resume->SetCornerRadius(FromDIP(12));
    //m_button_abort->SetMinSize(wxSize(FromDIP(48), FromDIP(24)));
    //m_button_abort->SetCornerRadius(FromDIP(12));
    m_button_clean->SetMinSize(wxSize(FromDIP(48), FromDIP(24)));
    m_button_clean->SetCornerRadius(FromDIP(12));
    m_button_unload->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_unload->SetCornerRadius(FromDIP(12));
    m_bpButton_z_10->SetMinSize(Z_BUTTON_SIZE);
    m_bpButton_z_10->SetCornerRadius(0);
    m_bpButton_z_1->SetMinSize(Z_BUTTON_SIZE);
    m_bpButton_z_1->SetCornerRadius(0);
    m_bpButton_z_down_1->SetMinSize(Z_BUTTON_SIZE);
    m_bpButton_z_down_1->SetCornerRadius(0);
    m_bpButton_z_down_10->SetMinSize(Z_BUTTON_SIZE);
    m_bpButton_z_down_10->SetCornerRadius(0);
    m_bpButton_e_10->SetMinSize(wxSize(FromDIP(40), FromDIP(40)));
    m_bpButton_e_10->SetCornerRadius(FromDIP(12));
    m_bpButton_e_down_10->SetMinSize(wxSize(FromDIP(40), FromDIP(40)));
    m_bpButton_e_down_10->SetCornerRadius(FromDIP(12));
}

void StatusPanel::clean_tasklist_info()
{
    m_tasklist_info_sizer = new wxGridBagSizer(4, 8);
    for (int i = 0; i < slice_info_list.size(); i++) { delete slice_info_list[i]; }
    slice_info_list.clear();
    show_task_list_info(false);
}

void StatusPanel::create_tasklist_info()
{
    m_tasklist_caption_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_text_tasklist_caption  = new wxStaticText(this, wxID_ANY, _L("Printing List"), wxDefaultPosition, wxDefaultSize, 0);
    m_text_tasklist_caption->Wrap(-1);
    m_text_tasklist_caption->SetFont(GROUP_TITLE_FONT);
    m_text_tasklist_caption->SetForegroundColour(GROUP_TITLE_FONT_COL);

    m_tasklist_caption_sizer->Add(m_text_tasklist_caption, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, GROUP_TITLE_LEFT_MARGIN);

    auto staticline = new StaticLine(this);
    staticline->SetLineColour(GROUP_STATIC_LINE_COL);
    m_tasklist_caption_sizer->Add(staticline, 1, wxRIGHT | wxLEFT | wxALIGN_CENTER_VERTICAL, GROUP_TITLE_LINE_MARGIN);
    m_tasklist_caption_sizer->Add(GROUP_TITLE_RIGHT_MARGIN - GROUP_TITLE_LINE_MARGIN, 0, 0, wxEXPAND, 0);

    m_tasklist_sizer->Add(m_tasklist_caption_sizer, 0, wxEXPAND | wxALL, 0);

    show_task_list_info(false);
}

void StatusPanel::show_task_list_info(bool show)
{
    if (show) {
        m_tasklist_sizer->Show(m_tasklist_caption_sizer);
    } else {
        m_tasklist_sizer->Hide(m_tasklist_caption_sizer);
    }
    Layout();

}

void StatusPanel::update_tasklist_info()
{
    clean_tasklist_info();

    // BBS do not show tasklist
    return;
}

void StatusPanel::on_subtask_pause_resume(wxCommandEvent &event)
{
    if (obj) {
        if (obj->can_resume())
            obj->command_task_resume();
        else
            obj->command_task_pause();
    }
}

void StatusPanel::on_subtask_abort(wxCommandEvent &event)
{
    if (obj) obj->command_task_abort();
}

void StatusPanel::error_info_reset()
{
    m_staticline->Hide();
    m_panel_error_txt->Hide();
    m_panel_error_txt->GetParent()->Layout();
    m_error_text->SetLabel("");
    before_error_code = 0;
}

void StatusPanel::on_subtask_clean(wxCommandEvent &event)
{
    error_info_reset();
}

void StatusPanel::on_webrequest_state(wxWebRequestEvent &evt)
{
    BOOST_LOG_TRIVIAL(trace) << "monitor: monitor_panel web request state = " << evt.GetState();
    switch (evt.GetState()) {
    case wxWebRequest::State_Completed: {
        wxImage img(*evt.GetResponse().GetStream());
        img_list.insert(std::make_pair(m_request_url, img));
        wxImage resize_img = img.Scale(m_bitmap_thumbnail->GetSize().x, m_bitmap_thumbnail->GetSize().y, wxIMAGE_QUALITY_HIGH);
        m_bitmap_thumbnail->SetBitmap(resize_img);
        break;
    }
    case wxWebRequest::State_Failed: {
        break;
    }
    case wxWebRequest::State_Cancelled:
    case wxWebRequest::State_Unauthorized:
    case wxWebRequest::State_Active:
    case wxWebRequest::State_Idle: break;
    default: break;
    }
}

bool StatusPanel::is_task_changed(MachineObject* obj)
{
    if (!obj)
        return false;

    if (last_subtask != obj->subtask_
        || last_profile_id != obj->profile_id_
        || last_task_id != obj->task_id_
        ) {
        last_subtask = obj->subtask_;
        last_profile_id = obj->profile_id_;
        last_task_id = obj->task_id_;
        return true;
    }
    return false;
}

void StatusPanel::update(MachineObject *obj)
{
    if (!obj) return;

    m_project_task_panel->Freeze();
    update_subtask(obj);
    m_project_task_panel->Thaw();


    m_machine_ctrl_panel->Freeze();

    if (obj->is_in_printing() && !obj->can_resume())
        show_printing_status(false, true);
    else
        show_printing_status();


    update_temp_ctrl(obj);
    update_misc_ctrl(obj);

    // BBS hide tasklist info
    // update_tasklist(obj);
    update_ams(obj);

    update_cali(obj);

    if (obj) {
        if (calibration_dlg == nullptr) {
            calibration_dlg = new CalibrationDialog();
            calibration_dlg->update_machine_obj(obj);
        } else {
            calibration_dlg->update_machine_obj(obj);
        }
        calibration_dlg->update_cali(obj);

        if (print_options_dlg == nullptr) {
            print_options_dlg = new PrintOptionsDialog(this);
            print_options_dlg->update_machine_obj(obj);
        } else {
            print_options_dlg->update_machine_obj(obj);
        }
        print_options_dlg->update_options(obj);
      
        update_error_message();
    }

    upodate_camera_state(obj->has_recording(), obj->has_timelapse(), obj->has_sdcard());
    m_machine_ctrl_panel->Thaw();
}

void StatusPanel::show_error_message(wxString msg)
{
    m_error_text->SetLabel(msg);
    m_staticline->Show();
    m_panel_error_txt->Show();
}

void StatusPanel::update_error_message()
{
    if (obj->print_error <= 0) {
        before_error_code = obj->print_error;

        if (m_panel_error_txt->IsShown())
            error_info_reset();
        return;
    }

    if (before_error_code != obj->print_error) {
        if (wxGetApp().get_hms_query()) {
            char buf[32];
            ::sprintf(buf, "%08X", obj->print_error);
            std::string print_error_str = std::string(buf);
            if (print_error_str.size() > 4) {
                print_error_str.insert(4, " ");
            }
            wxString error_msg = wxString::Format("%s[%s]",
                                 wxGetApp().get_hms_query()->query_print_error_msg(obj->print_error),
                                 print_error_str);
            show_error_message(error_msg);
        }
        before_error_code        = obj->print_error;
   }
}

void StatusPanel::show_printing_status(bool ctrl_area, bool temp_area)
{
    if (!ctrl_area) {
        m_bpButton_xy->Enable(false);
        m_bpButton_z_10->Enable(false);
        m_bpButton_z_1->Enable(false);
        m_bpButton_z_down_1->Enable(false);
        m_bpButton_z_down_10->Enable(false);
        m_bpButton_e_10->Enable(false);
        m_bpButton_e_down_10->Enable(false);
        m_staticText_z_tip->SetForegroundColour(DISCONNECT_TEXT_COL);
        m_staticText_e->SetForegroundColour(DISCONNECT_TEXT_COL);
        m_button_unload->Enable(false);
        m_switch_speed->SetValue(false);
    } else {
        m_switch_speed->Enable();
        m_switch_lamp->Enable();
        m_switch_nozzle_fan->Enable();
        m_switch_printing_fan->Enable();
        m_bpButton_xy->Enable();
        m_text_tasklist_caption->SetForegroundColour(GROUP_TITLE_FONT_COL);
        m_bpButton_z_10->Enable();
        m_bpButton_z_1->Enable();
        m_bpButton_z_down_1->Enable();
        m_bpButton_z_down_10->Enable();
        m_bpButton_e_10->Enable();
        m_bpButton_e_down_10->Enable();
        m_staticText_z_tip->SetForegroundColour(TEXT_LIGHT_FONT_COL);
        m_staticText_e->SetForegroundColour(TEXT_LIGHT_FONT_COL);
        m_button_unload->Enable();
        m_switch_speed->SetValue(true);
    }

    if (!temp_area) {
        m_tempCtrl_nozzle->Enable(false);
        m_tempCtrl_bed->Enable(false);
        m_tempCtrl_frame->Enable(false);
        m_switch_speed->Enable(false);
        m_switch_speed->SetValue(false);
        m_switch_lamp->Enable(false);
        m_switch_nozzle_fan->Enable(false);
        m_switch_printing_fan->Enable(false);
    } else {
        m_tempCtrl_nozzle->Enable();
        m_tempCtrl_bed->Enable();
        m_tempCtrl_frame->Enable();
        m_switch_speed->Enable();
        m_switch_speed->SetValue(true);
        m_switch_lamp->Enable();
        m_switch_nozzle_fan->Enable();
        m_switch_printing_fan->Enable();
    }
}

void StatusPanel::update_temp_ctrl(MachineObject *obj)
{
    if (!obj) return;

    m_tempCtrl_bed->SetCurrTemp((int) obj->bed_temp);

    // update temprature if not input temp target
    if (m_temp_bed_timeout > 0) {
        m_temp_bed_timeout--;
    } else {
        if (!bed_temp_input) { m_tempCtrl_bed->SetTagTemp((int) obj->bed_temp_target); }
    }

    if ((obj->bed_temp_target - obj->bed_temp) >= TEMP_THRESHOLD_VAL) {
        m_tempCtrl_bed->SetIconActive();
    } else {
        m_tempCtrl_bed->SetIconNormal();
    }

    m_tempCtrl_nozzle->SetCurrTemp((int) obj->nozzle_temp);

    if (m_temp_nozzle_timeout > 0) {
        m_temp_nozzle_timeout--;
    } else {
        if (!nozzle_temp_input) { m_tempCtrl_nozzle->SetTagTemp((int) obj->nozzle_temp_target); }
    }

    if ((obj->nozzle_temp_target - obj->nozzle_temp) >= TEMP_THRESHOLD_VAL) {
        m_tempCtrl_nozzle->SetIconActive();
    } else {
        m_tempCtrl_nozzle->SetIconNormal();
    }

    m_tempCtrl_frame->SetCurrTemp(obj->chamber_temp);
    m_tempCtrl_frame->SetTagTemp(obj->chamber_temp);
}

void StatusPanel::update_misc_ctrl(MachineObject *obj)
{
    if (!obj) return;

    if (obj->has_ams()) {
        if (m_button_unload->IsShown()) {
            m_button_unload->Hide();
            m_button_unload->GetParent()->Layout();
        }
       
    } else {
        if (!m_button_unload->IsShown()) {
            m_button_unload->Show();
            m_button_unload->GetParent()->Layout();
        } 
    }

    // nozzle fan
    if (m_switch_nozzle_fan_timeout > 0)
        m_switch_nozzle_fan_timeout--;
    else
        m_switch_nozzle_fan->SetValue(obj->cooling_fan_speed > 0);

    // printing fan
    if (m_switch_printing_fan_timeout > 0)
        m_switch_printing_fan_timeout--;
    else
        m_switch_printing_fan->SetValue(obj->big_fan1_speed > 0);

    bool light_on = obj->chamber_light != MachineObject::LIGHT_EFFECT::LIGHT_EFFECT_OFF;
    BOOST_LOG_TRIVIAL(trace) << "light: " << light_on ? "on" : "off";
    if (m_switch_lamp_timeout > 0)
        m_switch_lamp_timeout--;
    else {
        m_switch_lamp->SetValue(light_on);
        /*wxString label = light_on ? "On" : "Off";
        m_switch_lamp->SetLabels(label, label);*/
    }

    if (speed_lvl_timeout > 0)
        speed_lvl_timeout--;
    else {
        // update speed
        this->speed_lvl = obj->printing_speed_lvl;
        wxString text_speed = wxString::Format("%d%%", obj->printing_speed_mag);
        m_switch_speed->SetLabels(text_speed, text_speed);
    }
}

void StatusPanel::update_ams(MachineObject *obj)
{
    // update obj in sub dlg
    if (m_ams_setting_dlg) { m_ams_setting_dlg->obj = obj; }
    if (m_filament_setting_dlg) { m_filament_setting_dlg->obj = obj; }

    if (!obj || !obj->is_connected()) {
        last_tray_exist_bits  = -1;
        last_ams_exist_bits   = -1;
        last_tray_is_bbl_bits = -1;
        last_read_done_bits   = -1;
        last_ams_version      = -1;
        m_ams_control->EnterNoneAMSMode();
        show_ams_group(false);
        BOOST_LOG_TRIVIAL(trace) << "machine object" << obj->dev_name << " was disconnected, set show_ams_group is false";
        return;
    }

    if (obj->amsList.empty() || obj->ams_exist_bits == 0) {
        m_ams_control->EnterNoneAMSMode();
        show_ams_group(false);
        BOOST_LOG_TRIVIAL(trace) << "machine object" << obj->dev_name << " ams nonexistent, set show_ams_group is false";
        return;
    } else {
        show_ams_group(true);
        if (m_filament_setting_dlg) m_filament_setting_dlg->update();

        std::vector<AMSinfo> ams_info;
        for (auto ams = obj->amsList.begin(); ams != obj->amsList.end(); ams++) {
            AMSinfo info;
            info.ams_id = ams->first;
            if (ams->second->is_exists && info.parse_ams_info(ams->second)) ams_info.push_back(info);
        }
        //if (obj->ams_exist_bits != last_ams_exist_bits || obj->tray_exist_bits != last_tray_exist_bits || obj->tray_is_bbl_bits != last_tray_is_bbl_bits ||
        //    obj->tray_read_done_bits != last_read_done_bits || obj->ams_version != last_ams_version) {
        //    m_ams_control->UpdateAms(ams_info, false);
        //    // select current ams
        //    //if (!obj->m_ams_id.empty()) m_ams_control->SwitchAms(obj->m_ams_id);

        //    last_tray_exist_bits  = obj->tray_exist_bits;
        //    last_ams_exist_bits   = obj->ams_exist_bits;
        //    last_tray_is_bbl_bits = obj->tray_is_bbl_bits;
        //    last_read_done_bits   = obj->tray_read_done_bits;
        //    last_ams_version      = obj->ams_version;
        //}
       
        // select current ams
        // if (!obj->m_ams_id.empty()) m_ams_control->SwitchAms(obj->m_ams_id);

        m_ams_control->UpdateAms(ams_info, false);
        last_tray_exist_bits  = obj->tray_exist_bits;
        last_ams_exist_bits   = obj->ams_exist_bits;
        last_tray_is_bbl_bits = obj->tray_is_bbl_bits;
        last_read_done_bits   = obj->tray_read_done_bits;
        last_ams_version      = obj->ams_version;
    }

    if (!obj->is_ams_unload()) {
        ; // TODO set filament step to load
    } else {
        ; // TODO set filament step to unload
    }

    std::string curr_ams_id = m_ams_control->GetCurentAms();
    std::string curr_can_id = m_ams_control->GetCurrentCan(curr_ams_id);

    if (m_ams_control->GetCurentAms() != obj->m_ams_id) {
        m_ams_control->SetAmsStep(curr_ams_id, curr_can_id, AMSPassRoadType::AMS_ROAD_TYPE_LOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    } else {
        if (obj->ams_status_main == AMS_STATUS_MAIN_FILAMENT_CHANGE) {
            // wait to heat hotend
            if (obj->ams_status_sub == 0x02) {
                
                if (curr_ams_id == obj->m_ams_id) {
                    if (!obj->is_ams_unload()) {
                        m_ams_control->SetFilamentStep(FilamentStep::STEP_HEAT_NOZZLE, true);
                        m_ams_control->SetAmsStep(curr_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_LOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
                    } else {
                        m_ams_control->SetFilamentStep(FilamentStep::STEP_HEAT_NOZZLE, false);
                        m_ams_control->SetAmsStep(curr_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
                    }
                }
            } else if (obj->ams_status_sub == 0x03) {
                if (!obj->is_ams_unload()) {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_CUT_FILAMENT, true);
                    m_ams_control->SetAmsStep(curr_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_LOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1);
                }
                else {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_CUT_FILAMENT, false);
                    m_ams_control->SetAmsStep(curr_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3);
                }
                    
            } else if (obj->ams_status_sub == 0x04) {
                if (!obj->is_ams_unload()) {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_PULL_CURR_FILAMENT, true);
                    m_ams_control->SetAmsStep(curr_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_LOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2);
                }   
                else {
                    //FilamentStep::STEP_PULL_CURR_FILAMENT);
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_PULL_CURR_FILAMENT, false);
                    m_ams_control->SetAmsStep(curr_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
                }
            } else if (obj->ams_status_sub == 0x05) {
                if (!obj->is_ams_unload()) {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_PUSH_NEW_FILAMENT, true);
                    m_ams_control->SetAmsStep(curr_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_LOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2);
                } 
                else {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_PUSH_NEW_FILAMENT, false);
                    m_ams_control->SetAmsStep(curr_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
                }
            } else if (obj->ams_status_sub == 0x06) {
                if (!obj->is_ams_unload()) {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_PUSH_NEW_FILAMENT, true);
                    m_ams_control->SetAmsStep(curr_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_LOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3);
                } else {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_PUSH_NEW_FILAMENT, false);
                    m_ams_control->SetAmsStep(curr_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
                }
            } else if (obj->ams_status_sub == 0x07) {
                if (!obj->is_ams_unload()) { 
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_PURGE_OLD_FILAMENT);
                } else {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_PURGE_OLD_FILAMENT, false);
                }
                
                m_ams_control->SetAmsStep(curr_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_LOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3);
            } else {
                m_ams_control->SetAmsStep(curr_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
            }
        } else if (obj->ams_status_main == AMS_STATUS_MAIN_ASSIST) {
            m_ams_control->SetFilamentStep(FilamentStep::STEP_IDLE);
            if (obj->is_filament_move()) {
                m_ams_control->SetAmsStep(curr_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3);
            } else {
                m_ams_control->SetAmsStep(curr_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
            }
        } else {
            m_ams_control->SetFilamentStep(FilamentStep::STEP_IDLE, false);
            if (obj->is_filament_move()) {
                m_ams_control->SetAmsStep(curr_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3);
            } else {
                m_ams_control->SetAmsStep(curr_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
            }
        }
    }

    for (auto ams_it = obj->amsList.begin(); ams_it != obj->amsList.end(); ams_it++) {
        std::string ams_id = ams_it->first;
        try {
            int ams_id_int = atoi(ams_id.c_str());
            for (auto tray_it = ams_it->second->trayList.begin(); tray_it != ams_it->second->trayList.end(); tray_it++) {
                std::string tray_id     = tray_it->first;
                int         tray_id_int = atoi(tray_id.c_str());
                if ((obj->tray_read_done_bits & (1 << (ams_id_int * 4 + tray_id_int))) == 0) {
                    m_ams_control->PlayRridLoading(ams_id, tray_id);
                } else {
                    m_ams_control->StopRridLoading(ams_id, tray_id);
                }
            }
        } catch (...) {}
    }
    // update rfid button style

    // update load/unload enable state
    if (obj->is_in_printing() && !obj->can_resume()) {
        m_ams_control->SetActionState(AMSAction::AMS_ACTION_PRINTING);
    } else {
        if (obj->ams_status_main != AMS_STATUS_MAIN_FILAMENT_CHANGE) {
            if (obj->m_tray_now == "255") {
                m_ams_control->SetActionState(AMSAction::AMS_ACTION_LOAD);
            } else {
                m_ams_control->SetActionState(AMSAction::AMS_ACTION_NORMAL);
            }
        } else {
            m_ams_control->SetActionState(AMSAction::AMS_ACTION_PRINTING);
        }
    }
}

void StatusPanel::update_cali(MachineObject *obj)
{
    if (!obj) return;

    if (obj->is_in_calibration()) {
        m_calibration_btn->SetLabel(_L("Calibrating"));
        if (calibration_dlg && calibration_dlg->IsShown()) {
            m_calibration_btn->Disable();
        } else {
            m_calibration_btn->Enable();
        }
    } else {
        // IDLE
        m_calibration_btn->SetLabel(_L("Calibration"));
        // disable in printing
        if (obj->is_in_printing()) {
            m_calibration_btn->Disable();
        } else {
            m_calibration_btn->Enable();
        }
    }
}

void StatusPanel::update_left_time(int mc_left_time)
{
    // update gcode progress
    std::string left_time;
    wxString    left_time_text = NA_STR;

    try {
        left_time = get_bbl_monitor_time_dhm(mc_left_time);
    } catch (...) {
        ;
    }
    if (!left_time.empty()) left_time_text = wxString::Format("-%s", left_time);

    // update current subtask progress
    m_staticText_progress_left->SetLabelText(left_time_text);
}

void StatusPanel::update_basic_print_data(bool def) 
{
    if (def) {
        auto       aprint_stats = wxGetApp().plater()->get_partplate_list().get_current_fff_print().print_statistics();
        wxString   time;
        PartPlate *plate = wxGetApp().plater()->get_partplate_list().get_curr_plate();
        if (plate) {
            if (plate->get_slice_result()) { time = wxString::Format("%s", get_bbl_monitor_time_dhm(plate->get_slice_result()->print_statistics.modes[0].time)); }
        }

        char weight[64];
        ::sprintf(weight, "  %.2f g", aprint_stats.total_weight);

        m_staticText_consumption_of_time->SetLabelText(time);
        m_staticText_consumption_of_weight->SetLabelText(weight);
    } else {
        m_staticText_consumption_of_time->SetLabelText("0m");
        m_staticText_consumption_of_weight->SetLabelText("0g");
    }
}

void StatusPanel::update_subtask(MachineObject *obj)
{
    if (!obj) return;

    if (obj->is_system_printing()) {
        reset_printing_values();
    } else if (obj->is_in_printing() || obj->print_status == "FINISH") {
        if (obj->is_in_prepare()) {
            m_button_abort->Enable(false);
            m_button_abort->SetBitmap_("print_control_stop_disable");

            m_button_pause_resume->Enable(false);
            m_button_pause_resume->SetBitmap_("print_control_pause_disable");

            wxString prepare_text = wxString::Format(_L("Downloading..."));
            if (obj->gcode_file_prepare_percent >= 0 && obj->gcode_file_prepare_percent <= 100)
                prepare_text += wxString::Format("(%d%%)", obj->gcode_file_prepare_percent);
            m_printing_stage_value->SetLabelText(prepare_text);
            m_gauge_progress->SetValue(0);
            m_staticText_progress_percent->SetLabelText(NA_STR);
            m_staticText_progress_percent_icon->SetLabelText(wxEmptyString);
            m_staticText_progress_left->SetLabel(NA_STR);
            m_staticText_progress_left->SetLabelText(NA_STR);
            wxString subtask_text = wxString::Format("%s", GUI::from_u8(obj->subtask_name));
            m_staticText_subtask_value->SetLabelText(subtask_text);
            update_basic_print_data(true);
        } else {
            if (obj->can_resume()) {
                m_button_pause_resume->SetBitmap_("print_control_resume");
                if (m_button_pause_resume->GetToolTipText() != _L("Resume")) { m_button_pause_resume->SetToolTip(_L("Resume")); }
            } else {
                m_button_pause_resume->SetBitmap_("print_control_pause");
                if (m_button_pause_resume->GetToolTipText() != _L("Pause")) { m_button_pause_resume->SetToolTip(_L("Pause")); }
            }
                

            if (obj->print_status == "FINISH") {
                m_button_abort->Enable(false);
                m_button_abort->SetBitmap_("print_control_stop_disable");
                m_button_pause_resume->Enable(false);
                m_button_pause_resume->SetBitmap_("print_control_resume_disable");
            } else {
                m_button_abort->Enable(true);
                m_button_abort->SetBitmap_("print_control_stop");
                m_button_pause_resume->Enable(true);
            }
            // update printing stage
            m_printing_stage_value->SetLabelText(obj->get_curr_stage());
            update_left_time(obj->mc_left_time);
            if (obj->subtask_) {
                m_gauge_progress->SetValue(obj->subtask_->task_progress);
                m_staticText_progress_percent->SetLabelText(wxString::Format("%d", obj->subtask_->task_progress));
                m_staticText_progress_percent_icon->SetLabelText("%");
            } else {
                m_gauge_progress->SetValue(0);
                m_staticText_progress_percent->SetLabelText(NA_STR);
                m_staticText_progress_percent_icon->SetLabelText(wxEmptyString);
            }
        }
        wxString subtask_text = wxString::Format("%s", GUI::from_u8(obj->subtask_name));
        m_staticText_subtask_value->SetLabelText(subtask_text);
        update_basic_print_data(true);
        //update thumbnail
        if (obj->is_sdcard_printing()) {
            update_sdcard_subtask(obj);
        } else {
            update_cloud_subtask(obj);
        }
    } else {
        reset_printing_values();
    }

    this->Layout();
}

void StatusPanel::update_cloud_subtask(MachineObject *obj)
{
    if (!obj) return;
    if (!obj->subtask_) return;

    if (is_task_changed(obj)) {
        reset_printing_values();
        BOOST_LOG_TRIVIAL(trace) << "monitor: change to sub task id = " << obj->subtask_->task_id;
        if (web_request.IsOk()) web_request.Cancel();
        m_start_loading_thumbnail = true;
    }

    if (m_start_loading_thumbnail) {
        if (obj->slice_info) {
            m_request_url = wxString(obj->slice_info->thumbnail_url);
            if (!m_request_url.IsEmpty()) {
                wxImage                               img;
                std::map<wxString, wxImage>::iterator it = img_list.find(m_request_url);
                if (it != img_list.end()) {
                    img                = it->second;
                    wxImage resize_img = img.Scale(m_bitmap_thumbnail->GetSize().x, m_bitmap_thumbnail->GetSize().y);
                    m_bitmap_thumbnail->SetBitmap(resize_img);
                } else {
                    web_request = wxWebSession::GetDefault().CreateRequest(this, m_request_url);
                    BOOST_LOG_TRIVIAL(trace) << "monitor: start reqeust thumbnail, url = " << m_request_url;
                    web_request.Start();
                    m_start_loading_thumbnail = false;
                }
            }
        }
    }
}

void StatusPanel::update_sdcard_subtask(MachineObject *obj)
{
    if (!obj) return;

    if (!m_load_sdcard_thumbnail) {
        m_bitmap_thumbnail->SetBitmap(m_thumbnail_sdcard.bmp());
        m_load_sdcard_thumbnail = true;
    }
}

void StatusPanel::reset_printing_values()
{
    m_button_pause_resume->Enable(false);
    m_button_pause_resume->SetBitmap_("print_control_pause_disable");

    m_button_abort->Enable(false);
    m_button_abort->SetBitmap_("print_control_stop_disable");

    m_gauge_progress->SetValue(0);
    m_staticText_subtask_value->SetLabelText(NA_STR);
    update_basic_print_data(false);
    m_printing_stage_value->SetLabelText("");
    m_staticText_progress_left->SetLabelText(NA_STR);
    m_staticText_progress_percent->SetLabelText(NA_STR);
    m_staticText_progress_percent_icon->SetLabelText(wxEmptyString);
    m_bitmap_thumbnail->SetBitmap(m_thumbnail_placeholder.bmp());
    m_start_loading_thumbnail = false;
    m_load_sdcard_thumbnail   = false;
    this->Layout();
}

void StatusPanel::on_axis_ctrl_xy(wxCommandEvent &event)
{
    if (!obj) return;
    if (event.GetInt() == 0) { obj->command_axis_control("Y", 1.0, 10.0f, 3000); }
    if (event.GetInt() == 1) { obj->command_axis_control("X", 1.0, -10.0f, 3000); }
    if (event.GetInt() == 2) { obj->command_axis_control("Y", 1.0, -10.0f, 3000); }
    if (event.GetInt() == 3) { obj->command_axis_control("X", 1.0, 10.0f, 3000); }
    if (event.GetInt() == 4) { obj->command_axis_control("Y", 1.0, 1.0f, 3000); }
    if (event.GetInt() == 5) { obj->command_axis_control("X", 1.0, -1.0f, 3000); }
    if (event.GetInt() == 6) { obj->command_axis_control("Y", 1.0, -1.0f, 3000); }
    if (event.GetInt() == 7) { obj->command_axis_control("X", 1.0, 1.0f, 3000); }
    if (event.GetInt() == 8) { obj->command_go_home(); }
}

void StatusPanel::on_axis_ctrl_z_up_10(wxCommandEvent &event)
{
    if (obj) obj->command_axis_control("Z", 1.0, -10.0f, 900);
}

void StatusPanel::on_axis_ctrl_z_up_1(wxCommandEvent &event)
{
    if (obj) obj->command_axis_control("Z", 1.0, -1.0f, 900);
}

void StatusPanel::on_axis_ctrl_z_down_1(wxCommandEvent &event)
{
    if (obj) obj->command_axis_control("Z", 1.0, 1.0f, 900);
}

void StatusPanel::on_axis_ctrl_z_down_10(wxCommandEvent &event)
{
    if (obj) obj->command_axis_control("Z", 1.0, 10.0f, 900);
}

void StatusPanel::on_axis_ctrl_e_up_10(wxCommandEvent &event)
{
    if (obj) obj->command_axis_control("E", 1.0, -10.0f, 900);
}

void StatusPanel::on_axis_ctrl_e_down_10(wxCommandEvent &event)
{
    if (obj) obj->command_axis_control("E", 1.0, 10.0f, 900);
}

void StatusPanel::on_start_unload(wxCommandEvent &event)
{
    if (obj) obj->command_unload_filament();
}

void StatusPanel::on_set_bed_temp()
{
    wxString str = m_tempCtrl_bed->GetTextCtrl()->GetValue();
    try {
        long bed_temp;
        if (str.ToLong(&bed_temp) && obj) {
            set_hold_count(m_temp_bed_timeout);
            obj->command_set_bed(bed_temp);
        }
    } catch (...) {
        ;
    }
}

void StatusPanel::on_set_nozzle_temp()
{
    wxString str = m_tempCtrl_nozzle->GetTextCtrl()->GetValue();
    try {
        long nozzle_temp;
        if (str.ToLong(&nozzle_temp) && obj) {
            set_hold_count(m_temp_nozzle_timeout);
            obj->command_set_nozzle(nozzle_temp);
        }
    } catch (...) {
        ;
    }
}

void StatusPanel::on_ams_load(SimpleEvent &event)
{
    if (obj) {
        std::string                            curr_ams_id = m_ams_control->GetCurentAms();
        std::string                            curr_can_id = m_ams_control->GetCurrentCan(curr_ams_id);
        std::map<std::string, Ams *>::iterator it          = obj->amsList.find(curr_ams_id);
        if (it == obj->amsList.end()) {
            BOOST_LOG_TRIVIAL(trace) << "ams: find " << curr_ams_id << " failed";
            return;
        }
        auto tray_it = it->second->trayList.find(curr_can_id);
        if (tray_it == it->second->trayList.end()) {
            BOOST_LOG_TRIVIAL(trace) << "ams: find " << curr_can_id << " failed";
            return;
        }

        AmsTray *curr_tray = obj->get_curr_tray();
        AmsTray *targ_tray = obj->get_ams_tray(curr_ams_id, curr_can_id);
        if (curr_tray && targ_tray) {
            int old_temp = -1;
            int new_temp = -1;
            try {
                if (!curr_tray->nozzle_temp_max.empty() && !curr_tray->nozzle_temp_min.empty())
                    old_temp = (atoi(curr_tray->nozzle_temp_min.c_str()) + atoi(curr_tray->nozzle_temp_max.c_str())) / 2;
                if (!targ_tray->nozzle_temp_max.empty() && !targ_tray->nozzle_temp_min.empty())
                    new_temp = (atoi(targ_tray->nozzle_temp_min.c_str()) + atoi(targ_tray->nozzle_temp_max.c_str())) / 2;
            } catch (...) {
                ;
            }
            int tray_index = atoi(curr_ams_id.c_str()) * 4 + atoi(tray_it->second->id.c_str());
            obj->command_ams_switch(tray_index, old_temp, new_temp);
        } else {
            int tray_index = atoi(curr_ams_id.c_str()) * 4 + atoi(tray_it->second->id.c_str());
            obj->command_ams_switch(tray_index, -1, -1);
        }
    }
}

void StatusPanel::on_ams_unload(SimpleEvent &event)
{
    if (obj) { obj->command_ams_switch(255); }
}

void StatusPanel::on_ams_setting_click(SimpleEvent &event)
{
    if (!m_ams_setting_dlg) m_ams_setting_dlg = new AMSSetting((wxWindow *) this, wxID_ANY);
    if (obj) {
        m_ams_setting_dlg->update_insert_material_read_mode(true);
        m_ams_setting_dlg->update_starting_read_mode(true);
        std::string ams_id = m_ams_control->GetCurentAms();
        try {
            int ams_id_int            = atoi(ams_id.c_str());
            m_ams_setting_dlg->ams_id = ams_id_int;
            m_ams_setting_dlg->Show();
        } catch (...) {
            ;
        }
    }
}

void StatusPanel::on_filament_edit(wxCommandEvent &event)
{
    // update params
    if (!m_filament_setting_dlg) m_filament_setting_dlg = new AMSMaterialsSetting((wxWindow *) this, wxID_ANY);
    if (obj) {
        m_filament_setting_dlg->obj = obj;
        std::string ams_id          = m_ams_control->GetCurentAms();
        std::string tray_id         = event.GetString().ToStdString(); // m_ams_control->GetCurrentCan(ams_id);
        try {
            int ams_id_int                  = atoi(ams_id.c_str());
            int tray_id_int                 = atoi(tray_id.c_str());
            m_filament_setting_dlg->ams_id  = ams_id_int;
            m_filament_setting_dlg->tray_id = tray_id_int;

            std::string sn_number;
            std::string filament;
            std::string temp_max;
            std::string temp_min;
            auto it = obj->amsList.find(ams_id);
            if (it != obj->amsList.end()) {
                auto tray_it = it->second->trayList.find(tray_id);
                if (tray_it != it->second->trayList.end()) {
                    wxColor color = AmsTray::decode_color(tray_it->second->color);
                    m_filament_setting_dlg->set_color(color);
                    m_filament_setting_dlg->ams_filament_id = tray_it->second->setting_id;
                    m_filament_setting_dlg->m_is_third      = !MachineObject::is_bbl_filament(tray_it->second->tag_uid);
                    if (!m_filament_setting_dlg->m_is_third) {
                        sn_number = tray_it->second->uuid;
                        filament = tray_it->second->sub_brands;
                        temp_max = tray_it->second->nozzle_temp_max;
                        temp_min = tray_it->second->nozzle_temp_min;
                    }
                }
            }
            m_filament_setting_dlg->SetPosition(m_ams_control->GetScreenPosition());
            m_filament_setting_dlg->Popup(filament, sn_number, temp_min, temp_max);
        } catch (...) {
            ;
        }
    }
}

void StatusPanel::on_ams_refresh_rfid(wxCommandEvent &event)
{
    if (obj) {
        std::string curr_ams_id = m_ams_control->GetCurentAms();
        std::string curr_can_id = event.GetString().ToStdString();

        std::map<std::string, Ams *>::iterator it = obj->amsList.find(curr_ams_id);
        if (it == obj->amsList.end()) {
            BOOST_LOG_TRIVIAL(trace) << "ams: find " << curr_ams_id << " failed";
            return;
        }
        auto tray_it = it->second->trayList.find(curr_can_id);
        if (tray_it == it->second->trayList.end()) {
            BOOST_LOG_TRIVIAL(trace) << "ams: find " << curr_can_id << " failed";
            return;
        }

        try {
            int tray_index = atoi(curr_ams_id.c_str()) * 4 + atoi(tray_it->second->id.c_str());
            obj->command_ams_refresh_rfid(std::to_string(tray_index));
        } catch (...) {
            ;
        }
    }
}

void StatusPanel::on_ams_selected(wxCommandEvent &event)
{
    if (obj) {
        std::string curr_ams_id = m_ams_control->GetCurentAms();
        std::string curr_can_id = event.GetString().ToStdString();

         std::map<std::string, Ams *>::iterator it = obj->amsList.find(curr_ams_id);
        if (it == obj->amsList.end()) {
            BOOST_LOG_TRIVIAL(trace) << "ams: find " << curr_ams_id << " failed";
            return;
        }
        auto tray_it = it->second->trayList.find(curr_can_id);
        if (tray_it == it->second->trayList.end()) {
            BOOST_LOG_TRIVIAL(trace) << "ams: find " << curr_can_id << " failed";
            return;
        }

        
    try {
            int tray_index = atoi(curr_ams_id.c_str()) * 4 + atoi(tray_it->second->id.c_str());
            obj->command_ams_select_tray(std::to_string(tray_index));
        } catch (...) {
            ;
        }   
    }
}

void StatusPanel::on_bed_temp_kill_focus(wxFocusEvent &event)
{
    event.Skip();
    bed_temp_input = false;
}

void StatusPanel::on_bed_temp_set_focus(wxFocusEvent &event)
{
    event.Skip();
    bed_temp_input = true;
}

void StatusPanel::on_nozzle_temp_kill_focus(wxFocusEvent &event)
{
    event.Skip();
    nozzle_temp_input = false;
}

void StatusPanel::on_nozzle_temp_set_focus(wxFocusEvent &event)
{
    event.Skip();
    nozzle_temp_input = true;
}

void StatusPanel::on_switch_speed(wxCommandEvent &event)
{
    auto now = boost::posix_time::microsec_clock::universal_time();
    if ((now - speed_dismiss_time).total_milliseconds() < 200) {
        speed_dismiss_time = now - boost::posix_time::seconds(1);
        return;
    }
#if __WXOSX__
    // MacOS has focus problem
    wxPopupTransientWindow *popUp = new wxPopupTransientWindow(nullptr);
#else
    wxPopupTransientWindow *popUp = new wxPopupTransientWindow(m_switch_speed);
#endif
    popUp->SetBackgroundColour(0xeeeeee);
    StepCtrl *step = new StepCtrl(popUp, wxID_ANY);
    wxSizer *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(step, 1, wxEXPAND, 0);
    popUp->SetSizer(sizer);
    auto em = em_unit(this);
    popUp->SetSize(em * 36, em * 8);

    step->AppendItem(_L("Silent"), "");
    step->AppendItem(_L("Standard"), "");
    step->AppendItem(_L("Sport"), "");
    step->AppendItem(_L("Ludicrous"), "");
    
    // default speed lvl
    int selected_item = 1;
    if (obj) {
        int speed_lvl_idx = obj->printing_speed_lvl - 1;
        if (speed_lvl_idx >= 0 && speed_lvl_idx < 4) {
            selected_item = speed_lvl_idx;
        }
    }
    step->SelectItem(selected_item);
    
    step->Bind(EVT_STEP_CHANGED, [this](auto &e) {
        this->speed_lvl        = e.GetInt() + 1;
        if (obj) {
            set_hold_count(this->speed_lvl_timeout);
            obj->command_set_printing_speed((PrintingSpeedLevel)this->speed_lvl);
        }
    });
    popUp->Bind(wxEVT_SHOW, [this](auto &e) {
        if (!e.IsShown()) {
            wxGetApp().CallAfter([popUp = e.GetEventObject()] { delete popUp; });
            speed_dismiss_time = boost::posix_time::microsec_clock::universal_time();
        }
    });
    wxPoint pos = m_switch_speed->ClientToScreen(wxPoint(0, -6));
    popUp->Position(pos, {0, m_switch_speed->GetSize().y + 12});
    popUp->Popup();
}

void StatusPanel::on_printing_fan_switch(wxCommandEvent &event)
{
    if (!obj) return;

    bool value = m_switch_printing_fan->GetValue();

    if (value) {
        obj->command_control_fan(MachineObject::FanType::BIG_COOLING_FAN, true);
        m_switch_printing_fan->SetValue(true);
        set_hold_count(this->m_switch_printing_fan_timeout);
    } else {
        obj->command_control_fan(MachineObject::FanType::BIG_COOLING_FAN, false);
        m_switch_printing_fan->SetValue(false);
        set_hold_count(this->m_switch_printing_fan_timeout);
    }
}

void StatusPanel::on_nozzle_fan_switch(wxCommandEvent &event)
{
    if (!obj) return;

    bool value = m_switch_nozzle_fan->GetValue();

    if (value) {
        obj->command_control_fan(MachineObject::FanType::COOLING_FAN, true);
        m_switch_nozzle_fan->SetValue(true);
        set_hold_count(this->m_switch_nozzle_fan_timeout);
    } else {
        obj->command_control_fan(MachineObject::FanType::COOLING_FAN, false);
        m_switch_nozzle_fan->SetValue(false);
        set_hold_count(this->m_switch_nozzle_fan_timeout);
    }
}
void StatusPanel::on_lamp_switch(wxCommandEvent &event)
{
    if (!obj) return;

    bool value = m_switch_lamp->GetValue();

    if (value) {
        m_switch_lamp->SetValue(true);
        // do not update when timeout > 0
        set_hold_count(this->m_switch_lamp_timeout);
        obj->command_set_chamber_light(MachineObject::LIGHT_EFFECT::LIGHT_EFFECT_ON);
    } else {
        m_switch_lamp->SetValue(false);
        set_hold_count(this->m_switch_lamp_timeout);
        obj->command_set_chamber_light(MachineObject::LIGHT_EFFECT::LIGHT_EFFECT_OFF);
    }
}

void StatusPanel::on_thumbnail_enter(wxMouseEvent &event)
{
    if (obj) {
        if (!obj->slice_info || !obj->subtask_) return;
        /* do not popup when print status is failed */
        if (obj->print_status.compare("FAILED") == 0) {
            return;
        }

        if (m_slice_info_popup && m_slice_info_popup->IsShown())
            return;
        if (obj->slice_info) {
            m_slice_info_popup = std::make_shared<SliceInfoPopup>(this, m_bitmap_thumbnail->GetBitmap(), obj->slice_info);
            wxWindow *ctrl     = (wxWindow *) event.GetEventObject();
            wxPoint   pos      = ctrl->ClientToScreen(wxPoint(0, 0));
            wxSize    sz       = ctrl->GetSize();
            m_slice_info_popup->Position(pos, wxSize(sz.x / 2, sz.y / 2));
            m_slice_info_popup->Popup();
        }
    }
}

void StatusPanel::on_thumbnail_leave(wxMouseEvent &event)
{
    if (obj && m_slice_info_popup) {
        if (!m_bitmap_thumbnail->GetRect().Contains(event.GetPosition())) {
            m_slice_info_popup->Dismiss();
        }
    }
}

void StatusPanel::on_switch_recording(wxMouseEvent &event)
{
    if (!obj) return;
    bool value = m_recording_button->get_switch_status();
    obj->command_ipcam_record(!value);
}

void StatusPanel::on_camera_enter(wxMouseEvent& event)
{
    if (obj) {
        m_camera_popup = std::make_shared<CameraPopup>(this, obj);
        wxWindow* ctrl = (wxWindow*)event.GetEventObject();
        wxPoint   pos = ctrl->ClientToScreen(wxPoint(0, 0));
        wxSize    sz = ctrl->GetSize();
        m_camera_popup->Position(pos, wxSize(sz.x, sz.y));
        m_camera_popup->Popup();
    }
}

void StatusPanel::on_camera_leave(wxMouseEvent& event)
{
    if (obj && m_camera_popup) {
        m_camera_popup->Dismiss();
    }
}

void StatusPanel::on_auto_leveling(wxCommandEvent &event)
{
    if (obj) obj->command_auto_leveling();
}

void StatusPanel::on_xyz_abs(wxCommandEvent &event)
{
    if (obj) obj->command_xyz_abs();
}

void StatusPanel::on_show_print_options(wxCommandEvent &event)
{
    if (obj) {
        if (print_options_dlg == nullptr) {
            print_options_dlg = new PrintOptionsDialog(this);
            print_options_dlg->update_machine_obj(obj);
            print_options_dlg->ShowModal();
        }
        else {
            print_options_dlg->update_machine_obj(obj);
            print_options_dlg->ShowModal();
        }
    }
}

void StatusPanel::on_start_calibration(wxCommandEvent &event)
{
    if (obj) {
        if (calibration_dlg == nullptr) {
            calibration_dlg = new CalibrationDialog();
            calibration_dlg->update_machine_obj(obj);
            calibration_dlg->ShowModal();
        } else {
            calibration_dlg->update_machine_obj(obj);
            calibration_dlg->ShowModal();
        }
    }
}

bool StatusPanel::is_stage_list_info_changed(MachineObject *obj)
{
    if (!obj) return true;

    if (last_stage_list_info.size() != obj->stage_list_info.size()) return true;

    for (int i = 0; i < last_stage_list_info.size(); i++) {
        if (last_stage_list_info[i] != obj->stage_list_info[i]) return true;
    }
    last_stage_list_info = obj->stage_list_info;
    return false;
}

void StatusPanel::set_default()
{
    BOOST_LOG_TRIVIAL(trace) << "status_panel: set_default";
    obj                  = nullptr;
    last_subtask         = nullptr;
    last_tray_exist_bits = -1;
    speed_lvl         = 1;
    speed_lvl_timeout = 0;
    m_switch_lamp_timeout = 0;
    m_temp_nozzle_timeout = 0;
    m_temp_bed_timeout = 0;
    m_switch_nozzle_fan_timeout = 0;
    m_switch_printing_fan_timeout = 0;
    m_show_ams_group = false;
    reset_printing_values();

    reset_temp_misc_control();
    m_ams_control->Hide();
    m_ams_control_box->Hide();
    m_ams_control->Reset();
    clean_tasklist_info();
    error_info_reset();
}

void StatusPanel::show_status(int status)
{
    if (last_status == status) return;
    last_status = status;

    if (((status & (int) MonitorStatus::MONITOR_DISCONNECTED) != 0)
        || ((status & (int) MonitorStatus::MONITOR_DISCONNECTED_SERVER) != 0)
        || ((status & (int)MonitorStatus::MONITOR_CONNECTING) != 0)
        ) {
        m_text_tasklist_caption->SetForegroundColour(DISCONNECT_TEXT_COL);
        show_printing_status(false, false);
        m_calibration_btn->Disable();
        m_options_btn->Disable();
    } else if ((status & (int) MonitorStatus::MONITOR_NORMAL) != 0) {
        show_printing_status(true, true);
        m_calibration_btn->Disable();
        m_options_btn->Enable();
    }
}

void StatusPanel::set_hold_count(int& count)
{
    if (obj) {
        if (obj->is_U0_firmware()) {
        count = COMMAND_TIMEOUT_U0;
        }
    }
    count = COMMAND_TIMEOUT;
}

void StatusPanel::msw_rescale()
{
    init_bitmaps();

    m_panel_monitoring_title->SetSize(wxSize(-1, FromDIP(PAGE_TITLE_HEIGHT)));
    m_bmToggleBtn_timelapse->Rescale();
    m_panel_printing_title->SetSize(wxSize(-1, FromDIP(PAGE_TITLE_HEIGHT)));
    m_bitmap_thumbnail->SetSize(TASK_THUMBNAIL_SIZE);
    m_printing_sizer->SetMinSize(wxSize(PAGE_MIN_WIDTH, -1));
    m_gauge_progress->SetHeight(PROGRESSBAR_HEIGHT);
    m_panel_control_title->SetSize(wxSize(-1, FromDIP(PAGE_TITLE_HEIGHT)));
    m_bpButton_xy->SetBitmap(m_bitmap_axis_home);
    m_bpButton_xy->SetMinSize(AXIS_MIN_SIZE);
    m_bpButton_xy->SetSize(AXIS_MIN_SIZE);
    m_temp_extruder_line->SetSize(wxSize(FromDIP(1), -1));
    m_bitmap_extruder_img->SetBitmap(m_bitmap_extruder);
    m_bitmap_extruder_img->SetMinSize(EXTRUDER_IMAGE_SIZE);

    for (Button *btn : m_buttons) { btn->Rescale(); }
    init_scaled_buttons();

    m_gauge_progress->Rescale();

    for (int i = 0; i < slice_info_list.size(); i++) {
        slice_info_list[i]->SetImages(m_bitmap_item_prediction, m_bitmap_item_cost, m_bitmap_item_print);
        slice_info_list[i]->msw_rescale();
    }

    //m_bitmap_camera_img->SetBitmap(m_bitmap_camera);
    //m_bitmap_camera_img->SetMinSize(wxSize(FromDIP(32), FromDIP(18)));

    m_timelapse_button->SetMinSize(wxSize(38, 24));
    m_recording_button->SetMinSize(wxSize(38, 24));
    m_bitmap_sdcard_off_img->SetMinSize(wxSize(FromDIP(38), FromDIP(24)));
    m_bitmap_sdcard_on_img->SetMinSize(wxSize(FromDIP(38), FromDIP(24)));

    m_bpButton_xy->Rescale();
    m_tempCtrl_nozzle->SetMinSize(TEMP_CTRL_MIN_SIZE);
    m_tempCtrl_nozzle->Rescale();
    m_line_nozzle->SetSize(wxSize(-1, FromDIP(1)));
    m_tempCtrl_bed->SetMinSize(TEMP_CTRL_MIN_SIZE);
    m_tempCtrl_bed->Rescale();
    m_tempCtrl_frame->SetMinSize(TEMP_CTRL_MIN_SIZE);
    m_tempCtrl_frame->Rescale();

    m_switch_speed->SetImages(m_bitmap_speed, m_bitmap_speed);
    m_switch_speed->SetMinSize(MISC_BUTTON_SIZE);
    m_switch_speed->Rescale();
    m_switch_lamp->SetImages(m_bitmap_lamp_on, m_bitmap_lamp_off);
    m_switch_lamp->SetMinSize(MISC_BUTTON_SIZE);
    m_switch_lamp->Rescale();
    m_switch_nozzle_fan->SetImages(m_bitmap_fan_on, m_bitmap_fan_off);
    m_switch_nozzle_fan->SetMinSize(MISC_BUTTON_SIZE);
    m_switch_nozzle_fan->Rescale();
    m_switch_printing_fan->SetImages(m_bitmap_fan_on, m_bitmap_fan_off);
    m_switch_printing_fan->SetMinSize(MISC_BUTTON_SIZE);
    m_switch_printing_fan->Rescale();

    m_ams_control->msw_rescale();
    // m_filament_step->Rescale();

    m_calibration_btn->SetMinSize(wxSize(-1, FromDIP(26)));
    m_calibration_btn->Rescale();

    m_options_btn->SetMinSize(wxSize(-1, FromDIP(26)));
    m_options_btn->Rescale();

    Layout();
    Refresh();
}

}} // namespace Slic3r::GUI
