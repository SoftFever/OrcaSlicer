#include "StatusPanel.hpp"

#include "I18N.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/StepCtrl.hpp"
#include "Widgets/SideTools.hpp"
#include "Widgets/WebView.hpp"

#include "BitmapCache.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"

#include "MsgDialog.hpp"
#include "slic3r/Utils/Http.hpp"
#include "libslic3r/Thread.hpp"

#include "RecenterDialog.hpp"
#include "CalibUtils.hpp"
#include <slic3r/GUI/Widgets/ProgressDialog.hpp>
#include <wx/display.h>
#include <wx/mstream.h>
#include <wx/sstream.h>
#include <wx/zstream.h>


namespace Slic3r { namespace GUI {

#define TEMP_THRESHOLD_VAL 2
#define TEMP_THRESHOLD_ALLOW_E_CTRL 170.0f

/* const strings */
static const wxString NA_STR         = _L("N/A");
static const wxString TEMP_BLANK_STR = wxString("_");
static const wxFont   SWITCH_FONT    = Label::Body_10;

/* const values */
static const int bed_temp_range[2]    = {20, 120};
static const int nozzle_temp_range[2] = {20, 300};
static const int nozzle_chamber_range[2] = {20, 60};

/* colors */
static const wxColour STATUS_PANEL_BG     = wxColour(238, 238, 238);
static const wxColour STATUS_TITLE_BG     = wxColour(248, 248, 248);
static const wxColour STATIC_BOX_LINE_COL = wxColour(238, 238, 238);

static const wxColour BUTTON_NORMAL1_COL = wxColour(238, 238, 238);
static const wxColour BUTTON_NORMAL2_COL = wxColour(206, 206, 206);
static const wxColour BUTTON_PRESS_COL   = wxColour(172, 172, 172);
static const wxColour BUTTON_HOVER_COL   = wxColour(0, 150, 136);

static const wxColour DISCONNECT_TEXT_COL = wxColour(171, 172, 172);
static const wxColour NORMAL_TEXT_COL     = wxColour(48,58,60);
static const wxColour NORMAL_FAN_TEXT_COL = wxColour(107, 107, 107);
static const wxColour WARNING_INFO_BG_COL = wxColour(255, 111, 0);
static const wxColour STAGE_TEXT_COL      = wxColour(0, 150, 136);

static const wxColour GROUP_STATIC_LINE_COL = wxColour(206, 206, 206);

/* font and foreground colors */
static const wxFont PAGE_TITLE_FONT  = Label::Body_14;
//static const wxFont GROUP_TITLE_FONT = Label::sysFont(17);

static wxColour PAGE_TITLE_FONT_COL  = wxColour(107, 107, 107);
static wxColour GROUP_TITLE_FONT_COL = wxColour(172, 172, 172);
static wxColour TEXT_LIGHT_FONT_COL  = wxColour(107, 107, 107);

static std::vector<std::string> message_containing_retry{
    "0701 8004",
    "0701 8005",
    "0701 8006",
    "0701 8006",
    "0701 8007",
    "0700 8012",
    "0701 8012",
    "0702 8012",
    "0703 8012",
    "07FF 8003",
    "07FF 8004",
    "07FF 8005",
    "07FF 8006",
    "07FF 8007",
    "07FF 8010",
    "07FF 8011",
    "07FF 8012",
    "07FF 8013",
    "12FF 8007",
    "1200 8006"
};

static std::vector<std::string> message_containing_done{
    "07FF 8007",
    "12FF 8007"
};

static std::vector<std::string> message_containing_resume{
    "0300 8013"
};

static wxImage fail_image;


/* size */
#define PAGE_TITLE_HEIGHT FromDIP(36)
#define PAGE_TITLE_TEXT_WIDTH FromDIP(200)
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
#define MISC_BUTTON_PANEL_SIZE (wxSize(FromDIP(136), FromDIP(55)))
#define MISC_BUTTON_1FAN_SIZE (wxSize(FromDIP(132), FromDIP(51)))
#define MISC_BUTTON_2FAN_SIZE (wxSize(FromDIP(66), FromDIP(51)))
#define MISC_BUTTON_3FAN_SIZE (wxSize(FromDIP(44), FromDIP(51)))
#define TEMP_CTRL_MIN_SIZE (wxSize(FromDIP(122), FromDIP(52)))
#define AXIS_MIN_SIZE (wxSize(FromDIP(220), FromDIP(220)))
#define EXTRUDER_IMAGE_SIZE (wxSize(FromDIP(48), FromDIP(76)))

static void market_model_scoring_page(int design_id)
{
    std::string url;
    std::string country_code   = GUI::wxGetApp().app_config->get_country_code();
    std::string model_http_url = GUI::wxGetApp().get_model_http_url(country_code);
    if (GUI::wxGetApp().getAgent()->get_model_mall_detail_url(&url, std::to_string(design_id)) == 0) {
        std::string user_id = GUI::wxGetApp().getAgent()->get_user_id();
        boost::algorithm::replace_first(url, "models", "u/" + user_id + "/rating");
        // Prevent user_id from containing design_id
        size_t      sign_in = url.find("/rating");
        std::string sub_url = url.substr(0, sign_in + 7);
        url.erase(0, sign_in + 7);
        boost::algorithm::replace_first(url, std::to_string(design_id), "");
        url = sub_url + url;
        try {
            if (!url.empty()) { wxLaunchDefaultBrowser(url); }
        } catch (...) {
            return;
        }
    }
}

PrintingTaskPanel::PrintingTaskPanel(wxWindow* parent, PrintingTaskType type)
    : wxPanel(parent, wxID_ANY,wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL)
{
    m_type = type;
    create_panel(this);
    SetBackgroundColour(*wxWHITE);
    m_bitmap_background = ScalableBitmap(this, "thumbnail_grid", m_bitmap_thumbnail->GetSize().y);

    m_bitmap_thumbnail->Bind(wxEVT_PAINT, &PrintingTaskPanel::paint, this);
}

PrintingTaskPanel::~PrintingTaskPanel()
{
}

void PrintingTaskPanel::create_panel(wxWindow* parent)
{
    wxBoxSizer *sizer                 = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *bSizer_printing_title = new wxBoxSizer(wxHORIZONTAL);

    m_panel_printing_title = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, PAGE_TITLE_HEIGHT), wxTAB_TRAVERSAL);
    m_panel_printing_title->SetBackgroundColour(STATUS_TITLE_BG);

    m_staticText_printing = new wxStaticText(m_panel_printing_title, wxID_ANY ,_L("Printing Progress"));
    m_staticText_printing->Wrap(-1);
    //m_staticText_printing->SetFont(PAGE_TITLE_FONT);
    m_staticText_printing->SetForegroundColour(PAGE_TITLE_FONT_COL);

    bSizer_printing_title->Add(m_staticText_printing, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, PAGE_TITLE_LEFT_MARGIN);
    bSizer_printing_title->Add(0, 0, 1, wxEXPAND, 0);

    m_panel_printing_title->SetSizer(bSizer_printing_title);
    m_panel_printing_title->Layout();
    bSizer_printing_title->Fit(m_panel_printing_title);

    m_bitmap_thumbnail = new wxStaticBitmap(parent, wxID_ANY, m_thumbnail_placeholder.bmp(), wxDefaultPosition, TASK_THUMBNAIL_SIZE, 0);
    m_bitmap_thumbnail->SetMaxSize(TASK_THUMBNAIL_SIZE);
    m_bitmap_thumbnail->SetMinSize(TASK_THUMBNAIL_SIZE);

    wxBoxSizer *bSizer_subtask_info = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *bSizer_task_name = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *bSizer_task_name_hor = new wxBoxSizer(wxHORIZONTAL);
    wxPanel*    task_name_panel      = new wxPanel(parent);

    m_staticText_subtask_value = new wxStaticText(task_name_panel, wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT | wxST_ELLIPSIZE_END);
    m_staticText_subtask_value->SetMaxSize(wxSize(FromDIP(600), -1));
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
    bSizer_task_name_hor->Add(m_bitmap_static_use_time, 0, wxALIGN_CENTER_VERTICAL, 0);
    bSizer_task_name_hor->Add(m_staticText_consumption_of_time, 0, wxALIGN_CENTER_VERTICAL|wxLEFT, FromDIP(3));
    bSizer_task_name_hor->Add(0, 0, 0, wxLEFT, FromDIP(10));
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
    m_printing_stage_value->SetMaxSize(wxSize(FromDIP(800),-1));
    #ifdef __WXOSX_MAC__
    m_printing_stage_value->SetFont(::Label::Body_11);
    #else
    m_printing_stage_value->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
    #endif

    m_printing_stage_value->SetForegroundColour(STAGE_TEXT_COL);


    m_staticText_profile_value = new wxStaticText(parent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT | wxST_ELLIPSIZE_END);
    m_staticText_profile_value->Wrap(-1);
#ifdef __WXOSX_MAC__
    m_staticText_profile_value->SetFont(::Label::Body_11);
#else
    m_staticText_profile_value->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
#endif

    m_staticText_profile_value->SetForegroundColour(0x6B6B6B);


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
    m_staticText_progress_percent->SetForegroundColour(wxColour(0, 150, 136));

    m_staticText_progress_percent_icon = new wxStaticText(penel_text, wxID_ANY, L("%"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_progress_percent_icon->SetFont(::Label::Body_11);
    m_staticText_progress_percent_icon->SetMaxSize(wxSize(-1, FromDIP(13)));
    m_staticText_progress_percent_icon->SetForegroundColour(wxColour(0, 150, 136));

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

    // Orca: display the end time of the print
    m_staticText_progress_end = new wxStaticText(penel_text, wxID_ANY, L("N/A"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_progress_end->Wrap(-1);
    m_staticText_progress_end->SetFont(
        wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
    m_staticText_progress_end->SetForegroundColour(wxColour(146, 146, 146));

    //fgSizer_task->Add(bSizer_buttons, 0, wxEXPAND, 0);
    //fgSizer_task->Add(0, 0, 0, wxEXPAND, FromDIP(5));

    wxPanel* panel_button_block = new wxPanel(penel_bottons, wxID_ANY);
    panel_button_block->SetMinSize(wxSize(TASK_BUTTON_SIZE.x * 2 + FromDIP(5) * 4, -1));
    panel_button_block->SetMinSize(wxSize(TASK_BUTTON_SIZE.x * 2 + FromDIP(5) * 4, -1));
    panel_button_block->SetSize(wxSize(TASK_BUTTON_SIZE.x * 2 + FromDIP(5) * 2, -1));
    panel_button_block->SetBackgroundColour(*wxWHITE);

    m_staticText_layers = new wxStaticText(penel_text, wxID_ANY, _L("Layer: N/A"));
    m_staticText_layers->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
    m_staticText_layers->SetForegroundColour(wxColour(146, 146, 146));
    m_staticText_layers->Hide();

    //bSizer_text->Add(m_staticText_progress_percent, 0,  wxALL, 0);
    bSizer_text->Add(sizer_percent, 0, wxEXPAND, 0);
    bSizer_text->Add(sizer_percent_icon, 0, wxEXPAND, 0);
    bSizer_text->Add(0, 0, 1, wxEXPAND, 0);
    bSizer_text->Add(m_staticText_layers, 0, wxALIGN_CENTER | wxALL, 0);
    bSizer_text->Add(0, 0, 0, wxLEFT, FromDIP(20));
    bSizer_text->Add(m_staticText_progress_left, 0, wxALIGN_CENTER | wxALL, 0);
    // Orca: display the end time of the print
    bSizer_text->Add(0, 0, 0, wxLEFT, FromDIP(8));
    bSizer_text->Add(m_staticText_progress_end, 0, wxALIGN_CENTER | wxALL, 0);

    penel_text->SetMaxSize(wxSize(FromDIP(600), -1));
    penel_text->SetSizer(bSizer_text);
    penel_text->Layout();

    bSizer_buttons->Add(penel_text, 1, wxEXPAND | wxALL, 0);
    bSizer_buttons->Add(panel_button_block, 0, wxALIGN_CENTER | wxALL, 0);

    penel_bottons->SetSizer(bSizer_buttons);
    penel_bottons->Layout();

    bSizer_subtask_info->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(14));
    bSizer_subtask_info->Add(bSizer_task_name, 0, wxEXPAND|wxRIGHT, FromDIP(18));
    bSizer_subtask_info->Add(m_staticText_profile_value, 0, wxEXPAND | wxTOP, FromDIP(5));
    bSizer_subtask_info->Add(m_printing_stage_value, 0, wxEXPAND | wxTOP, FromDIP(5));
    bSizer_subtask_info->Add(penel_bottons, 0, wxEXPAND | wxTOP, FromDIP(10));
    bSizer_subtask_info->Add(m_panel_progress, 0, wxEXPAND|wxRIGHT, FromDIP(25));


    m_printing_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_printing_sizer->SetMinSize(wxSize(PAGE_MIN_WIDTH, -1));
    m_printing_sizer->Add(m_bitmap_thumbnail, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxLEFT, FromDIP(12));
    m_printing_sizer->Add(FromDIP(8), 0, 0, wxEXPAND, 0);
    m_printing_sizer->Add(bSizer_subtask_info, 1, wxALL | wxEXPAND, 0);


    m_staticline = new wxPanel( parent, wxID_ANY);
    m_staticline->SetBackgroundColour(wxColour(238,238,238));
    m_staticline->Layout();
    m_staticline->Hide();

    m_panel_error_txt = new wxPanel(parent, wxID_ANY);
    m_panel_error_txt->SetBackgroundColour(*wxWHITE);

    wxBoxSizer *static_text_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_error_text = new Label(m_panel_error_txt, "", LB_AUTO_WRAP);
    m_error_text->SetForegroundColour(wxColour(255, 0, 0));
    static_text_sizer->Add(m_error_text, 1, wxEXPAND | wxLEFT, FromDIP(17));

    m_button_clean = new Button(m_panel_error_txt, _L("Clear"));
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

    static_text_sizer->Add( FromDIP(10), 0, 0, 0, 0 );
    static_text_sizer->Add(m_button_clean, 0, wxALIGN_CENTRE_VERTICAL|wxRIGHT, FromDIP(5));

    m_panel_error_txt->SetSizer(static_text_sizer);
    m_panel_error_txt->Hide();

    sizer->Add(m_panel_printing_title, 0, wxEXPAND | wxALL, 0);
    sizer->Add(0, FromDIP(12), 0);
    sizer->Add(m_printing_sizer, 0, wxEXPAND | wxALL, 0);
    sizer->Add(0, 0, 0, wxTOP, FromDIP(15));
    sizer->Add(m_staticline, 0, wxEXPAND | wxALL, FromDIP(10));
    sizer->Add(m_panel_error_txt, 0, wxEXPAND | wxALL, 0);
    sizer->Add(0, FromDIP(12), 0);

    m_score_staticline = new wxPanel(parent, wxID_ANY);
    m_score_staticline->SetBackgroundColour(wxColour(238, 238, 238));
    m_score_staticline->Layout();
    m_score_staticline->Hide();
    sizer->Add(0, 0, 0, wxTOP, FromDIP(15));
    sizer->Add(m_score_staticline, 0, wxEXPAND | wxALL, FromDIP(10));
    m_request_failed_panel    = new wxPanel(parent, wxID_ANY);
    m_request_failed_panel->SetBackgroundColour(*wxWHITE);
    wxBoxSizer *static_request_failed_panel_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_request_failed_info = new wxStaticText(m_request_failed_panel, wxID_ANY, _L("You have completed printing the mall model, \nbut the synchronization of rating information has failed."), wxDefaultPosition, wxDefaultSize, 0);
    m_request_failed_info->Wrap(-1);
    m_request_failed_info->SetForegroundColour(*wxRED);
    m_request_failed_info->SetFont(::Label::Body_10);
    static_request_failed_panel_sizer->Add(m_request_failed_info, 0, wxEXPAND | wxALL, FromDIP(10));
    StateColor btn_bg_green(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled), std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed),
                            std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered), std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
    StateColor btn_bd_green(std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Disabled), std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Enabled));
    m_button_market_retry = new Button(m_request_failed_panel, _L("Retry"));
    m_button_market_retry->SetBackgroundColor(btn_bg_green);
    m_button_market_retry->SetBorderColor(btn_bd_green);
    m_button_market_retry->SetTextColor(wxColour("#FFFFFE"));
    m_button_market_retry->SetSize(wxSize(FromDIP(128), FromDIP(26)));
    m_button_market_retry->SetMinSize(wxSize(-1, FromDIP(26)));
    m_button_market_retry->SetCornerRadius(FromDIP(13));
    static_request_failed_panel_sizer->Add(0, 0, 1, wxEXPAND, 0);
    static_request_failed_panel_sizer->Add(m_button_market_retry, 0, wxEXPAND | wxALL, FromDIP(10));
    m_request_failed_panel->SetSizer(static_request_failed_panel_sizer);
    m_request_failed_panel->Hide();
    sizer->Add(m_request_failed_panel, 0, wxEXPAND | wxALL, FromDIP(10));


    m_score_subtask_info = new wxPanel(parent, wxID_ANY);
    m_score_subtask_info->SetBackgroundColour(*wxWHITE);

    wxBoxSizer *  static_score_sizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_score_text  = new wxStaticText(m_score_subtask_info, wxID_ANY, _L("How do you like this printing file?"), wxDefaultPosition, wxDefaultSize, 0);
    static_score_text->Wrap(-1);
    static_score_sizer->Add(static_score_text, 1, wxEXPAND | wxALL, FromDIP(10));
    m_has_rated_prompt = new wxStaticText(m_score_subtask_info, wxID_ANY, _L("(The model has already been rated. Your rating will overwrite the previous rating.)"), wxDefaultPosition, wxDefaultSize, 0);
    m_has_rated_prompt->Wrap(-1);
    m_has_rated_prompt->SetForegroundColour(*wxBLACK);
    m_has_rated_prompt->SetFont(::Label::Body_10);
    m_has_rated_prompt->Hide();

    m_star_count                        = 0;
    wxBoxSizer *static_score_star_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_score_star.resize(5);
    for (int i = 0; i < m_score_star.size(); ++i) {
        m_score_star[i] = new ScalableButton(m_score_subtask_info, wxID_ANY, "score_star_dark", wxEmptyString, wxSize(FromDIP(26), FromDIP(26)), wxDefaultPosition,
                                             wxBU_EXACTFIT | wxNO_BORDER, true, 26);
        m_score_star[i]->Bind(wxEVT_LEFT_DOWN, [this, i](auto &e) {
            for (int j = 0; j < m_score_star.size(); ++j) {
                ScalableBitmap light_star = ScalableBitmap(nullptr, "score_star_light", 26);
                m_score_star[j]->SetBitmap(light_star.bmp());
                if (m_score_star[j] == m_score_star[i]) {
                    m_star_count = j + 1;
                    break;
                }
            }
            for (int k = m_star_count; k < m_score_star.size(); ++k) {
                ScalableBitmap dark_star = ScalableBitmap(nullptr, "score_star_dark", 26);
                m_score_star[k]->SetBitmap(dark_star.bmp());
            }
            m_star_count_dirty = true;
            m_button_market_scoring->Enable(true);
        });
        static_score_star_sizer->Add(m_score_star[i], 0, wxEXPAND | wxLEFT, FromDIP(10));
    }

    m_button_market_scoring = new Button(m_score_subtask_info, _L("Rate"));
    m_button_market_scoring->SetBackgroundColor(btn_bg_green);
    m_button_market_scoring->SetBorderColor(btn_bd_green);
    m_button_market_scoring->SetTextColor(wxColour("#FFFFFE"));
    m_button_market_scoring->SetSize(wxSize(FromDIP(128), FromDIP(26)));
    m_button_market_scoring->SetMinSize(wxSize(-1, FromDIP(26)));
    m_button_market_scoring->SetCornerRadius(FromDIP(13));
    m_button_market_scoring->Enable(false);

    static_score_star_sizer->Add(0, 0, 1, wxEXPAND, 0);
    static_score_star_sizer->Add(m_button_market_scoring, 0, wxEXPAND | wxRIGHT, FromDIP(10));
    static_score_sizer->Add(static_score_star_sizer, 0, wxEXPAND, FromDIP(10));
    static_score_sizer->Add(m_has_rated_prompt, 1, wxEXPAND | wxALL, FromDIP(10));

    m_score_subtask_info->SetSizer(static_score_sizer);
    m_score_subtask_info->Layout();
    m_score_subtask_info->Hide();

    sizer->Add(m_score_subtask_info, 0, wxEXPAND | wxALL, 0);
    sizer->Add(0, FromDIP(12), 0);

    if (m_type == CALIBRATION) {
        m_panel_printing_title->Hide();
        m_bitmap_thumbnail->Hide();
        task_name_panel->Hide();
        m_staticText_profile_value->Hide();
    }

    parent->SetSizer(sizer);
    parent->Layout();
    parent->Fit();
}

void PrintingTaskPanel::paint(wxPaintEvent&)
{
    wxPaintDC dc(m_bitmap_thumbnail);
    if (wxGetApp().dark_mode()) {
        if (m_brightness_value > 0 && m_brightness_value < SHOW_BACKGROUND_BITMAP_PIXEL_THRESHOLD) {
            dc.DrawBitmap(m_bitmap_background.bmp(), 0, 0);
            dc.SetTextForeground(*wxBLACK);
        }
        else
            dc.SetTextForeground(*wxWHITE);
    }
    else
        dc.SetTextForeground(*wxBLACK);
    dc.DrawBitmap(m_thumbnail_bmp_display, wxPoint(0, 0));
    dc.SetFont(Label::Body_12);
    
    if (m_plate_index >= 0) {
        wxString plate_id_str = wxString::Format("%d", m_plate_index);
        dc.DrawText(plate_id_str, wxPoint(4, 4));
    }
}

void PrintingTaskPanel::set_has_reted_text(bool has_rated)
{
    if (has_rated) {
        m_has_rated_prompt->Show();
    } else {
        m_has_rated_prompt->Hide();
    }
    Layout();
    Fit();
}

void PrintingTaskPanel::msw_rescale()
{
    m_panel_printing_title->SetSize(wxSize(-1, FromDIP(PAGE_TITLE_HEIGHT)));
    m_printing_sizer->SetMinSize(wxSize(PAGE_MIN_WIDTH, -1));
    //m_staticText_printing->SetMinSize(wxSize(PAGE_TITLE_TEXT_WIDTH, PAGE_TITLE_HEIGHT));
    m_gauge_progress->SetHeight(PROGRESSBAR_HEIGHT);
    m_gauge_progress->Rescale();
    m_button_abort->msw_rescale();
    m_bitmap_thumbnail->SetSize(TASK_THUMBNAIL_SIZE);
}

void PrintingTaskPanel::init_bitmaps()
{
    m_thumbnail_placeholder     = ScalableBitmap(this, "monitor_placeholder", 120);
    m_bitmap_use_time           = ScalableBitmap(this, "print_info_time", 16);
    m_bitmap_use_weight         = ScalableBitmap(this, "print_info_weight", 16);
}

void PrintingTaskPanel::init_scaled_buttons()
{
    m_button_clean->SetMinSize(wxSize(FromDIP(48), FromDIP(24)));
    m_button_clean->SetCornerRadius(FromDIP(12));
}

void PrintingTaskPanel::error_info_reset()
{
    if (m_panel_error_txt->IsShown()) {
        m_staticline->Hide();
        m_panel_error_txt->Hide();
        m_panel_error_txt->GetParent()->Layout();
        m_error_text->SetLabel(wxEmptyString);
    }
}

void PrintingTaskPanel::show_error_msg(wxString msg)
{
    m_staticline->Show();
    m_panel_error_txt->Show();
    m_error_text->SetLabel(msg);
}

void PrintingTaskPanel::reset_printing_value()
{
    this->set_thumbnail_img(m_thumbnail_placeholder.bmp());
    this->set_plate_index(-1);
}

void PrintingTaskPanel::enable_pause_resume_button(bool enable, std::string type)
{
    if (!enable) {
        m_button_pause_resume->Enable(false);

        if (type == "pause_disable") {
            m_button_pause_resume->SetBitmap_("print_control_pause_disable");
        }
        else if (type == "resume_disable") {
            m_button_pause_resume->SetBitmap_("print_control_resume_disable");
        }
    }
    else {
        m_button_pause_resume->Enable(true);
        if (type == "resume") {
        m_button_pause_resume->SetBitmap_("print_control_resume");
        if (m_button_pause_resume->GetToolTipText() != _L("Resume")) { m_button_pause_resume->SetToolTip(_L("Resume")); }
        }
        else if (type == "pause") {
        m_button_pause_resume->SetBitmap_("print_control_pause");
        if (m_button_pause_resume->GetToolTipText() != _L("Pause")) { m_button_pause_resume->SetToolTip(_L("Pause")); }
        }
    }
}

void PrintingTaskPanel::enable_abort_button(bool enable)
{
    if (!enable) {
        m_button_abort->Enable(false);
        m_button_abort->SetBitmap_("print_control_stop_disable");
    }
    else {
        m_button_abort->Enable(true);
        m_button_abort->SetBitmap_("print_control_stop");
    }
}

void PrintingTaskPanel::update_subtask_name(wxString name)
{
    m_staticText_subtask_value->SetLabelText(name);
}

void PrintingTaskPanel::update_stage_value(wxString stage, int val)
{
    m_printing_stage_value->SetLabelText(stage);
    m_gauge_progress->SetValue(val);
}

void PrintingTaskPanel::update_progress_percent(wxString percent, wxString icon)
{
    m_staticText_progress_percent->SetLabelText(percent);
    m_staticText_progress_percent_icon->SetLabelText(icon);
}

void PrintingTaskPanel::update_left_time(wxString time)
{
    m_staticText_progress_left->SetLabelText(time);
}

void PrintingTaskPanel::update_left_time(int mc_left_time)
{
    // update gcode progress
    std::string left_time;
    wxString    left_time_text = NA_STR;

    try {
        left_time = get_bbl_monitor_time_dhm(mc_left_time);
    }
    catch (...) {
        ;
    }

    if (!left_time.empty()) left_time_text = wxString::Format("-%s", left_time);
    update_left_time(left_time_text);

    //Update end time
    std::string end_time;
    wxString    end_time_text = NA_STR;
    try {
        end_time = get_bbl_monitor_end_time_dhm(mc_left_time);
    } catch (...) {
        ;
    }
    if (!end_time.empty())
        end_time_text = wxString::Format("%s", end_time);
    else
        end_time_text = NA_STR;

    m_staticText_progress_end->SetLabelText(end_time_text);

}

void PrintingTaskPanel::update_layers_num(bool show, wxString num)
{
    if (show) {
        m_staticText_layers->Show(true);
        m_staticText_layers->SetLabelText(num);
    }
    else {
        m_staticText_layers->Show(false);
        m_staticText_layers->SetLabelText(num);
    }
}

void PrintingTaskPanel::show_priting_use_info(bool show, wxString time /*= wxEmptyString*/, wxString weight /*= wxEmptyString*/)
{
    if (show) {
        if (!m_staticText_consumption_of_time->IsShown()) {
            m_bitmap_static_use_time->Show();
            m_staticText_consumption_of_time->Show();
        }

        if (!m_staticText_consumption_of_weight->IsShown()) {
            m_bitmap_static_use_weight->Show();
            m_staticText_consumption_of_weight->Show();
        }

        m_staticText_consumption_of_time->SetLabelText(time);
        m_staticText_consumption_of_weight->SetLabelText(weight);
    }
    else {
        m_staticText_consumption_of_time->SetLabelText("0m");
        m_staticText_consumption_of_weight->SetLabelText("0g");
        if (m_staticText_consumption_of_time->IsShown()) {
            m_bitmap_static_use_time->Hide();
            m_staticText_consumption_of_time->Hide();
        }

        if (m_staticText_consumption_of_weight->IsShown()) {
            m_bitmap_static_use_weight->Hide();
            m_staticText_consumption_of_weight->Hide();
        }    }
}


void PrintingTaskPanel::show_profile_info(bool show, wxString profile /*= wxEmptyString*/)
{
    if (show) {
        if (!m_staticText_profile_value->IsShown()) { m_staticText_profile_value->Show(); }
        m_staticText_profile_value->SetLabelText(profile);
    }
    else {
        m_staticText_profile_value->SetLabelText(wxEmptyString);
        m_staticText_profile_value->Hide();
    }
}

void PrintingTaskPanel::set_thumbnail_img(const wxBitmap& bmp)
{
    m_thumbnail_bmp_display = bmp;
}

void PrintingTaskPanel::set_plate_index(int plate_idx)
{
    m_plate_index = plate_idx;
}

void PrintingTaskPanel::market_scoring_show()
{ 
    m_score_staticline->Show();
    m_score_subtask_info->Show();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " show market scoring page";
}

void PrintingTaskPanel::market_scoring_hide()
{
    m_score_staticline->Hide();
    m_score_subtask_info->Hide();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " hide market scoring page";
}

void PrintingTaskPanel::set_star_count(int star_count)
{
    m_star_count = star_count;

    for (int i = 0; i < m_score_star.size(); ++i) {
        if (i < star_count) {
            ScalableBitmap light_star = ScalableBitmap(nullptr, "score_star_light", 26);
            m_score_star[i]->SetBitmap(light_star.bmp());
        } else {
            ScalableBitmap dark_star = ScalableBitmap(nullptr, "score_star_dark", 26);
            m_score_star[i]->SetBitmap(dark_star.bmp());
        }
    }
}

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

    m_project_task_panel = new PrintingTaskPanel(this, PrintingTaskType::PRINGINT);
    m_project_task_panel->init_bitmaps();
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

StatusBasePanel::~StatusBasePanel()
{
    delete m_media_play_ctrl;

    if (m_custom_camera_view) {
        delete m_custom_camera_view;
        m_custom_camera_view = nullptr;
    }
}

void StatusBasePanel::init_bitmaps()
{
    static Slic3r::GUI::BitmapCache cache;
    m_bitmap_item_prediction = create_scaled_bitmap("monitor_item_prediction", nullptr, 16);
    m_bitmap_item_cost       = create_scaled_bitmap("monitor_item_cost", nullptr, 16);
    m_bitmap_item_print      = create_scaled_bitmap("monitor_item_print", nullptr, 18);
    m_bitmap_axis_home       = ScalableBitmap(this, "monitor_axis_home", 32);
    m_bitmap_lamp_on         = ScalableBitmap(this, "monitor_lamp_on", 24);
    m_bitmap_lamp_off        = ScalableBitmap(this, "monitor_lamp_off", 24);
    m_bitmap_fan_on          = ScalableBitmap(this, "monitor_fan_on", 22);
    m_bitmap_fan_off         = ScalableBitmap(this, "monitor_fan_off", 22);
    m_bitmap_speed           = ScalableBitmap(this, "monitor_speed", 24);
    m_bitmap_speed_active    = ScalableBitmap(this, "monitor_speed_active", 24);
    
    m_thumbnail_brokenimg    = ScalableBitmap(this, "monitor_brokenimg", 120);
    m_thumbnail_sdcard       = ScalableBitmap(this, "monitor_sdcard_thumbnail", 120);
    //m_bitmap_camera          = create_scaled_bitmap("monitor_camera", nullptr, 18);
    m_bitmap_extruder_empty_load      = *cache.load_png("monitor_extruder_empty_load", FromDIP(28), FromDIP(70), false, false);
    m_bitmap_extruder_filled_load     = *cache.load_png("monitor_extruder_filled_load", FromDIP(28), FromDIP(70), false, false);
    m_bitmap_extruder_empty_unload    = *cache.load_png("monitor_extruder_empty_unload", FromDIP(28), FromDIP(70), false, false);
    m_bitmap_extruder_filled_unload   = *cache.load_png("monitor_extruder_filled_unload", FromDIP(28), FromDIP(70), false, false);

    m_bitmap_sdcard_state_abnormal = ScalableBitmap(this, wxGetApp().dark_mode() ? "sdcard_state_abnormal_dark" : "sdcard_state_abnormal", 20);
    m_bitmap_sdcard_state_normal = ScalableBitmap(this, wxGetApp().dark_mode() ? "sdcard_state_normal_dark" : "sdcard_state_normal", 20);
    m_bitmap_sdcard_state_no = ScalableBitmap(this, wxGetApp().dark_mode() ? "sdcard_state_no_dark" : "sdcard_state_no", 20);
    m_bitmap_recording_on = ScalableBitmap(this, wxGetApp().dark_mode() ? "monitor_recording_on_dark" : "monitor_recording_on", 20);
    m_bitmap_recording_off = ScalableBitmap(this, wxGetApp().dark_mode() ? "monitor_recording_off_dark" : "monitor_recording_off", 20);
    m_bitmap_timelapse_on = ScalableBitmap(this, wxGetApp().dark_mode() ? "monitor_timelapse_on_dark" : "monitor_timelapse_on", 20);
    m_bitmap_timelapse_off = ScalableBitmap(this, wxGetApp().dark_mode() ? "monitor_timelapse_off_dark" : "monitor_timelapse_off", 20);
    m_bitmap_vcamera_on = ScalableBitmap(this, wxGetApp().dark_mode() ? "monitor_vcamera_on_dark" : "monitor_vcamera_on", 20);
    m_bitmap_vcamera_off = ScalableBitmap(this, wxGetApp().dark_mode() ? "monitor_vcamera_off_dark" : "monitor_vcamera_off", 20);
    m_bitmap_switch_camera = ScalableBitmap(this, wxGetApp().dark_mode() ? "camera_switch_dark" : "camera_switch", 20);

}

wxBoxSizer *StatusBasePanel::create_monitoring_page()
{
    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

    m_panel_monitoring_title = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, PAGE_TITLE_HEIGHT), wxTAB_TRAVERSAL);
    m_panel_monitoring_title->SetBackgroundColour(STATUS_TITLE_BG);

    wxBoxSizer *bSizer_monitoring_title;
    bSizer_monitoring_title = new wxBoxSizer(wxHORIZONTAL);

    m_staticText_monitoring = new Label(m_panel_monitoring_title, _L("Camera"));
    m_staticText_monitoring->Wrap(-1);
    //m_staticText_monitoring->SetFont(PAGE_TITLE_FONT);
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

#if !BBL_RELEASE_TO_PUBLIC
    m_staticText_timelapse->Show();
    m_bmToggleBtn_timelapse->Show();
    m_bmToggleBtn_timelapse->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &e) {
        if (e.IsChecked())
            wxGetApp().getAgent()->start_subscribe("tunnel");
        else
            wxGetApp().getAgent()->stop_subscribe("tunnel");
    });
#endif

    //m_bitmap_camera_img = new wxStaticBitmap(m_panel_monitoring_title, wxID_ANY, m_bitmap_camera , wxDefaultPosition, wxSize(FromDIP(32), FromDIP(18)), 0);
    //m_bitmap_camera_img->SetMinSize(wxSize(FromDIP(32), FromDIP(18)));
    //bSizer_monitoring_title->Add(m_bitmap_camera_img, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));

    m_bitmap_sdcard_img = new wxStaticBitmap(m_panel_monitoring_title, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(FromDIP(38), FromDIP(24)), 0);
    m_bitmap_sdcard_img->SetMinSize(wxSize(FromDIP(38), FromDIP(24)));

    m_bitmap_timelapse_img = new wxStaticBitmap(m_panel_monitoring_title, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(FromDIP(38), FromDIP(24)), 0);
    m_bitmap_timelapse_img->SetMinSize(wxSize(FromDIP(38), FromDIP(24)));
    m_bitmap_timelapse_img->Hide();

    m_bitmap_recording_img = new wxStaticBitmap(m_panel_monitoring_title, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(FromDIP(38), FromDIP(24)), 0);
    m_bitmap_recording_img->SetMinSize(wxSize(FromDIP(38), FromDIP(24)));
    m_bitmap_timelapse_img->Hide();

    m_bitmap_vcamera_img = new wxStaticBitmap(m_panel_monitoring_title, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(FromDIP(38), FromDIP(24)), 0);
    m_bitmap_vcamera_img->SetMinSize(wxSize(FromDIP(38), FromDIP(24)));
    m_bitmap_vcamera_img->Hide();

    m_setting_button = new CameraItem(m_panel_monitoring_title, "camera_setting", "camera_setting_hover");
    m_setting_button->SetMinSize(wxSize(FromDIP(38), FromDIP(24)));
    m_setting_button->SetBackgroundColour(STATUS_TITLE_BG);

    m_camera_switch_button = new wxStaticBitmap(m_panel_monitoring_title, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(FromDIP(38), FromDIP(24)), 0);
    m_camera_switch_button->SetMinSize(wxSize(FromDIP(38), FromDIP(24)));
    m_camera_switch_button->SetBackgroundColour(STATUS_TITLE_BG);
    m_camera_switch_button->SetBitmap(m_bitmap_switch_camera.bmp());
    m_camera_switch_button->Bind(wxEVT_LEFT_DOWN, &StatusBasePanel::on_camera_switch_toggled, this);
    m_camera_switch_button->Bind(wxEVT_RIGHT_DOWN, [this](auto& e) {
        const std::string js_request_pip = R"(
            document.querySelector('video').requestPictureInPicture();
        )";
        m_custom_camera_view->RunScript(js_request_pip);
    });
    m_camera_switch_button->Hide();

    m_bitmap_sdcard_img->SetToolTip(_L("SD Card"));
    m_bitmap_timelapse_img->SetToolTip(_L("Timelapse"));
    m_bitmap_recording_img->SetToolTip(_L("Video"));
    m_bitmap_vcamera_img->SetToolTip(_L("Go Live"));
    m_setting_button->SetToolTip(_L("Camera Setting"));
    m_camera_switch_button->SetToolTip(_L("Switch Camera View"));

    bSizer_monitoring_title->Add(m_camera_switch_button, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    bSizer_monitoring_title->Add(m_bitmap_sdcard_img, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    bSizer_monitoring_title->Add(m_bitmap_timelapse_img, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    bSizer_monitoring_title->Add(m_bitmap_recording_img, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    bSizer_monitoring_title->Add(m_bitmap_vcamera_img, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    bSizer_monitoring_title->Add(m_setting_button, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));

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

    m_custom_camera_view = WebView::CreateWebView(this, wxEmptyString);
    m_custom_camera_view->EnableContextMenu(false);
    Bind(wxEVT_WEBVIEW_NAVIGATING, &StatusBasePanel::on_webview_navigating, this, m_custom_camera_view->GetId());

    m_media_play_ctrl = new MediaPlayCtrl(this, m_media_ctrl, wxDefaultPosition, wxSize(-1, FromDIP(40)));
    m_custom_camera_view->Hide();
    m_custom_camera_view->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, [this](wxWebViewEvent& evt) {
        if (evt.GetString() == "leavepictureinpicture") {
            // When leaving PiP, video gets paused in some cases and toggling play
            // programmatically does not work.
            m_custom_camera_view->Reload();
        }
        else if (evt.GetString() == "enterpictureinpicture") {
            toggle_builtin_camera();
        }
    });

    sizer->Add(m_media_ctrl, 1, wxEXPAND | wxALL, 0);
    sizer->Add(m_custom_camera_view, 1, wxEXPAND | wxALL, 0);
    sizer->Add(m_media_play_ctrl, 0, wxEXPAND | wxALL, 0);
//    media_ctrl_panel->SetSizer(bSizer_monitoring);
//    media_ctrl_panel->Layout();
//
//    sizer->Add(media_ctrl_panel, 1, wxEXPAND | wxALL, 1);

    if (wxGetApp().app_config->get("camera", "enable_custom_source") == "true") {
        handle_camera_source_change();
    }

    return sizer;
}

void StatusBasePanel::on_webview_navigating(wxWebViewEvent& evt) {
    wxGetApp().CallAfter([this] {
        remove_controls();
    });
}

wxBoxSizer *StatusBasePanel::create_machine_control_page(wxWindow *parent)
{
    wxBoxSizer *bSizer_right = new wxBoxSizer(wxVERTICAL);

    m_panel_control_title = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, PAGE_TITLE_HEIGHT), wxTAB_TRAVERSAL);
    m_panel_control_title->SetBackgroundColour(STATUS_TITLE_BG);

    wxBoxSizer *bSizer_control_title = new wxBoxSizer(wxHORIZONTAL);
    m_staticText_control             = new Label(m_panel_control_title,_L("Control"));
    m_staticText_control->Wrap(-1);
    //m_staticText_control->SetFont(PAGE_TITLE_FONT);
    m_staticText_control->SetForegroundColour(PAGE_TITLE_FONT_COL);

    StateColor btn_bg_green(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled), std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered), std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
    StateColor btn_bd_green(std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Disabled), std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Enabled));

    m_parts_btn = new Button(m_panel_control_title, _L("Printer Parts"));
    m_parts_btn->SetBackgroundColor(btn_bg_green);
    m_parts_btn->SetBorderColor(btn_bd_green);
    m_parts_btn->SetTextColor(wxColour("#FFFFFE"));
    m_parts_btn->SetSize(wxSize(FromDIP(128), FromDIP(26)));
    m_parts_btn->SetMinSize(wxSize(-1, FromDIP(26)));

    m_options_btn = new Button(m_panel_control_title, _L("Print Options"));
    m_options_btn->SetBackgroundColor(btn_bg_green);
    m_options_btn->SetBorderColor(btn_bd_green);
    m_options_btn->SetTextColor(wxColour("#FFFFFE"));
    m_options_btn->SetSize(wxSize(FromDIP(128), FromDIP(26)));
    m_options_btn->SetMinSize(wxSize(-1, FromDIP(26)));


    m_calibration_btn = new Button(m_panel_control_title, _L("Calibration"));
    m_calibration_btn->SetBackgroundColor(btn_bg_green);
    m_calibration_btn->SetBorderColor(btn_bd_green);
    m_calibration_btn->SetTextColor(wxColour("#FFFFFE"));
    m_calibration_btn->SetSize(wxSize(FromDIP(128), FromDIP(26)));
    m_calibration_btn->SetMinSize(wxSize(-1, FromDIP(26)));

    bSizer_control_title->Add(m_staticText_control, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, PAGE_TITLE_LEFT_MARGIN);
    bSizer_control_title->Add(0, 0, 1, wxEXPAND, 0);
    bSizer_control_title->Add(m_parts_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
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
    auto        box           = new StaticBox(parent);

    StateColor box_colour(std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));
    StateColor box_border_colour(std::pair<wxColour, int>(STATUS_PANEL_BG, StateColor::Normal));

    box->SetBackgroundColor(box_colour);
    box->SetBorderColor(box_border_colour);
    box->SetCornerRadius(5);

    box->SetMinSize(wxSize(FromDIP(586), -1));
    box->SetMaxSize(wxSize(FromDIP(586), -1));

    wxBoxSizer *content_sizer = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_temp_ctrl   = create_temp_control(box);


    m_temp_extruder_line = new StaticLine(box, true);
    m_temp_extruder_line->SetLineColour(STATIC_BOX_LINE_COL);


    auto m_axis_sizer = create_axis_control(box);


    wxBoxSizer *bed_sizer = create_bed_control(box);
    wxBoxSizer *extruder_sizer = create_extruder_control(box);

    content_sizer->Add(m_temp_ctrl, 0, wxEXPAND | wxALL, FromDIP(5));
    content_sizer->Add(m_temp_extruder_line, 0, wxEXPAND, 1);
    content_sizer->Add(FromDIP(9), 0, 0, wxEXPAND, 1);
    content_sizer->Add(0, 0, 0, wxLEFT, FromDIP(18));
    content_sizer->Add(m_axis_sizer, 0, wxALIGN_CENTER_VERTICAL | wxALL, 0);
    content_sizer->Add(0, 0, 0, wxLEFT, FromDIP(18));
    content_sizer->Add(bed_sizer, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, FromDIP(12));
    content_sizer->Add(0, 0, 0, wxLEFT, FromDIP(18));
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

    StateColor tempinput_text_colour(std::make_pair(DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(NORMAL_TEXT_COL, (int) StateColor::Normal));
    StateColor tempinput_border_colour(std::make_pair(*wxWHITE, (int)StateColor::Disabled), std::make_pair(BUTTON_HOVER_COL, (int)StateColor::Focused),
        std::make_pair(BUTTON_HOVER_COL, (int)StateColor::Hovered), std::make_pair(*wxWHITE, (int)StateColor::Normal));

    m_tempCtrl_nozzle->SetTextColor(tempinput_text_colour);
    m_tempCtrl_nozzle->SetBorderColor(tempinput_border_colour);

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
    m_tempCtrl_bed->SetTextColor(tempinput_text_colour);
    m_tempCtrl_bed->SetBorderColor(tempinput_border_colour);
    sizer->Add(m_tempCtrl_bed, 0, wxEXPAND | wxALL, 1);

    auto line = new StaticLine(parent);
    line->SetLineColour(STATIC_BOX_LINE_COL);
    sizer->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

    wxWindowID frame_id = wxWindow::NewControlId();
    m_tempCtrl_chamber    = new TempInput(parent, frame_id, TEMP_BLANK_STR, TEMP_BLANK_STR, wxString("monitor_frame_temp"), wxString("monitor_frame_temp_active"), wxDefaultPosition,
                                     wxDefaultSize, wxALIGN_CENTER);
    m_tempCtrl_chamber->SetReadOnly(true);
    m_tempCtrl_chamber->SetMinTemp(nozzle_chamber_range[0]);
    m_tempCtrl_chamber->SetMaxTemp(nozzle_chamber_range[1]);
    m_tempCtrl_chamber->SetMinSize(TEMP_CTRL_MIN_SIZE);
    m_tempCtrl_chamber->SetBorderWidth(FromDIP(2));
    m_tempCtrl_chamber->SetTextColor(tempinput_text_colour);
    m_tempCtrl_chamber->SetBorderColor(tempinput_border_colour);

    sizer->Add(m_tempCtrl_chamber, 0, wxEXPAND | wxALL, 1);
    line = new StaticLine(parent);
    line->SetLineColour(STATIC_BOX_LINE_COL);
    sizer->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

    m_misc_ctrl_sizer = create_misc_control(parent);

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
    m_switch_speed->SetMinSize(MISC_BUTTON_2FAN_SIZE);
    m_switch_speed->SetMaxSize(MISC_BUTTON_2FAN_SIZE);
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
    m_switch_lamp->SetMinSize(MISC_BUTTON_2FAN_SIZE);
    m_switch_lamp->SetMaxSize(MISC_BUTTON_2FAN_SIZE);
    m_switch_lamp->SetPadding(FromDIP(3));
    m_switch_lamp->SetBorderWidth(FromDIP(2));
    m_switch_lamp->SetFont(Label::Head_13);
    m_switch_lamp->SetTextColor(StateColor(std::make_pair(DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(NORMAL_TEXT_COL, (int) StateColor::Normal)));
    line_sizer->Add(m_switch_lamp, 1, wxALIGN_CENTER | wxALL, 0);

    sizer->Add(line_sizer, 0, wxEXPAND, FromDIP(5));
    line = new StaticLine(parent);
    line->SetLineColour(STATIC_BOX_LINE_COL);
    sizer->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

    m_fan_panel = new StaticBox(parent);
    m_fan_panel->SetMinSize(MISC_BUTTON_PANEL_SIZE);
    m_fan_panel->SetMaxSize(MISC_BUTTON_PANEL_SIZE);
    m_fan_panel->SetBackgroundColor(*wxWHITE);
    m_fan_panel->SetBorderWidth(0);
    m_fan_panel->SetCornerRadius(0);

    auto fan_line_sizer          = new wxBoxSizer(wxHORIZONTAL);
    m_switch_nozzle_fan = new FanSwitchButton(m_fan_panel, m_bitmap_fan_on, m_bitmap_fan_off);
    m_switch_nozzle_fan->SetMinSize(MISC_BUTTON_3FAN_SIZE);
    m_switch_nozzle_fan->SetMaxSize(MISC_BUTTON_3FAN_SIZE);
    m_switch_nozzle_fan->SetValue(false);
    m_switch_nozzle_fan->SetLabels(_L("Part"), _L("Part"));
    m_switch_nozzle_fan->SetPadding(FromDIP(1));
    m_switch_nozzle_fan->SetBorderWidth(0);
    m_switch_nozzle_fan->SetCornerRadius(0);
    m_switch_nozzle_fan->SetFont(::Label::Body_10);
    m_switch_nozzle_fan->SetTextColor(StateColor(std::make_pair(DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(NORMAL_FAN_TEXT_COL, (int) StateColor::Normal)));

    m_switch_nozzle_fan->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
        m_fan_panel->SetBackgroundColor(wxColour(0, 150, 136));
    });

    m_switch_nozzle_fan->Bind(wxEVT_LEAVE_WINDOW, [this, parent](auto& e) {
        m_fan_panel->SetBackgroundColor(parent->GetBackgroundColour());
    });

    m_switch_printing_fan = new FanSwitchButton(m_fan_panel, m_bitmap_fan_on, m_bitmap_fan_off);
    m_switch_printing_fan->SetValue(false);
    m_switch_printing_fan->SetMinSize(MISC_BUTTON_3FAN_SIZE);
    m_switch_printing_fan->SetMaxSize(MISC_BUTTON_3FAN_SIZE);
    m_switch_printing_fan->SetPadding(FromDIP(1));
    m_switch_printing_fan->SetBorderWidth(0);
    m_switch_printing_fan->SetCornerRadius(0);
    m_switch_printing_fan->SetFont(::Label::Body_10);
    m_switch_printing_fan->SetLabels(_L("Aux"), _L("Aux"));
    m_switch_printing_fan->SetTextColor(
        StateColor(std::make_pair(DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(NORMAL_FAN_TEXT_COL, (int) StateColor::Normal)));

    m_switch_printing_fan->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
        m_fan_panel->SetBackgroundColor(wxColour(0, 150, 136));
    });

    m_switch_printing_fan->Bind(wxEVT_LEAVE_WINDOW, [this, parent](auto& e) {
        m_fan_panel->SetBackgroundColor(parent->GetBackgroundColour());
    });

    m_switch_cham_fan = new FanSwitchButton(m_fan_panel, m_bitmap_fan_on, m_bitmap_fan_off);
    m_switch_cham_fan->SetValue(false);
    m_switch_cham_fan->SetMinSize(MISC_BUTTON_3FAN_SIZE);
    m_switch_cham_fan->SetMaxSize(MISC_BUTTON_3FAN_SIZE);
    m_switch_cham_fan->SetPadding(FromDIP(1));
    m_switch_cham_fan->SetBorderWidth(0);
    m_switch_cham_fan->SetCornerRadius(0);
    m_switch_cham_fan->SetFont(::Label::Body_10);
    m_switch_cham_fan->SetLabels(_L("Cham"), _L("Cham"));
    m_switch_cham_fan->SetTextColor(
        StateColor(std::make_pair(DISCONNECT_TEXT_COL, (int)StateColor::Disabled), std::make_pair(NORMAL_FAN_TEXT_COL, (int)StateColor::Normal)));

    m_switch_cham_fan->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
        m_fan_panel->SetBackgroundColor(wxColour(0, 150, 136));
    });

    m_switch_cham_fan->Bind(wxEVT_LEAVE_WINDOW, [this, parent](auto& e) {
        m_fan_panel->SetBackgroundColor(parent->GetBackgroundColour());
    });

    //m_switch_block_fan = new wxPanel(m_fan_panel);
    //m_switch_block_fan->SetBackgroundColour(parent->GetBackgroundColour());

    fan_line_sizer->Add(0, 0, 0, wxLEFT, FromDIP(2));
    fan_line_sizer->Add(m_switch_nozzle_fan, 0, wxALIGN_CENTER | wxTOP | wxBOTTOM , FromDIP(2));
    fan_line_sizer->Add(m_switch_printing_fan, 0, wxALIGN_CENTER | wxTOP | wxBOTTOM, FromDIP(2));
    fan_line_sizer->Add(m_switch_cham_fan, 0, wxALIGN_CENTER | wxTOP | wxBOTTOM , FromDIP(2));
    //fan_line_sizer->Add(m_switch_block_fan, 1, wxEXPAND | wxTOP | wxBOTTOM , FromDIP(2));
    fan_line_sizer->Add(0, 0, 0, wxLEFT, FromDIP(2));

    m_fan_panel->SetSizer(fan_line_sizer);
    m_fan_panel->Layout();
    m_fan_panel->Fit();
    sizer->Add(m_fan_panel, 0, wxEXPAND, FromDIP(5));


    return sizer;
}

void StatusBasePanel::reset_temp_misc_control()
{
    // reset temp string
    m_tempCtrl_nozzle->SetLabel(TEMP_BLANK_STR);
    m_tempCtrl_nozzle->GetTextCtrl()->SetValue(TEMP_BLANK_STR);
    m_tempCtrl_bed->SetLabel(TEMP_BLANK_STR);
    m_tempCtrl_bed->GetTextCtrl()->SetValue(TEMP_BLANK_STR);
    m_tempCtrl_chamber->SetLabel(TEMP_BLANK_STR);
    m_tempCtrl_chamber->GetTextCtrl()->SetValue(TEMP_BLANK_STR);
    m_button_unload->Show();

    m_tempCtrl_nozzle->Enable(true);
    m_tempCtrl_chamber->Enable(true);
    m_tempCtrl_bed->Enable(true);

    // reset misc control
    m_switch_speed->SetLabels(_L("100%"), _L("100%"));
    m_switch_speed->SetValue(false);
    m_switch_lamp->SetLabels(_L("Lamp"), _L("Lamp"));
    m_switch_lamp->SetValue(false);
    m_switch_nozzle_fan->SetValue(false);
    m_switch_printing_fan->SetValue(false);
    m_switch_cham_fan->SetValue(false);
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
    if (wxGetApp().app_config->get("language") == "de_DE") m_staticText_z_tip->SetFont(::Label::Body_11);
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

    m_bitmap_extruder_img = new wxStaticBitmap(panel, wxID_ANY, m_bitmap_extruder_empty_load, wxDefaultPosition, wxDefaultSize, 0);
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

    m_ams_control_box = new StaticBox(parent);

    StateColor box_colour(std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));
    StateColor box_border_colour(std::pair<wxColour, int>(STATUS_PANEL_BG, StateColor::Normal));

    m_ams_control_box->SetBackgroundColor(box_colour);
    m_ams_control_box->SetBorderColor(box_border_colour);
    m_ams_control_box->SetCornerRadius(5);

    m_ams_control_box->SetMinSize(wxSize(FromDIP(586), -1));
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
    m_ams_control->Show(true);
    m_ams_control_box->Show(true);
    m_ams_control->show_noams_mode();
    if (m_show_ams_group != show) {
        Fit();
    }
    m_show_ams_group = show;
}

void StatusPanel::update_camera_state(MachineObject* obj)
{
    if (!obj) return;

    //m_bitmap_sdcard_abnormal_img->SetToolTip(_L("SD Card Abnormal"));
    //sdcard
    if (m_last_sdcard != (int)obj->get_sdcard_state()) {
        if (obj->get_sdcard_state() == MachineObject::SdcardState::NO_SDCARD) {
            m_bitmap_sdcard_img->SetBitmap(m_bitmap_sdcard_state_no.bmp());
            m_bitmap_sdcard_img->SetToolTip(_L("No SD Card"));
        } else if (obj->get_sdcard_state() == MachineObject::SdcardState::HAS_SDCARD_NORMAL) {
            m_bitmap_sdcard_img->SetBitmap(m_bitmap_sdcard_state_normal.bmp());
            m_bitmap_sdcard_img->SetToolTip(_L("SD Card"));
        } else if (obj->get_sdcard_state() == MachineObject::SdcardState::HAS_SDCARD_ABNORMAL) {
            m_bitmap_sdcard_img->SetBitmap(m_bitmap_sdcard_state_abnormal.bmp());
            m_bitmap_sdcard_img->SetToolTip(_L("SD Card Abnormal"));
        } else {
            m_bitmap_sdcard_img->SetBitmap(m_bitmap_sdcard_state_normal.bmp());
            m_bitmap_sdcard_img->SetToolTip(_L("SD Card"));
        }
        m_last_sdcard = (int)obj->get_sdcard_state();
    }

    //recording
    if (m_last_recording != (obj->is_recording() ? 1 : 0)) {
        if (obj->is_recording()) {
            m_bitmap_recording_img->SetBitmap(m_bitmap_recording_on.bmp());
        }
        else {
            m_bitmap_recording_img->SetBitmap(m_bitmap_recording_off.bmp());
        }
        m_last_recording = obj->is_recording() ? 1 : 0;
    }
    if (!m_bitmap_recording_img->IsShown())
        m_bitmap_recording_img->Show();

    /*if (m_bitmap_recording_img->IsShown())
        m_bitmap_recording_img->Hide();*/

    //timelapse
    if (obj->is_support_timelapse) {
        if (m_last_timelapse != (obj->is_timelapse() ? 1: 0)) {
            if (obj->is_timelapse()) {
                m_bitmap_timelapse_img->SetBitmap(m_bitmap_timelapse_on.bmp());
            } else {
                m_bitmap_timelapse_img->SetBitmap(m_bitmap_timelapse_off.bmp());
            }
            m_last_timelapse = obj->is_timelapse() ? 1 : 0;
        }
        if (!m_bitmap_timelapse_img->IsShown())
            m_bitmap_timelapse_img->Show();
    } else {
        if (m_bitmap_timelapse_img->IsShown())
            m_bitmap_timelapse_img->Hide();
    }

    //vcamera
    if (obj->virtual_camera) {
        if (m_last_vcamera != (m_media_play_ctrl->IsStreaming() ? 1: 0)) {
            if (m_media_play_ctrl->IsStreaming()) {
                m_bitmap_vcamera_img->SetBitmap(m_bitmap_vcamera_on.bmp());
            } else {
                m_bitmap_vcamera_img->SetBitmap(m_bitmap_vcamera_off.bmp());
            }
            m_last_vcamera = m_media_play_ctrl->IsStreaming() ? 1 : 0;
        }
        if (!m_bitmap_vcamera_img->IsShown())
            m_bitmap_vcamera_img->Show();
    } else {
        if (m_bitmap_vcamera_img->IsShown())
            m_bitmap_vcamera_img->Hide();
    }

    //camera setting
    if (m_camera_popup && m_camera_popup->IsShown()) {
        bool show_vcamera = m_media_play_ctrl->IsStreaming();
        m_camera_popup->update(show_vcamera);
    }

    m_panel_monitoring_title->Layout();
}

StatusPanel::StatusPanel(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style, const wxString &name)
    : StatusBasePanel(parent, id, pos, size, style)
    , m_fan_control_popup(new FanControlPopup(this))
{
    init_scaled_buttons();
    m_buttons.push_back(m_button_unload);
    m_buttons.push_back(m_bpButton_z_10);
    m_buttons.push_back(m_bpButton_z_1);
    m_buttons.push_back(m_bpButton_z_down_1);
    m_buttons.push_back(m_bpButton_z_down_10);
    m_buttons.push_back(m_bpButton_e_10);
    m_buttons.push_back(m_bpButton_e_down_10);

    obj = nullptr;
    m_score_data         = new ScoreData;
    m_score_data->rating_id = -1;
    /* set default values */
    m_switch_lamp->SetValue(false);
    m_switch_printing_fan->SetValue(false);
    m_switch_nozzle_fan->SetValue(false);
    m_switch_cham_fan->SetValue(false);

    /* set default enable state */
    m_project_task_panel->enable_pause_resume_button(false, "resume_disable");
    m_project_task_panel->enable_abort_button(false);


    Bind(wxEVT_WEBREQUEST_STATE, &StatusPanel::on_webrequest_state, this);

    Bind(wxCUSTOMEVT_SET_TEMP_FINISH, [this](wxCommandEvent e) {
        int id = e.GetInt();
        if (id == m_tempCtrl_bed->GetType()) {
            on_set_bed_temp();
        } else if (id == m_tempCtrl_nozzle->GetType()) {
            on_set_nozzle_temp();
        } else if (id == m_tempCtrl_chamber->GetType()) {
            on_set_chamber_temp();
        }
    });


    // Connect Events
    m_project_task_panel->get_bitmap_thumbnail()->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(StatusPanel::refresh_thumbnail_webrequest), NULL, this);
    m_project_task_panel->get_pause_resume_button()->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_subtask_pause_resume), NULL, this);
    m_project_task_panel->get_abort_button()->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_subtask_abort), NULL, this);
    m_project_task_panel->get_market_scoring_button()->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_market_scoring), NULL, this);
    m_project_task_panel->get_market_retry_buttom()->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_market_retry), NULL, this); 
    m_project_task_panel->get_clean_button()->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_print_error_clean), NULL, this);

    m_setting_button->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(StatusPanel::on_camera_enter), NULL, this);
    m_setting_button->Connect(wxEVT_LEFT_DCLICK, wxMouseEventHandler(StatusPanel::on_camera_enter), NULL, this);
    m_tempCtrl_bed->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(StatusPanel::on_bed_temp_kill_focus), NULL, this);
    m_tempCtrl_bed->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(StatusPanel::on_bed_temp_set_focus), NULL, this);
    m_tempCtrl_nozzle->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(StatusPanel::on_nozzle_temp_kill_focus), NULL, this);
    m_tempCtrl_nozzle->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(StatusPanel::on_nozzle_temp_set_focus), NULL, this);
    m_tempCtrl_chamber->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(StatusPanel::on_cham_temp_kill_focus), NULL, this);
    m_tempCtrl_chamber->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(StatusPanel::on_cham_temp_set_focus), NULL, this);
    m_switch_lamp->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_lamp_switch), NULL, this);
    m_switch_nozzle_fan->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_nozzle_fan_switch), NULL, this); // TODO
    m_switch_printing_fan->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_nozzle_fan_switch), NULL, this);
    m_switch_cham_fan->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_nozzle_fan_switch), NULL, this); 
    m_bpButton_xy->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_xy), NULL, this); // TODO
    m_bpButton_z_10->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_up_10), NULL, this);
    m_bpButton_z_1->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_up_1), NULL, this);
    m_bpButton_z_down_1->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_down_1), NULL, this);
    m_bpButton_z_down_10->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_down_10), NULL, this);
    m_bpButton_e_10->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_e_up_10), NULL, this);
    m_bpButton_e_down_10->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_e_down_10), NULL, this);
    m_button_unload->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_start_unload), NULL, this);
    Bind(EVT_AMS_EXTRUSION_CALI, &StatusPanel::on_filament_extrusion_cali, this);
    Bind(EVT_AMS_LOAD, &StatusPanel::on_ams_load, this);
    Bind(EVT_AMS_UNLOAD, &StatusPanel::on_ams_unload, this);
    Bind(EVT_AMS_FILAMENT_BACKUP, &StatusPanel::on_ams_filament_backup, this);
    Bind(EVT_AMS_SETTINGS, &StatusPanel::on_ams_setting_click, this);
    Bind(EVT_AMS_REFRESH_RFID, &StatusPanel::on_ams_refresh_rfid, this);
    Bind(EVT_AMS_ON_SELECTED, &StatusPanel::on_ams_selected, this);
    Bind(EVT_AMS_ON_FILAMENT_EDIT, &StatusPanel::on_filament_edit, this);
    Bind(EVT_VAMS_ON_FILAMENT_EDIT, &StatusPanel::on_ext_spool_edit, this);
    Bind(EVT_AMS_GUIDE_WIKI, &StatusPanel::on_ams_guide, this);
    Bind(EVT_AMS_RETRY, &StatusPanel::on_ams_retry, this);
    Bind(EVT_FAN_CHANGED, &StatusPanel::on_fan_changed, this);
    Bind(EVT_SECONDARY_CHECK_DONE, &StatusPanel::on_print_error_done, this);
    Bind(EVT_SECONDARY_CHECK_RESUME, &StatusPanel::on_subtask_pause_resume, this);
    Bind(EVT_PRINT_ERROR_STOP, &StatusPanel::on_subtask_abort, this);
    Bind(EVT_LOAD_VAMS_TRAY, &StatusPanel::on_ams_load_vams, this);
    Bind(EVT_JUMP_TO_LIVEVIEW, [this](wxCommandEvent& e) {
        m_media_play_ctrl->jump_to_play();
        if (m_print_error_dlg)
            m_print_error_dlg->on_hide();
    });


    m_switch_speed->Connect(wxEVT_LEFT_DOWN, wxCommandEventHandler(StatusPanel::on_switch_speed), NULL, this);
    m_calibration_btn->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_start_calibration), NULL, this);
    m_options_btn->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_show_print_options), NULL, this);
    m_parts_btn->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_show_parts_options), NULL, this);
}

StatusPanel::~StatusPanel()
{
    // Disconnect Events
    m_project_task_panel->get_bitmap_thumbnail()->Disconnect(wxEVT_LEFT_DOWN, wxMouseEventHandler(StatusPanel::refresh_thumbnail_webrequest), NULL, this);
    m_project_task_panel->get_pause_resume_button()->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_subtask_pause_resume), NULL, this);
    m_project_task_panel->get_abort_button()->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_subtask_abort), NULL, this);
    m_project_task_panel->get_market_scoring_button()->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_market_scoring), NULL, this);
    m_project_task_panel->get_market_retry_buttom()->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_market_retry), NULL, this); 
    m_project_task_panel->get_clean_button()->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_print_error_clean), NULL, this);

    m_setting_button->Disconnect(wxEVT_LEFT_DOWN, wxMouseEventHandler(StatusPanel::on_camera_enter), NULL, this);
    m_setting_button->Disconnect(wxEVT_LEFT_DCLICK, wxMouseEventHandler(StatusPanel::on_camera_enter), NULL, this);
    m_tempCtrl_bed->Disconnect(wxEVT_KILL_FOCUS, wxFocusEventHandler(StatusPanel::on_bed_temp_kill_focus), NULL, this);
    m_tempCtrl_bed->Disconnect(wxEVT_SET_FOCUS, wxFocusEventHandler(StatusPanel::on_bed_temp_set_focus), NULL, this);
    m_tempCtrl_nozzle->Disconnect(wxEVT_KILL_FOCUS, wxFocusEventHandler(StatusPanel::on_nozzle_temp_kill_focus), NULL, this);
    m_tempCtrl_nozzle->Disconnect(wxEVT_SET_FOCUS, wxFocusEventHandler(StatusPanel::on_nozzle_temp_set_focus), NULL, this);
    m_switch_lamp->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_lamp_switch), NULL, this);
    m_switch_nozzle_fan->Disconnect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_nozzle_fan_switch), NULL, this);
    m_switch_printing_fan->Disconnect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_nozzle_fan_switch), NULL, this);
    m_switch_cham_fan->Disconnect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_nozzle_fan_switch), NULL, this);
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
    m_parts_btn->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_show_parts_options), NULL, this);
    m_button_unload->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_start_unload), NULL, this);

    // remove warning dialogs
    if (m_print_error_dlg != nullptr)
        delete m_print_error_dlg;

    if (abort_dlg != nullptr)
        delete abort_dlg;

    if (ctrl_e_hint_dlg != nullptr)
        delete ctrl_e_hint_dlg;

    if (sdcard_hint_dlg != nullptr)
        delete sdcard_hint_dlg;

    if (m_score_data != nullptr) { 
        delete m_score_data;
    }
}

void StatusPanel::init_scaled_buttons()
{
    m_project_task_panel->init_scaled_buttons();
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

void StatusPanel::on_market_scoring(wxCommandEvent &event) { 
    if (obj && obj->is_makeworld_subtask() && obj->rating_info && obj->rating_info->request_successful) { // model is mall model and has rating_id
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": on_market_scoring" ;
        if (m_score_data && m_score_data->rating_id == obj->rating_info->rating_id) { // current score data for model is same as mall model
            if (m_score_data->star_count != m_project_task_panel->get_star_count()) m_score_data->star_count = m_project_task_panel->get_star_count();
            ScoreDialog m_score_dlg(this, m_score_data);
            int ret = m_score_dlg.ShowModal();
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": old data";

            if (ret == wxID_OK) { 
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": old data is upload";
                m_score_data->rating_id = -1;
                m_project_task_panel->set_star_count_dirty(false);
                if (obj) obj->get_model_mall_result_need_retry = true;
                return;
            }
            if (m_score_data != nullptr) {
                delete m_score_data;
                m_score_data = nullptr;
            }
            m_score_data = new ScoreData(m_score_dlg.get_score_data()); // when user do not submit score, store the data for next opening the score dialog
            m_project_task_panel->set_star_count(m_score_data->star_count);
        } else {
            int star_count      = m_project_task_panel->get_star_count_dirty() ? m_project_task_panel->get_star_count() : obj->rating_info->start_count;
            bool        success_print = obj->rating_info->success_printed;
            ScoreDialog m_score_dlg(this, obj->get_modeltask()->design_id, obj->get_modeltask()->model_id, obj->get_modeltask()->profile_id, obj->rating_info->rating_id,
                                    success_print, star_count);
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": new data";

            std::string comment = obj->rating_info->content;
            if (!comment.empty()) { m_score_dlg.set_comment(comment); }
            
            std::vector<std::string> images_json_array;
            images_json_array = obj->rating_info->image_url_paths;
            if (!images_json_array.empty()) m_score_dlg.set_cloud_bitmap(images_json_array);
            
            int ret = m_score_dlg.ShowModal();

            if (ret == wxID_OK) {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": new data is upload";
                m_score_data->rating_id = -1;
                m_project_task_panel->set_star_count_dirty(false);
                if (obj) obj->get_model_mall_result_need_retry = true;
                return;
            }
            if (m_score_data != nullptr) {
                delete m_score_data;
                m_score_data = nullptr;
            }
            m_score_data = new ScoreData(m_score_dlg.get_score_data());
            m_project_task_panel->set_star_count(m_score_data->star_count);
        }
    }
}

void StatusPanel::on_market_retry(wxCommandEvent &event)
{
    if (obj) {
    obj->get_model_mall_result_need_retry = true;
    } else {
        BOOST_LOG_TRIVIAL(info)<< __FUNCTION__ << "retury failed";
    }
}

void StatusPanel::on_subtask_pause_resume(wxCommandEvent &event)
{
    if (obj) {
        if (obj->can_resume()) {
            BOOST_LOG_TRIVIAL(info) << "monitor: resume current print task dev_id =" << obj->dev_id;
            obj->command_task_resume();
        }  
        else {
            BOOST_LOG_TRIVIAL(info) << "monitor: pause current print task dev_id =" << obj->dev_id;
            obj->command_task_pause();
        } 
        if (m_print_error_dlg) {
            m_print_error_dlg->on_hide();
        }if (m_print_error_dlg_no_action) {
            m_print_error_dlg_no_action->on_hide();
        }

    }
}

void StatusPanel::on_subtask_abort(wxCommandEvent &event)
{
    if (abort_dlg == nullptr) {
        abort_dlg = new SecondaryCheckDialog(this->GetParent(), wxID_ANY, _L("Cancel print"));
        abort_dlg->Bind(EVT_SECONDARY_CHECK_CONFIRM, [this](wxCommandEvent &e) {
            if (obj) {
                BOOST_LOG_TRIVIAL(info) << "monitor: stop current print task dev_id =" << obj->dev_id;
                obj->command_task_abort(); 
            }
        });
    }
    abort_dlg->update_text(_L("Are you sure you want to cancel this print?"));
    abort_dlg->on_show();
}

void StatusPanel::error_info_reset()
{
    m_project_task_panel->error_info_reset();
    before_error_code = 0;
}

void StatusPanel::on_print_error_clean(wxCommandEvent &event)
{
    error_info_reset();
    skip_print_error = obj->print_error;
    char buf[32];
    ::sprintf(buf, "%08X", skip_print_error);
    BOOST_LOG_TRIVIAL(info) << "skip_print_error: " << buf;
    before_error_code = obj->print_error;
}

void StatusPanel::on_webrequest_state(wxWebRequestEvent &evt)
{
    BOOST_LOG_TRIVIAL(trace) << "monitor: monitor_panel web request state = " << evt.GetState();
    switch (evt.GetState()) {
    case wxWebRequest::State_Completed: {
        if (m_current_print_mode != PrintingTaskType::CALIBRATION ||(m_calib_mode == CalibMode::Calib_Flow_Rate && m_calib_method == CalibrationMethod::CALI_METHOD_MANUAL)) {
            wxImage img(*evt.GetResponse().GetStream());
            img_list.insert(std::make_pair(m_request_url, img));
            wxImage resize_img = img.Scale(m_project_task_panel->get_bitmap_thumbnail()->GetSize().x, m_project_task_panel->get_bitmap_thumbnail()->GetSize().y, wxIMAGE_QUALITY_HIGH);
            m_project_task_panel->set_thumbnail_img(resize_img);
            m_project_task_panel->set_brightness_value(get_brightness_value(resize_img));
        }
        if (obj) {
            m_project_task_panel->set_plate_index(obj->m_plate_index);
        } else {
            m_project_task_panel->set_plate_index(-1);
        }
        task_thumbnail_state = ThumbnailState::TASK_THUMBNAIL;
        break;
    }
    case wxWebRequest::State_Failed:
    case wxWebRequest::State_Cancelled:
    case wxWebRequest::State_Unauthorized: {
        m_project_task_panel->set_thumbnail_img(m_thumbnail_brokenimg.bmp());
        m_project_task_panel->set_plate_index(-1);
        task_thumbnail_state = ThumbnailState::BROKEN_IMG;
        break;
    }
    case wxWebRequest::State_Active:
    case wxWebRequest::State_Idle: break;
    default: break;
    }
}

void StatusPanel::refresh_thumbnail_webrequest(wxMouseEvent& event)
{
    if (!obj) return;
    if (task_thumbnail_state != ThumbnailState::BROKEN_IMG) return;

    if (obj->slice_info) {
        m_request_url = wxString(obj->slice_info->thumbnail_url);
        if (!m_request_url.IsEmpty()) {
            web_request = wxWebSession::GetDefault().CreateRequest(this, m_request_url);
            BOOST_LOG_TRIVIAL(trace) << "monitor: create new webrequest, state = " << web_request.GetState() << ", url = " << m_request_url;
            if (web_request.GetState() == wxWebRequest::State_Idle)
                web_request.Start();
            BOOST_LOG_TRIVIAL(trace) << "monitor: start new webrequest, state = " << web_request.GetState() << ", url = " << m_request_url;
        }
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
        request_model_info_flag = false;
        m_project_task_panel->set_star_count_dirty(false);
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

#if !BBL_RELEASE_TO_PUBLIC
    auto delay1  = std::chrono::duration_cast<std::chrono::milliseconds>(obj->last_utc_time - std::chrono::system_clock::now()).count();
    auto delay2  = std::chrono::duration_cast<std::chrono::milliseconds>(obj->last_push_time - std::chrono::system_clock::now()).count();
    auto delay = wxString::Format(" %ld/%ld", delay1, delay2);
    m_staticText_timelapse
        ->SetLabel((obj->is_lan_mode_printer() ? "Local Mqtt" : obj->is_tunnel_mqtt ? "Tunnel Mqtt" : "Cloud Mqtt") + delay);
    m_bmToggleBtn_timelapse
        ->Enable(!obj->is_lan_mode_printer());
    m_bmToggleBtn_timelapse
        ->SetValue(obj->is_tunnel_mqtt);
#endif

    m_machine_ctrl_panel->Freeze();

    if (obj->is_in_printing() && !obj->can_resume())
        show_printing_status(false, true);
    else
        show_printing_status();

    update_temp_ctrl(obj);
    update_misc_ctrl(obj);

    update_ams(obj);
    update_cali(obj);

    if (obj) {
        // update extrusion calibration
        if (m_extrusion_cali_dlg) {
            m_extrusion_cali_dlg->update_machine_obj(obj);
            m_extrusion_cali_dlg->update();
        }

        // update calibration status
        if (calibration_dlg != nullptr) {
            calibration_dlg->update_machine_obj(obj);
            calibration_dlg->update_cali(obj);
        }
        


        if (obj->is_support_first_layer_inspect
            || obj->is_support_ai_monitoring
            || obj->is_support_build_plate_marker_detect
            || obj->is_support_auto_recovery_step_loss) {
            m_options_btn->Show();
            if (print_options_dlg) {
                print_options_dlg->update_machine_obj(obj);
                print_options_dlg->update_options(obj);
            }
        } else {
            m_options_btn->Hide();
        }

        m_parts_btn->Show();

        //support edit chamber temp
        if (obj->is_support_chamber_edit) {
            m_tempCtrl_chamber->SetReadOnly(false);
            m_tempCtrl_chamber->Enable();
            wxCursor cursor(wxCURSOR_IBEAM);
            m_tempCtrl_chamber->GetTextCtrl()->SetCursor(cursor);
        } else {
            m_tempCtrl_chamber->SetReadOnly(true);

            wxCursor cursor(wxCURSOR_ARROW);
            m_tempCtrl_chamber->GetTextCtrl()->SetCursor(cursor);

            if (obj->get_printer_series() == PrinterSeries::SERIES_X1) {
                m_tempCtrl_chamber->SetTagTemp(TEMP_BLANK_STR);
            }if (obj->get_printer_series() == PrinterSeries::SERIES_P1P)
            {
                m_tempCtrl_chamber->SetLabel(TEMP_BLANK_STR);
                m_tempCtrl_chamber->GetTextCtrl()->SetValue(TEMP_BLANK_STR);
            }

            //m_tempCtrl_chamber->Disable();

        }

        if (!obj->dev_connection_type.empty()) {
            auto iter_connect_type = m_print_connect_types.find(obj->dev_id);
            if (iter_connect_type != m_print_connect_types.end()) {
                if (iter_connect_type->second != obj->dev_connection_type) {

                    if (iter_connect_type->second == "lan" && obj->dev_connection_type == "cloud") {
                        m_print_connect_types[obj->dev_id] = obj->dev_connection_type;
                    }

                    if (iter_connect_type->second == "cloud" && obj->dev_connection_type == "lan") {
                        m_print_connect_types[obj->dev_id] = obj->dev_connection_type;
                    }
                }
            }
             m_print_connect_types[obj->dev_id] = obj->dev_connection_type;
        }

        update_error_message();
    }

    update_camera_state(obj);

    m_machine_ctrl_panel->Thaw();
}

void StatusPanel::show_recenter_dialog() {
    RecenterDialog dlg(this);
    if (dlg.ShowModal() == wxID_OK)
        obj->command_go_home();
}

void StatusPanel::show_error_message(MachineObject *obj, bool is_exist, wxString msg, std::string print_error_str, wxString image_url, std::vector<int> used_button)
{
    if (is_exist && msg.IsEmpty()) {
        error_info_reset();
    } else {
        m_project_task_panel->show_error_msg(msg);

        if (!used_button.empty()) {
            BOOST_LOG_TRIVIAL(info) << "show print error! error_msg = " << msg;
            if (m_print_error_dlg == nullptr) {
                m_print_error_dlg = new PrintErrorDialog(this->GetParent(), wxID_ANY, _L("Error"));
            }

            m_print_error_dlg->update_title_style(_L("Error"), used_button, this);
            m_print_error_dlg->update_text_image(msg, print_error_str, image_url);
            m_print_error_dlg->Bind(EVT_SECONDARY_CHECK_CONFIRM, [this, obj](wxCommandEvent& e) {
                if (obj) {
                    obj->command_clean_print_error(obj->subtask_id_, obj->print_error);
                }
                });

            m_print_error_dlg->Bind(EVT_SECONDARY_CHECK_RETRY, [this, obj](wxCommandEvent& e) {
                if (m_ams_control) {
                    m_ams_control->on_retry();
                }
                });

            m_print_error_dlg->on_show();
        }
        else {
            //old error code dialog
            auto it_retry = std::find(message_containing_retry.begin(), message_containing_retry.end(), print_error_str);
            auto it_done = std::find(message_containing_done.begin(), message_containing_done.end(), print_error_str);
            auto it_resume = std::find(message_containing_resume.begin(), message_containing_resume.end(), print_error_str);

            BOOST_LOG_TRIVIAL(info) << "show print error! error_msg = " << msg;

            wxDateTime now = wxDateTime::Now();
            wxString show_time = now.Format("%H%M%d");
            wxString error_code_msg = wxString::Format("%S\n[%S %S]", msg, print_error_str, show_time);

            if (m_print_error_dlg_no_action == nullptr) {
                m_print_error_dlg_no_action = new SecondaryCheckDialog(this->GetParent(), wxID_ANY, _L("Warning"), SecondaryCheckDialog::ButtonStyle::ONLY_CONFIRM);
            }

            if (it_done != message_containing_done.end() && it_retry != message_containing_retry.end()) {
                m_print_error_dlg_no_action->update_title_style(_L("Warning"), SecondaryCheckDialog::ButtonStyle::DONE_AND_RETRY, this);
            }
            else if (it_done != message_containing_done.end()) {
                m_print_error_dlg_no_action->update_title_style(_L("Warning"), SecondaryCheckDialog::ButtonStyle::CONFIRM_AND_DONE, this);
            }
            else if (it_retry != message_containing_retry.end()) {
                m_print_error_dlg_no_action->update_title_style(_L("Warning"), SecondaryCheckDialog::ButtonStyle::CONFIRM_AND_RETRY, this);
            }
            else if (it_resume != message_containing_resume.end()) {
                m_print_error_dlg_no_action->update_title_style(_L("Warning"), SecondaryCheckDialog::ButtonStyle::CONFIRM_AND_RESUME, this);
            }
            else {
                m_print_error_dlg_no_action->update_title_style(_L("Warning"), SecondaryCheckDialog::ButtonStyle::ONLY_CONFIRM, this);
            }
            m_print_error_dlg_no_action->update_text(error_code_msg);
            m_print_error_dlg_no_action->Bind(EVT_SECONDARY_CHECK_CONFIRM, [this, obj](wxCommandEvent& e) {
                if (obj) {
                    obj->command_clean_print_error(obj->subtask_id_, obj->print_error);
                }
                });

            m_print_error_dlg_no_action->Bind(EVT_SECONDARY_CHECK_RETRY, [this, obj](wxCommandEvent& e) {
                if (m_ams_control) {
                    m_ams_control->on_retry();
                }
                });

            m_print_error_dlg_no_action->on_show();
        }
        wxGetApp().mainframe->RequestUserAttention(wxUSER_ATTENTION_ERROR);
    }
}

void StatusPanel::update_error_message()
{
    if (obj->print_error <= 0) {
        before_error_code = obj->print_error;
        show_error_message(obj, true, wxEmptyString);
        return;
    } else if (before_error_code != obj->print_error && obj->print_error != skip_print_error) {
        before_error_code = obj->print_error;

        if (wxGetApp().get_hms_query()) {
            char buf[32];
            ::sprintf(buf, "%08X", obj->print_error);
            std::string print_error_str = std::string(buf);
            if (print_error_str.size() > 4) { print_error_str.insert(4, " "); }

            wxString error_msg;
            bool is_errocode_exist = wxGetApp().get_hms_query()->query_print_error_msg(obj->print_error, error_msg);
            std::vector<int> used_button;
            wxString error_image_url = wxGetApp().get_hms_query()->query_print_error_url_action(obj->print_error, obj->dev_id, used_button);
            // special case
            if (print_error_str == "0300 8003" || print_error_str == "0300 8002" || print_error_str == "0300 800A") {
                used_button.emplace_back(PrintErrorDialog::PrintErrorButton::JUMP_TO_LIVEVIEW);
            }
            show_error_message(obj, is_errocode_exist, error_msg, print_error_str, error_image_url, used_button);
        }
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

		m_bpButton_z_10->SetIcon("monitor_bed_up_disable");
		m_bpButton_z_1->SetIcon("monitor_bed_up_disable");
		m_bpButton_z_down_1->SetIcon("monitor_bed_down_disable");
		m_bpButton_z_down_10->SetIcon("monitor_bed_down_disable");
        m_bpButton_e_10->SetIcon("monitor_extruder_up_disable");
        m_bpButton_e_down_10->SetIcon("monitor_extrduer_down_disable");

        m_staticText_z_tip->SetForegroundColour(DISCONNECT_TEXT_COL);
        m_staticText_e->SetForegroundColour(DISCONNECT_TEXT_COL);
        m_button_unload->Enable(false);
        m_switch_speed->SetValue(false);
    } else {
        m_switch_speed->Enable();
        m_switch_lamp->Enable();
        m_switch_nozzle_fan->Enable();
        m_switch_printing_fan->Enable();
        m_switch_cham_fan->Enable();
        m_bpButton_xy->Enable();
        m_bpButton_z_10->Enable();
        m_bpButton_z_1->Enable();
        m_bpButton_z_down_1->Enable();
        m_bpButton_z_down_10->Enable();
        m_bpButton_e_10->Enable();
        m_bpButton_e_down_10->Enable();

		m_bpButton_z_10->SetIcon("monitor_bed_up");
		m_bpButton_z_1->SetIcon("monitor_bed_up");
		m_bpButton_z_down_1->SetIcon("monitor_bed_down");
		m_bpButton_z_down_10->SetIcon("monitor_bed_down");
		m_bpButton_e_10->SetIcon("monitor_extruder_up");
		m_bpButton_e_down_10->SetIcon("monitor_extrduer_down");

        m_staticText_z_tip->SetForegroundColour(TEXT_LIGHT_FONT_COL);
        m_staticText_e->SetForegroundColour(TEXT_LIGHT_FONT_COL);
        m_button_unload->Enable();
        m_switch_speed->SetValue(true);
    }

    if (!temp_area) {
        m_tempCtrl_nozzle->Enable(false);
        m_tempCtrl_bed->Enable(false);
        m_tempCtrl_chamber->Enable(false);
        m_switch_speed->Enable(false);
        m_switch_speed->SetValue(false);
        m_switch_lamp->Enable(false);
        m_switch_nozzle_fan->Enable(false);
        m_switch_printing_fan->Enable(false);
        m_switch_cham_fan->Enable(false);
    } else {
        m_tempCtrl_nozzle->Enable();
        m_tempCtrl_bed->Enable();
        m_tempCtrl_chamber->Enable();
        m_switch_speed->Enable();
        m_switch_speed->SetValue(true);
        m_switch_lamp->Enable();
        m_switch_nozzle_fan->Enable();
        m_switch_printing_fan->Enable();
        m_switch_cham_fan->Enable();
    }
}

void StatusPanel::update_temp_ctrl(MachineObject *obj)
{
    if (!obj) return;

    m_tempCtrl_bed->SetCurrTemp((int) obj->bed_temp);
    m_tempCtrl_bed->SetMaxTemp(obj->get_bed_temperature_limit());

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
    if (obj->nozzle_max_temperature > -1) {
        if (m_tempCtrl_nozzle) m_tempCtrl_nozzle->SetMaxTemp(obj->nozzle_max_temperature);
    }
    else {
        if (m_tempCtrl_nozzle) m_tempCtrl_nozzle->SetMaxTemp(nozzle_temp_range[1]);
    }

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

    m_tempCtrl_chamber->SetCurrTemp(obj->chamber_temp);
    // update temprature if not input temp target
    if (m_temp_chamber_timeout > 0) {
        m_temp_chamber_timeout--;
    }
    else {
        if (!cham_temp_input) { m_tempCtrl_chamber->SetTagTemp(obj->chamber_temp_target); }
    }

    if ((obj->chamber_temp_target - obj->chamber_temp) >= TEMP_THRESHOLD_VAL) {
        m_tempCtrl_chamber->SetIconActive();
    }
    else {
        m_tempCtrl_chamber->SetIconNormal();
    }
}

void StatusPanel::update_misc_ctrl(MachineObject *obj)
{
    if (!obj) return;

    if (obj->can_unload_filament()) {
        if (!m_button_unload->IsShown()) {
            m_button_unload->Show();
            m_button_unload->GetParent()->Layout();
        }
    } else {
        if (m_button_unload->IsShown()) {
            m_button_unload->Hide();
            m_button_unload->GetParent()->Layout();
        }
    }

    if (obj->is_core_xy()) {
        m_staticText_z_tip->SetLabel(_L("Bed"));
    } else {
        m_staticText_z_tip->SetLabel("Z");
    }

    // update extruder icon
    update_extruder_status(obj);

    bool is_suppt_aux_fun = obj->is_support_aux_fan;
    bool is_suppt_cham_fun = obj->is_support_chamber_fan;

    //update cham fan
    if (m_current_support_cham_fan != is_suppt_cham_fun) {
        if (is_suppt_cham_fun) {
            m_switch_cham_fan->Show();
            m_switch_nozzle_fan->SetMinSize(MISC_BUTTON_3FAN_SIZE);
            m_switch_nozzle_fan->SetMaxSize(MISC_BUTTON_3FAN_SIZE);
            m_switch_printing_fan->SetMinSize(MISC_BUTTON_3FAN_SIZE);
            m_switch_printing_fan->SetMaxSize(MISC_BUTTON_3FAN_SIZE);
        }
        else {
            m_switch_cham_fan->Hide();
            m_switch_nozzle_fan->SetMinSize(MISC_BUTTON_2FAN_SIZE);
            m_switch_nozzle_fan->SetMaxSize(MISC_BUTTON_2FAN_SIZE);
            m_switch_printing_fan->SetMinSize(MISC_BUTTON_2FAN_SIZE);
            m_switch_printing_fan->SetMaxSize(MISC_BUTTON_2FAN_SIZE);
        }

        m_misc_ctrl_sizer->Layout();
    }

    if (m_current_support_aux_fan != is_suppt_aux_fun) {
        if (is_suppt_aux_fun) {
            m_switch_printing_fan->Show();
            m_switch_nozzle_fan->SetMinSize(MISC_BUTTON_3FAN_SIZE);
            m_switch_nozzle_fan->SetMaxSize(MISC_BUTTON_3FAN_SIZE);
            m_switch_cham_fan->SetMinSize(MISC_BUTTON_3FAN_SIZE);
            m_switch_cham_fan->SetMaxSize(MISC_BUTTON_3FAN_SIZE);
        }
        else {
            m_switch_printing_fan->Hide();
            m_switch_nozzle_fan->SetMinSize(MISC_BUTTON_2FAN_SIZE);
            m_switch_nozzle_fan->SetMaxSize(MISC_BUTTON_2FAN_SIZE);
            m_switch_cham_fan->SetMinSize(MISC_BUTTON_2FAN_SIZE);
            m_switch_cham_fan->SetMaxSize(MISC_BUTTON_2FAN_SIZE);
        }

        m_misc_ctrl_sizer->Layout();
    }

    if (!is_suppt_aux_fun && !is_suppt_cham_fun) {
        m_switch_nozzle_fan->SetMinSize(MISC_BUTTON_1FAN_SIZE);
        m_switch_nozzle_fan->SetMaxSize(MISC_BUTTON_1FAN_SIZE);
        m_misc_ctrl_sizer->Layout();
    }


    // nozzle fan
    if (m_switch_nozzle_fan_timeout > 0) {
        m_switch_nozzle_fan_timeout--;
    }  else{
        int speed = round(obj->cooling_fan_speed / float(25.5));
        m_switch_nozzle_fan->SetValue(speed > 0 ? true : false);
        m_switch_nozzle_fan->setFanValue(speed * 10);
        if (m_fan_control_popup) {
            m_fan_control_popup->update_fan_data(MachineObject::FanType::COOLING_FAN, obj);
        }
    }

    // printing fan
    if (m_switch_printing_fan_timeout > 0) {
        m_switch_printing_fan_timeout--;
    }else{
        int speed = round(obj->big_fan1_speed / float(25.5));
        m_switch_printing_fan->SetValue(speed > 0 ? true : false);
        m_switch_printing_fan->setFanValue(speed * 10);
        if (m_fan_control_popup) {
            m_fan_control_popup->update_fan_data(MachineObject::FanType::BIG_COOLING_FAN, obj);
        }
    }

    // cham fan
    if (m_switch_cham_fan_timeout > 0) {
        m_switch_cham_fan_timeout--;
    }else{
        int speed = round(obj->big_fan2_speed / float(25.5));
        m_switch_cham_fan->SetValue(speed > 0 ? true : false);
        m_switch_cham_fan->setFanValue(speed * 10);
        if (m_fan_control_popup) {
            m_fan_control_popup->update_fan_data(MachineObject::FanType::CHAMBER_FAN, obj);
        }
    }

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

    m_current_support_aux_fan = is_suppt_aux_fun;
    m_current_support_cham_fan = is_suppt_cham_fun;
}

void StatusPanel::update_extruder_status(MachineObject* obj)
{
    if (!obj) return;
    if (obj->is_filament_at_extruder()) {
        if (obj->extruder_axis_status == MachineObject::ExtruderAxisStatus::LOAD) {
            m_bitmap_extruder_img->SetBitmap(m_bitmap_extruder_filled_load);
        }
        else {
            m_bitmap_extruder_img->SetBitmap(m_bitmap_extruder_filled_unload);
        }
    }
    else {
        if (obj->extruder_axis_status == MachineObject::ExtruderAxisStatus::LOAD) {
            m_bitmap_extruder_img->SetBitmap(m_bitmap_extruder_empty_load);
        } else {
            m_bitmap_extruder_img->SetBitmap(m_bitmap_extruder_empty_unload);
        }
    }
}

void StatusPanel::update_ams(MachineObject *obj)
{
    // update obj in sub dlg
    if (m_ams_setting_dlg) {
        m_ams_setting_dlg->obj = obj;

        if (obj && m_ams_setting_dlg->IsShown()) {
            update_ams_insert_material(obj);
            m_ams_setting_dlg->update_starting_read_mode(obj->ams_power_on_flag);
            m_ams_setting_dlg->update_remain_mode(obj->ams_calibrate_remain_flag);
            m_ams_setting_dlg->update_switch_filament(obj->ams_auto_switch_filament_flag);
            m_ams_setting_dlg->update_air_printing_detection(obj->ams_air_print_status);
        }
    }
    if (m_filament_setting_dlg) { m_filament_setting_dlg->obj = obj; }

    if (obj->cali_version != -1 && last_cali_version != obj->cali_version) {
        last_cali_version = obj->cali_version;
        CalibUtils::emit_get_PA_calib_info(obj->nozzle_diameter, "");
    }

    bool is_support_virtual_tray    = obj->ams_support_virtual_tray;
    bool is_support_filament_backup = obj->is_support_filament_backup;
    AMSModel ams_mode               = AMSModel::GENERIC_AMS;

    if (obj) {
        if (obj->get_printer_ams_type() == "f1") { ams_mode = AMSModel::EXTRA_AMS; }
        else if(obj->get_printer_ams_type() == "generic") { ams_mode = AMSModel::GENERIC_AMS; }
    }

    if (!obj
        || !obj->is_connected()
        || obj->amsList.empty()
        || obj->ams_exist_bits == 0) {
        if (!obj || !obj->is_connected()) {
            last_tray_exist_bits = -1;
            last_ams_exist_bits = -1;
            last_tray_is_bbl_bits = -1;
            last_read_done_bits = -1;
            last_reading_bits = -1;
            last_ams_version = -1;
            BOOST_LOG_TRIVIAL(trace) << "machine object" << obj->dev_name << " was disconnected, set show_ams_group is false";
        }


        m_ams_control->SetAmsModel(AMSModel::NO_AMS, ams_mode);
        show_ams_group(false);

        m_ams_control->show_auto_refill(false);
    }
    else {

        m_ams_control->SetAmsModel(ams_mode, ams_mode);
        show_ams_group(true);
        m_ams_control->show_auto_refill(true); 
    }


    if (is_support_virtual_tray) m_ams_control->update_vams_kn_value(obj->vt_tray, obj);
    if (m_filament_setting_dlg) m_filament_setting_dlg->update();

    std::vector<AMSinfo> ams_info;
    for (auto ams = obj->amsList.begin(); ams != obj->amsList.end(); ams++) {
        AMSinfo info;
        info.ams_id = ams->first;
        if (ams->second->is_exists && info.parse_ams_info(obj, ams->second, obj->ams_calibrate_remain_flag, obj->is_support_ams_humidity)) ams_info.push_back(info);
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

    // must select a current can
    m_ams_control->UpdateAms(ams_info, false);

    last_tray_exist_bits  = obj->tray_exist_bits;
    last_ams_exist_bits   = obj->ams_exist_bits;
    last_tray_is_bbl_bits = obj->tray_is_bbl_bits;
    last_read_done_bits   = obj->tray_read_done_bits;
    last_reading_bits     = obj->tray_reading_bits;
    last_ams_version      = obj->ams_version;


    std::string curr_ams_id = m_ams_control->GetCurentAms();
    std::string curr_can_id = m_ams_control->GetCurrentCan(curr_ams_id);
    bool is_vt_tray = false;
    if (obj->m_tray_tar == std::to_string(VIRTUAL_TRAY_ID))
        is_vt_tray = true;

    // set segment 1, 2
    if (obj->m_tray_now == std::to_string(VIRTUAL_TRAY_ID) ) {
         m_ams_control->SetAmsStep(obj->m_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    }
    else {
        if (obj->m_tray_now != "255" && obj->is_filament_at_extruder() && !obj->m_tray_id.empty()) {
            m_ams_control->SetAmsStep(obj->m_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_LOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2);
        }
        else if (obj->m_tray_now != "255") {
            m_ams_control->SetAmsStep(obj->m_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_LOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1);
        }
        else {
            m_ams_control->SetAmsStep(obj->m_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
        }
    }

    // set segment 3
    if (obj->m_tray_now == std::to_string(VIRTUAL_TRAY_ID)) {
        m_ams_control->SetExtruder(obj->is_filament_at_extruder(), true, obj->m_ams_id, obj->vt_tray.get_color());
    } else {
        m_ams_control->SetExtruder(obj->is_filament_at_extruder(), false, obj->m_ams_id, m_ams_control->GetCanColour(obj->m_ams_id, obj->m_tray_id));
       
    }

    if (obj->ams_status_main == AMS_STATUS_MAIN_FILAMENT_CHANGE) {
        update_filament_step();

        if (obj->m_tray_tar == std::to_string(VIRTUAL_TRAY_ID) && (obj->m_tray_now != std::to_string(VIRTUAL_TRAY_ID) || obj->m_tray_now != "255")) {
            // wait to heat hotend
            if (obj->ams_status_sub == 0x02) {
                m_ams_control->SetFilamentStep(FilamentStep::STEP_HEAT_NOZZLE, FilamentStepType::STEP_TYPE_VT_LOAD);
            }
            else if (obj->ams_status_sub == 0x05) {
                m_ams_control->SetFilamentStep(FilamentStep::STEP_FEED_FILAMENT, FilamentStepType::STEP_TYPE_VT_LOAD);
            }
            else if (obj->ams_status_sub == 0x06) {
                m_ams_control->SetFilamentStep(FilamentStep::STEP_CONFIRM_EXTRUDED, FilamentStepType::STEP_TYPE_VT_LOAD);
            }
            else if (obj->ams_status_sub == 0x07) {
                m_ams_control->SetFilamentStep(FilamentStep::STEP_PURGE_OLD_FILAMENT, FilamentStepType::STEP_TYPE_VT_LOAD);
            }
            else {
                m_ams_control->SetFilamentStep(FilamentStep::STEP_IDLE, FilamentStepType::STEP_TYPE_VT_LOAD);
            }
        } else {
            // wait to heat hotend
            if (obj->ams_status_sub == 0x02) {
                if (!obj->is_ams_unload()) {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_HEAT_NOZZLE, FilamentStepType::STEP_TYPE_LOAD);
                }
                else {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_HEAT_NOZZLE, FilamentStepType::STEP_TYPE_UNLOAD);
                }
            } else if (obj->ams_status_sub == 0x03) {
                if (!obj->is_ams_unload()) {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_CUT_FILAMENT, FilamentStepType::STEP_TYPE_LOAD);
                }
                else {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_CUT_FILAMENT, FilamentStepType::STEP_TYPE_UNLOAD);
                }
            } else if (obj->ams_status_sub == 0x04) {
                if (!obj->is_ams_unload()) {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_PULL_CURR_FILAMENT, FilamentStepType::STEP_TYPE_LOAD);
                }
                else {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_PULL_CURR_FILAMENT, FilamentStepType::STEP_TYPE_UNLOAD);
                }
            } else if (obj->ams_status_sub == 0x05) {
                if (!obj->is_ams_unload()) {
                    if(m_is_load_with_temp){
                        m_ams_control->SetFilamentStep(FilamentStep::STEP_CUT_FILAMENT, FilamentStepType::STEP_TYPE_LOAD);
                    }else{
                        m_ams_control->SetFilamentStep(FilamentStep::STEP_PUSH_NEW_FILAMENT, FilamentStepType::STEP_TYPE_LOAD);
                    }
                    
                }
                else {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_PUSH_NEW_FILAMENT, FilamentStepType::STEP_TYPE_UNLOAD);
                }
            } else if (obj->ams_status_sub == 0x06) {
                if (!obj->is_ams_unload()) {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_PUSH_NEW_FILAMENT, FilamentStepType::STEP_TYPE_LOAD);
                }
                else {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_PUSH_NEW_FILAMENT, FilamentStepType::STEP_TYPE_UNLOAD);
                }
            } else if (obj->ams_status_sub == 0x07) {
                if (!obj->is_ams_unload()) {
                    if (m_is_load_with_temp) {
                        m_ams_control->SetFilamentStep(FilamentStep::STEP_PULL_CURR_FILAMENT, FilamentStepType::STEP_TYPE_LOAD);
                    }else{
                        m_ams_control->SetFilamentStep(FilamentStep::STEP_PURGE_OLD_FILAMENT, FilamentStepType::STEP_TYPE_LOAD);
                    }
                }
                else {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_PURGE_OLD_FILAMENT, FilamentStepType::STEP_TYPE_UNLOAD);
                }
            }
            else if (obj->ams_status_sub == 0x08) {
                if (!obj->is_ams_unload()) {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_CHECK_POSITION, FilamentStepType::STEP_TYPE_LOAD);
                }
                else {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_CHECK_POSITION, FilamentStepType::STEP_TYPE_UNLOAD);
                }
            } else {
                m_ams_control->SetFilamentStep(FilamentStep::STEP_IDLE, FilamentStepType::STEP_TYPE_UNLOAD);
            }
        }
    } else if (obj->ams_status_main == AMS_STATUS_MAIN_ASSIST) {
        m_ams_control->SetFilamentStep(FilamentStep::STEP_IDLE, FilamentStepType::STEP_TYPE_LOAD);
    } else {
        m_ams_control->SetFilamentStep(FilamentStep::STEP_IDLE, FilamentStepType::STEP_TYPE_LOAD);
    }
    

    for (auto ams_it = obj->amsList.begin(); ams_it != obj->amsList.end(); ams_it++) {
        std::string ams_id = ams_it->first;
        try {
            int ams_id_int = atoi(ams_id.c_str());
            for (auto tray_it = ams_it->second->trayList.begin(); tray_it != ams_it->second->trayList.end(); tray_it++) {
                std::string tray_id     = tray_it->first;
                int         tray_id_int = atoi(tray_id.c_str());
                // new protocol
                if ((obj->tray_reading_bits & (1 << (ams_id_int * 4 + tray_id_int))) != 0) {
                    m_ams_control->PlayRridLoading(ams_id, tray_id);
                } else {
                    m_ams_control->StopRridLoading(ams_id, tray_id);
                }
            }
        } catch (...) {}
    }

    bool is_curr_tray_selected = false;
    if (!curr_ams_id.empty() && !curr_can_id.empty() && (curr_ams_id != std::to_string(VIRTUAL_TRAY_ID)) ) {
        if (curr_can_id == obj->m_tray_now) {
            is_curr_tray_selected = true;
        }
        else {
            std::map<std::string, Ams*>::iterator it = obj->amsList.find(curr_ams_id);
            if (it == obj->amsList.end()) {
                BOOST_LOG_TRIVIAL(trace) << "ams: find " << curr_ams_id << " failed";
                return;
            }
            auto tray_it = it->second->trayList.find(curr_can_id);
            if (tray_it == it->second->trayList.end()) {
                BOOST_LOG_TRIVIAL(trace) << "ams: find " << curr_can_id << " failed";
                return;
            }

            if (!tray_it->second->is_exists) {
                is_curr_tray_selected = true;
            }
        }
    }else if (curr_ams_id == std::to_string(VIRTUAL_TRAY_ID)) {
        if (curr_ams_id == obj->m_tray_now) {
            is_curr_tray_selected = true;
        }
    }else {
        is_curr_tray_selected = true;
    }
        
    update_ams_control_state(is_curr_tray_selected);
}

void StatusPanel::update_ams_insert_material(MachineObject* obj) {
    std::string extra_ams_str = (boost::format("ams_f1/%1%") % 0).str();
    auto extra_ams_it = obj->module_vers.find(extra_ams_str);
    if (extra_ams_it != obj->module_vers.end()) {
        m_ams_setting_dlg->update_insert_material_read_mode(obj->ams_insert_flag, extra_ams_it->second.sw_ver);
    }
    else {
        m_ams_setting_dlg->update_insert_material_read_mode(obj->ams_insert_flag, "");
    }
}


void StatusPanel::update_ams_control_state(bool is_curr_tray_selected)
{
    // set default value to true
    bool enable[ACTION_BTN_COUNT];
    enable[ACTION_BTN_CALI] = true;
    enable[ACTION_BTN_LOAD] = true;
    enable[ACTION_BTN_UNLOAD] = true;

    if (obj->is_in_printing()) {
        if (obj->is_in_extrusion_cali()) {
            enable[ACTION_BTN_LOAD] = false;
            enable[ACTION_BTN_UNLOAD] = false;
            enable[ACTION_BTN_CALI] = true;
        }
        else {
            enable[ACTION_BTN_CALI] = false;
        }
    }
    else {
        enable[ACTION_BTN_CALI] = true;
    }

    if (obj->is_in_printing() && !obj->can_resume()) {
        enable[ACTION_BTN_LOAD] = false;
        enable[ACTION_BTN_UNLOAD] = false;
    }

    if (obj->ams_status_main == AMS_STATUS_MAIN_FILAMENT_CHANGE) {
        enable[ACTION_BTN_LOAD] = false;
        enable[ACTION_BTN_UNLOAD] = false;
    }

    // select current
    if (is_curr_tray_selected) {
        enable[ACTION_BTN_LOAD] = false;
    }

    if (!obj->is_filament_at_extruder()) {
        enable[ACTION_BTN_UNLOAD] = false;
    }
    
    if (obj->ams_exist_bits == 0) {
        if (obj->is_in_printing()) {
            if (!obj->can_resume()) {
                enable[ACTION_BTN_LOAD] = false;
                enable[ACTION_BTN_UNLOAD] = false;
            }
            else{
                if (obj->m_tray_now == "255") {
                    enable[ACTION_BTN_LOAD] = true;
                    enable[ACTION_BTN_UNLOAD] = false;
                }
                else if (obj->m_tray_now == std::to_string(VIRTUAL_TRAY_ID)) {
                    enable[ACTION_BTN_LOAD] = false;
                    enable[ACTION_BTN_UNLOAD] = true;
                }
            }
        }
        
    }
    else {
        if (obj->is_in_printing() /*&& obj->can_resume() && obj->m_tray_now != std::to_string(VIRTUAL_TRAY_ID) */) {

            if (!obj->can_resume()) {
                enable[ACTION_BTN_LOAD] = false;
                enable[ACTION_BTN_UNLOAD] = false;
            }
            else {
                if (obj->m_tray_now == "255") {

                    if ( m_ams_control->GetCurentAms() == std::to_string(VIRTUAL_TRAY_ID) ) {
                        enable[ACTION_BTN_LOAD] = true;
                        enable[ACTION_BTN_UNLOAD] = false;
                    }
                    else if (!m_ams_control->GetCurrentCan(m_ams_control->GetCurentAms()).empty()) {
                        enable[ACTION_BTN_LOAD] = false;
                        enable[ACTION_BTN_UNLOAD] = false;
                    } 
                }
                else if (obj->m_tray_now == std::to_string(VIRTUAL_TRAY_ID)) {
                    if (m_ams_control->GetCurentAms() == std::to_string(VIRTUAL_TRAY_ID)) {
                        enable[ACTION_BTN_LOAD] = false;
                        enable[ACTION_BTN_UNLOAD] = true;
                    }
                    else if (!m_ams_control->GetCurrentCan(m_ams_control->GetCurentAms()).empty()) {
                        enable[ACTION_BTN_LOAD] = false;
                        enable[ACTION_BTN_UNLOAD] = false;
                    }
                }
                else {
                    enable[ACTION_BTN_LOAD] = false;
                    enable[ACTION_BTN_UNLOAD] = false;
                }
            } 
        }
    }

//    if (obj->m_tray_now == "255") {
//        enable[ACTION_BTN_UNLOAD] = false;
//    }

    m_ams_control->SetActionState(enable);
}

void StatusPanel::update_cali(MachineObject *obj)
{
    if (!obj) return;

    if (obj->is_calibration_running()) {
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

void StatusPanel::update_calib_bitmap() {
    m_current_print_mode = PrintingTaskType::NOT_CLEAR;  //printing task might be changed when updating.
    if (calib_bitmap != nullptr) {
        delete calib_bitmap;
        calib_bitmap = nullptr;
    }
}

void StatusPanel::update_basic_print_data(bool def)
{
    if (def) {
        if (!obj) return;
        if (!obj->slice_info) return;
        wxString prediction = wxString::Format("%s", get_bbl_time_dhms(obj->slice_info->prediction));
        wxString weight = wxString::Format("%.2fg", obj->slice_info->weight);

        m_project_task_panel->show_priting_use_info(true, prediction, weight);
    }
    else {
        m_project_task_panel->show_priting_use_info(false, "0m", "0g");
    }
}

void StatusPanel::update_model_info()
{
    auto get_subtask_fn = [this](BBLModelTask* subtask) {
        CallAfter([this, subtask]() { 
            if (obj && obj->subtask_id_ == subtask->task_id) {
                obj->set_modeltask(subtask);
            }
        });
    };

     
    if (wxGetApp().getAgent() && obj) {
        BBLSubTask* curr_task = obj->get_subtask();
        if (curr_task) {
            BBLModelTask* curr_model_task = obj->get_modeltask();
            if (!curr_model_task && !request_model_info_flag) {
                curr_model_task = new BBLModelTask();
                curr_model_task->task_id = curr_task->task_id;
                request_model_info_flag = true;
                if (!curr_model_task->task_id.empty() && curr_model_task->task_id.compare("0") != 0) {
                    wxGetApp().getAgent()->get_subtask(curr_model_task,  get_subtask_fn);
                }
            }
        }
    }
}

void StatusPanel::update_subtask(MachineObject *obj)
{
    if (!obj) return;
    if (m_current_print_mode != PRINGINT) {
        if (calib_bitmap == nullptr) {
            m_calib_mode = get_obj_calibration_mode(obj, m_calib_method, cali_stage);
            if (m_calib_mode == CalibMode::Calib_None)
                m_current_print_mode = PRINGINT;
            // the printing task is calibrattion, not normal printing.
            else if (m_calib_mode != CalibMode::Calib_None) {
                m_current_print_mode = CALIBRATION;
                auto get_bitmap = [](wxString& png_path, int width, int height) {
                    wxImage image(width, height);
                    image.LoadFile(png_path, wxBITMAP_TYPE_PNG);
                    image = image.Scale(width, height, wxIMAGE_QUALITY_NORMAL);
                    return wxBitmap(image);
                };
                wxString png_path = "";
                int width = m_project_task_panel->get_bitmap_thumbnail()->GetSize().x;
                int height = m_project_task_panel->get_bitmap_thumbnail()->GetSize().y;
                if (m_calib_method == CALI_METHOD_AUTO) {
                    if (m_calib_mode == CalibMode::Calib_PA_Line) {
                        png_path = (boost::format("%1%/images/fd_calibration_auto.png") % resources_dir()).str();
                    }
                    else if (m_calib_mode == CalibMode::Calib_Flow_Rate) {
                        png_path = (boost::format("%1%/images/flow_rate_calibration_auto.png") % resources_dir()).str();
                    }

                }
                else if (m_calib_method == CALI_METHOD_MANUAL) {
                    if (m_calib_mode== CalibMode::Calib_PA_Line) {
                        if (cali_stage == 0) {  // Line mode
                            png_path = (boost::format("%1%/images/fd_calibration_manual.png") % resources_dir()).str();
                        }
                        else if (cali_stage == 1) { // Pattern mode
                            png_path = (boost::format("%1%/images/fd_pattern_manual_device.png") % resources_dir()).str();
                        }
                    }
                }
                if (png_path != "") {
                    calib_bitmap = new wxBitmap;
                    *calib_bitmap = get_bitmap(png_path, width, height);
                }
            }
        }
        if (calib_bitmap != nullptr)
            m_project_task_panel->set_thumbnail_img(*calib_bitmap);
    }
    
    if (obj->is_support_layer_num) {
        m_project_task_panel->update_layers_num(true);
    }
    else {
        m_project_task_panel->update_layers_num(false);
    }

    update_model_info();

    if (obj->is_system_printing()
        || obj->is_in_calibration()) {
        reset_printing_values();
    } else if (obj->is_in_printing() || obj->print_status == "FINISH") {
        if (obj->is_in_prepare() || obj->print_status == "SLICING") {
            m_project_task_panel->market_scoring_hide();
            m_project_task_panel->get_request_failed_panel()->Hide();
            m_project_task_panel->enable_abort_button(false);
            m_project_task_panel->enable_pause_resume_button(false, "pause_disable");
            wxString prepare_text;
            bool show_percent = true;

            if (obj->is_in_prepare()) {
                prepare_text = wxString::Format(_L("Downloading..."));
            }
            else if (obj->print_status == "SLICING") {
                if (obj->queue_number <= 0) {
                    prepare_text = wxString::Format(_L("Cloud Slicing..."));
                } else {
                    prepare_text = wxString::Format(_L("In Cloud Slicing Queue, there are %s tasks ahead."), std::to_string(obj->queue_number));
                    show_percent = false;
                }
            } else
                prepare_text = wxString::Format(_L("Downloading..."));

            if (obj->gcode_file_prepare_percent >= 0 && obj->gcode_file_prepare_percent <= 100 && show_percent)
                prepare_text += wxString::Format("(%d%%)", obj->gcode_file_prepare_percent);

            m_project_task_panel->update_stage_value(prepare_text, 0);
            m_project_task_panel->update_progress_percent(NA_STR, wxEmptyString);
            m_project_task_panel->update_left_time(NA_STR);
            m_project_task_panel->update_layers_num(true, wxString::Format(_L("Layer: %s"), NA_STR));
            m_project_task_panel->update_subtask_name(wxString::Format("%s", GUI::from_u8(obj->subtask_name)));


            if (obj->get_modeltask() && obj->get_modeltask()->design_id > 0) {
                m_project_task_panel->show_profile_info(true, wxString::FromUTF8(obj->get_modeltask()->profile_name));
            }
            else {
                m_project_task_panel->show_profile_info(false);
            }
            update_basic_print_data(false);
        } else {
            if (obj->can_resume()) {
                m_project_task_panel->enable_pause_resume_button(true, "resume");
            } else {
                 m_project_task_panel->enable_pause_resume_button(true, "pause");
            }

            // update printing stage
            m_project_task_panel->update_left_time(obj->mc_left_time);
            if (obj->subtask_) {
                m_project_task_panel->update_stage_value(obj->get_curr_stage(), obj->subtask_->task_progress);
                m_project_task_panel->update_progress_percent(wxString::Format("%d", obj->subtask_->task_progress), "%");
                m_project_task_panel->update_layers_num(true, wxString::Format(_L("Layer: %d/%d"), obj->curr_layer, obj->total_layers));

            } else {
                m_project_task_panel->update_stage_value(obj->get_curr_stage(), 0);
                m_project_task_panel->update_progress_percent(NA_STR, wxEmptyString);
                m_project_task_panel->update_layers_num(true, wxString::Format(_L("Layer: %s"), NA_STR));
            }

            if (obj->is_printing_finished()) {
                obj->update_model_task();
                m_project_task_panel->enable_abort_button(false);
                m_project_task_panel->enable_pause_resume_button(false, "resume_disable");
                // is makeworld subtask
                if (wxGetApp().has_model_mall() && obj->is_makeworld_subtask()) {
                    // has model mall rating result
                    if (obj && obj->rating_info && obj->rating_info->request_successful) {
                        m_project_task_panel->get_request_failed_panel()->Hide();
                        BOOST_LOG_TRIVIAL(info) << "model mall result request successful";
                        // has start count
                        if (!m_project_task_panel->get_star_count_dirty()) {
                            if (obj->rating_info->start_count > 0) {
                                m_project_task_panel->set_star_count(obj->rating_info->start_count);
                                m_project_task_panel->set_star_count_dirty(true);
                                BOOST_LOG_TRIVIAL(info) << "Initialize scores";
                                m_project_task_panel->get_market_scoring_button()->Enable(true);
                                m_project_task_panel->set_has_reted_text(true);
                            } else {
                                m_project_task_panel->set_star_count(0);
                                m_project_task_panel->set_star_count_dirty(false);
                                m_project_task_panel->get_market_scoring_button()->Enable(false);
                                m_project_task_panel->set_has_reted_text(false);
                            }
                        }
                        m_project_task_panel->market_scoring_show();
                    } else if (obj && obj->rating_info && !obj->rating_info->request_successful) {
                        BOOST_LOG_TRIVIAL(info) << "model mall result request failed";
                        if (403 != obj->rating_info->http_code) {
                            BOOST_LOG_TRIVIAL(info) << "Request need retry";
                            m_project_task_panel->get_market_retry_buttom()->Enable(!obj->get_model_mall_result_need_retry);
                            m_project_task_panel->get_request_failed_panel()->Show();
                        } else {
                            BOOST_LOG_TRIVIAL(info) << "Request rejected";
                        }
                    }
                } else {
                    m_project_task_panel->market_scoring_hide();
                }
            } else { // model printing is not finished, hide scoring page
                m_project_task_panel->enable_abort_button(true);
                m_project_task_panel->market_scoring_hide();
                m_project_task_panel->get_request_failed_panel()->Hide();
            }
        }

        m_project_task_panel->update_subtask_name(wxString::Format("%s", GUI::from_u8(obj->subtask_name)));

        if (obj->get_modeltask() && obj->get_modeltask()->design_id > 0) {
            m_project_task_panel->show_profile_info(true, wxString::FromUTF8(obj->get_modeltask()->profile_name));
        }
        else {
            m_project_task_panel->show_profile_info(false);
        }

        //update thumbnail
        if (obj->is_sdcard_printing()) {
            update_basic_print_data(false);
            update_sdcard_subtask(obj);
        } else {
            update_basic_print_data(true);
            update_cloud_subtask(obj);
        }
    } else {
        reset_printing_values();
    }

    Layout();
}

void StatusPanel::update_cloud_subtask(MachineObject *obj)
{
    if (!obj) return;
    if (!obj->subtask_) return;

    if (is_task_changed(obj)) {
        obj->set_modeltask(nullptr);
        reset_printing_values();
        BOOST_LOG_TRIVIAL(info) << "monitor: change to sub task id = " << obj->subtask_->task_id;
        if (web_request.IsOk() && web_request.GetState() == wxWebRequest::State_Active) {
            BOOST_LOG_TRIVIAL(info) << "web_request: cancelled";
            web_request.Cancel();
        }
        m_start_loading_thumbnail = true;
    }

    if (m_start_loading_thumbnail) {
        update_calib_bitmap();
        if (obj->slice_info) {
            m_request_url = wxString(obj->slice_info->thumbnail_url);
            if (!m_request_url.IsEmpty()) {
                wxImage                               img;
                std::map<wxString, wxImage>::iterator it = img_list.find(m_request_url);
                if (it != img_list.end()) {
                    if (m_current_print_mode != PrintingTaskType::CALIBRATION  ||(m_calib_mode == CalibMode::Calib_Flow_Rate && m_calib_method == CalibrationMethod::CALI_METHOD_MANUAL)) {
                        img = it->second;
                        wxImage resize_img = img.Scale(m_project_task_panel->get_bitmap_thumbnail()->GetSize().x, m_project_task_panel->get_bitmap_thumbnail()->GetSize().y);
                        m_project_task_panel->set_thumbnail_img(resize_img);
                        m_project_task_panel->set_brightness_value(get_brightness_value(resize_img));
                    }
                    if (this->obj) {
                        m_project_task_panel->set_plate_index(obj->m_plate_index);
                    } else {
                        m_project_task_panel->set_plate_index(-1);
                    }
                    task_thumbnail_state = ThumbnailState::TASK_THUMBNAIL;
                    BOOST_LOG_TRIVIAL(trace) << "web_request: use cache image";
                } else {
                    web_request = wxWebSession::GetDefault().CreateRequest(this, m_request_url);
                    BOOST_LOG_TRIVIAL(trace) << "monitor: start request thumbnail, url = " << m_request_url;
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
        update_calib_bitmap();
        if (m_current_print_mode != PrintingTaskType::CALIBRATION) {
            m_project_task_panel->get_bitmap_thumbnail()->SetBitmap(m_thumbnail_sdcard.bmp());
        }
        task_thumbnail_state = ThumbnailState::SDCARD_THUMBNAIL;
        m_load_sdcard_thumbnail = true;
    }
}

void StatusPanel::reset_printing_values()
{
    m_project_task_panel->enable_pause_resume_button(false, "pause_disable");
    m_project_task_panel->enable_abort_button(false);
    m_project_task_panel->reset_printing_value();
    m_project_task_panel->update_subtask_name(NA_STR);
    m_project_task_panel->show_profile_info(false);
    m_project_task_panel->update_stage_value(wxEmptyString, 0);
    m_project_task_panel->update_progress_percent(NA_STR, wxEmptyString);

    m_project_task_panel->market_scoring_hide();
    m_project_task_panel->get_request_failed_panel()->Hide();
    update_basic_print_data(false);
    m_project_task_panel->update_left_time(NA_STR);
    m_project_task_panel->update_layers_num(true, wxString::Format(_L("Layer: %s"), NA_STR));
    update_calib_bitmap();
    
    task_thumbnail_state = ThumbnailState::PLACE_HOLDER;
    m_start_loading_thumbnail = false;
    m_load_sdcard_thumbnail   = false;
    skip_print_error = 0;
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

    //check is at home
    if (event.GetInt() == 1
        || event.GetInt() == 3
        || event.GetInt() == 5
        || event.GetInt() == 7) {
        if (!obj->is_axis_at_home("X")) {
            BOOST_LOG_TRIVIAL(info) << "axis x is not at home";
            show_recenter_dialog();
            return;
        }
    }
    else if (event.GetInt() == 0
        || event.GetInt() == 2
        || event.GetInt() == 4
        || event.GetInt() == 6) {
        if (!obj->is_axis_at_home("Y")) {
            BOOST_LOG_TRIVIAL(info) << "axis y is not at home";
            show_recenter_dialog();
            return;
        }
    }
}

bool StatusPanel::check_axis_z_at_home(MachineObject* obj)
{
    if (obj) {
        if (!obj->is_axis_at_home("Z")) {
            BOOST_LOG_TRIVIAL(info) << "axis z is not at home";
            show_recenter_dialog();
            return false;
        }
        return true;
    }
    return false;
}

void StatusPanel::on_axis_ctrl_z_up_10(wxCommandEvent &event)
{    
    if (obj) {
        obj->command_axis_control("Z", 1.0, -10.0f, 900);
        if (!check_axis_z_at_home(obj))
            return;
    }
}

void StatusPanel::on_axis_ctrl_z_up_1(wxCommandEvent &event)
{
    if (obj) {
        obj->command_axis_control("Z", 1.0, -1.0f, 900);
        if (!check_axis_z_at_home(obj))
            return;
    }
}

void StatusPanel::on_axis_ctrl_z_down_1(wxCommandEvent &event)
{
    if (obj) {
        obj->command_axis_control("Z", 1.0, 1.0f, 900);
        if (!check_axis_z_at_home(obj))
            return;
    }
}

void StatusPanel::on_axis_ctrl_z_down_10(wxCommandEvent &event)
{
    if (obj) {
        obj->command_axis_control("Z", 1.0, 10.0f, 900);
        if (!check_axis_z_at_home(obj))
            return;
    }
}

void StatusPanel::axis_ctrl_e_hint(bool up_down)
{
    if (ctrl_e_hint_dlg == nullptr) {
        ctrl_e_hint_dlg = new SecondaryCheckDialog(this->GetParent(), wxID_ANY, _L("Warning"), SecondaryCheckDialog::ButtonStyle::CONFIRM_AND_CANCEL, wxDefaultPosition, wxDefaultSize, wxCLOSE_BOX | wxCAPTION, true);
        ctrl_e_hint_dlg->update_text(_L("Please heat the nozzle to above 170 degree before loading or unloading filament."));
        ctrl_e_hint_dlg->show_again_config_text = std::string("not_show_ectrl_hint");
    }
    if (up_down) {
        ctrl_e_hint_dlg->update_btn_label(_L("Confirm"), _L("Still unload"));
        ctrl_e_hint_dlg->Bind(EVT_SECONDARY_CHECK_CANCEL, [this](wxCommandEvent& e) {
            obj->command_axis_control("E", 1.0, -10.0f, 900);
            });
    }
    else {
        ctrl_e_hint_dlg->update_btn_label(_L("Confirm"), _L("Still load"));
        ctrl_e_hint_dlg->Bind(EVT_SECONDARY_CHECK_CANCEL, [this](wxCommandEvent& e) {
            obj->command_axis_control("E", 1.0, 10.0f, 900);
            });
    }
    ctrl_e_hint_dlg->on_show();
}

void StatusPanel::on_axis_ctrl_e_up_10(wxCommandEvent &event)
{
    if (obj) {
        if (obj->nozzle_temp >= TEMP_THRESHOLD_ALLOW_E_CTRL || (wxGetApp().app_config->get("not_show_ectrl_hint") == "1"))
            obj->command_axis_control("E", 1.0, -10.0f, 900);
        else
            axis_ctrl_e_hint(true);
    }
}

void StatusPanel::on_axis_ctrl_e_down_10(wxCommandEvent &event)
{
    if (obj) {
        if (obj->nozzle_temp >= TEMP_THRESHOLD_ALLOW_E_CTRL || (wxGetApp().app_config->get("not_show_ectrl_hint") == "1"))
            obj->command_axis_control("E", 1.0, 10.0f, 900);
        else
            axis_ctrl_e_hint(false);
    }
}

void StatusPanel::on_start_unload(wxCommandEvent &event)
{
    if (obj) obj->command_ams_switch(255);
}

void StatusPanel::on_set_bed_temp()
{
    wxString str = m_tempCtrl_bed->GetTextCtrl()->GetValue();
    try {
        long bed_temp;
        if (str.ToLong(&bed_temp) && obj) {
            set_hold_count(m_temp_bed_timeout);
            int limit = obj->get_bed_temperature_limit();
            if (bed_temp >= limit) {
                BOOST_LOG_TRIVIAL(info) << "can not set over limit = " << limit << ", set temp = " << bed_temp;
                bed_temp = limit;
                m_tempCtrl_bed->SetTagTemp(wxString::Format("%d", bed_temp));
                m_tempCtrl_bed->Warning(false);
            }
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
            if (nozzle_temp > m_tempCtrl_nozzle->get_max_temp()) {
                nozzle_temp = m_tempCtrl_nozzle->get_max_temp();
                m_tempCtrl_nozzle->SetTagTemp(wxString::Format("%d", nozzle_temp));
                m_tempCtrl_nozzle->Warning(false);
            }
            obj->command_set_nozzle(nozzle_temp);
        }
    } catch (...) {
        ;
    }
}

void StatusPanel::on_set_chamber_temp()
{
    wxString str = m_tempCtrl_chamber->GetTextCtrl()->GetValue();
    try {
        long chamber_temp;
        if (str.ToLong(&chamber_temp) && obj) {
            set_hold_count(m_temp_chamber_timeout);
            if (chamber_temp > m_tempCtrl_chamber->get_max_temp()) {
                chamber_temp = m_tempCtrl_chamber->get_max_temp();
                m_tempCtrl_chamber->SetTagTemp(wxString::Format("%d", chamber_temp));
                m_tempCtrl_chamber->Warning(false);
            }
            obj->command_set_chamber(chamber_temp);
        }
    }
    catch (...) {
        ;
    }
}

void StatusPanel::on_ams_load(SimpleEvent &event)
{
    BOOST_LOG_TRIVIAL(info) << "on_ams_load";
    on_ams_load_curr();
}

void StatusPanel::update_filament_step()
{
    m_ams_control->UpdateStepCtrl(obj->is_filament_at_extruder());
    if (!obj->is_filament_at_extruder()) {
        m_is_load_with_temp = true;
    }
    else {
        m_is_load_with_temp = false;
    }
}

void StatusPanel::on_ams_load_curr()
{
    if (obj) {
        std::string                            curr_ams_id = m_ams_control->GetCurentAms();
        std::string                            curr_can_id = m_ams_control->GetCurrentCan(curr_ams_id);

        
        update_filament_step();
        //virtual tray
        if (curr_ams_id.compare(std::to_string(VIRTUAL_TRAY_ID)) == 0) {
            int old_temp = -1;
            int new_temp = -1;
            AmsTray* curr_tray = &obj->vt_tray;

            if (!curr_tray) return;

            try {
                if (!curr_tray->nozzle_temp_max.empty() && !curr_tray->nozzle_temp_min.empty())
                    old_temp = (atoi(curr_tray->nozzle_temp_min.c_str()) + atoi(curr_tray->nozzle_temp_max.c_str())) / 2;
                if (!obj->vt_tray.nozzle_temp_max.empty() && !obj->vt_tray.nozzle_temp_min.empty())
                    new_temp = (atoi(obj->vt_tray.nozzle_temp_min.c_str()) + atoi(obj->vt_tray.nozzle_temp_max.c_str())) / 2;
            }
            catch (...) {
                ;
            }
            obj->command_ams_switch(VIRTUAL_TRAY_ID, old_temp, new_temp);
        }

        std::map<std::string, Ams*>::iterator it = obj->amsList.find(curr_ams_id);
        if (it == obj->amsList.end()) {
            BOOST_LOG_TRIVIAL(trace) << "ams: find " << curr_ams_id << " failed";
            return;
        }
        auto tray_it = it->second->trayList.find(curr_can_id);
        if (tray_it == it->second->trayList.end()) {
            BOOST_LOG_TRIVIAL(trace) << "ams: find " << curr_can_id << " failed";
            return;
        }
        AmsTray* curr_tray = obj->get_curr_tray();
        AmsTray* targ_tray = obj->get_ams_tray(curr_ams_id, curr_can_id);
        if (curr_tray && targ_tray) {
            int old_temp = -1;
            int new_temp = -1;
            try {
                if (!curr_tray->nozzle_temp_max.empty() && !curr_tray->nozzle_temp_min.empty())
                    old_temp = (atoi(curr_tray->nozzle_temp_min.c_str()) + atoi(curr_tray->nozzle_temp_max.c_str())) / 2;
                if (!targ_tray->nozzle_temp_max.empty() && !targ_tray->nozzle_temp_min.empty())
                    new_temp = (atoi(targ_tray->nozzle_temp_min.c_str()) + atoi(targ_tray->nozzle_temp_max.c_str())) / 2;
            }
            catch (...) {
                ;
            }
            int tray_index = atoi(curr_ams_id.c_str()) * 4 + atoi(tray_it->second->id.c_str());
            obj->command_ams_switch(tray_index, old_temp, new_temp);
        }
        else {
            int tray_index = atoi(curr_ams_id.c_str()) * 4 + atoi(tray_it->second->id.c_str());
            obj->command_ams_switch(tray_index, -1, -1);
        }
    }
}

void StatusPanel::on_ams_load_vams(wxCommandEvent& event) {
    BOOST_LOG_TRIVIAL(info) << "on_ams_load_vams_tray";

    m_ams_control->SwitchAms(std::to_string(VIRTUAL_TRAY_ID));
    on_ams_load_curr();
    if (m_print_error_dlg) {
        m_print_error_dlg->on_hide();
    }
}

void StatusPanel::on_ams_unload(SimpleEvent &event)
{
    if (obj) { obj->command_ams_switch(255); }
}

void StatusPanel::on_ams_filament_backup(SimpleEvent& event)
{
    if (obj) {
        AmsReplaceMaterialDialog* m_replace_material_popup = new AmsReplaceMaterialDialog(this);
        m_replace_material_popup->update_machine_obj(obj);
        m_replace_material_popup->ShowModal();
    }
}

void StatusPanel::on_ams_setting_click(SimpleEvent &event)
{
    if (!m_ams_setting_dlg) m_ams_setting_dlg = new AMSSetting((wxWindow *) this, wxID_ANY);
    if (obj) {
        update_ams_insert_material(obj);
        m_ams_setting_dlg->update_starting_read_mode(obj->ams_power_on_flag);
        m_ams_setting_dlg->update_ams_img(DeviceManager::get_printer_ams_img(obj->printer_type));
        std::string ams_id = m_ams_control->GetCurentShowAms();
        if (obj->amsList.size() == 0) {
            /* wxString txt = _L("AMS settings are not supported for external spool");
             MessageDialog msg_dlg(nullptr, txt, wxEmptyString, wxICON_WARNING | wxOK);
             msg_dlg.ShowModal();*/
            return;
        } else {
            try {
                int ams_id_int = atoi(ams_id.c_str());
                m_ams_setting_dlg->ams_id = ams_id_int;
                m_ams_setting_dlg->ams_support_remain = obj->is_support_update_remain;
                m_ams_setting_dlg->Show();
            }
            catch (...) {
                ;
            }
        }
    }
}

void StatusPanel::on_filament_extrusion_cali(wxCommandEvent &event)
{
    if (!m_extrusion_cali_dlg)
        m_extrusion_cali_dlg = new ExtrusionCalibration((wxWindow*)this, wxID_ANY);

    if (obj) {
        m_extrusion_cali_dlg->obj = obj;
        std::string ams_id = m_ams_control->GetCurentAms();
        std::string tray_id = m_ams_control->GetCurrentCan(ams_id);
        if (tray_id.empty() && ams_id.compare(std::to_string(VIRTUAL_TRAY_ID)) != 0) {
            wxString txt = _L("Please select an AMS slot before calibration");
            MessageDialog msg_dlg(nullptr, txt, wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return;
        }

        int ams_id_int  = 0;
        int tray_id_int = 0;


        // set ams_filament id is is bbl filament
        if (ams_id.compare(std::to_string(VIRTUAL_TRAY_ID)) == 0) {
            tray_id_int = VIRTUAL_TRAY_ID;
            m_extrusion_cali_dlg->ams_filament_id = "";
        }
        else {
            ams_id_int = atoi(ams_id.c_str());
            tray_id_int = atoi(tray_id.c_str());

            auto it = obj->amsList.find(ams_id);
            if (it != obj->amsList.end()) {
                auto tray_it = it->second->trayList.find(tray_id);
                if (tray_it != it->second->trayList.end()) {
                    if (MachineObject::is_bbl_filament(tray_it->second->tag_uid))
                        m_extrusion_cali_dlg->ams_filament_id = tray_it->second->setting_id;
                    else
                        m_extrusion_cali_dlg->ams_filament_id = "";
                }
            }
        }

        try {
            m_extrusion_cali_dlg->ams_id = ams_id_int;
            m_extrusion_cali_dlg->tray_id = tray_id_int;
            m_extrusion_cali_dlg->SetPosition(m_staticText_control->GetScreenPosition());
            m_extrusion_cali_dlg->Popup();
        } catch(...) {
            ;
        }
    }
}

void StatusPanel::on_filament_edit(wxCommandEvent &event)
{
    // update params
    if (!m_filament_setting_dlg) m_filament_setting_dlg = new AMSMaterialsSetting((wxWindow *) this, wxID_ANY);

    int current_position_x = m_ams_control->GetScreenPosition().x;
    int current_position_y = m_ams_control->GetScreenPosition().y - FromDIP(40);
    auto drect = wxDisplay(GetParent()).GetGeometry().GetHeight() - FromDIP(50);
    current_position_y = current_position_y + m_filament_setting_dlg->GetSize().GetHeight() > drect ? drect - m_filament_setting_dlg->GetSize().GetHeight() : current_position_y;

    if (obj) {
        m_filament_setting_dlg->obj = obj;
        std::string ams_id          = m_ams_control->GetCurentAms();
        int ams_id_int = 0;
        int tray_id_int = 0;
        if (ams_id.compare(std::to_string(VIRTUAL_TRAY_ID)) == 0) {
            tray_id_int = VIRTUAL_TRAY_ID;
            m_filament_setting_dlg->ams_id = ams_id_int;
            m_filament_setting_dlg->tray_id = tray_id_int;
            wxString k_val;
            wxString n_val;
            k_val = wxString::Format("%.3f", obj->vt_tray.k);
            n_val = wxString::Format("%.3f", obj->vt_tray.n);
            m_filament_setting_dlg->Move(wxPoint(current_position_x, current_position_y));
            m_filament_setting_dlg->Popup(wxEmptyString, wxEmptyString, wxEmptyString, wxEmptyString, k_val, n_val);
        } else {
            std::string tray_id = event.GetString().ToStdString(); // m_ams_control->GetCurrentCan(ams_id);
            try {
                ams_id_int = atoi(ams_id.c_str());
                tray_id_int = atoi(tray_id.c_str());
                m_filament_setting_dlg->ams_id = ams_id_int;
                m_filament_setting_dlg->tray_id = tray_id_int;

                std::string sn_number;
                std::string filament;
                std::string temp_max;
                std::string temp_min;
                wxString k_val;
                wxString n_val;
                auto it = obj->amsList.find(ams_id);
                if (it != obj->amsList.end()) {
                    auto tray_it = it->second->trayList.find(tray_id);
                    if (tray_it != it->second->trayList.end()) {
                        k_val = wxString::Format("%.3f", tray_it->second->k);
                        n_val = wxString::Format("%.3f", tray_it->second->n);
                        wxColor color = AmsTray::decode_color(tray_it->second->color);
                        //m_filament_setting_dlg->set_color(color);

                        std::vector<wxColour> cols;
                        for (auto col : tray_it->second->cols) {
                            cols.push_back( AmsTray::decode_color(col));
                        }
                        m_filament_setting_dlg->set_ctype(tray_it->second->ctype);
                        m_filament_setting_dlg->ams_filament_id = tray_it->second->setting_id;

                        if (m_filament_setting_dlg->ams_filament_id.empty()) {
                            m_filament_setting_dlg->set_empty_color(color);
                        }
                        else {
                            m_filament_setting_dlg->set_color(color);
                            m_filament_setting_dlg->set_colors(cols);
                        }

                        m_filament_setting_dlg->m_is_third = !MachineObject::is_bbl_filament(tray_it->second->tag_uid);
                        if (!m_filament_setting_dlg->m_is_third) {
                            sn_number = tray_it->second->uuid;
                            filament = tray_it->second->sub_brands;
                            temp_max = tray_it->second->nozzle_temp_max;
                            temp_min = tray_it->second->nozzle_temp_min;
                        }
                    }
                }

                m_filament_setting_dlg->Move(wxPoint(current_position_x, current_position_y));
                m_filament_setting_dlg->Popup(filament, sn_number, temp_min, temp_max, k_val, n_val);
            }
            catch (...) {
                ;
            }
        }
    }
}

void StatusPanel::on_ext_spool_edit(wxCommandEvent &event)
{
    // update params
    if (!m_filament_setting_dlg) m_filament_setting_dlg = new AMSMaterialsSetting((wxWindow*)this, wxID_ANY);

    int current_position_x = m_ams_control->GetScreenPosition().x;
    int current_position_y = m_ams_control->GetScreenPosition().y - FromDIP(40);
    auto drect = wxDisplay(GetParent()).GetGeometry().GetHeight() - FromDIP(50);
    current_position_y = current_position_y + m_filament_setting_dlg->GetSize().GetHeight() > drect ? drect - m_filament_setting_dlg->GetSize().GetHeight() : current_position_y;

    if (obj) {
        m_filament_setting_dlg->obj = obj;
        try {
            m_filament_setting_dlg->tray_id = VIRTUAL_TRAY_ID;
            std::string sn_number;
            std::string filament;
            std::string temp_max;
            std::string temp_min;
            wxString k_val;
            wxString n_val;
            k_val = wxString::Format("%.3f", obj->vt_tray.k);
            n_val = wxString::Format("%.3f", obj->vt_tray.n);
            wxColor color = AmsTray::decode_color(obj->vt_tray.color);
            m_filament_setting_dlg->ams_filament_id = obj->vt_tray.setting_id;

            std::vector<wxColour> cols;
            for (auto col : obj->vt_tray.cols) {
                cols.push_back(AmsTray::decode_color(col));
            }
            m_filament_setting_dlg->set_ctype(obj->vt_tray.ctype);

            if (m_filament_setting_dlg->ams_filament_id.empty()) {
                m_filament_setting_dlg->set_empty_color(color);
            }
            else {
                m_filament_setting_dlg->set_color(color);
                m_filament_setting_dlg->set_colors(cols);

            }
            
            m_filament_setting_dlg->m_is_third = !MachineObject::is_bbl_filament(obj->vt_tray.tag_uid);
            if (!m_filament_setting_dlg->m_is_third) {
                sn_number = obj->vt_tray.uuid;
                filament = obj->vt_tray.sub_brands;
                temp_max = obj->vt_tray.nozzle_temp_max;
                temp_min = obj->vt_tray.nozzle_temp_min;
            }

            m_filament_setting_dlg->Move(wxPoint(current_position_x,current_position_y));
            m_filament_setting_dlg->Popup(filament, sn_number, temp_min, temp_max, k_val, n_val);
        }
        catch (...) {
            ;
        }
    }
}

void StatusPanel::on_ams_refresh_rfid(wxCommandEvent &event)
{
    if (obj) {

        if (obj->is_filament_at_extruder()) {
            MessageDialog msg_dlg(
                nullptr,
                _L("Cannot read filament info: the filament is loaded to the tool head,please unload the filament and try again."),
                wxEmptyString,
                wxICON_WARNING | wxYES);
            msg_dlg.ShowModal();
            return;
        }

        std::string curr_ams_id = m_ams_control->GetCurentAms();
        // do not support refresh rfid for VIRTUAL_TRAY_ID
        if (curr_ams_id.compare(std::to_string(VIRTUAL_TRAY_ID)) == 0) {
            return;
        }
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
        if (curr_ams_id.compare(std::to_string(VIRTUAL_TRAY_ID)) == 0) {
            //update_ams_control_state(curr_ams_id, true);
            return;
        } else {
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
}

void StatusPanel::on_ams_guide(wxCommandEvent& event)
{
    wxString ams_wiki_url = "https://wiki.bambulab.com/en/software/bambu-studio/use-ams-on-bambu-studio";
    wxLaunchDefaultBrowser(ams_wiki_url);
}

void StatusPanel::on_ams_retry(wxCommandEvent& event)
{
    BOOST_LOG_TRIVIAL(info) << "on_ams_retry";
    if (obj) {
        obj->command_ams_control("resume");
    }
}

void StatusPanel::on_print_error_done(wxCommandEvent& event)
{
    BOOST_LOG_TRIVIAL(info) << "on_print_error_done";
    if (obj) {
        obj->command_ams_control("done");
        if (m_print_error_dlg) {
            m_print_error_dlg->on_hide();
        }if (m_print_error_dlg_no_action) {
            m_print_error_dlg_no_action->on_hide();
        }
    }
}

void StatusPanel::on_fan_changed(wxCommandEvent& event)
{
    auto type = event.GetInt();
    auto speed = atoi(event.GetString().c_str());

    if (type == MachineObject::FanType::COOLING_FAN) {
        set_hold_count(this->m_switch_nozzle_fan_timeout);
        m_switch_nozzle_fan->SetValue(speed > 0 ? true : false);
        m_switch_nozzle_fan->setFanValue(speed * 10);
    }
    else if (type == MachineObject::FanType::BIG_COOLING_FAN) {
        set_hold_count(this->m_switch_printing_fan_timeout);
        m_switch_printing_fan->SetValue(speed > 0 ? true : false);
        m_switch_printing_fan->setFanValue(speed * 10);
    }
    else if (type == MachineObject::FanType::CHAMBER_FAN) {
        set_hold_count(this->m_switch_cham_fan_timeout);
        m_switch_cham_fan->SetValue(speed > 0 ? true : false);
        m_switch_cham_fan->setFanValue(speed * 10);
    }
}

void StatusPanel::on_cham_temp_kill_focus(wxFocusEvent& event)
{
    event.Skip();
    cham_temp_input = false;
}

void StatusPanel::on_cham_temp_set_focus(wxFocusEvent& event)
{
    event.Skip();
    cham_temp_input = true;
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
    PopupWindow *popUp = new PopupWindow(nullptr);
#else
    PopupWindow *popUp = new PopupWindow(m_switch_speed);
#endif
    popUp->SetBackgroundColour(StateColor::darkModeColorFor(0xeeeeee));
    StepCtrl *step = new StepCtrl(popUp, wxID_ANY);
    wxSizer *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(step, 1, wxEXPAND, 0);
    popUp->SetSizer(sizer);
    auto em = em_unit(this);
    popUp->SetSize(em * 36, em * 8);
    step->SetHint(_L("This only takes effect during printing"));
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

    if (!obj->is_in_printing()) {
        step->Bind(wxEVT_LEFT_DOWN, [](auto& e) {
            return; });
    }

    step->Bind(EVT_STEP_CHANGED, [this](auto &e) {
        this->speed_lvl        = e.GetInt() + 1;
        if (obj) {
            set_hold_count(this->speed_lvl_timeout);
            obj->command_set_printing_speed((PrintingSpeedLevel)this->speed_lvl);
        }
    });
    popUp->Bind(wxEVT_SHOW, [this, popUp](auto &e) {
        if (!e.IsShown()) {
            popUp->Destroy();
            m_showing_speed_popup = false;
            speed_dismiss_time = boost::posix_time::microsec_clock::universal_time();
        }
        });
    
    m_ams_control->Bind(EVT_CLEAR_SPEED_CONTROL, [this, popUp](auto& e) {
        if (m_showing_speed_popup) {
            if (popUp && popUp->IsShown()) {
                popUp->Show(false);
            }
        }
        e.Skip();
    });
    wxPoint pos = m_switch_speed->ClientToScreen(wxPoint(0, -6));
    popUp->Position(pos, {0, m_switch_speed->GetSize().y + 12});
    popUp->Popup();
    m_showing_speed_popup = true;
}

void StatusPanel::on_printing_fan_switch(wxCommandEvent &event)
{
   /* if (!obj) return;

    bool value = m_switch_printing_fan->GetValue();

    if (value) {
        obj->command_control_fan(MachineObject::FanType::BIG_COOLING_FAN, true);
        m_switch_printing_fan->SetValue(true);
        set_hold_count(this->m_switch_printing_fan_timeout);
    } else {
        obj->command_control_fan(MachineObject::FanType::BIG_COOLING_FAN, false);
        m_switch_printing_fan->SetValue(false);
        set_hold_count(this->m_switch_printing_fan_timeout);
    }*/
}

void StatusPanel::on_nozzle_fan_switch(wxCommandEvent &event)
{
    m_fan_control_popup->Destroy();
    m_fan_control_popup = nullptr;
    m_fan_control_popup = new FanControlPopup(this);

    if (obj) {
        m_fan_control_popup->show_cham_fan(obj->is_support_chamber_fan);
        m_fan_control_popup->show_aux_fan(obj->is_support_aux_fan);
    }

    auto pos = m_switch_nozzle_fan->GetScreenPosition();
    pos.y = pos.y + m_switch_nozzle_fan->GetSize().y;

    int display_idx = wxDisplay::GetFromWindow(this);
    auto display = wxDisplay(display_idx).GetClientArea();


    wxSize screenSize = wxSize(display.GetWidth(), display.GetHeight());
    auto fan_popup_size = m_fan_control_popup->GetSize();

    if (screenSize.y - fan_popup_size.y < FromDIP(300)) {
        pos.x += FromDIP(50);
        pos.y = (screenSize.y - fan_popup_size.y) / 2;
    }
    m_fan_control_popup->SetPosition(pos);
    m_fan_control_popup->Popup();



    /*if (!obj) return;

    bool value = m_switch_nozzle_fan->GetValue();

    if (value) {
        obj->command_control_fan(MachineObject::FanType::COOLING_FAN, true);
        m_switch_nozzle_fan->SetValue(true);
        set_hold_count(this->m_switch_nozzle_fan_timeout);
    } else {
        obj->command_control_fan(MachineObject::FanType::COOLING_FAN, false);
        m_switch_nozzle_fan->SetValue(false);
        set_hold_count(this->m_switch_nozzle_fan_timeout);
    }*/
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

void StatusPanel::on_switch_vcamera(wxMouseEvent &event)
{
    //if (!obj) return;
    //bool value = m_recording_button->get_switch_status();
    //obj->command_ipcam_record(!value);
    m_media_play_ctrl->ToggleStream();
    show_vcamera = m_media_play_ctrl->IsStreaming();
    if (m_camera_popup)
        m_camera_popup->sync_vcamera_state(show_vcamera);
}

void StatusPanel::on_camera_enter(wxMouseEvent& event)
{
    if (obj) {
        if (m_camera_popup == nullptr)
            m_camera_popup = std::make_shared<CameraPopup>(this);
        m_camera_popup->check_func_supported(obj);
        m_camera_popup->sync_vcamera_state(show_vcamera);
        m_camera_popup->Bind(EVT_VCAMERA_SWITCH, &StatusPanel::on_switch_vcamera, this);
        m_camera_popup->Bind(EVT_SDCARD_ABSENT_HINT, [this](wxCommandEvent &e) {
            if (sdcard_hint_dlg == nullptr) {
                sdcard_hint_dlg = new SecondaryCheckDialog(this->GetParent(), wxID_ANY, _L("Warning"), SecondaryCheckDialog::ButtonStyle::ONLY_CONFIRM);
                sdcard_hint_dlg->update_text(_L("Can't start this without SD card."));
            }
            sdcard_hint_dlg->on_show();
            });
        m_camera_popup->Bind(EVT_CAM_SOURCE_CHANGE, &StatusPanel::on_camera_source_change, this);
        wxWindow* ctrl = (wxWindow*)event.GetEventObject();
        wxPoint   pos = ctrl->ClientToScreen(wxPoint(0, 0));
        wxSize    sz = ctrl->GetSize();
        pos.x += sz.x;
        pos.y += sz.y;
        m_camera_popup->SetPosition(pos);
        m_camera_popup->update(m_media_play_ctrl->IsStreaming());
        m_camera_popup->Popup();
    }
}

void StatusBasePanel::on_camera_source_change(wxCommandEvent& event)
{
    handle_camera_source_change();
}

void StatusBasePanel::handle_camera_source_change()
{
    const auto new_cam_url = wxGetApp().app_config->get("camera", "custom_source");
    const auto enabled = wxGetApp().app_config->get("camera", "enable_custom_source") == "true";

    if (enabled && !new_cam_url.empty()) {
        m_custom_camera_view->LoadURL(new_cam_url);
        toggle_custom_camera();
        m_camera_switch_button->Show();
    } else {
        toggle_builtin_camera();
        m_camera_switch_button->Hide();
    }
}

void StatusBasePanel::toggle_builtin_camera()
{
    m_custom_camera_view->Hide();
    m_media_ctrl->Show();
    m_media_play_ctrl->Show();
}

void StatusBasePanel::toggle_custom_camera()
{
    const auto enabled = wxGetApp().app_config->get("camera", "enable_custom_source") == "true";

    if (enabled) {
        m_custom_camera_view->Show();
        m_media_ctrl->Hide();
        m_media_play_ctrl->Hide();
    }
}

void StatusBasePanel::on_camera_switch_toggled(wxMouseEvent& event)
{
    const auto enabled = wxGetApp().app_config->get("camera", "enable_custom_source") == "true";
    if (enabled && m_media_ctrl->IsShown()) {
        toggle_custom_camera();
    } else {
        toggle_builtin_camera();
    }
}

void StatusBasePanel::remove_controls()
{
    const std::string js_cleanup_video_element = R"(
        document.body.style.overflow='hidden';
        const video = document.querySelector('video');
        video.setAttribute('style', 'width: 100% !important;');
        video.removeAttribute('controls');
        video.addEventListener('leavepictureinpicture', () => {
            window.wx.postMessage('leavepictureinpicture');
        });
        video.addEventListener('enterpictureinpicture', () => {
            window.wx.postMessage('enterpictureinpicture');
        });
    )";
    m_custom_camera_view->RunScript(js_cleanup_video_element);
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


void StatusPanel::on_show_print_options(wxCommandEvent& event)
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


void StatusPanel::on_show_parts_options(wxCommandEvent &event)
{
    if (obj) {
        if (print_parts_dlg == nullptr) {
            print_parts_dlg = new PrinterPartsDialog(this);
            print_parts_dlg->update_machine_obj(obj);
            print_parts_dlg->ShowModal();
        }
        else {
            print_parts_dlg->update_machine_obj(obj);
            print_parts_dlg->ShowModal();
        }
    }
}

void StatusPanel::on_start_calibration(wxCommandEvent &event)
{
    if (obj) {
        if (calibration_dlg == nullptr) {
            calibration_dlg = new CalibrationDialog();
            calibration_dlg->update_machine_obj(obj);
            calibration_dlg->update_cali(obj);
            calibration_dlg->ShowModal();
        } else {
            calibration_dlg->update_machine_obj(obj);
            calibration_dlg->update_cali(obj);
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
    m_temp_chamber_timeout = 0;
    m_switch_nozzle_fan_timeout = 0;
    m_switch_printing_fan_timeout = 0;
    m_switch_cham_fan_timeout = 0;
    m_show_ams_group = false;
    reset_printing_values();

    m_bitmap_timelapse_img->Hide();
    m_bitmap_recording_img->Hide();
    m_bitmap_vcamera_img->Hide();
    m_setting_button->Show();
    m_tempCtrl_chamber->Show();
    m_options_btn->Show();
    m_parts_btn->Show();

    reset_temp_misc_control();
    m_ams_control->Hide();
    m_ams_control_box->Hide();
    m_ams_control->Reset();
    error_info_reset();
    SetFocus();
}

void StatusPanel::show_status(int status)
{
    if (last_status == status) return;
    last_status = status;

    if (((status & (int) MonitorStatus::MONITOR_DISCONNECTED) != 0)
     || ((status & (int) MonitorStatus::MONITOR_DISCONNECTED_SERVER) != 0)
     || ((status & (int)MonitorStatus::MONITOR_CONNECTING) != 0)
     || ((status & (int)MonitorStatus::MONITOR_NO_PRINTER) != 0)
        ) {
        show_printing_status(false, false);
        m_calibration_btn->Disable();
        m_options_btn->Disable();
        m_parts_btn->Disable();
        m_panel_monitoring_title->Disable();
    } else if ((status & (int) MonitorStatus::MONITOR_NORMAL) != 0) {
        show_printing_status(true, true);
        m_calibration_btn->Disable();
        m_options_btn->Enable();
        m_parts_btn->Enable();
        m_panel_monitoring_title->Enable();
    }
}

void StatusPanel::set_hold_count(int& count)
{
    count = COMMAND_TIMEOUT;
}

void StatusPanel::rescale_camera_icons()
{
    m_setting_button->msw_rescale();


    m_bitmap_sdcard_state_abnormal = ScalableBitmap(this, wxGetApp().dark_mode()?"sdcard_state_abnormal_dark":"sdcard_state_abnormal", 20);
    m_bitmap_sdcard_state_normal = ScalableBitmap(this, wxGetApp().dark_mode()?"sdcard_state_normal_dark":"sdcard_state_normal", 20);
    m_bitmap_sdcard_state_no = ScalableBitmap(this, wxGetApp().dark_mode()?"sdcard_state_no_dark":"sdcard_state_no", 20);
    m_bitmap_recording_on = ScalableBitmap(this, wxGetApp().dark_mode()?"monitor_recording_on_dark":"monitor_recording_on", 20);
    m_bitmap_recording_off = ScalableBitmap(this, wxGetApp().dark_mode()?"monitor_recording_off_dark":"monitor_recording_off", 20);
    m_bitmap_timelapse_on = ScalableBitmap(this, wxGetApp().dark_mode()?"monitor_timelapse_on_dark":"monitor_timelapse_on", 20);
    m_bitmap_timelapse_off = ScalableBitmap(this, wxGetApp().dark_mode()?"monitor_timelapse_off_dark":"monitor_timelapse_off", 20);
    m_bitmap_vcamera_on = ScalableBitmap(this, wxGetApp().dark_mode()?"monitor_vcamera_on_dark":"monitor_vcamera_on", 20);
    m_bitmap_vcamera_off = ScalableBitmap(this, wxGetApp().dark_mode()?"monitor_vcamera_off_dark":"monitor_vcamera_off", 20);

    if (m_media_play_ctrl->IsStreaming()) {
        m_bitmap_vcamera_img->SetBitmap(m_bitmap_vcamera_on.bmp());
    }
    else {
        m_bitmap_vcamera_img->SetBitmap(m_bitmap_vcamera_off.bmp());
    }

    if (!obj) return;

    if (obj->get_sdcard_state() == MachineObject::SdcardState::NO_SDCARD) {
        m_bitmap_sdcard_img->SetBitmap(m_bitmap_sdcard_state_no.bmp());
    } else if (obj->get_sdcard_state() == MachineObject::SdcardState::HAS_SDCARD_NORMAL) {
        m_bitmap_sdcard_img->SetBitmap(m_bitmap_sdcard_state_normal.bmp());
    } else if (obj->get_sdcard_state() == MachineObject::SdcardState::HAS_SDCARD_ABNORMAL) {
        m_bitmap_sdcard_img->SetBitmap(m_bitmap_sdcard_state_abnormal.bmp());
    } else {
        m_bitmap_sdcard_img->SetBitmap(m_bitmap_sdcard_state_normal.bmp());
    }

    if (obj->is_recording()) {
        m_bitmap_recording_img->SetBitmap(m_bitmap_recording_on.bmp());
    } else {
        m_bitmap_recording_img->SetBitmap(m_bitmap_recording_off.bmp());
    }

    if (obj->is_timelapse()) {
        m_bitmap_timelapse_img->SetBitmap(m_bitmap_timelapse_on.bmp());
    } else {
        m_bitmap_timelapse_img->SetBitmap(m_bitmap_timelapse_off.bmp());
    }
}

void StatusPanel::on_sys_color_changed()
{
    m_project_task_panel->msw_rescale();
    m_bitmap_speed.msw_rescale();
    m_bitmap_speed_active.msw_rescale();
    m_switch_speed->SetImages(m_bitmap_speed, m_bitmap_speed);
    m_ams_control->msw_rescale();
    if (m_print_error_dlg) { m_print_error_dlg->msw_rescale(); }
    if (m_filament_setting_dlg) {m_filament_setting_dlg->msw_rescale();}
    rescale_camera_icons();
}

void StatusPanel::msw_rescale()
{
    init_bitmaps();
    m_project_task_panel->init_bitmaps();
    m_project_task_panel->msw_rescale();
    m_panel_monitoring_title->SetSize(wxSize(-1, FromDIP(PAGE_TITLE_HEIGHT)));
    //m_staticText_monitoring->SetMinSize(wxSize(PAGE_TITLE_TEXT_WIDTH, PAGE_TITLE_HEIGHT));
    m_bmToggleBtn_timelapse->Rescale();
    m_panel_control_title->SetSize(wxSize(-1, FromDIP(PAGE_TITLE_HEIGHT)));
    //m_staticText_control->SetMinSize(wxSize(-1, PAGE_TITLE_HEIGHT));
    m_media_play_ctrl->msw_rescale();
    m_bpButton_xy->SetBitmap(m_bitmap_axis_home);
    m_bpButton_xy->SetMinSize(AXIS_MIN_SIZE);
    m_bpButton_xy->SetSize(AXIS_MIN_SIZE);
    m_temp_extruder_line->SetSize(wxSize(FromDIP(1), -1));
    update_extruder_status(obj);
    m_bitmap_extruder_img->SetMinSize(EXTRUDER_IMAGE_SIZE);

    for (Button *btn : m_buttons) { btn->Rescale(); }
    init_scaled_buttons();


    m_bpButton_xy->Rescale();
    m_tempCtrl_nozzle->SetMinSize(TEMP_CTRL_MIN_SIZE);
    m_tempCtrl_nozzle->Rescale();
    m_line_nozzle->SetSize(wxSize(-1, FromDIP(1)));
    m_tempCtrl_bed->SetMinSize(TEMP_CTRL_MIN_SIZE);
    m_tempCtrl_bed->Rescale();
    m_tempCtrl_chamber->SetMinSize(TEMP_CTRL_MIN_SIZE);
    m_tempCtrl_chamber->Rescale();

    m_bitmap_speed.msw_rescale();
    m_bitmap_speed_active.msw_rescale();

    m_switch_speed->SetImages(m_bitmap_speed, m_bitmap_speed);
    m_switch_speed->SetMinSize(MISC_BUTTON_2FAN_SIZE);
    m_switch_speed->Rescale();
    m_switch_lamp->SetImages(m_bitmap_lamp_on, m_bitmap_lamp_off);
    m_switch_lamp->SetMinSize(MISC_BUTTON_2FAN_SIZE);
    m_switch_lamp->Rescale();
    m_switch_nozzle_fan->SetImages(m_bitmap_fan_on, m_bitmap_fan_off);
    m_switch_nozzle_fan->Rescale();
    m_switch_printing_fan->SetImages(m_bitmap_fan_on, m_bitmap_fan_off);
    m_switch_printing_fan->Rescale();
    m_switch_cham_fan->SetImages(m_bitmap_fan_on, m_bitmap_fan_off);
    m_switch_cham_fan->Rescale();


    m_ams_control->msw_rescale();
    // m_filament_step->Rescale();


    m_calibration_btn->SetMinSize(wxSize(-1, FromDIP(26)));
    m_calibration_btn->Rescale();

    m_options_btn->SetMinSize(wxSize(-1, FromDIP(26)));
    m_options_btn->Rescale(); 
    
    m_parts_btn->SetMinSize(wxSize(-1, FromDIP(26)));
    m_parts_btn->Rescale();

    rescale_camera_icons();

    Layout();
    Refresh();
}

ScoreDialog::ScoreDialog(wxWindow *parent, int design_id, std::string model_id, int profile_id, int rating_id, bool success_printed, int star_count)
    : DPIDialog(parent, wxID_ANY, _L("Rate the Print Profile"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
    , m_design_id(design_id)
    , m_model_id(model_id)
    , m_profile_id(profile_id)
    , m_star_count(star_count)
    , m_rating_id(rating_id)
    , m_success_printed(success_printed)
    , m_upload_status_code(StatusCode::CODE_NUMBER)
{
    m_tocken.reset(new int(0));

    wxBoxSizer *m_main_sizer = get_main_sizer();

    this->SetSizer(m_main_sizer);
    Fit();
    Layout();
    wxGetApp().UpdateDlgDarkUI(this);
}

ScoreDialog::ScoreDialog(wxWindow *parent, ScoreData *score_data)
    : DPIDialog(parent, wxID_ANY, _L("Rate the Print Profile"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
    , m_design_id(score_data->design_id)
    , m_rating_id(score_data->rating_id)
    , m_model_id(score_data->model_id)
    , m_profile_id(score_data->profile_id)
    , m_star_count(score_data->star_count)
    , m_success_printed(score_data->success_printed)
    , m_upload_status_code(StatusCode::CODE_NUMBER)
{
    m_tocken.reset(new int(0));
    
    wxBoxSizer *m_main_sizer = get_main_sizer(score_data->local_to_url_image, score_data->comment_text);

    m_image_url_paths        = score_data->image_url_paths;
    

    this->SetSizer(m_main_sizer);
    Fit();
    Layout();
    wxGetApp().UpdateDlgDarkUI(this);

}

ScoreDialog::~ScoreDialog() {}

void ScoreDialog::on_dpi_changed(const wxRect &suggested_rect) {}

void ScoreDialog::OnBitmapClicked(wxMouseEvent &event)
{
    wxStaticBitmap *clickedBitmap = dynamic_cast<wxStaticBitmap *>(event.GetEventObject());
    if (m_image.find(clickedBitmap) != m_image.end()) { 
        if (!m_image[clickedBitmap].is_selected) {
            for (auto panel : m_image[clickedBitmap].image_broad) { 
                panel->Show();
            }
            m_image[clickedBitmap].is_selected = true;
            m_selected_image_list.insert(clickedBitmap);
        } else {
            for (auto panel : m_image[clickedBitmap].image_broad) { 
                panel->Hide(); 
            }
            m_image[clickedBitmap].is_selected = false;
            m_selected_image_list.erase(clickedBitmap);
            m_selected_image_list.erase(clickedBitmap);
        }
    }
    if (m_selected_image_list.empty())
        m_delete_photo->Hide();
    else
        m_delete_photo->Show();
    Fit();
    Layout();

}

 std::set <std::pair<wxStaticBitmap * ,wxString>> ScoreDialog::add_need_upload_imgs()
{ 
    std::set<std::pair<wxStaticBitmap *, wxString>> need_upload_images;
    for (auto bitmap : m_image) { 
        if (!bitmap.second.is_uploaded) {
            wxString &local_image_path = bitmap.second.local_image_url;
            if (!local_image_path.empty()) { need_upload_images.insert(std::make_pair(bitmap.first, local_image_path)); }
        }
    }
    return need_upload_images;
}



std::pair<wxStaticBitmap *, ScoreDialog::ImageMsg> ScoreDialog::create_local_thumbnail(wxString &local_path)
{
    std::pair<wxStaticBitmap *, ImageMsg> bitmap_to_image_msg;
    if (local_path.empty()) return bitmap_to_image_msg;

    ImageMsg cur_image_msg;
    cur_image_msg.local_image_url = local_path;
    cur_image_msg.img_url_paths   = "";
    cur_image_msg.is_uploaded     = false;
    
    wxStaticBitmap *imageCtrl = new wxStaticBitmap(this, wxID_ANY, wxBitmap(wxImage(local_path, wxBITMAP_TYPE_ANY).Rescale(FromDIP(80), FromDIP(60))), wxDefaultPosition,
                                                   wxDefaultSize, 0);
    imageCtrl->Bind(wxEVT_LEFT_DOWN, &ScoreDialog::OnBitmapClicked, this);

    m_image_sizer->Add(create_broad_sizer(imageCtrl, cur_image_msg), 0, wxALL, 5);

    bitmap_to_image_msg.first = imageCtrl;
    bitmap_to_image_msg.second = cur_image_msg;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": local picture is download";
    return bitmap_to_image_msg;
}

std::pair<wxStaticBitmap *, ScoreDialog::ImageMsg> ScoreDialog::create_oss_thumbnail(std::string &oss_path)
{
    std::pair<wxStaticBitmap *, ImageMsg> bitmap_to_image_msg;
    if (oss_path.empty()) return bitmap_to_image_msg;

    ImageMsg cur_image_msg;
    cur_image_msg.local_image_url = "";
    cur_image_msg.img_url_paths   = oss_path;
    cur_image_msg.is_uploaded     = true;


    wxImage         image(Slic3r::resources_dir() + "/images/oss_picture_loading.png", wxBITMAP_TYPE_ANY);
    wxStaticBitmap *imageCtrl = new wxStaticBitmap(this, wxID_ANY, wxBitmap(image.Rescale(FromDIP(80), FromDIP(60))), wxDefaultPosition, wxDefaultSize, 0);
    imageCtrl->Bind(wxEVT_LEFT_DOWN, &ScoreDialog::OnBitmapClicked, this);

    Slic3r::Http http   = Slic3r::Http::get(oss_path);
    std::string  suffix = oss_path.substr(oss_path.find_last_of(".") + 1);
    http.header("accept", "image/" + suffix) //"image/" + suffix
        .header("Accept-Encoding", "gzip")
        .on_complete([this, imageCtrl, time = std::weak_ptr<int>(m_tocken)](std::string body, unsigned int status) {
            if (time.expired()) return;
            wxMemoryInputStream stream(body.data(), body.size());
            wxImage             success_image;
            if (success_image.LoadFile(stream, wxBITMAP_TYPE_ANY)) {
                CallAfter([this, success_image, imageCtrl]() { update_static_bitmap(imageCtrl, success_image); });

            } else {
                CallAfter([this, imageCtrl]() { update_static_bitmap(imageCtrl, fail_image); });
            }
        })
        .on_error([this, imageCtrl, &oss_path](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << "load oss picture failed, oss path: " << oss_path << " status:" << status << " error:" << error;
            CallAfter([this, imageCtrl]() { update_static_bitmap(imageCtrl, fail_image); });
        }).perform();

    m_image_sizer->Add(create_broad_sizer(imageCtrl, cur_image_msg), 0, wxALL, 5);

    bitmap_to_image_msg.first  = imageCtrl;
    bitmap_to_image_msg.second = cur_image_msg;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": oss picture is download";
    return bitmap_to_image_msg;
}

void ScoreDialog::update_static_bitmap(wxStaticBitmap* static_bitmap, wxImage image)
{
    static_bitmap->SetBitmap(wxBitmap(image.Rescale(FromDIP(80), FromDIP(60))));
    Layout();
    Fit();
    //Refresh();
}

wxBoxSizer *ScoreDialog::create_broad_sizer(wxStaticBitmap *bitmap, ImageMsg& cur_image_msg)
{ 
    // tb: top and bottom  lr: left and right
    auto m_image_tb_broad = new wxBoxSizer(wxVERTICAL);
    auto line_top         = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    line_top->SetBackgroundColour(wxColour(0xA6, 0xa9, 0xAA));
    m_image_tb_broad->Add(line_top, 0, wxEXPAND, 0);
    cur_image_msg.image_broad.push_back(line_top);
    line_top->Hide();

    auto m_image_lr_broad = new wxBoxSizer(wxHORIZONTAL);
    auto line_left        = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(1, -1), wxTAB_TRAVERSAL);
    line_left->SetBackgroundColour(wxColour(0xA6, 0xa9, 0xAA));
    m_image_lr_broad->Add(line_left, 0, wxEXPAND, 0);
    cur_image_msg.image_broad.push_back(line_left);
    line_left->Hide();

    m_image_lr_broad->Add(bitmap, 0, wxALL, 5);

    auto line_right = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(1, -1), wxTAB_TRAVERSAL);
    line_right->SetBackgroundColour(wxColour(0xA6, 0xa9, 0xAA));
    m_image_lr_broad->Add(line_right, 0, wxEXPAND, 0);
    m_image_tb_broad->Add(m_image_lr_broad, 0, wxEXPAND, 0);
    cur_image_msg.image_broad.push_back(line_right);
    line_right->Hide();

    auto line_bottom = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    line_bottom->SetBackgroundColour(wxColour(0xA6, 0xa9, 0xAA));
    m_image_tb_broad->Add(line_bottom, 0, wxEXPAND, 0);
    cur_image_msg.image_broad.push_back(line_bottom);
    line_bottom->Hide();

    cur_image_msg.is_selected    = false;
    cur_image_msg.image_tb_broad = m_image_tb_broad;

    return m_image_tb_broad;
}

void ScoreDialog::init() {
    SetBackgroundColour(*wxWHITE);
    SetMinSize(wxSize(FromDIP(540), FromDIP(380)));

    fail_image = wxImage(Slic3r::resources_dir() + "/images/oss_picture_load_failed.png", wxBITMAP_TYPE_ANY);
    // icon
    std::string icon_path = (boost::format("%1%/images/OrcaSlicer.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));
}

wxBoxSizer *ScoreDialog::get_score_sizer() { 
    wxBoxSizer    *score_sizer      = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText *static_score_text = new wxStaticText(this, wxID_ANY, _L("Rate"), wxDefaultPosition, wxDefaultSize, 0);
    static_score_text->Wrap(-1);
    score_sizer->Add(static_score_text, 1, wxEXPAND | wxLEFT, FromDIP(24));
    score_sizer->Add(0, 0, 1, wxEXPAND, 0);
    return score_sizer;
}

wxBoxSizer *ScoreDialog::get_star_sizer()
{
    wxBoxSizer *static_score_star_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_score_star.resize(5);
    for (int i = 0; i < m_score_star.size(); ++i) {
        if (!m_success_printed && m_star_count > 3) {
            m_star_count = 3;
            warning_text->Show();
            Layout();
            Fit();
        }
        if (i < m_star_count) {
            m_score_star[i] = new ScalableButton(this, wxID_ANY, "score_star_light", wxEmptyString, wxSize(FromDIP(26), FromDIP(26)), wxDefaultPosition,
                                                 wxBU_EXACTFIT | wxNO_BORDER, true, 26);
        } else
            m_score_star[i] = new ScalableButton(this, wxID_ANY, "score_star_dark", wxEmptyString, wxSize(FromDIP(26), FromDIP(26)), wxDefaultPosition,
                                                 wxBU_EXACTFIT | wxNO_BORDER, true, 26);

        m_score_star[i]->Bind(wxEVT_LEFT_DOWN, [this, i](auto &e) {
            if (!m_success_printed && i >= 3) {
                warning_text->Show();
                Layout();
                Fit();
                return;
            } else {
                warning_text->Hide();
                Layout();
                Fit();
            }
            for (int j = 0; j < m_score_star.size(); ++j) {
                ScalableBitmap light_star = ScalableBitmap(nullptr, "score_star_light", 26);
                m_score_star[j]->SetBitmap(light_star.bmp());
                if (m_score_star[j] == m_score_star[i]) {
                    m_star_count = j + 1;
                    break;
                }
            }
            for (int k = m_star_count; k < m_score_star.size(); ++k) {
                ScalableBitmap dark_star = ScalableBitmap(nullptr, "score_star_dark", 26);
                m_score_star[k]->SetBitmap(dark_star.bmp());
            }
        });
        static_score_star_sizer->Add(m_score_star[i], 0, wxEXPAND | wxLEFT, FromDIP(20));
    }

    return static_score_star_sizer;
}

wxBoxSizer* ScoreDialog::get_comment_text_sizer() {
    wxBoxSizer*    m_comment_sizer    = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText *static_comment_text = new wxStaticText(this, wxID_ANY, _L("Comment"), wxDefaultPosition, wxDefaultSize, 0);
    static_comment_text->Wrap(-1);
    m_comment_sizer->Add(static_comment_text, 1, wxEXPAND | wxLEFT, FromDIP(24));
    m_comment_sizer->Add(0, 0, 1, wxEXPAND, 0);
    return m_comment_sizer;
}

void ScoreDialog::create_comment_text(const wxString& comment) {
    m_comment_text = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(FromDIP(492), FromDIP(104)), wxTE_MULTILINE);
    m_comment_text->SetBackgroundColour(wxColor(*wxWHITE));

    if (!comment.empty()) {
        m_comment_text->SetValue(comment);
    }
    m_comment_text->SetHint(_L("Rate this print"));
    m_comment_text->SetBackgroundColour(*wxWHITE);
    //m_comment_text->SetForegroundColour(wxColor("#BBBBBB"));
    m_comment_text->SetMinSize(wxSize(FromDIP(492), FromDIP(104)));

    m_comment_text->Bind(wxEVT_SET_FOCUS, [this](auto &event) {
        if (wxGetApp().dark_mode()) {
            m_comment_text->SetForegroundColour(wxColor(*wxWHITE));
        } else
            m_comment_text->SetForegroundColour(wxColor(*wxBLACK));
        m_comment_text->Refresh();
        event.Skip();
    });
}

wxBoxSizer *ScoreDialog::get_photo_btn_sizer() {
    wxBoxSizer *    m_photo_sizer    = new wxBoxSizer(wxHORIZONTAL);
    ScalableBitmap little_photo  = wxGetApp().dark_mode() ? ScalableBitmap(this, "single_little_photo_dark", 20) : ScalableBitmap(this, "single_little_photo", 20);
    wxStaticBitmap *little_photo_img   = new wxStaticBitmap(this, wxID_ANY, little_photo.bmp(), wxDefaultPosition, wxSize(FromDIP(20), FromDIP(20)), 0);
    m_photo_sizer->Add(little_photo_img, 0, wxEXPAND | wxLEFT, FromDIP(24));
    m_add_photo = new Label(this, _L("Add Photo"));
    m_add_photo->SetBackgroundColour(*wxWHITE);
    //m_add_photo->SetForegroundColour(wxColor("#898989"));
    m_add_photo->SetSize(wxSize(-1, FromDIP(20)));
    m_photo_sizer->Add(m_add_photo, 0, wxEXPAND | wxLEFT, FromDIP(12));

    m_delete_photo = new Label(this, _L("Delete Photo"));
    m_delete_photo->SetBackgroundColour(*wxWHITE);
    //m_delete_photo->SetForegroundColour(wxColor("#898989"));
    m_delete_photo->SetSize(wxSize(-1, FromDIP(20)));
    m_photo_sizer->Add(m_delete_photo, 0, wxEXPAND | wxLEFT, FromDIP(12));
    m_delete_photo->Hide();
    m_photo_sizer->Add(0, 0, 1, wxEXPAND, 0);

    m_add_photo->Bind(wxEVT_LEFT_DOWN, [this](auto &e) {
        // add photo logic
        wxFileDialog openFileDialog(this, "Select Images", "", "", "Image files (*.png;*.jpg;*jpeg)|*.png;*.jpg;*.jpeg", wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);

        if (openFileDialog.ShowModal() == wxID_CANCEL) return;

        wxArrayString filePaths;
        openFileDialog.GetPaths(filePaths);
        //wxArrayString filePaths_reduction;
        std::vector<std::pair<wxString, std::string>> local_path;
        for (int i = 0; i < filePaths.GetCount(); i++) { //It's ugly, but useful
            bool is_repeat = false;
            for (auto image : m_image) {
                if (filePaths[i] == image.second.local_image_url) { 
                    is_repeat = true;
                    continue;
                }
            }
            if (!is_repeat) {
                local_path.push_back(std::make_pair(filePaths[i], ""));
                if (local_path.size() + m_image.size() > m_photo_nums) { 
                    break; 
                }
            }
            
        }

        load_photo(local_path);

        m_image_sizer->Layout();
        this->Fit();
        this->Layout();
    });

        m_delete_photo->Bind(wxEVT_LEFT_DOWN, [this](auto &e) {
            for (auto it = m_selected_image_list.begin(); it != m_selected_image_list.end();) {
                auto bitmap = *it;
                m_image_sizer->Detach(m_image[bitmap].image_tb_broad);
                m_image[bitmap].image_tb_broad->DeleteWindows();

                m_image.erase(bitmap);
                it = m_selected_image_list.erase(it);
            }
            m_image_url_paths.clear();
            for (const auto& bitmap : m_image) {
                if (bitmap.second.is_uploaded) {
                    if (!bitmap.second.img_url_paths.empty()) {
                        m_image_url_paths.push_back(bitmap.second.img_url_paths);
                    }
                }
            }
            m_delete_photo->Hide();
            Layout();
            Fit();
        });

    return m_photo_sizer;
}

wxBoxSizer *ScoreDialog::get_button_sizer()
{
    wxBoxSizer *bSizer_button = new wxBoxSizer(wxHORIZONTAL);
    bSizer_button->Add(0, 0, 1, wxEXPAND, 0);

    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
                            std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    m_button_ok = new Button(this, _L("Submit"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(wxColour(0xFFFFFE));
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetCornerRadius(FromDIP(12));
    bSizer_button->Add(m_button_ok, 0, wxRIGHT, FromDIP(24));

    m_button_ok->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        m_upload_status_code = StatusCode::UPLOAD_PROGRESS;

        if (m_star_count == 0) {
            MessageDialog dlg(this, _L("Please click on the star first."), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("InFo"), wxOK);
            dlg.ShowModal();
            return;
        }

        std::set<std::pair<wxStaticBitmap *, wxString>> need_upload_images = add_need_upload_imgs();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": get need upload picture count: " << need_upload_images.size();

        std::string  comment = into_u8(m_comment_text->GetValue());
        unsigned int http_code;
        std::string  http_error;
        wxString  error_info;

        if (!need_upload_images.empty()) {
            std::string config;
            int         ret = wxGetApp().getAgent()->get_oss_config(config, wxGetApp().app_config->get_country_code(), http_code, http_error);
            if (ret == -1) {
                error_info += into_u8(_L("Get oss config failed.")) + "\n\thttp code: " + std::to_string(http_code) + "\n\thttp error: " + http_error;
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": get oss config filed and http_error: " << http_error;
                m_upload_status_code = StatusCode::UPLOAD_EXIST_ISSUE;
            }
            if (m_upload_status_code == StatusCode::UPLOAD_PROGRESS) {
                int             need_upload_nums   = need_upload_images.size();
                int             upload_nums        = 0;
                int             upload_failed_nums = 0;
                ProgressDialog *progress_dialog    = new ProgressDialog(_L("Upload Pictures"), _L("Number of images successfully uploaded") + ": " + std::to_string(upload_nums) + "/" + std::to_string(need_upload_nums), need_upload_nums, this);
                for (std::set<std::pair<wxStaticBitmap *, wxString>>::iterator it = need_upload_images.begin(); it != need_upload_images.end();) {
                    std::pair<wxStaticBitmap *, wxString> need_upload     = *it;
                    std::string need_upload_uf8 = into_u8(need_upload.second);
                    //Local path when incoming, cloud path when outgoing
                    ret = wxGetApp().getAgent()->put_rating_picture_oss(config, need_upload_uf8, m_model_id, m_profile_id, http_code, http_error);
                    std::unordered_map<wxStaticBitmap *, ImageMsg>::iterator iter;
                    switch (ret) {
                    case 0:
                        upload_nums++;
                        iter = m_image.find(need_upload.first);
                        if (m_image.end() != iter) {
                            iter->second.img_url_paths = need_upload_uf8;
                            iter->second.is_uploaded   = true;
                            m_image_url_paths.push_back(need_upload_uf8);
                        }
                        it++;
                        progress_dialog->Update(upload_nums, _L("Number of images successfully uploaded") + ": " + std::to_string(upload_nums) + "/" + std::to_string(need_upload_nums));
                        progress_dialog->Fit();
                        BOOST_LOG_TRIVIAL(info) << "put_rating_picture_oss: model_id [" << m_model_id << "] profile_id [" << m_profile_id << "] http_code [" << http_code
                                                << "] http_error [" << http_error << "] config [" << config << "]  image_path [" << need_upload.second << "]";
                        break;
                    case -1:
                        error_info += need_upload.second + _L(" upload failed").ToUTF8().data() + "\n\thttp code:" + std::to_string(http_code) + "\n\thttp_error:" + http_error + "\n";
                        m_upload_status_code = StatusCode::UPLOAD_IMG_FAILED;
                        ++it;
                        break;
                    case BAMBU_NETWORK_ERR_PARSE_CONFIG_FAILED:
                        error_info += need_upload.second + _L(" upload config prase failed\n").ToUTF8().data() + "\n";
                        m_upload_status_code = StatusCode::UPLOAD_IMG_FAILED;
                        ++it;
                        break;
                    case BAMBU_NETWORK_ERR_NO_CORRESPONDING_BUCKET:
                        error_info += need_upload.second + _L(" No corresponding storage bucket\n").ToUTF8().data() + "\n";
                        m_upload_status_code = StatusCode::UPLOAD_IMG_FAILED;
                        ++it;
                        break;
                    case BAMBU_NETWORK_ERR_OPEN_FILE_FAILED:
                        error_info += need_upload.second + _L(" can not be opened\n").ToUTF8().data() + "\n";
                        m_upload_status_code = StatusCode::UPLOAD_IMG_FAILED;
                        ++it;
                        break;
                    }
                }
                progress_dialog->Hide();
                if (progress_dialog) { 
                    delete progress_dialog;
                    progress_dialog = nullptr;
                }

                if (m_upload_status_code == StatusCode::UPLOAD_IMG_FAILED) {
                    std::string   upload_failed_images = into_u8(_L("The following issues occurred during the process of uploading images. Do you want to ignore them?\n\n"));
                    MessageDialog dlg_info(this, upload_failed_images + error_info, wxString(_L("info")), wxOK | wxNO | wxCENTER);
                    if (dlg_info.ShowModal() == wxID_OK) {
                        m_upload_status_code = StatusCode::UPLOAD_PROGRESS;
                    }
                }
            }
        }

        if (m_upload_status_code == StatusCode::UPLOAD_PROGRESS) {
            int            ret = wxGetApp().getAgent()->put_model_mall_rating(m_rating_id, m_star_count, comment, m_image_url_paths, http_code, http_error);
            MessageDialog *dlg_info;
            switch (ret) {
            case 0: EndModal(wxID_OK); break;
            case BAMBU_NETWORK_ERR_GET_RATING_ID_FAILED:
                dlg_info = new MessageDialog(this, _L("Synchronizing the printing results. Please retry a few seconds later."), wxString(_L("info")), wxOK | wxCENTER);
                dlg_info->ShowModal();
                delete dlg_info;
                break;
            default: // Upload failed and obtaining instance_id failed
                if (ret == -1)
                    error_info += _L("Upload failed\n").ToUTF8().data();
                else
                    error_info += _L("obtaining instance_id failed\n").ToUTF8().data();
                if (!error_info.empty()) { BOOST_LOG_TRIVIAL(info) << error_info; }

                dlg_info = new MessageDialog(this,
                                             _L("Your comment result cannot be uploaded due to some reasons. As follows:\n\n  error code: ") + std::to_string(http_code) +
                                                 "\n  " + _L("error message: ") + http_error + _L("\n\nWould you like to redirect to the webpage for rating?"),
                                             wxString(_L("info")), wxOK | wxNO | wxCENTER);
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": upload rating picture failed and http error" << http_error;
                if (dlg_info->ShowModal() == wxID_OK) {
                    market_model_scoring_page(m_design_id);
                    EndModal(wxID_OK);
                }
                delete dlg_info;
                break;
            }
        } else if (m_upload_status_code == StatusCode::UPLOAD_IMG_FAILED) {
            MessageDialog *dlg_info = new MessageDialog(this, _L("Some of your images failed to upload. Would you like to redirect to the webpage for rating?"),
                                                        wxString(_L("info")), wxOK | wxNO | wxCENTER);
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": upload rating picture failed or get oss config failed";
            if (dlg_info->ShowModal() == wxID_OK) {
                market_model_scoring_page(m_design_id);
                EndModal(wxID_OK);
            }
            delete dlg_info;
            if (!error_info.empty()) { BOOST_LOG_TRIVIAL(info) << error_info; }
        }
    });

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetCornerRadius(FromDIP(12));
    bSizer_button->Add(m_button_cancel, 0, wxRIGHT, FromDIP(24));

    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { EndModal(wxID_CANCEL); });

    return bSizer_button;
}

void ScoreDialog::load_photo(const std::vector<std::pair<wxString, std::string>> &filePaths)
{
    for (size_t i = 0; i < filePaths.size(); ++i) {
        if (m_image.size() < m_photo_nums) {
            std::pair<wxString, std::string> local_to_url_path = filePaths[i];
            wxString                         filePath          = local_to_url_path.first;

            ImageMsg cur_image_msg;

            if (filePath.empty()) {  // local img path is empty, oss url path is exist
                std::string oss_url_path = local_to_url_path.second;
                //to do: load oss image, create wxStaticBitmap

                if (!oss_url_path.empty()) {
                    m_image.insert(create_oss_thumbnail(oss_url_path));
                }
                continue;
            } else {
                m_image.insert(create_local_thumbnail(filePath));
            }

        } else {
            MessageDialog *dlg_info_up_to_8 = new MessageDialog(this, _L("You can select up to 16 images."), wxString(_L("info")), wxOK | wxCENTER);
            dlg_info_up_to_8->ShowModal();
            break;
        }

    }
}

wxBoxSizer *ScoreDialog::get_main_sizer(const std::vector<std::pair<wxString, std::string>> &images, const wxString &comment)
{
    init();
    wxBoxSizer *m_main_sizer = new wxBoxSizer(wxVERTICAL);
    // top line
    auto m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_top->SetBackgroundColour(wxColour(0xA6, 0xa9, 0xAA));
    m_main_sizer->Add(m_line_top, 0, wxEXPAND, 0);
    m_main_sizer->Add(0, 0, 0, wxTOP, FromDIP(32));

    warning_text = new wxStaticText(this, wxID_ANY, _L("At least one successful print record of this print profile is required \nto give a positive rating(4 or 5stars)."));
    warning_text->SetForegroundColour(*wxRED);
    warning_text->SetFont(::Label::Body_13);

    wxBoxSizer *score_sizer = get_score_sizer();
    m_main_sizer->Add(score_sizer, 0, wxEXPAND, FromDIP(20));
    m_main_sizer->Add(0, 0, 0, wxBOTTOM, FromDIP(8));

    wxBoxSizer *static_score_star_sizer = get_star_sizer();
    m_main_sizer->Add(static_score_star_sizer, 0, wxEXPAND | wxBOTTOM, FromDIP(20));

    m_main_sizer->Add(warning_text, 0, wxEXPAND | wxLEFT, FromDIP(24));
    m_main_sizer->Add(0, 0, 0, wxBOTTOM, FromDIP(8));
    warning_text->Hide();

    wxBoxSizer *m_comment_sizer = get_comment_text_sizer();
    m_main_sizer->Add(m_comment_sizer, 0, wxEXPAND, FromDIP(20));
    m_main_sizer->Add(0, 0, 0, wxBOTTOM, FromDIP(8));

    create_comment_text(comment);
    m_main_sizer->Add(m_comment_text, 0, wxLEFT, FromDIP(24));

    wxBoxSizer *m_photo_sizer = get_photo_btn_sizer();
    m_main_sizer->Add(m_photo_sizer, 0, wxEXPAND | wxTOP, FromDIP(8));

    m_image_sizer = new wxGridSizer(5, FromDIP(5), FromDIP(5));
    if (!images.empty()) { 
        load_photo(images);
    }
    m_main_sizer->Add(m_image_sizer, 0, wxEXPAND | wxLEFT, FromDIP(24));
    m_main_sizer->Add(0, 0, 1, wxEXPAND, 0);

    wxBoxSizer *bSizer_button = get_button_sizer();
    m_main_sizer->Add(bSizer_button, 0, wxEXPAND | wxBOTTOM, FromDIP(24));

    return m_main_sizer;
}

ScoreData ScoreDialog::get_score_data() { 
    ScoreData score_data;
    score_data.rating_id          = m_rating_id;
    score_data.design_id          = m_design_id;
    score_data.model_id           = m_model_id;
    score_data.profile_id         = m_profile_id;
    score_data.star_count         = m_star_count;
    score_data.success_printed    = m_success_printed;
    score_data.comment_text       = m_comment_text->GetValue();
    score_data.image_url_paths    = m_image_url_paths;
    for (auto img : m_image) { score_data.local_to_url_image.push_back(std::make_pair(img.second.local_image_url, img.second.img_url_paths)); }
    
    return score_data;
}

void ScoreDialog::set_comment(std::string comment)
{
    if (m_comment_text) { 

        m_comment_text->SetValue(wxString::FromUTF8(comment));
    }
}

void ScoreDialog::set_cloud_bitmap(std::vector<std::string> cloud_bitmaps)
{ 
    m_image_url_paths = cloud_bitmaps;
    for (std::string &url : cloud_bitmaps) {
        if (std::string::npos == url.find(m_model_id)) continue;
        m_image.insert(create_oss_thumbnail(url));
    }
    Layout();
    Fit();
}

} // namespace GUI
} // namespace Slic3r
