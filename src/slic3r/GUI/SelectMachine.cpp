#include "SelectMachine.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/Color.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_Preview.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/StaticBox.hpp"
#include "ConnectPrinter.hpp"
#include "Jobs/BoostThreadWorker.hpp"
#include "Jobs/PlaterWorker.hpp"


#include <wx/progdlg.h>
#include <wx/clipbrd.h>
#include <wx/dcgraph.h>
#include <wx/mstream.h>
#include <miniz.h>
#include <algorithm>
#include "Plater.hpp"
#include "Notebook.hpp"
#include "BitmapCache.hpp"
#include "BindDialog.hpp"

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_UPDATE_USER_MACHINE_LIST, wxCommandEvent);
wxDEFINE_EVENT(EVT_PRINT_JOB_CANCEL, wxCommandEvent);
wxDEFINE_EVENT(EVT_CLEAR_IPADDRESS, wxCommandEvent);

#define INITIAL_NUMBER_OF_MACHINES 0

#define MACHINE_LIST_REFRESH_INTERVAL 2000

#define WRAP_GAP FromDIP(2)

static wxString task_canceled_text = _L("Task canceled");

static wxString MACHINE_BED_TYPE_STRING[BED_TYPE_COUNT] = {
    //_L("Auto"),
    _L("Bambu Cool Plate") + " / " + _L("PLA Plate"),
    _L("Bambu Engineering Plate"),
    _L("Bambu Smooth PEI Plate") + "/" + _L("High temperature Plate"),
    _L("Bambu Textured PEI Plate")};

void SelectMachineDialog::stripWhiteSpace(std::string& str)
{
    if (str == "") { return; }

    string::iterator cur_it;
    cur_it = str.begin();

    while (cur_it != str.end()) {
        if ((*cur_it) == '\n' || (*cur_it) == ' ') {
            cur_it = str.erase(cur_it);
        }
        else {
            cur_it++;
        }
    }
}

static std::string MachineBedTypeString[BED_TYPE_COUNT] = {
    //"auto",
    "pc",
    "pe",
    "pei",
    "pte",
};

wxString SelectMachineDialog::format_text(wxString &m_msg)
{
    if (wxGetApp().app_config->get("language") != "zh_CN") {return m_msg; }

    wxString out_txt      = m_msg;
    wxString count_txt    = "";
    int      new_line_pos = 0;

    for (int i = 0; i < m_msg.length(); i++) {
        auto text_size = m_statictext_ams_msg->GetTextExtent(count_txt);
        if (text_size.x < (FromDIP(400))) {
            count_txt += m_msg[i];
        } else {
            out_txt.insert(i - 1, '\n');
            count_txt = "";
        }
    }
    return out_txt;
}
SelectMachineDialog::SelectMachineDialog(Plater *plater)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Send print job to"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    , m_plater(plater), m_export_3mf_cancel(false)
    , m_mapping_popup(AmsMapingPopup(this))
    , m_mapping_tip_popup(AmsMapingTipPopup(this))
    , m_mapping_tutorial_popup(AmsTutorialPopup(this))
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    SetMinSize(wxSize(FromDIP(688), -1));
    SetMaxSize(wxSize(FromDIP(688), -1));

    // bind
    Bind(wxEVT_CLOSE_WINDOW, &SelectMachineDialog::on_cancel, this);

    for (int i = 0; i < BED_TYPE_COUNT; i++) { m_bedtype_list.push_back(MACHINE_BED_TYPE_STRING[i]); }

    // font
    SetFont(wxGetApp().normal_font());

    Freeze();
    SetBackgroundColour(m_colour_def_color);

    m_sizer_main = new wxBoxSizer(wxVERTICAL);
    m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));


    m_basic_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_basic_panel->SetBackgroundColour(*wxWHITE);
    m_basicl_sizer = new wxBoxSizer(wxHORIZONTAL);

    /*basic info*/
    /*thumbnail*/
    auto m_sizer_thumbnail_area = new wxBoxSizer(wxHORIZONTAL);

    auto m_panel_image = new wxPanel(m_basic_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_panel_image->SetBackgroundColour(m_colour_def_color);
    m_sizer_thumbnail = new wxBoxSizer(wxHORIZONTAL);
    m_thumbnailPanel = new ThumbnailPanel(m_panel_image);
    m_thumbnailPanel->SetSize(wxSize(FromDIP(198), FromDIP(198)));
    m_thumbnailPanel->SetMinSize(wxSize(FromDIP(198), FromDIP(198)));
    m_thumbnailPanel->SetMaxSize(wxSize(FromDIP(198), FromDIP(198)));
    m_thumbnailPanel->SetBackgroundColour(*wxWHITE);
    m_sizer_thumbnail->Add(m_thumbnailPanel, 0, wxALIGN_CENTER, 0);
    m_panel_image->SetSizer(m_sizer_thumbnail);
    m_panel_image->Layout();

    m_sizer_thumbnail_area->Add(m_panel_image, 0, wxALIGN_CENTER, 0);
    m_sizer_thumbnail_area->Layout();

    /*basic info right*/
    auto  sizer_basic_right_info = new wxBoxSizer(wxVERTICAL);

    /*rename*/
    auto sizer_rename = new wxBoxSizer(wxHORIZONTAL);

    m_rename_switch_panel = new wxSimplebook(m_basic_panel);
    m_rename_switch_panel->SetBackgroundColour(*wxWHITE);
    m_rename_switch_panel->SetSize(wxSize(FromDIP(360), FromDIP(25)));
    m_rename_switch_panel->SetMinSize(wxSize(FromDIP(360), FromDIP(25)));
    m_rename_switch_panel->SetMaxSize(wxSize(FromDIP(360), FromDIP(25)));

    m_rename_normal_panel = new wxPanel(m_rename_switch_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_rename_normal_panel->SetBackgroundColour(*wxWHITE);
    rename_sizer_v = new wxBoxSizer(wxVERTICAL);
    rename_sizer_h = new wxBoxSizer(wxHORIZONTAL);
    m_rename_text = new wxStaticText(m_rename_normal_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    m_rename_text->SetFont(::Label::Body_13);
    m_rename_text->SetBackgroundColour(*wxWHITE);
    m_rename_text->SetMaxSize(wxSize(FromDIP(340), -1));
    rename_editable       = new ScalableBitmap(this, "rename_edit", FromDIP(13)); // ORCA Match edit icon and its size
    rename_editable_light = new ScalableBitmap(this, "rename_edit", FromDIP(13)); // ORCA Match edit icon and its size
    m_rename_button = new wxStaticBitmap(m_rename_normal_panel, wxID_ANY, rename_editable->bmp(), wxDefaultPosition, wxSize(FromDIP(20), FromDIP(20)), 0);
    m_rename_button->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_HAND); });
    m_rename_button->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_ARROW); });

    rename_sizer_h->Add(m_rename_text, 0, wxALIGN_CENTER, 0); // ORCA align text with icon
    rename_sizer_h->Add(m_rename_button, 0, wxALIGN_CENTER| wxLEFT, FromDIP(3)); // ORCA add gap between text and icon
    rename_sizer_v->Add(rename_sizer_h, 1, wxTOP, 0);

    m_rename_normal_panel->SetSizer(rename_sizer_v);
    m_rename_normal_panel->Layout();
    rename_sizer_v->Fit(m_rename_normal_panel);

    auto m_rename_edit_panel = new wxPanel(m_rename_switch_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_rename_edit_panel->SetBackgroundColour(*wxWHITE);
    auto rename_edit_sizer_v = new wxBoxSizer(wxVERTICAL);

    m_rename_input = new ::TextInput(m_rename_edit_panel, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    m_rename_input->GetTextCtrl()->SetFont(::Label::Body_13);
    m_rename_input->SetSize(wxSize(FromDIP(360), FromDIP(24)));
    m_rename_input->SetMinSize(wxSize(FromDIP(360), FromDIP(24)));
    m_rename_input->SetMaxSize(wxSize(FromDIP(360), FromDIP(24)));
    m_rename_input->Bind(wxEVT_TEXT_ENTER, [this](auto& e) {on_rename_enter();});
    m_rename_input->Bind(wxEVT_KILL_FOCUS, [this](auto& e) {
        if (!m_rename_input->HasFocus() && !m_rename_text->HasFocus())
            on_rename_enter();
        else
            e.Skip(); });
    rename_edit_sizer_v->Add(m_rename_input, 1, wxALIGN_CENTER, 0);


    m_rename_edit_panel->SetSizer(rename_edit_sizer_v);
    m_rename_edit_panel->Layout();
    rename_edit_sizer_v->Fit(m_rename_edit_panel);

    m_rename_button->Bind(wxEVT_LEFT_DOWN, &SelectMachineDialog::on_rename_click, this);
    m_rename_switch_panel->AddPage(m_rename_normal_panel, wxEmptyString, true);
    m_rename_switch_panel->AddPage(m_rename_edit_panel, wxEmptyString, false);

    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_ESCAPE) {
            if (m_rename_switch_panel->GetSelection() == 0) {
                e.Skip();
            }
            else {
                m_rename_switch_panel->SetSelection(0);
                m_rename_text->SetLabel(m_current_project_name);
                m_rename_normal_panel->Layout();
            }
        }
        else {
            e.Skip();
        }
    });


    /*weight & time*/
    wxBoxSizer *m_sizer_basic_weight_time = new wxBoxSizer(wxHORIZONTAL);

    print_time   = new ScalableBitmap(this, "print-time", 18);
    timeimg = new wxStaticBitmap(m_basic_panel, wxID_ANY, print_time->bmp(), wxDefaultPosition, wxSize(FromDIP(18), FromDIP(18)), 0);
    m_stext_time = new Label(m_basic_panel, wxEmptyString);
    m_stext_time->SetFont(Label::Body_13);

    print_weight   = new ScalableBitmap(this, "print-weight", 18);
    weightimg = new wxStaticBitmap(m_basic_panel, wxID_ANY, print_weight->bmp(), wxDefaultPosition, wxSize(FromDIP(18), FromDIP(18)), 0);
    m_stext_weight = new Label(m_basic_panel, wxEmptyString);
    m_stext_weight->SetFont(Label::Body_13);

    m_sizer_basic_weight_time->Add(timeimg, 0, wxALIGN_CENTER, 0);
    m_sizer_basic_weight_time->Add(m_stext_time, 0, wxALIGN_CENTER|wxLEFT, FromDIP(6));
    m_sizer_basic_weight_time->Add(weightimg, 0, wxALIGN_CENTER|wxLEFT, FromDIP(30));
    m_sizer_basic_weight_time->Add(m_stext_weight, 0, wxALIGN_CENTER|wxLEFT, FromDIP(6));

    /*bed type*/
    m_text_bed_type = new Label(m_basic_panel);
    m_text_bed_type->SetFont(Label::Body_13);

    /*last & next page*/
    auto last_plate_sizer = new wxBoxSizer(wxVERTICAL);
    m_bitmap_last_plate = new wxStaticBitmap(m_basic_panel, wxID_ANY, create_scaled_bitmap("go_last_plate", this, 25), wxDefaultPosition, wxSize(FromDIP(25), FromDIP(25)), 0);
    m_bitmap_last_plate->Hide();
    last_plate_sizer->Add(m_bitmap_last_plate, 0, wxALIGN_CENTER, 0);

    auto next_plate_sizer = new wxBoxSizer(wxVERTICAL);
    m_bitmap_next_plate = new wxStaticBitmap(m_basic_panel, wxID_ANY, create_scaled_bitmap("go_next_plate", this, 25), wxDefaultPosition, wxSize(FromDIP(25), FromDIP(25)), 0);
    m_bitmap_next_plate->Hide();
    next_plate_sizer->Add(m_bitmap_next_plate, 0, wxALIGN_CENTER, 0);

    sizer_rename->Add(m_rename_switch_panel, 0,  wxALIGN_CENTER, 0);
    sizer_rename->Add(0, 0, 0, wxEXPAND, 0);
    sizer_rename->Add(m_bitmap_last_plate, 0,  wxALIGN_CENTER, 0);
    sizer_rename->Add(m_bitmap_next_plate, 0,  wxALIGN_CENTER, 0);

    /*printer combobox*/
    wxBoxSizer* m_sizer_printer = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer* sizer_split_printer = new wxBoxSizer(wxHORIZONTAL);
    m_stext_printer_title = new Label(m_basic_panel, _L("Printer"));
    m_stext_printer_title->SetFont(::Label::Body_14);
    m_stext_printer_title->SetForegroundColour(0x909090);
    auto m_split_line = new wxPanel(m_basic_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_split_line->SetBackgroundColour(0xeeeeee);
    m_split_line->SetMinSize(wxSize(-1, 1));
    m_split_line->SetMaxSize(wxSize(-1, 1));
    sizer_split_printer->Add(0, 0, 0, wxEXPAND, 0);
    sizer_split_printer->Add(m_stext_printer_title, 0, wxALIGN_CENTER, 0);
    sizer_split_printer->Add(m_split_line, 1, wxALIGN_CENTER_VERTICAL, 0);


    m_comboBox_printer = new ::ComboBox(m_basic_panel, wxID_ANY, "", wxDefaultPosition, wxSize(FromDIP(300), -1), 0, nullptr, wxCB_READONLY);
    m_comboBox_printer->SetMinSize(wxSize(FromDIP(300), -1));
    m_comboBox_printer->SetMaxSize(wxSize(FromDIP(300), -1));
    m_comboBox_printer->Bind(wxEVT_COMBOBOX, &SelectMachineDialog::on_selection_changed, this);


    m_btn_bg_enable = StateColor(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
                               std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal));

    m_button_refresh = new Button(m_basic_panel, _L("Refresh"));
    m_button_refresh->SetBackgroundColor(m_btn_bg_enable);
    m_button_refresh->SetBorderColor(m_btn_bg_enable);
    m_button_refresh->SetTextColor(StateColor::darkModeColorFor("#FFFFFE"));
    m_button_refresh->SetSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_refresh->SetMinSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_refresh->SetCornerRadius(FromDIP(10));
    m_button_refresh->Bind(wxEVT_BUTTON, &SelectMachineDialog::on_refresh, this);

    m_sizer_printer->Add(m_comboBox_printer, 0, wxEXPAND, 0);
    m_sizer_printer->Add(m_button_refresh, 0, wxALL | wxLEFT, FromDIP(5));

    m_text_printer_msg = new wxStaticText(m_basic_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    m_text_printer_msg->SetMinSize(wxSize(FromDIP(420), -1));
    m_text_printer_msg->SetMaxSize(wxSize(FromDIP(420), -1));
    m_text_printer_msg->SetFont(::Label::Body_13);
    m_text_printer_msg->Hide();


    sizer_basic_right_info->Add(sizer_rename, 0, wxTOP, 0);
    sizer_basic_right_info->Add(0, 0, 0, wxTOP, FromDIP(5));
    sizer_basic_right_info->Add(m_sizer_basic_weight_time, 0, wxTOP, 0);
    sizer_basic_right_info->Add(0, 0, 0, wxTOP, FromDIP(10));
    sizer_basic_right_info->Add(m_text_bed_type, 0, wxTOP, 0);
    sizer_basic_right_info->Add(0, 0, 0, wxTOP, FromDIP(15));
    sizer_basic_right_info->Add(sizer_split_printer, 1, wxEXPAND, 0);
    sizer_basic_right_info->Add(0, 0, 0, wxTOP, FromDIP(8));
    sizer_basic_right_info->Add(m_sizer_printer, 0, wxTOP, 0);
    sizer_basic_right_info->Add(0, 0, 0, wxTOP, FromDIP(4));
    sizer_basic_right_info->Add(m_text_printer_msg, 0, wxLEFT, 0);


    m_basicl_sizer->Add(m_sizer_thumbnail_area, 0, wxLEFT, 0);
    m_basicl_sizer->Add(0, 0, 0, wxLEFT, FromDIP(8));
    m_basicl_sizer->Add(sizer_basic_right_info, 0, wxLEFT, 0);



    m_basic_panel->SetSizer(m_basicl_sizer);
    m_basic_panel->Layout();


    /*filaments info*/
    wxBoxSizer* sizer_split_filament = new wxBoxSizer(wxHORIZONTAL);

    auto m_stext_filament_title = new Label(this, _L("Filament"));
    m_stext_filament_title->SetFont(::Label::Body_14);
    m_stext_filament_title->SetForegroundColour(0x909090);

    auto m_split_line_filament = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_split_line_filament->SetBackgroundColour(0xeeeeee);
    m_split_line_filament->SetMinSize(wxSize(-1, 1));
    m_split_line_filament->SetMaxSize(wxSize(-1, 1));

    m_sizer_autorefill = new wxBoxSizer(wxHORIZONTAL);
    m_ams_backup_tip = new Label(this, _L("Auto Refill"));
    m_ams_backup_tip->SetFont(::Label::Head_12);
    m_ams_backup_tip->SetForegroundColour(wxColour(0x9C27B0));
    m_ams_backup_tip->SetBackgroundColour(*wxWHITE);
    img_ams_backup = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("automatic_material_renewal", this, 16), wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)), 0);
    img_ams_backup->SetBackgroundColour(*wxWHITE);

    m_sizer_autorefill->Add(0, 0, 1, wxEXPAND, 0);
    m_sizer_autorefill->Add(img_ams_backup, 0, wxALL, FromDIP(3));
    m_sizer_autorefill->Add(m_ams_backup_tip, 0, wxTOP, FromDIP(5));

    m_ams_backup_tip->Hide();
    img_ams_backup->Hide();

    m_ams_backup_tip->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_HAND); });
    img_ams_backup->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_HAND); });

    m_ams_backup_tip->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_ARROW); });
    img_ams_backup->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_ARROW); });

    m_ams_backup_tip->Bind(wxEVT_LEFT_DOWN, [this](auto& e) { if (!m_is_in_sending_mode) { popup_filament_backup(); on_rename_enter(); }  });
    img_ams_backup->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {if (!m_is_in_sending_mode) popup_filament_backup(); on_rename_enter(); });

    sizer_split_filament->Add(0, 0, 0, wxEXPAND, 0);
    sizer_split_filament->Add(m_stext_filament_title, 0, wxALIGN_CENTER, 0);
    sizer_split_filament->Add(m_split_line_filament, 1, wxALIGN_CENTER_VERTICAL, 0);
    sizer_split_filament->Add(m_sizer_autorefill, 0, wxALIGN_CENTER, 0);

    /*filament area*/
    /*1 extruder*/
    m_filament_panel = new wxPanel(this, wxID_ANY);
    m_filament_panel->SetBackgroundColour(wxColour(0xf8f8f8));
    m_filament_panel->SetMinSize(wxSize(FromDIP(637), -1));
    m_filament_panel->SetMaxSize(wxSize(FromDIP(637), -1));
    m_filament_panel_sizer = new wxBoxSizer(wxVERTICAL);

    m_sizer_ams_mapping = new wxGridSizer(0, 10, FromDIP(7), FromDIP(7));
    m_filament_panel_sizer->Add(m_sizer_ams_mapping, 0, wxEXPAND|wxALL, FromDIP(10));
    m_filament_panel->SetSizer(m_filament_panel_sizer);
    m_filament_panel->Layout();
    m_filament_panel->Fit();

    /*left & right extruder*/
    m_sizer_filament_2extruder = new wxBoxSizer(wxHORIZONTAL);
    m_filament_left_panel = new wxPanel(this, wxID_ANY);
    m_filament_right_panel = new wxPanel(this, wxID_ANY);
    m_filament_left_panel->SetBackgroundColour(wxColour(0xf8f8f8));
    m_filament_right_panel->SetBackgroundColour(wxColour(0xf8f8f8));
    m_filament_left_panel->SetMinSize(wxSize(FromDIP(315), 180));
    m_filament_left_panel->SetMaxSize(wxSize(FromDIP(315), 180));
    m_filament_right_panel->SetMinSize(wxSize(FromDIP(315), 180));
    m_filament_right_panel->SetMaxSize(wxSize(FromDIP(315), 180));

    m_filament_panel_left_sizer = new wxBoxSizer(wxVERTICAL);
    m_sizer_ams_mapping_left = new wxGridSizer(0, 5, FromDIP(7), FromDIP(7));
    m_filament_panel_left_sizer->Add(m_sizer_ams_mapping_left, 0, wxEXPAND | wxALL, FromDIP(10));
    m_filament_left_panel->SetSizer(m_filament_panel_left_sizer);
    m_filament_left_panel->Layout();
    m_filament_left_panel->Fit();

    m_filament_panel_right_sizer = new wxBoxSizer(wxVERTICAL);
    m_sizer_ams_mapping_right = new wxGridSizer(0, 5, FromDIP(7), FromDIP(7));
    m_filament_panel_right_sizer->Add(m_sizer_ams_mapping_right, 0, wxEXPAND | wxALL, FromDIP(10));
    m_filament_right_panel->SetSizer(m_filament_panel_right_sizer);
    m_filament_right_panel->Layout();
    m_filament_right_panel->Fit();

    m_sizer_filament_2extruder->Add(m_filament_left_panel, 0, wxALIGN_CENTER, 0);
    m_sizer_filament_2extruder->Add(0, 0, 0, wxLEFT, FromDIP(7));
    m_sizer_filament_2extruder->Add(m_filament_right_panel, 0, wxALIGN_CENTER, 0);
    m_sizer_filament_2extruder->Layout();

    m_filament_left_panel->Hide();
    m_filament_right_panel->Hide();





    m_statictext_ams_msg = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
    m_statictext_ams_msg->SetFont(::Label::Body_13);
    m_statictext_ams_msg->Hide();



    /*options*/
    wxBoxSizer* sizer_split_options = new wxBoxSizer(wxHORIZONTAL);
    auto m_stext_options_title = new Label(this, _L("Print Options"));
    m_stext_options_title->SetFont(::Label::Body_14);
    m_stext_options_title->SetForegroundColour(0x909090);
    auto m_split_options_line = new wxPanel(this, wxID_ANY);
    m_split_options_line->SetBackgroundColour(0xeeeeee);
    m_split_options_line->SetSize(wxSize(-1, FromDIP(1)));
    m_split_options_line->SetMinSize(wxSize(-1, FromDIP(1)));
    m_split_options_line->SetMaxSize(wxSize(-1, FromDIP(1)));
    sizer_split_options->Add(0, 0, 0, wxEXPAND, 0);
    sizer_split_options->Add(m_stext_options_title, 0, wxALIGN_CENTER, 0);
    sizer_split_options->Add(m_split_options_line, 1, wxALIGN_CENTER_VERTICAL, 0);


    m_sizer_options = new wxBoxSizer(wxHORIZONTAL);
    select_bed     = create_item_checkbox(_L("Bed Leveling"), this, _L("Bed Leveling"), "bed_leveling");
    select_flow    = create_item_checkbox(_L("Flow Dynamics Calibration"), this, _L("Flow Dynamics Calibration"), "flow_cali");
    select_timelapse = create_item_checkbox(_L("Timelapse"), this, _L("Timelapse"), "timelapse");
    select_use_ams = create_ams_checkbox(_L("Enable AMS"), this, _L("Enable AMS"));

    m_sizer_options->Add(select_bed, 0, wxLEFT | wxRIGHT, WRAP_GAP);
    m_sizer_options->Add(select_flow, 0, wxLEFT | wxRIGHT, WRAP_GAP);
    m_sizer_options->Add(select_timelapse, 0, wxLEFT | wxRIGHT, WRAP_GAP);
    m_sizer_options->Add(select_use_ams, 0, wxLEFT | wxRIGHT, WRAP_GAP);

    select_bed->Show(false);
    select_flow->Show(false);
    select_timelapse->Show(false);
    select_use_ams->Show(false);

    m_sizer_options->Layout();


    m_simplebook   = new wxSimplebook(this, wxID_ANY, wxDefaultPosition, SELECT_MACHINE_DIALOG_SIMBOOK_SIZE, 0);

    // perpare mode
    m_panel_prepare = new wxPanel(m_simplebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_panel_prepare->SetBackgroundColour(m_colour_def_color);
    wxBoxSizer *m_sizer_prepare = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_pcont   = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_prepare->Add(0, 0, 1, wxTOP, FromDIP(12));

    auto hyperlink_sizer = new wxBoxSizer( wxHORIZONTAL );
    m_hyperlink = new wxHyperlinkCtrl(m_panel_prepare, wxID_ANY, _L("Click here if you can't connect to the printer"), wxT("https://wiki.bambulab.com/en/software/bambu-studio/failed-to-connect-printer"), wxDefaultPosition, wxDefaultSize, wxHL_DEFAULT_STYLE);

    hyperlink_sizer->Add(m_hyperlink, 0, wxALIGN_CENTER | wxALL, 5);
    m_sizer_prepare->Add(hyperlink_sizer, 0, wxALIGN_CENTER | wxALL, 5);

    m_button_ensure = new Button(m_panel_prepare, _L("Send"));
    m_button_ensure->SetBackgroundColor(m_btn_bg_enable);
    m_button_ensure->SetBorderColor(m_btn_bg_enable);
    m_button_ensure->SetTextColor(StateColor::darkModeColorFor("#FFFFFE"));
    m_button_ensure->SetSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_ensure->SetMinSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_ensure->SetMinSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_ensure->SetCornerRadius(FromDIP(5));
    m_button_ensure->Bind(wxEVT_BUTTON, &SelectMachineDialog::on_ok_btn, this);

    m_sizer_pcont->Add(0, 0, 1, wxEXPAND, 0);
    m_sizer_pcont->Add(m_button_ensure, 0,wxRIGHT, 0);


    m_sizer_prepare->Add(m_sizer_pcont, 0, wxEXPAND, 0);
    m_panel_prepare->SetSizer(m_sizer_prepare);
    m_panel_prepare->Layout();
    m_simplebook->AddPage(m_panel_prepare, wxEmptyString, true);

    // sending mode
    m_status_bar    = std::make_shared<BBLStatusBarSend>(m_simplebook);
    m_panel_sending = m_status_bar->get_panel();
    m_simplebook->AddPage(m_panel_sending, wxEmptyString, false);
    
    m_worker = std::make_unique<PlaterWorker<BoostThreadWorker>>(this, m_status_bar, "send_worker");

    // finish mode
    m_panel_finish = new wxPanel(m_simplebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_panel_finish->SetBackgroundColour(wxColour(135, 206, 250));
    wxBoxSizer *m_sizer_finish   = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_sizer_finish_v = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_finish_h = new wxBoxSizer(wxHORIZONTAL);

    auto imgsize      = FromDIP(25);
    auto completedimg = new wxStaticBitmap(m_panel_finish, wxID_ANY, create_scaled_bitmap("completed", m_panel_finish, 25), wxDefaultPosition, wxSize(imgsize, imgsize), 0);
    m_sizer_finish_h->Add(completedimg, 0, wxALIGN_CENTER | wxALL, FromDIP(5));

    m_statictext_finish = new wxStaticText(m_panel_finish, wxID_ANY, L("send completed"), wxDefaultPosition, wxDefaultSize, 0);
    m_statictext_finish->Wrap(-1);
    m_statictext_finish->SetForegroundColour(wxColour(0, 150, 136));
    m_sizer_finish_h->Add(m_statictext_finish, 0, wxALIGN_CENTER | wxALL, FromDIP(5));

    m_sizer_finish_v->Add(m_sizer_finish_h, 1, wxALIGN_CENTER, 0);

    m_sizer_finish->Add(m_sizer_finish_v, 1, wxALIGN_CENTER, 0);

    m_panel_finish->SetSizer(m_sizer_finish);
    m_panel_finish->Layout();
    m_sizer_finish->Fit(m_panel_finish);
    m_simplebook->AddPage(m_panel_finish, wxEmptyString, false);

    //show bind failed info
    m_sw_print_failed_info = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(380), FromDIP(125)), wxVSCROLL);
    m_sw_print_failed_info->SetBackgroundColour(*wxWHITE);
    m_sw_print_failed_info->SetScrollRate(0, 5);
    m_sw_print_failed_info->SetMinSize(wxSize(FromDIP(380), FromDIP(125)));
    m_sw_print_failed_info->SetMaxSize(wxSize(FromDIP(380), FromDIP(125)));

    wxBoxSizer* sizer_print_failed_info = new wxBoxSizer(wxVERTICAL);
    m_sw_print_failed_info->SetSizer(sizer_print_failed_info);


    wxBoxSizer* sizer_error_code = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer* sizer_error_desc = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer* sizer_extra_info = new wxBoxSizer(wxHORIZONTAL);

    auto st_title_error_code = new wxStaticText(m_sw_print_failed_info, wxID_ANY, _L("Error code"));
    auto st_title_error_code_doc = new wxStaticText(m_sw_print_failed_info, wxID_ANY,": ");
    m_st_txt_error_code = new Label(m_sw_print_failed_info, wxEmptyString);
    st_title_error_code->SetForegroundColour(0x909090);
    st_title_error_code_doc->SetForegroundColour(0x909090);
    m_st_txt_error_code->SetForegroundColour(0x909090);
    st_title_error_code->SetFont(::Label::Body_13);
    st_title_error_code_doc->SetFont(::Label::Body_13);
    m_st_txt_error_code->SetFont(::Label::Body_13);
    st_title_error_code->SetMinSize(wxSize(FromDIP(74), -1));
    st_title_error_code->SetMaxSize(wxSize(FromDIP(74), -1));
    m_st_txt_error_code->SetMinSize(wxSize(FromDIP(260), -1));
    m_st_txt_error_code->SetMaxSize(wxSize(FromDIP(260), -1));
    sizer_error_code->Add(st_title_error_code, 0, wxALL, 0);
    sizer_error_code->Add(st_title_error_code_doc, 0, wxALL, 0);
    sizer_error_code->Add(m_st_txt_error_code, 0, wxALL, 0);


    auto st_title_error_desc = new wxStaticText(m_sw_print_failed_info, wxID_ANY, wxT("Error desc"));
    auto st_title_error_desc_doc = new wxStaticText(m_sw_print_failed_info, wxID_ANY,": ");
    m_st_txt_error_desc = new Label(m_sw_print_failed_info, wxEmptyString);
    st_title_error_desc->SetForegroundColour(0x909090);
    st_title_error_desc_doc->SetForegroundColour(0x909090);
    m_st_txt_error_desc->SetForegroundColour(0x909090);
    st_title_error_desc->SetFont(::Label::Body_13);
    st_title_error_desc_doc->SetFont(::Label::Body_13);
    m_st_txt_error_desc->SetFont(::Label::Body_13);
    st_title_error_desc->SetMinSize(wxSize(FromDIP(74), -1));
    st_title_error_desc->SetMaxSize(wxSize(FromDIP(74), -1));
    m_st_txt_error_desc->SetMinSize(wxSize(FromDIP(260), -1));
    m_st_txt_error_desc->SetMaxSize(wxSize(FromDIP(260), -1));
    sizer_error_desc->Add(st_title_error_desc, 0, wxALL, 0);
    sizer_error_desc->Add(st_title_error_desc_doc, 0, wxALL, 0);
    sizer_error_desc->Add(m_st_txt_error_desc, 0, wxALL, 0);

    auto st_title_extra_info = new wxStaticText(m_sw_print_failed_info, wxID_ANY, wxT("Extra info"));
    auto st_title_extra_info_doc = new wxStaticText(m_sw_print_failed_info, wxID_ANY, ": ");
    m_st_txt_extra_info = new Label(m_sw_print_failed_info, wxEmptyString);
    st_title_extra_info->SetForegroundColour(0x909090);
    st_title_extra_info_doc->SetForegroundColour(0x909090);
    m_st_txt_extra_info->SetForegroundColour(0x909090);
    st_title_extra_info->SetFont(::Label::Body_13);
    st_title_extra_info_doc->SetFont(::Label::Body_13);
    m_st_txt_extra_info->SetFont(::Label::Body_13);
    st_title_extra_info->SetMinSize(wxSize(FromDIP(74), -1));
    st_title_extra_info->SetMaxSize(wxSize(FromDIP(74), -1));
    m_st_txt_extra_info->SetMinSize(wxSize(FromDIP(260), -1));
    m_st_txt_extra_info->SetMaxSize(wxSize(FromDIP(260), -1));
    sizer_extra_info->Add(st_title_extra_info, 0, wxALL, 0);
    sizer_extra_info->Add(st_title_extra_info_doc, 0, wxALL, 0);
    sizer_extra_info->Add(m_st_txt_extra_info, 0, wxALL, 0);


    m_link_network_state = new wxHyperlinkCtrl(m_sw_print_failed_info, wxID_ANY,_L("Check the status of current system services"),"");
    m_link_network_state->SetFont(::Label::Body_12);
    m_link_network_state->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {wxGetApp().link_to_network_check();});
    m_link_network_state->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {m_link_network_state->SetCursor(wxCURSOR_HAND);});
    m_link_network_state->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {m_link_network_state->SetCursor(wxCURSOR_ARROW);});

    sizer_print_failed_info->Add(m_link_network_state, 0, wxLEFT, 5);
    sizer_print_failed_info->Add(sizer_error_code, 0, wxLEFT, 5);
    sizer_print_failed_info->Add(0, 0, 0, wxTOP, FromDIP(3));
    sizer_print_failed_info->Add(sizer_error_desc, 0, wxLEFT, 5);
    sizer_print_failed_info->Add(0, 0, 0, wxTOP, FromDIP(3));
    sizer_print_failed_info->Add(sizer_extra_info, 0, wxLEFT, 5);


    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(12));
    m_sizer_main->Add(m_basic_panel, 0, wxEXPAND|wxLEFT|wxRIGHT, FromDIP(15));
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(14));
    m_sizer_main->Add(sizer_split_filament, 1, wxEXPAND|wxLEFT|wxRIGHT, FromDIP(15));
    m_sizer_main->Add(m_filament_panel, 0, wxALIGN_CENTER|wxLEFT|wxRIGHT, FromDIP(15));
    m_sizer_main->Add(m_sizer_filament_2extruder, 0, wxALIGN_CENTER|wxLEFT|wxRIGHT, FromDIP(15));
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(6));
    m_sizer_main->Add(m_statictext_ams_msg, 0, wxLEFT, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(16));
    m_sizer_main->Add(sizer_split_options, 1, wxEXPAND|wxLEFT|wxRIGHT, FromDIP(15));
    m_sizer_main->Add(m_sizer_options, 0, wxLEFT|wxRIGHT, FromDIP(15));
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(10));
    m_sizer_main->Add(m_simplebook, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    m_sizer_main->Add(m_sw_print_failed_info, 0, wxALIGN_CENTER, 0);
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(18));

    show_print_failed_info(false);

    SetSizer(m_sizer_main);
    Layout();
    Fit();
    Thaw();

    init_bind();
    init_timer();
    Centre(wxBOTH);
    wxGetApp().UpdateDlgDarkUI(this);
}

void SelectMachineDialog::init_bind()
{
    Bind(wxEVT_TIMER, &SelectMachineDialog::on_timer, this);
    Bind(EVT_CLEAR_IPADDRESS, &SelectMachineDialog::clear_ip_address_config, this);
    Bind(EVT_SHOW_ERROR_INFO, [this](auto& e) {show_print_failed_info(true);});
    Bind(EVT_UPDATE_USER_MACHINE_LIST, &SelectMachineDialog::update_printer_combobox, this);
    Bind(EVT_PRINT_JOB_CANCEL, &SelectMachineDialog::on_print_job_cancel, this);
    Bind(EVT_SET_FINISH_MAPPING, &SelectMachineDialog::on_set_finish_mapping, this);
    Bind(wxEVT_LEFT_DOWN, [this](auto& e) {check_fcous_state(this);e.Skip();});
    m_panel_prepare->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {check_fcous_state(this);e.Skip();});
    m_basic_panel->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {check_fcous_state(this);e.Skip();});
    m_bitmap_last_plate->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_HAND); });
    m_bitmap_last_plate->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_ARROW); });
    m_bitmap_next_plate->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_HAND); });
    m_bitmap_next_plate->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_ARROW); });


    Bind(EVT_CONNECT_LAN_MODE_PRINT, [this](wxCommandEvent& e) {
        if (e.GetInt() == 0) {
            DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
            if (!dev) return;
            MachineObject* obj = dev->get_selected_machine();
            if (!obj) return;

            if (obj->dev_id == e.GetString()) {
                m_comboBox_printer->SetValue(obj->dev_name + "(LAN)");
            }
        }
    });

    m_bitmap_last_plate->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        if (m_print_plate_idx > 0) {
            m_print_plate_idx--;
            update_page_turn_state(true);
            set_default_from_sdcard();
        }
    });

    m_bitmap_next_plate->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        if (m_print_plate_idx < (m_print_plate_total - 1)) {
            m_print_plate_idx++;
            update_page_turn_state(true);
            set_default_from_sdcard();
        }
    });
}

void SelectMachineDialog::check_focus(wxWindow* window)
{
    if (window == m_rename_input || window == m_rename_input->GetTextCtrl()) {
        on_rename_enter();
    }
}

void SelectMachineDialog::show_print_failed_info(bool show, int code, wxString description, wxString extra)
{

    if (show) {
        if (!m_sw_print_failed_info->IsShown()) {
            m_sw_print_failed_info->Show(true);

            m_st_txt_error_code->SetLabelText(wxString::Format("%d", m_print_error_code));
            m_st_txt_error_desc->SetLabelText( wxGetApp().filter_string(m_print_error_msg));
            m_st_txt_extra_info->SetLabelText( wxGetApp().filter_string(m_print_error_extra));

            m_st_txt_error_code->Wrap(FromDIP(260));
            m_st_txt_error_desc->Wrap(FromDIP(260));
            m_st_txt_extra_info->Wrap(FromDIP(260));
        }
        else {
            m_sw_print_failed_info->Show(false);
        }
        Layout();
        Fit();
    }
    else {
        if (!m_sw_print_failed_info->IsShown()) {return;}
        m_sw_print_failed_info->Show(false);
        m_st_txt_error_code->SetLabelText(wxEmptyString);
        m_st_txt_error_desc->SetLabelText(wxEmptyString);
        m_st_txt_extra_info->SetLabelText(wxEmptyString);
        Layout();
        Fit();
    }
}

void SelectMachineDialog::check_fcous_state(wxWindow* window)
{
    check_focus(window);
    auto children = window->GetChildren();
    for (auto child : children) {
        check_fcous_state(child);
    }
}

void SelectMachineDialog::popup_filament_backup()
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;
    if (dev->get_selected_machine()/* && dev->get_selected_machine()->filam_bak.size() > 0*/) {
        AmsReplaceMaterialDialog* m_replace_material_popup = new AmsReplaceMaterialDialog(this);
        m_replace_material_popup->update_mapping_result(m_ams_mapping_result);
        m_replace_material_popup->update_machine_obj(dev->get_selected_machine());
        m_replace_material_popup->ShowModal();
    }
}

wxWindow *SelectMachineDialog::create_ams_checkbox(wxString title, wxWindow *parent, wxString tooltip)
{
    auto checkbox = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    checkbox->SetBackgroundColour(m_colour_def_color);

    wxBoxSizer *sizer_checkbox = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *sizer_check    = new wxBoxSizer(wxVERTICAL);

    auto check = new ::CheckBox(checkbox);

    sizer_check->Add(check, 0, wxBOTTOM | wxEXPAND | wxTOP, FromDIP(5));

    sizer_checkbox->Add(sizer_check, 0, wxEXPAND, FromDIP(5));
    sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(7));

    auto text = new wxStaticText(checkbox, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, 0);
    text->SetFont(::Label::Body_12);
    text->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3C")));
    text->Wrap(-1);
    sizer_checkbox->Add(text, 0, wxALIGN_CENTER, 0);

    enable_ams       = new ScalableBitmap(this, "enable_ams", 16);
    img_use_ams_tip = new wxStaticBitmap(checkbox, wxID_ANY, enable_ams->bmp(), wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)), 0);
    sizer_checkbox->Add(img_use_ams_tip, 0, wxALIGN_CENTER | wxLEFT, FromDIP(5));

    img_use_ams_tip->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
        wxPoint img_pos = img_use_ams_tip->ClientToScreen(wxPoint(0, 0));
        wxPoint popup_pos(img_pos.x, img_pos.y + img_use_ams_tip->GetRect().height);
        m_mapping_tip_popup.Position(popup_pos, wxSize(0, 0));
        m_mapping_tip_popup.Popup();

        if (m_mapping_tip_popup.ClientToScreen(wxPoint(0, 0)).y < img_pos.y) {
            m_mapping_tip_popup.Dismiss();
            popup_pos = wxPoint(img_pos.x, img_pos.y - m_mapping_tip_popup.GetRect().height);
            m_mapping_tip_popup.Position(popup_pos, wxSize(0, 0));
            m_mapping_tip_popup.Popup();
        }
    });

    img_use_ams_tip->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) {
        m_mapping_tip_popup.Dismiss();
        });

    checkbox->SetSizer(sizer_checkbox);
    checkbox->Layout();
    sizer_checkbox->Fit(checkbox);

    checkbox->SetToolTip(tooltip);
    text->SetToolTip(tooltip);

    text->Bind(wxEVT_LEFT_DOWN, [this, check](wxMouseEvent& event) {
        check->SetValue(check->GetValue() ? false : true);
        });

    checkbox->Bind(wxEVT_LEFT_DOWN, [this, check](wxMouseEvent& event) {
        check->SetValue(check->GetValue() ? false : true);
        });

    m_checkbox_list["use_ams"] = check;
    return checkbox;
}

wxWindow *SelectMachineDialog::create_item_checkbox(wxString title, wxWindow *parent, wxString tooltip, std::string param)
{
    auto checkbox = new wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    checkbox->SetBackgroundColour(m_colour_def_color);

    wxBoxSizer *sizer_checkbox = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *sizer_check    = new wxBoxSizer(wxVERTICAL);

    auto check = new ::CheckBox(checkbox);

    sizer_check->Add(check, 0, wxBOTTOM | wxEXPAND | wxTOP, FromDIP(5));

    auto text = new wxStaticText(checkbox, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    text->SetFont(::Label::Body_12);
    text->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3C")));
    text->Wrap(-1);
    text->SetMinSize(wxSize(FromDIP(140), -1));
    text->SetMaxSize(wxSize(FromDIP(140), -1));

    sizer_checkbox->Add(sizer_check, 0, wxEXPAND, FromDIP(5));
    sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(7));
    sizer_checkbox->Add(text, 0, wxALIGN_CENTER, 0);

    checkbox->SetSizer(sizer_checkbox);
    checkbox->Layout();
    sizer_checkbox->Fit(checkbox);

    check->SetToolTip(tooltip);
    text->SetToolTip(tooltip);



    check->Bind(wxEVT_LEFT_DOWN, [this, check, param](wxMouseEvent &e) {
        //if (!m_checkbox_state_list[param]) {return;}
        AppConfig* config = wxGetApp().app_config;
        if (config) {
            if (check->GetValue())
                config->set_str("print", param, "0");
            else
                config->set_str("print", param, "1");
        }
        e.Skip();
    });

    checkbox->Bind(wxEVT_LEFT_DOWN, [this, check, param](wxMouseEvent&) {
        //if (!m_checkbox_state_list[param]) {return;}
        check->SetValue(check->GetValue() ? false : true);
        AppConfig* config = wxGetApp().app_config;
        if (config) {
            if (check->GetValue())
                config->set_str("print", param, "1");
            else
                config->set_str("print", param, "0");
        }
        });

    text->Bind(wxEVT_LEFT_DOWN, [this, check, param](wxMouseEvent &) {
        //if (!m_checkbox_state_list[param]) {return;}
        check->SetValue(check->GetValue() ? false : true);
        AppConfig* config = wxGetApp().app_config;
        if (config) {
            if (check->GetValue())
                config->set_str("print", param, "1");
            else
                config->set_str("print", param, "0");
        }
    });

    //m_checkbox_state_list[param] = true;
    m_checkbox_list[param] = check;
    return checkbox;
}

void SelectMachineDialog::update_select_layout(MachineObject *obj)
{
    if (obj && obj->is_support_auto_flow_calibration) {
        select_flow->Show();
    } else {
        select_flow->Hide();
    }

    if (obj && obj->is_support_auto_leveling) {
        select_bed->Show();
    } else {
        select_bed->Hide();
    }

    if (obj && obj->is_support_timelapse && is_show_timelapse()) {
        select_timelapse->Show();
        update_timelapse_enable_status();
    } else {
        select_timelapse->Hide();
    }

    m_sizer_options->Layout();
    Layout();
    Fit();
}

void SelectMachineDialog::prepare_mode(bool refresh_button)
{
    // disable combobox
    m_comboBox_printer->Enable();
    Enable_Auto_Refill(true);
    show_print_failed_info(false);

    m_is_in_sending_mode = false;
    m_worker->wait_for_idle();

    if (wxIsBusy())
        wxEndBusyCursor();

    if (refresh_button) {
        Enable_Send_Button(true);
    }

    m_status_bar->reset();
    if (m_simplebook->GetSelection() != 0) {
        m_simplebook->SetSelection(0);
        Layout();
        Fit();
    }

    if (m_print_page_mode != PrintPageModePrepare) {
        m_print_page_mode = PrintPageModePrepare;
        for (auto it = m_materialList.begin(); it != m_materialList.end(); it++) {
            it->second->item->enable();
        }
    }
}

void SelectMachineDialog::sending_mode()
{
    // disable combobox
    m_comboBox_printer->Disable();
    Enable_Auto_Refill(false);

    m_is_in_sending_mode = true;
    if (m_simplebook->GetSelection() != 1){
        m_simplebook->SetSelection(1);
        Layout();
        Fit();
    }


    if (m_print_page_mode != PrintPageModeSending) {
        m_print_page_mode = PrintPageModeSending;
        for (auto it = m_materialList.begin(); it != m_materialList.end(); it++) {
            it->second->item->disable();
        }
    }
}

void SelectMachineDialog::finish_mode()
{
    m_print_page_mode = PrintPageModeFinish;
    m_is_in_sending_mode = false;
    m_simplebook->SetSelection(2);
    Layout();
    Fit();
}


void SelectMachineDialog::sync_ams_mapping_result(std::vector<FilamentInfo> &result)
{
    if (result.empty()) {
        BOOST_LOG_TRIVIAL(trace) << "ams_mapping result is empty";
        for (auto it = m_materialList.begin(); it != m_materialList.end(); it++) {
            wxString ams_id = "-";
            wxColour ams_col = wxColour(0xCE, 0xCE, 0xCE);
            it->second->item->set_ams_info(ams_col, ams_id);
        }
        return;
    }

    for (auto f = result.begin(); f != result.end(); f++) {
        BOOST_LOG_TRIVIAL(trace) << "ams_mapping f id = " << f->id << ", tray_id = " << f->tray_id << ", color = " << f->color << ", type = " << f->type;

        MaterialHash::iterator iter = m_materialList.begin();
        while (iter != m_materialList.end()) {
            int           id   = iter->second->id;
            Material *    item = iter->second;
            MaterialItem *m    = item->item;

            if (f->id == id) {
                wxString ams_id;
                wxColour ams_col;

                if (f->tray_id >= 0) {
                    ams_id = wxGetApp().transition_tridid(f->tray_id);
                    //ams_id = wxString::Format("%02d", f->tray_id + 1);
                } else {
                    ams_id = "-";
                }

                if (!f->color.empty()) {
                    ams_col = AmsTray::decode_color(f->color);
                } else {
                    // default color
                    ams_col = wxColour(0xCE, 0xCE, 0xCE);
                }
                std::vector<wxColour> cols;
                for (auto col : f->colors) {
                    cols.push_back(AmsTray::decode_color(col));
                }
                m->set_ams_info(ams_col, ams_id,f->ctype, cols);
                break;
            }
            iter++;
        }
    }
    auto tab_index = (MainFrame::TabPosition) dynamic_cast<Notebook *>(wxGetApp().tab_panel())->GetSelection();
    if (tab_index == MainFrame::TabPosition::tp3DEditor || tab_index == MainFrame::TabPosition::tpPreview) {
        updata_thumbnail_data_after_connected_printer();
    }
}

void print_ams_mapping_result(std::vector<FilamentInfo>& result)
{
    if (result.empty()) {
        BOOST_LOG_TRIVIAL(info) << "print_ams_mapping_result: empty";
    }

    char buffer[256];
    for (int i = 0; i < result.size(); i++) {
        ::sprintf(buffer, "print_ams_mapping: F(%02d) -> A(%02d)", result[i].id+1, result[i].tray_id+1);
        BOOST_LOG_TRIVIAL(info) << std::string(buffer);
    }
}

bool SelectMachineDialog::do_ams_mapping(MachineObject *obj_)
{
    if (!obj_) return false;
    obj_->get_ams_colors(m_cur_colors_in_thumbnail);
    // try color and type mapping
    int result = obj_->ams_filament_mapping(m_filaments, m_ams_mapping_result);
    if (result == 0) {
        print_ams_mapping_result(m_ams_mapping_result);
        std::string ams_array;
        std::string ams_array2;
        std::string mapping_info;
        get_ams_mapping_result(ams_array, ams_array2, mapping_info);
        if (ams_array.empty()) {
            reset_ams_material();
            BOOST_LOG_TRIVIAL(info) << "ams_mapping_array=[]";
        } else {
            sync_ams_mapping_result(m_ams_mapping_result);
            BOOST_LOG_TRIVIAL(info) << "ams_mapping_array=" << ams_array;
            BOOST_LOG_TRIVIAL(info) << "ams_mapping_array2=" << ams_array2;
            BOOST_LOG_TRIVIAL(info) << "ams_mapping_info=" << mapping_info;
        }
        return obj_->is_valid_mapping_result(m_ams_mapping_result);
    } else {
        // do not support ams mapping try to use order mapping
        bool is_valid = obj_->is_valid_mapping_result(m_ams_mapping_result);
        if (result != 1 && !is_valid) {
            //reset invalid result
            for (int i = 0; i < m_ams_mapping_result.size(); i++) {
                m_ams_mapping_result[i].tray_id = -1;
                m_ams_mapping_result[i].distance = 99999;
            }
        }
        sync_ams_mapping_result(m_ams_mapping_result);
        return is_valid;
    }

    return true;
}

bool SelectMachineDialog::get_ams_mapping_result(std::string &mapping_array_str, std::string& mapping_array_str2, std::string &ams_mapping_info)
{
    if (m_ams_mapping_result.empty())
        return false;

    bool valid_mapping_result = true;
    int invalid_count = 0;
    for (int i = 0; i < m_ams_mapping_result.size(); i++) {
        if (m_ams_mapping_result[i].tray_id == -1) {
            valid_mapping_result = false;
            invalid_count++;
        }
    }

    if (invalid_count == m_ams_mapping_result.size()) {
        return false;
    } else {

        json mapping_v0_json    = json::array();
        json mapping_v1_json    = json::array();

        json mapping_info_json  = json::array();

        for (int i = 0; i < wxGetApp().preset_bundle->filament_presets.size(); i++) {

            int tray_id = -1;

            json mapping_item_v1;
            mapping_item_v1["ams_id"] = 0xff;
            mapping_item_v1["slot_id"] = 0xff;

            json mapping_item;
            mapping_item["ams"] = tray_id;
            mapping_item["targetColor"] = "";
            mapping_item["filamentId"] = "";
            mapping_item["filamentType"] = "";

            

            for (int k = 0; k < m_ams_mapping_result.size(); k++) {
                if (m_ams_mapping_result[k].id == i) {
                    tray_id = m_ams_mapping_result[k].tray_id;
                    mapping_item["ams"]             = tray_id;
                    mapping_item["filamentType"]    = m_filaments[k].type;
                    auto it = wxGetApp().preset_bundle->filaments.find_preset(wxGetApp().preset_bundle->filament_presets[i]);
                    if (it != nullptr) {
                        mapping_item["filamentId"] = it->filament_id;
                    }
                    //convert #RRGGBB to RRGGBBAA
                    mapping_item["sourceColor"]     = m_filaments[k].color;
                    mapping_item["targetColor"]     = m_ams_mapping_result[k].color;


                    /*new ams mapping data*/
                    
                    try
                    {
                        if (m_ams_mapping_result[k].ams_id.empty() || m_ams_mapping_result[k].slot_id.empty()) {  // invalid case
                            mapping_item_v1["ams_id"]  = 255; // TODO: Orca hack
                            mapping_item_v1["slot_id"] = 255;
                        }
                        else {
                            mapping_item_v1["ams_id"] = std::stoi(m_ams_mapping_result[k].ams_id);
                            mapping_item_v1["slot_id"] = std::stoi(m_ams_mapping_result[k].slot_id);
                        }
                    }
                    catch (...)
                    {
                    }
                }
            }
            mapping_v0_json.push_back(tray_id);
            mapping_v1_json.push_back(mapping_item_v1);
            mapping_info_json.push_back(mapping_item);
        }


        mapping_array_str = mapping_v0_json.dump();
        mapping_array_str2 = mapping_v1_json.dump();

        ams_mapping_info = mapping_info_json.dump();
        return valid_mapping_result;
    }
    return true;
}

bool SelectMachineDialog::build_nozzles_info(std::string& nozzles_info)
{
    /* init nozzles info */
    json nozzle_info_json = json::array();
    nozzles_info = nozzle_info_json.dump();

    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    if (!preset_bundle)
        return false;
    auto opt_nozzle_diameters = preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloats>("nozzle_diameter");
    if (opt_nozzle_diameters == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "build_nozzles_info, opt_nozzle_diameters is nullptr";
        return false;
    }
    //auto opt_nozzle_volume_type = preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type");
    //if (opt_nozzle_volume_type == nullptr) {
    //    BOOST_LOG_TRIVIAL(error) << "build_nozzles_info, opt_nozzle_volume_type is nullptr";
    //    return false;
    //}
    json nozzle_item;
    /* only o1d two nozzles has build_nozzles info now */
    if (opt_nozzle_diameters->size() != 2) {
        return false;
    }
    for (size_t i = 0; i < opt_nozzle_diameters->size(); i++) {
        if (i == (size_t)ConfigNozzleIdx::NOZZLE_LEFT) {
            nozzle_item["id"] = CloudTaskNozzleId::NOZZLE_LEFT;
        }
        else if (i == (size_t)ConfigNozzleIdx::NOZZLE_RIGHT) {
            nozzle_item["id"] = CloudTaskNozzleId::NOZZLE_RIGHT;
        }
        else {
            /* unknown ConfigNozzleIdx */
            BOOST_LOG_TRIVIAL(error) << "build_nozzles_info, unknown ConfigNozzleIdx = " << i;
            assert(false);
            continue;
        }
        nozzle_item["type"] = nullptr;
        //if (i >= 0 && i < opt_nozzle_volume_type->size()) {
            nozzle_item["flowSize"] = "standard_flow"; // TODO: Orca hack
        //}
        if (i >= 0 && i < opt_nozzle_diameters->size()) {
            nozzle_item["diameter"] = opt_nozzle_diameters->get_at(i);
        }
        nozzle_info_json.push_back(nozzle_item);
    }
    nozzles_info = nozzle_info_json.dump();
    return true;
}

void SelectMachineDialog::prepare(int print_plate_idx)
{
    m_print_plate_idx = print_plate_idx;
}

void SelectMachineDialog::update_ams_status_msg(wxString msg, bool is_warning)
{
    auto colour = is_warning ? wxColour(0xFF, 0x6F, 0x00):wxColour(0x6B, 0x6B, 0x6B);
    m_statictext_ams_msg->SetForegroundColour(colour);

    if (msg.empty()) {
        if (!m_statictext_ams_msg->GetLabel().empty()) {
            m_statictext_ams_msg->SetLabel(wxEmptyString);
            m_statictext_ams_msg->Hide();
            Layout();
            Fit();
        }
    } else {
        msg = format_text(msg);

        auto str_new = msg.utf8_string();
        stripWhiteSpace(str_new);

        auto str_old = m_statictext_ams_msg->GetLabel().utf8_string();
        stripWhiteSpace(str_old);

        if (str_new != str_old) {
            if (m_statictext_ams_msg->GetLabel() != msg) {
                m_statictext_ams_msg->SetLabel(msg);
                m_statictext_ams_msg->SetMinSize(wxSize(FromDIP(400), -1));
                m_statictext_ams_msg->SetMaxSize(wxSize(FromDIP(400), -1));
                m_statictext_ams_msg->Wrap(FromDIP(400));
                m_statictext_ams_msg->Show();
                Layout();
                Fit();
            }
        }
    }
}

void SelectMachineDialog::update_priner_status_msg(wxString msg, bool is_warning)
{
    auto colour = is_warning ? wxColour(0xFF, 0x6F, 0x00) : wxColour(0x6B, 0x6B, 0x6B);
    m_text_printer_msg->SetForegroundColour(colour);

    if (msg.empty()) {
        if (!m_text_printer_msg->GetLabel().empty()) {
            m_text_printer_msg->SetLabel(wxEmptyString);
            m_text_printer_msg->Hide();
            Layout();
            Fit();
        }
    } else {
        msg          = format_text(msg);

        auto str_new = msg.utf8_string();
        stripWhiteSpace(str_new);

        auto str_old = m_text_printer_msg->GetLabel().utf8_string();
        stripWhiteSpace(str_old);

        if (str_new != str_old) {
            if (m_text_printer_msg->GetLabel() != msg) {
                m_text_printer_msg->SetLabel(msg);
                m_text_printer_msg->SetMinSize(wxSize(FromDIP(420), -1));
                m_text_printer_msg->SetMaxSize(wxSize(FromDIP(420), -1));
                m_text_printer_msg->Wrap(FromDIP(420));
                m_text_printer_msg->Show();
                Layout();
                Fit();
            }
        }
    }
}

void SelectMachineDialog::update_print_status_msg(wxString msg, bool is_warning, bool is_printer_msg)
{
    if (is_printer_msg) {
        update_ams_status_msg(wxEmptyString, false);
        update_priner_status_msg(msg, is_warning);
    } else {
        update_ams_status_msg(msg, is_warning);
        update_priner_status_msg(wxEmptyString, false);
    }
}

void SelectMachineDialog::update_print_error_info(int code, std::string msg, std::string extra)
{
    m_print_error_code  = code;
    m_print_error_msg   = msg;
    m_print_error_extra = extra;
}

bool SelectMachineDialog::has_tips(MachineObject* obj)
{
    if (!obj) return false;

    // must set to a status if return true
    if (select_timelapse->IsShown() &&
        m_checkbox_list["timelapse"]->GetValue()) {
        if (obj->get_sdcard_state() == MachineObject::SdcardState::NO_SDCARD) {
            show_status(PrintDialogStatus::PrintStatusTimelapseNoSdcard);
            return true;
        }
    }

    return false;
}

void SelectMachineDialog::show_status(PrintDialogStatus status, std::vector<wxString> params)
{
    if (m_print_status != status)
        BOOST_LOG_TRIVIAL(info) << "select_machine_dialog: show_status = " << status << "(" << get_print_status_info(status) << ")";
    m_print_status = status;

    // m_comboBox_printer
    if (status == PrintDialogStatus::PrintStatusRefreshingMachineList)
        m_comboBox_printer->Disable();
    else
        m_comboBox_printer->Enable();

    // other
    if (status == PrintDialogStatus::PrintStatusInit) {
        update_print_status_msg(wxEmptyString, false, false);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusNoUserLogin) {
        wxString msg_text = _L("No login account, only printers in LAN mode are displayed");
        update_print_status_msg(msg_text, false, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    }else if (status == PrintDialogStatus::PrintStatusInvalidPrinter) {
        update_print_status_msg(wxEmptyString, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusConnectingServer) {
        wxString msg_text = _L("Connecting to server");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusReading) {
        wxString msg_text = _L("Synchronizing device information");
        update_print_status_msg(msg_text, false, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusReadingFinished) {
        update_print_status_msg(wxEmptyString, false, true);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusReadingTimeout) {
        wxString msg_text = _L("Synchronizing device information time out");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusInUpgrading) {
        wxString msg_text = _L("Cannot send the print job when the printer is updating firmware");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusInSystemPrinting) {
        wxString msg_text = _L("The printer is executing instructions. Please restart printing after it ends");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusInPrinting) {
        wxString msg_text = _L("The printer is busy on other print job");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusDisableAms) {
        update_print_status_msg(wxEmptyString, false, false);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusNeedUpgradingAms) {
        wxString msg_text;
        if (params.size() > 0)
            msg_text = wxString::Format(_L("Filament %s exceeds the number of AMS slots. Please update the printer firmware to support AMS slot assignment."), params[0]);
        else
            msg_text = _L("Filament exceeds the number of AMS slots. Please update the printer firmware to support AMS slot assignment.");
        update_print_status_msg(msg_text, true, false);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusAmsMappingSuccess){
        wxString msg_text = _L("Filaments to AMS slots mappings have been established. You can click a filament above to change its mapping AMS slot");
        update_print_status_msg(msg_text, false, false);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusAmsMappingInvalid) {
        wxString msg_text = _L("Please click each filament above to specify its mapping AMS slot before sending the print job");
        update_print_status_msg(msg_text, true, false);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusAmsMappingU0Invalid) {
        wxString msg_text;
        if (params.size() > 1)
            msg_text = wxString::Format(_L("Filament %s does not match the filament in AMS slot %s. Please update the printer firmware to support AMS slot assignment."), params[0], params[1]);
        else
            msg_text = _L("Filament does not match the filament in AMS slot. Please update the printer firmware to support AMS slot assignment.");
        update_print_status_msg(msg_text, true, false);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusAmsMappingValid) {
        wxString msg_text = _L("Filaments to AMS slots mappings have been established. You can click a filament above to change its mapping AMS slot");
        update_print_status_msg(msg_text, false, false);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusRefreshingMachineList) {
        update_print_status_msg(wxEmptyString, false, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(false);
    } else if (status == PrintDialogStatus::PrintStatusSending) {
        Enable_Send_Button(false);
        Enable_Refresh_Button(false);
    } else if (status == PrintDialogStatus::PrintStatusSendingCanceled) {
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusLanModeNoSdcard) {
        wxString msg_text = _L("An SD card needs to be inserted before printing via LAN.");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusAmsMappingByOrder) {
        wxString msg_text = _L("The printer firmware only supports sequential mapping of filament => AMS slot.");
        update_print_status_msg(msg_text, false, false);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusNoSdcard) {
        wxString msg_text = _L("An SD card needs to be inserted before printing.");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    }else if (status == PrintDialogStatus::PrintStatusUnsupportedPrinter) {
        wxString msg_text;
        try
        {
            DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
            if (!dev) return;

            //source print
            MachineObject* obj_ = dev->get_selected_machine();
            if (obj_ == nullptr) return;
            auto sourcet_print_name = obj_->get_printer_type_display_str();
            sourcet_print_name.Replace(wxT("Bambu Lab "), wxEmptyString);

            //target print
            std::string target_model_id;
            if (m_print_type == PrintFromType::FROM_NORMAL){
                PresetBundle* preset_bundle = wxGetApp().preset_bundle;
                target_model_id = preset_bundle->printers.get_edited_preset().get_printer_type(preset_bundle);
            }
            else if (m_print_type == PrintFromType::FROM_SDCARD_VIEW) {
                if (m_required_data_plate_data_list.size() > 0) {
                    target_model_id = m_required_data_plate_data_list[m_print_plate_idx]->printer_model_id;
                }
            }

            auto target_print_name = wxString(obj_->get_preset_printer_model_name(target_model_id));
            target_print_name.Replace(wxT("Bambu Lab "), wxEmptyString);
            msg_text = wxString::Format(_L("The selected printer (%s) is incompatible with the chosen printer profile in the slicer (%s)."), sourcet_print_name, target_print_name);
            
            update_print_status_msg(msg_text, true, true);
        }
        catch (...){}
        
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    }else if (status == PrintDialogStatus::PrintStatusTimelapseNoSdcard) {
        wxString msg_text = _L("An SD card needs to be inserted to record timelapse.");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusNeedForceUpgrading) {
        wxString msg_text = _L("Cannot send the print job to a printer whose firmware is required to get updated.");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusNeedConsistencyUpgrading) {
        wxString msg_text = _L("Cannot send the print job to a printer whose firmware is required to get updated.");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusBlankPlate) {
        wxString msg_text = _L("Cannot send the print job for empty plate");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusNotSupportedPrintAll) {
        wxString msg_text = _L("This printer does not support printing all plates");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusTimelapseWarning) {
        wxString   msg_text;
        PartPlate *plate = m_plater->get_partplate_list().get_curr_plate();
        for (auto warning : plate->get_slice_result()->warnings) {
            if (warning.msg == NOT_GENERATE_TIMELAPSE) {
                if (warning.error_code == "1001C001") {
                    msg_text = _L("When enable spiral vase mode, machines with I3 structure will not generate timelapse videos.");
                }
                else if (warning.error_code == "1001C002") {
                    msg_text = _L("Timelapse is not supported because Print sequence is set to \"By object\".");
                }
            }
        }
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    }

    // m_panel_warn m_simplebook
    if (status == PrintDialogStatus::PrintStatusSending) {
        sending_mode();
    }
    else {
        prepare_mode(false);
    }
}

void SelectMachineDialog::init_timer()
{
    m_refresh_timer = new wxTimer();
    m_refresh_timer->SetOwner(this);
}

void SelectMachineDialog::on_cancel(wxCloseEvent &event)
{
    if (m_mapping_popup.IsShown())
        m_mapping_popup.Dismiss();

    m_worker->cancel_all();
    this->EndModal(wxID_CANCEL);
}

bool SelectMachineDialog::is_blocking_printing(MachineObject* obj_)
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return true;
    auto target_model = obj_->printer_type;
    std::string source_model = "";

    if (m_print_type == PrintFromType::FROM_NORMAL) {
        PresetBundle* preset_bundle = wxGetApp().preset_bundle;
        source_model = preset_bundle->printers.get_edited_preset().get_printer_type(preset_bundle);


    }else if (m_print_type == PrintFromType::FROM_SDCARD_VIEW) {
        if (m_required_data_plate_data_list.size() > 0) {
            source_model = m_required_data_plate_data_list[m_print_plate_idx]->printer_model_id;
        }
    }

    if (source_model != target_model) {
        std::vector<std::string> compatible_machine = dev->get_compatible_machine(target_model);
        vector<std::string>::iterator it = find(compatible_machine.begin(), compatible_machine.end(), source_model);
        if (it == compatible_machine.end()) {
            return true;
        }
    }

    return false;
}


/**************************************************************//*
 * @param tag_nozzle_type -- return the mismatch nozzle type
 * @param tag_nozzle_diameter -- return the target nozzle_diameter but mismatch
 * @return is same or not
/*************************************************************/
bool SelectMachineDialog::is_same_nozzle_diameters(float &tag_nozzle_diameter) const
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return false;

    MachineObject* obj_ = dev->get_selected_machine();
    if (obj_ == nullptr) return false;

    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    auto opt_nozzle_diameters = preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloats>("nozzle_diameter");
    if (!opt_nozzle_diameters)
    {
        return false;
    }

    try
    {
        auto extruders = wxGetApp().plater()->get_partplate_list().get_curr_plate()->get_used_extruders();
        for (auto i = 0; i < extruders.size(); i++) {
            auto extruder = extruders[i] - 1;
            tag_nozzle_diameter = float(opt_nozzle_diameters->get_at(extruder));
            if (tag_nozzle_diameter != obj_->m_extder_data.extders[0].current_nozzle_diameter) {
                return false;
            }
        }
    }
    catch (const std::exception&)
    {
        return false;
    }

    return true;
}

bool SelectMachineDialog::is_same_nozzle_type(const Extder& extruder, std::string& filament_type) const
{
    auto printer_nozzle_hrc = Print::get_hrc_by_nozzle_type(extruder.current_nozzle_type);

    auto preset_bundle = wxGetApp().preset_bundle;
    MaterialHash::const_iterator iter = m_materialList.begin();
    while (iter != m_materialList.end()) {
        Material* item = iter->second;
        MaterialItem* m = item->item;
        auto filament_nozzle_hrc = preset_bundle->get_required_hrc_by_filament_type(m->m_material_name.ToStdString());

        if (abs(filament_nozzle_hrc) > abs(printer_nozzle_hrc)) {
            filament_type = m->m_material_name.ToStdString();
            BOOST_LOG_TRIVIAL(info) << "filaments hardness mismatch: filament = " << filament_type << " printer_nozzle_hrc = " << printer_nozzle_hrc;
            return false;
        }

        iter++;
    }

    return true;
}

bool SelectMachineDialog::is_same_printer_model()
{
    bool result = true;
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return result;

    MachineObject* obj_ = dev->get_selected_machine();

    assert(obj_->dev_id == m_printer_last_select);
    if (obj_ == nullptr) {
        return result;
    }

    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    if(preset_bundle == nullptr) return result;
    const auto source_model = preset_bundle->printers.get_edited_preset().get_printer_type(preset_bundle);
    const auto target_model = obj_->printer_type;
    // Orca: ignore P1P -> P1S
    if (source_model != target_model) {
        if ((source_model == "C12" && target_model == "C11") || (source_model == "C11" && target_model == "C12") ||
            ((obj_->is_support_upgrade_kit && obj_->installed_upgrade_kit) && (source_model == "C12"))) {
            return true;
        }

        BOOST_LOG_TRIVIAL(info) << "printer_model: source = " << source_model;
        BOOST_LOG_TRIVIAL(info) << "printer_model: target = " << target_model;
        return false;
    }

    if (obj_->is_support_upgrade_kit && obj_->installed_upgrade_kit) {
        BOOST_LOG_TRIVIAL(info) << "printer_model: source = " << source_model;
        BOOST_LOG_TRIVIAL(info) << "printer_model: target = " << obj_->printer_type << " (plus)";
        return false;
    }
    return true;
}

void SelectMachineDialog::show_errors(wxString &info)
{
    ConfirmBeforeSendDialog confirm_dlg(this, wxID_ANY, _L("Errors"));
    confirm_dlg.update_text(info);
    confirm_dlg.on_show();
}

void SelectMachineDialog::on_ok_btn(wxCommandEvent &event)
{

    bool has_slice_warnings = false;
    bool is_printing_block  = false;

    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;
    MachineObject* obj_ = dev->get_selected_machine();
    if (!obj_) return;


    std::vector<ConfirmBeforeSendInfo> confirm_text;
    confirm_text.push_back(ConfirmBeforeSendInfo(_L("Please check the following:")));

    //Check Printer Model Id
    bool is_same_printer_type = is_same_printer_model();
    if (!is_same_printer_type && (m_print_type == PrintFromType::FROM_NORMAL)) {
        confirm_text.push_back(ConfirmBeforeSendInfo(_L("The printer type selected when generating G-code is not consistent with the currently selected printer. It is recommended that you use the same printer type for slicing.")));
        has_slice_warnings = true;
    }


    //check blacklist
    for (auto i = 0; i < m_ams_mapping_result.size(); i++) {

        auto tid = m_ams_mapping_result[i].tray_id;

        std::string filament_type = boost::to_upper_copy(m_ams_mapping_result[i].type);
        std::string filament_brand;

        for (auto fs : m_filaments) {
            if (fs.id == m_ams_mapping_result[i].id) {
                filament_brand = m_filaments[i].brand;
            }
        }

        bool in_blacklist = false;
        std::string action;
        std::string info;

        DeviceManager::check_filaments_in_blacklist(filament_brand, filament_type, in_blacklist, action, info);

        if (in_blacklist && action == "warning") {
            wxString prohibited_error = wxString::FromUTF8(info);

            confirm_text.push_back(ConfirmBeforeSendInfo(prohibited_error));
            has_slice_warnings = true;
        }
    }

    PartPlate* plate = m_plater->get_partplate_list().get_curr_plate();

    for (auto warning : plate->get_slice_result()->warnings) {
        if (warning.msg == BED_TEMP_TOO_HIGH_THAN_FILAMENT) {
            if ((obj_->get_printer_is_enclosed())){
                // confirm_text.push_back(Plater::get_slice_warning_string(warning) + "\n");
                // has_slice_warnings = true;
            }
        }
        else if (warning.msg == NOT_SUPPORT_TRADITIONAL_TIMELAPSE) {
            if (obj_->get_printer_arch() == PrinterArch::ARCH_I3 && m_checkbox_list["timelapse"]->GetValue()) {
                confirm_text.push_back(ConfirmBeforeSendInfo(Plater::get_slice_warning_string(warning)));
                has_slice_warnings = true;
            }
        }
        else if (warning.msg == NOT_GENERATE_TIMELAPSE) {
            continue;
        }
        else if(warning.msg == NOZZLE_HRC_CHECKER){
            wxString error_info = Plater::get_slice_warning_string(warning);
            if (error_info.IsEmpty()) {
                error_info = wxString::Format("%s\n", warning.msg);
            }

            confirm_text.push_back(ConfirmBeforeSendInfo(error_info));
            has_slice_warnings = true;
        }
    }


    //check for unidentified material
    auto mapping_result = m_mapping_popup.parse_ams_mapping(obj_->amsList);
    auto has_unknown_filament = false;

    // check if ams mapping is has errors, tpu
    bool has_prohibited_filament = false;
    wxString prohibited_error = wxEmptyString;


    for (auto i = 0; i < m_ams_mapping_result.size(); i++) {

        auto tid = m_ams_mapping_result[i].tray_id;

        std::string filament_type = boost::to_upper_copy(m_ams_mapping_result[i].type);
        std::string filament_brand;

        for (auto fs : m_filaments) {
            if (fs.id == m_ams_mapping_result[i].id) {
                filament_brand = m_filaments[i].brand;
            }
        }

        bool in_blacklist = false;
        std::string action;
        std::string info;

        DeviceManager::check_filaments_in_blacklist(filament_brand, filament_type, in_blacklist, action, info);
        
        if (in_blacklist && action == "prohibition") {
            has_prohibited_filament = true;
            prohibited_error = wxString::FromUTF8(info);
        }

        for (auto miter : mapping_result) {
            //matching
            if (miter.id == tid) {
                if (miter.type == TrayType::THIRD || miter.type == TrayType::EMPTY) {
                    has_unknown_filament = true;
                    break;
                }
            }
        }
    }

    if (has_prohibited_filament && obj_->has_ams() && m_checkbox_list["use_ams"]->GetValue()) {
        wxString tpu_tips = prohibited_error;
        show_errors(tpu_tips);
        return;
    }

    if (has_unknown_filament) {
        has_slice_warnings = true;
        confirm_text.push_back(ConfirmBeforeSendInfo(_L("There are some unknown filaments in the AMS mappings. Please check whether they are the required filaments. If they are okay, press \"Confirm\" to start printing.")));
    }

    if (!obj_->m_extder_data.extders[0].current_nozzle_type != ntUndefine && (m_print_type == PrintFromType::FROM_NORMAL))
    {
        float nozzle_diameter = 0;
        if (!is_same_nozzle_diameters(nozzle_diameter))
        {
            has_slice_warnings = true;
            // is_printing_block  = true;  # Removed to allow nozzle overrides (to support non-standard nozzles)
            
            wxString nozzle_in_preset = wxString::Format(_L("nozzle in preset: %.1f %s"),nozzle_diameter, "");
            wxString nozzle_in_printer = wxString::Format(_L("nozzle memorized: %.1f %s"), obj_->m_extder_data.extders[0].current_nozzle_diameter, "");

            confirm_text.push_back(ConfirmBeforeSendInfo(_L("Your nozzle diameter in sliced file is not consistent with memorized nozzle. If you changed your nozzle lately, please go to Device > Printer Parts to change settings.") 
                + "\n    " + nozzle_in_preset 
                + "\n    " + nozzle_in_printer
                + "\n",  ConfirmBeforeSendInfo::InfoLevel::Warning));
        }
        
        std::string filament_type;
        if (!is_same_nozzle_type(obj_->m_extder_data.extders[0], filament_type))
        {
            has_slice_warnings = true;
            is_printing_block = true;

                wxString nozzle_in_preset = wxString::Format(_L("Printing high temperature material (%s material) with %s may cause nozzle damage"), filament_type, format_steel_name(obj_->m_extder_data.extders[0].current_nozzle_type));
            confirm_text.push_back(ConfirmBeforeSendInfo(nozzle_in_preset, ConfirmBeforeSendInfo::InfoLevel::Warning));
        }
    }
    

    if (has_slice_warnings) {
        wxString confirm_title = _L("Warning");
        ConfirmBeforeSendDialog confirm_dlg(this, wxID_ANY, confirm_title);

        if(is_printing_block){
            confirm_dlg.hide_button_ok();
            confirm_dlg.edit_cancel_button_txt(_L("Close"));
            confirm_text.push_back(ConfirmBeforeSendInfo(_L("Please fix the error above, otherwise printing cannot continue."), ConfirmBeforeSendInfo::InfoLevel::Warning));
        }
        else {
            confirm_text.push_back(ConfirmBeforeSendInfo(_L("Please click the confirm button if you still want to proceed with printing.")));
        }

        confirm_dlg.Bind(EVT_SECONDARY_CHECK_CONFIRM, [this, &confirm_dlg](wxCommandEvent& e) {
            confirm_dlg.on_hide();
           /* if (m_print_type == PrintFromType::FROM_SDCARD_VIEW) {
                this->connect_printer_mqtt();
            }
            else {*/
                this->on_send_print();
            //}
        });

        //confirm_dlg.Bind(EVT_UPDATE_NOZZLE, [this, obj_, tag_nozzle_type, nozzle_diameter, &confirm_dlg](wxCommandEvent& e) {
        //    if (obj_ && !tag_nozzle_type.empty() && !nozzle_diameter.empty()) {
        //        try
        //        {
        //            float diameter = std::stof(nozzle_diameter);
        //            diameter = round(diameter * 10) / 10;
        //            obj_->command_set_printer_nozzle(tag_nozzle_type, diameter);
        //        }
        //        catch (...) {}
        //    }
        //    });

       
        wxString info_msg = wxEmptyString;

        for (auto i = 0; i < confirm_text.size(); i++) {
            if (i == 0) {
                //info_msg += confirm_text[i];
            }
            else if (i == confirm_text.size() - 1) {
                //info_msg += confirm_text[i];
            }
            else {
                confirm_text[i].text = wxString::Format("%d. %s",i, confirm_text[i].text);
            }

        }
        confirm_dlg.update_text(confirm_text);
        confirm_dlg.on_show();

    } else {
        /* if (m_print_type == PrintFromType::FROM_SDCARD_VIEW) {
             this->connect_printer_mqtt();
         }
         else {*/
            this->on_send_print();
        //}
    }
}

wxString SelectMachineDialog::format_steel_name(NozzleType type)
{
    if (type == NozzleType::ntHardenedSteel) {
        return _L("Hardened Steel");
    }
    else if (type == NozzleType::ntStainlessSteel) {
        return _L("Stainless Steel");
    }

    return _L("Unknown");
}


void SelectMachineDialog::Enable_Auto_Refill(bool enable)
{
    if (enable) {
        m_ams_backup_tip->SetForegroundColour(wxColour(0x9C27B0));
    }
    else {
        m_ams_backup_tip->SetForegroundColour(wxColour(0x90, 0x90, 0x90));
    }
    m_ams_backup_tip->Refresh();
}

void SelectMachineDialog::connect_printer_mqtt()
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;
    MachineObject* obj_ = dev->get_selected_machine();

    if (obj_->connection_type() == "cloud") {
        show_status(PrintDialogStatus::PrintStatusSending);
        m_status_bar->disable_cancel_button();
        m_status_bar->set_status_text(_L("Connecting to the printer. Unable to cancel during the connection process."));
#if !BBL_RELEASE_TO_PUBLIC
        obj_->connect(false, wxGetApp().app_config->get("enable_ssl_for_mqtt") == "true" ? true : false);
#else
        obj_->connect(false, obj_->local_use_ssl_for_mqtt);
#endif
    }
    else {
        on_send_print();
    }
}

void SelectMachineDialog::on_send_print()
{
    BOOST_LOG_TRIVIAL(info) << "print_job: on_ok to send";
    m_is_canceled = false;
    Enable_Send_Button(false);

    if (m_mapping_popup.IsShown())
        m_mapping_popup.Dismiss();

    if (m_print_type == PrintFromType::FROM_NORMAL && m_is_in_sending_mode)
        return;

    int result = 0;
    if (m_printer_last_select.empty()) {
        return;
    }

    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;

    MachineObject* obj_ = dev->get_selected_machine();
    assert(obj_->dev_id == m_printer_last_select);
    if (obj_ == nullptr) {
        return;
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", print_job: for send task, current printer id =  " << m_printer_last_select << std::endl;
    show_status(PrintDialogStatus::PrintStatusSending);

    m_status_bar->reset();
    m_status_bar->set_prog_block();
    m_status_bar->set_cancel_callback_fina([this]() {
        BOOST_LOG_TRIVIAL(info) << "print_job: enter canceled";
        m_worker->cancel_all();
        m_is_canceled = true;
        wxCommandEvent* event = new wxCommandEvent(EVT_PRINT_JOB_CANCEL);
        wxQueueEvent(this, event);
        });

    if (m_is_canceled) {
        BOOST_LOG_TRIVIAL(info) << "print_job: m_is_canceled";
        m_status_bar->set_status_text(task_canceled_text);
        return;
    }

    // enter sending mode
    sending_mode();
    m_status_bar->enable_cancel_button();

    // get ams_mapping_result
    std::string ams_mapping_array;
    std::string ams_mapping_array2;
    std::string ams_mapping_info;
    if (m_checkbox_list["use_ams"]->GetValue())
        get_ams_mapping_result(ams_mapping_array,ams_mapping_array2, ams_mapping_info);
    else {
        json mapping_info_json = json::array();
        json item;
        if (m_filaments.size() > 0) {
            item["sourceColor"] = m_filaments[0].color.substr(1, 8);
            item["filamentType"] = m_filaments[0].type;
            mapping_info_json.push_back(item);
            ams_mapping_info = mapping_info_json.dump();
        }
    }

    if (m_print_type == PrintFromType::FROM_NORMAL) {
        result = m_plater->send_gcode(m_print_plate_idx, [this](int export_stage, int current, int total, bool& cancel) {
            if (this->m_is_canceled) return;
            bool     cancelled = false;
            wxString msg = _L("Preparing print job");
            m_status_bar->update_status(msg, cancelled, 10, true);
            m_export_3mf_cancel = cancel = cancelled;
            });

        if (m_is_canceled || m_export_3mf_cancel) {
            BOOST_LOG_TRIVIAL(info) << "print_job: m_export_3mf_cancel or m_is_canceled";
            m_status_bar->set_status_text(task_canceled_text);
            return;
        }

        if (result < 0) {
            wxString msg = _L("Abnormal print file data. Please slice again");
            m_status_bar->set_status_text(msg);
            return;
        }

        // export config 3mf if needed
        if (!obj_->is_lan_mode_printer()) {
            result = m_plater->export_config_3mf(m_print_plate_idx);
            if (result < 0) {
                BOOST_LOG_TRIVIAL(trace) << "export_config_3mf failed, result = " << result;
                return;
            }
        }
        if (m_is_canceled || m_export_3mf_cancel) {
            BOOST_LOG_TRIVIAL(info) << "print_job: m_export_3mf_cancel or m_is_canceled";
            m_status_bar->set_status_text(task_canceled_text);
            return;
        }
    }

    auto m_print_job = std::make_unique<PrintJob>(m_printer_last_select);
    m_print_job->m_dev_ip = obj_->dev_ip;
    m_print_job->m_ftp_folder = obj_->get_ftp_folder();
    m_print_job->m_access_code = obj_->get_access_code();
#if !BBL_RELEASE_TO_PUBLIC
    m_print_job->m_local_use_ssl_for_ftp = wxGetApp().app_config->get("enable_ssl_for_ftp") == "true" ? true : false;
    m_print_job->m_local_use_ssl_for_mqtt = wxGetApp().app_config->get("enable_ssl_for_mqtt") == "true" ? true : false;
#else
    m_print_job->m_local_use_ssl_for_ftp = obj_->local_use_ssl_for_ftp;
    m_print_job->m_local_use_ssl_for_mqtt = obj_->local_use_ssl_for_mqtt;
#endif
    m_print_job->connection_type = obj_->connection_type();
    m_print_job->cloud_print_only = obj_->is_support_cloud_print_only;

    if (m_print_type == PrintFromType::FROM_NORMAL) {
        BOOST_LOG_TRIVIAL(info) << "print_job: m_print_type = from_normal";
        m_print_job->m_print_type = "from_normal";
        m_print_job->set_project_name(m_current_project_name.utf8_string());
    }
    else if(m_print_type == PrintFromType::FROM_SDCARD_VIEW){
        BOOST_LOG_TRIVIAL(info) << "print_job: m_print_type = from_sdcard_view";
        m_print_job->m_print_type = "from_sdcard_view";
        //m_print_job->connection_type = "lan";

        try {
            m_print_job->m_print_from_sdc_plate_idx = m_required_data_plate_data_list[m_print_plate_idx]->plate_index + 1;
            m_print_job->set_dst_name(m_required_data_file_path);
        }
        catch (...) {}
        BOOST_LOG_TRIVIAL(info) << "print_job: m_print_plate_idx =" << m_print_job->m_print_from_sdc_plate_idx;

        auto input_str_arr = wxGetApp().split_str(m_required_data_file_name, ".gcode.3mf");
        if (input_str_arr.size() <= 1) {
            input_str_arr = wxGetApp().split_str(m_required_data_file_name, ".3mf");
            if (input_str_arr.size() > 1) {
                m_print_job->set_project_name(input_str_arr[0]);
            }
        }
        else {
            m_print_job->set_project_name(input_str_arr[0]);
        }
    }

    if (obj_->is_support_ams_mapping()) {
        m_print_job->task_ams_mapping = ams_mapping_array;
        m_print_job->task_ams_mapping2= ams_mapping_array2;
        m_print_job->task_ams_mapping_info = ams_mapping_info;
    } else {
        m_print_job->task_ams_mapping = "";
        m_print_job->task_ams_mapping2 = "";
        m_print_job->task_ams_mapping_info = "";
    }

    /* build nozzles info for multi extruders printers */
    if (build_nozzles_info(m_print_job->task_nozzles_info)) {
        BOOST_LOG_TRIVIAL(error) << "build_nozzle_info errors";
    }

    m_print_job->has_sdcard = obj_->get_sdcard_state() == MachineObject::SdcardState::HAS_SDCARD_NORMAL;


    bool timelapse_option = select_timelapse->IsShown() ? m_checkbox_list["timelapse"]->GetValue() : true;

    m_print_job->set_print_config(
        MachineBedTypeString[0],
        m_checkbox_list["bed_leveling"]->GetValue(),
        m_checkbox_list["flow_cali"]->GetValue(),
        false,
        timelapse_option,
        true,
        0, // TODO: Orca hack
        0,
        0);

    if (obj_->has_ams()) {
        m_print_job->task_use_ams = m_checkbox_list["use_ams"]->GetValue();
    } else {
        m_print_job->task_use_ams = false;
    }

    BOOST_LOG_TRIVIAL(info) << "print_job: timelapse_option = " << timelapse_option;
    BOOST_LOG_TRIVIAL(info) << "print_job: use_ams = " << m_print_job->task_use_ams;

    m_print_job->on_success([this]() { finish_mode(); });

    m_print_job->on_check_ip_address_fail([this]() {
        wxCommandEvent* evt = new wxCommandEvent(EVT_CLEAR_IPADDRESS);
        wxQueueEvent(this, evt);
        wxGetApp().show_ip_address_enter_dialog();
     });

    // update ota version
    NetworkAgent* agent = wxGetApp().getAgent();
    if (agent) {
        std::string dev_ota_str = "dev_ota_ver:" + obj_->dev_id;
        agent->track_update_property(dev_ota_str, obj_->get_ota_version());
    }

    replace_job(*m_worker, std::move(m_print_job));
    BOOST_LOG_TRIVIAL(info) << "print_job: start print job";
}

void SelectMachineDialog::clear_ip_address_config(wxCommandEvent& e)
{
    prepare_mode();
}

void SelectMachineDialog::update_user_machine_list()
{
    NetworkAgent* m_agent = wxGetApp().getAgent();
    if (m_agent && m_agent->is_user_login()) {
        boost::thread get_print_info_thread = Slic3r::create_thread([this, token = std::weak_ptr<int>(m_token)] {
            NetworkAgent* agent = wxGetApp().getAgent();
            unsigned int http_code;
            std::string body;
            int result = agent->get_user_print_info(&http_code, &body);
            CallAfter([token, this, result, body] {
                if (token.expired()) {return;}
                if (result == 0) {
                    m_print_info = body;
                }
                else {
                    m_print_info = "";
                }
                wxCommandEvent event(EVT_UPDATE_USER_MACHINE_LIST);
                event.SetEventObject(this);
                wxPostEvent(this, event);
            });
        });
    } else {
        wxCommandEvent event(EVT_UPDATE_USER_MACHINE_LIST);
        event.SetEventObject(this);
        wxPostEvent(this, event);
    }
}

void SelectMachineDialog::on_refresh(wxCommandEvent &event)
{
    BOOST_LOG_TRIVIAL(info) << "m_printer_last_select: on_refresh";
    show_status(PrintDialogStatus::PrintStatusRefreshingMachineList);

    update_user_machine_list();
}

void SelectMachineDialog::on_set_finish_mapping(wxCommandEvent &evt)
{
    auto selection_data = evt.GetString();
    auto selection_data_arr = wxSplit(selection_data.ToStdString(), '|');

    BOOST_LOG_TRIVIAL(info) << "The ams mapping selection result: data is " << selection_data;

    if (selection_data_arr.size() == 8) {
        auto ams_colour      = wxColour(wxAtoi(selection_data_arr[0]), wxAtoi(selection_data_arr[1]), wxAtoi(selection_data_arr[2]), wxAtoi(selection_data_arr[3]));
        int  old_filament_id = (int) wxAtoi(selection_data_arr[5]);
        if (m_print_type == PrintFromType::FROM_NORMAL) {//todo:support sd card
            change_default_normal(old_filament_id, ams_colour);
            final_deal_edge_pixels_data(m_preview_thumbnail_data);
            set_default_normal(m_preview_thumbnail_data); // do't reset ams
        }

        int ctype = 0;
        std::vector<wxColour> material_cols;
        std::vector<std::string> tray_cols;
        for (auto mapping_item : m_mapping_popup.m_mapping_item_list) {
            if (mapping_item->m_tray_data.id == evt.GetInt()) {
                ctype = mapping_item->m_tray_data.ctype;
                material_cols = mapping_item->m_tray_data.material_cols;
                for (auto col : mapping_item->m_tray_data.material_cols) {
                    wxString color = wxString::Format("#%02X%02X%02X%02X", col.Red(), col.Green(), col.Blue(), col.Alpha());
                    tray_cols.push_back(color.ToStdString());
                }
                break;
            }
        }

        for (auto i = 0; i < m_ams_mapping_result.size(); i++) {
            if (m_ams_mapping_result[i].id == wxAtoi(selection_data_arr[5])) {
                m_ams_mapping_result[i].tray_id = evt.GetInt();
                auto ams_colour = wxColour(wxAtoi(selection_data_arr[0]), wxAtoi(selection_data_arr[1]), wxAtoi(selection_data_arr[2]), wxAtoi(selection_data_arr[3]));
                wxString color = wxString::Format("#%02X%02X%02X%02X", ams_colour.Red(), ams_colour.Green(), ams_colour.Blue(), ams_colour.Alpha());
                m_ams_mapping_result[i].color = color.ToStdString();
                m_ams_mapping_result[i].ctype = ctype;
                m_ams_mapping_result[i].colors = tray_cols;

                m_ams_mapping_result[i].ams_id = selection_data_arr[6].ToStdString();
                m_ams_mapping_result[i].slot_id = selection_data_arr[7].ToStdString();
            }
            BOOST_LOG_TRIVIAL(trace) << "The ams mapping result: id is " << m_ams_mapping_result[i].id << "tray_id is " << m_ams_mapping_result[i].tray_id;
        }

        MaterialHash::iterator iter = m_materialList.begin();
        while (iter != m_materialList.end()) {
            Material*        item = iter->second;
            MaterialItem *m = item->item;
            if (item->id == m_current_filament_id) {
                auto ams_colour = wxColour(wxAtoi(selection_data_arr[0]), wxAtoi(selection_data_arr[1]), wxAtoi(selection_data_arr[2]), wxAtoi(selection_data_arr[3]));
                m->set_ams_info(ams_colour, selection_data_arr[4], ctype, material_cols);
            }
            iter++;
        }
    }
}

void SelectMachineDialog::on_print_job_cancel(wxCommandEvent &evt)
{
    BOOST_LOG_TRIVIAL(info) << "print_job: canceled";
    show_status(PrintDialogStatus::PrintStatusInit);
    // enter prepare mode
    prepare_mode();
}

std::vector<std::string> SelectMachineDialog::sort_string(std::vector<std::string> strArray)
{
    std::vector<std::string> outputArray;
    std::sort(strArray.begin(), strArray.end());
    std::vector<std::string>::iterator st;
    for (st = strArray.begin(); st != strArray.end(); st++) { outputArray.push_back(*st); }

    return outputArray;
}

bool  SelectMachineDialog::is_timeout()
{
    if (m_timeout_count > 15 * 1000 / LIST_REFRESH_INTERVAL) {
        return true;
    }
    return false;
}

int SelectMachineDialog::update_print_required_data(Slic3r::DynamicPrintConfig config, Slic3r::Model model, Slic3r::PlateDataPtrs plate_data_list, std::string file_name, std::string file_path)
{
    m_required_data_plate_data_list.clear();
    m_required_data_config = config;
    m_required_data_model = model;
    //m_required_data_plate_data_list = plate_data_list;
    for (auto i = 0; i < plate_data_list.size(); i++) {
        if (!plate_data_list[i]->gcode_file.empty()) {
            m_required_data_plate_data_list.push_back(plate_data_list[i]);
        }
    }

    m_required_data_file_name = file_name;
    m_required_data_file_path = file_path;
    return m_required_data_plate_data_list.size();
}

void  SelectMachineDialog::reset_timeout()
{
    m_timeout_count = 0;
}

void SelectMachineDialog::update_user_printer()
{
    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;

    // update user print info
    if (!m_print_info.empty()) {
        dev->parse_user_print_info(m_print_info);
        m_print_info = "";
    }

    // clear machine list
    m_list.clear();
    m_comboBox_printer->Clear();
    std::vector<std::string>              machine_list;
    wxArrayString                         machine_list_name;
    std::map<std::string, MachineObject*> option_list;

    //user machine list
    option_list = dev->get_my_machine_list();

    // same machine only appear once
    for (auto it = option_list.begin(); it != option_list.end(); it++) {
        if (it->second && (it->second->is_online() || it->second->is_connected())) {
            machine_list.push_back(it->second->dev_name);
        }
    }



    //lan machine list
    auto lan_option_list = dev->get_local_machine_list();

    for (auto elem : lan_option_list) {
        MachineObject* mobj = elem.second;

        /* do not show printer bind state is empty */
        if (!mobj->is_avaliable()) continue;
        if (!mobj->is_online()) continue;
        if (!mobj->is_lan_mode_printer()) continue;
        /*if (mobj->is_in_printing()) {op->set_printer_state(PrinterState::BUSY);}*/

        if (!mobj->has_access_right()) {
            option_list[mobj->dev_name] = mobj;
            machine_list.push_back(mobj->dev_name);
        }
    }

    machine_list = sort_string(machine_list);
    for (auto tt = machine_list.begin(); tt != machine_list.end(); tt++) {
        for (auto it = option_list.begin(); it != option_list.end(); it++) {
            if (it->second->dev_name == *tt) {
                m_list.push_back(it->second);
                wxString dev_name_text = from_u8(it->second->dev_name);
                if (it->second->is_lan_mode_printer()) {
                    dev_name_text += "(LAN)";
                }
                machine_list_name.Add(dev_name_text);
                break;
            }
        }
    }

    m_comboBox_printer->Set(machine_list_name);

    MachineObject* obj = dev->get_selected_machine();
    if (!obj) {
        dev->load_last_machine();
        obj = dev->get_selected_machine();
    }

    if (obj) {
        if (obj->is_lan_mode_printer() && !obj->has_access_right()) {
            m_printer_last_select = "";
        }
        else {
           m_printer_last_select = obj->dev_id;
        }

    } else {
        m_printer_last_select = "";
    }

    if (m_list.size() > 0) {
        // select a default machine
        if (m_printer_last_select.empty()) {
            int def_selection = -1;
            for (int i = 0; i < m_list.size(); i++) {
                if (m_list[i]->is_lan_mode_printer() && !m_list[i]->has_access_right()) {
                    continue;
                }
                else {
                    def_selection = i;
                }
            }

            if (def_selection >= 0) {
                m_printer_last_select = m_list[def_selection]->dev_id;
                m_comboBox_printer->SetSelection(def_selection);
                wxCommandEvent event(wxEVT_COMBOBOX);
                event.SetEventObject(m_comboBox_printer);
                wxPostEvent(m_comboBox_printer, event);
            }
        }

        for (auto i = 0; i < m_list.size(); i++) {
            if (m_list[i]->dev_id == m_printer_last_select) {
                m_comboBox_printer->SetSelection(i);
                wxCommandEvent event(wxEVT_COMBOBOX);
                event.SetEventObject(m_comboBox_printer);
                wxPostEvent(m_comboBox_printer, event);
            }
        }
    }
    else {
        m_printer_last_select = "";
        update_select_layout(nullptr);
        m_comboBox_printer->SetTextLabel("");
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "for send task, current printer id =  " << m_printer_last_select << std::endl;
}

void SelectMachineDialog::on_rename_click(wxMouseEvent& event)
{
    m_is_rename_mode = true;
    m_rename_input->GetTextCtrl()->SetValue(m_current_project_name);
    m_rename_switch_panel->SetSelection(1);
    m_rename_input->GetTextCtrl()->SetFocus();
    m_rename_input->GetTextCtrl()->SetInsertionPointEnd();
}

void SelectMachineDialog::on_rename_enter()
{
    if (m_is_rename_mode == false){
        return;
    }
    else {
        m_is_rename_mode = false;
    }

    auto     new_file_name = m_rename_input->GetTextCtrl()->GetValue();
    wxString temp;
    int      num = 0;
    for (auto t : new_file_name) {
        if (t == wxString::FromUTF8("\x20")) {
            num++;
            if (num == 1) temp += t;
        } else {
            num = 0;
            temp += t;
        }
    }
    new_file_name         = temp;
    auto     m_valid_type = Valid;
    wxString info_line;

    const char* unusable_symbols = "<>[]:/\\|?*\"";

    const std::string unusable_suffix = PresetCollection::get_suffix_modified(); //"(modified)";
    for (size_t i = 0; i < std::strlen(unusable_symbols); i++) {
        if (new_file_name.find_first_of(unusable_symbols[i]) != std::string::npos) {
            info_line = _L("Name is invalid;") + "\n" + _L("illegal characters:") + " " + unusable_symbols;
            m_valid_type = NoValid;
            break;
        }
    }

    if (m_valid_type == Valid && new_file_name.find(unusable_suffix) != std::string::npos) {
        info_line = _L("Name is invalid;") + "\n" + _L("illegal suffix:") + "\n\t" + from_u8(PresetCollection::get_suffix_modified());
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && new_file_name.empty()) {
        info_line = _L("The name is not allowed to be empty.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && new_file_name.find_first_of(' ') == 0) {
        info_line = _L("The name is not allowed to start with space character.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && new_file_name.find_last_of(' ') == new_file_name.length() - 1) {
        info_line = _L("The name is not allowed to end with space character.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && new_file_name.size()  >= 100) {
        info_line = _L("The name length exceeds the limit.");
        m_valid_type = NoValid;
    }

    if (m_valid_type != Valid) {
        MessageDialog msg_wingow(nullptr, info_line, "", wxICON_WARNING | wxOK);
        if (msg_wingow.ShowModal() == wxID_OK) {
             m_rename_switch_panel->SetSelection(0);
             m_rename_text->SetLabel(m_current_project_name);
             m_rename_normal_panel->Layout();
             return;
        }
    }

    m_current_project_name = new_file_name;
    m_rename_switch_panel->SetSelection(0);
    m_rename_text->SetLabelText(m_current_project_name);
    m_rename_normal_panel->Layout();
}

void SelectMachineDialog::update_printer_combobox(wxCommandEvent &event)
{
    show_status(PrintDialogStatus::PrintStatusInit);
    update_user_printer();
}

void SelectMachineDialog::on_timer(wxTimerEvent &event)
{
    wxGetApp().reset_to_active();
    update_show_status();

    ///show auto refill
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if(!dev) return;
    MachineObject* obj_ = dev->get_selected_machine();
    if(!obj_) return;

    update_ams_check(obj_);
    update_select_layout(obj_);
    if (!obj_
        || obj_->amsList.empty()
        || obj_->ams_exist_bits == 0
        || !obj_->is_support_filament_backup
        || !obj_->is_support_show_filament_backup
        || !obj_->ams_auto_switch_filament_flag
        || !m_checkbox_list["use_ams"]->GetValue() ) {
        if (m_ams_backup_tip->IsShown()) {
            m_ams_backup_tip->Hide();
            img_ams_backup->Hide();
            Layout();
            Fit();
        }
    }
    else {
        if (!m_ams_backup_tip->IsShown()) {
            m_ams_backup_tip->Show();
            img_ams_backup->Show();
            Layout();
            Fit();
        }
    }
}

void SelectMachineDialog::on_selection_changed(wxCommandEvent &event)
{
    /* reset timeout and reading printer info */
    m_status_bar->reset();
    m_timeout_count      = 0;
    m_ams_mapping_res  = false;
    m_ams_mapping_valid  = false;
    m_ams_mapping_result.clear();

    auto selection = m_comboBox_printer->GetSelection();
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;

    MachineObject* obj = nullptr;
    for (int i = 0; i < m_list.size(); i++) {
        if (i == selection) {

            //check lan mode machine
            if (m_list[i]->is_lan_mode_printer() && !m_list[i]->has_access_right()) {
                ConnectPrinterDialog dlg(wxGetApp().mainframe, wxID_ANY, _L("Input access code"));
                dlg.set_machine_object(m_list[i]);
                auto res = dlg.ShowModal();
                m_printer_last_select = "";
                m_comboBox_printer->SetSelection(-1);
                m_comboBox_printer->Refresh();
                m_comboBox_printer->Update();
            }

            m_printer_last_select = m_list[i]->dev_id;
            obj = m_list[i];

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "for send task, current printer id =  " << m_printer_last_select << std::endl;
            break;
        }
    }

    if (obj && !obj->get_lan_mode_connection_state()) {
        obj->command_get_version();
        obj->command_request_push_all();
        if (!dev->get_selected_machine()) {
            dev->set_selected_machine(m_printer_last_select, true);
        }else if (dev->get_selected_machine()->dev_id != m_printer_last_select) {
            dev->set_selected_machine(m_printer_last_select, true);
        }

        // reset the timelapse check status for I3 structure
        if (obj->get_printer_arch() == PrinterArch::ARCH_I3) {
            m_checkbox_list["timelapse"]->SetValue(false);
            AppConfig *config = wxGetApp().app_config;
            if (config) config->set_str("print", "timelapse", "0");
        }

        // Has changed machine unrecoverably
        GUI::wxGetApp().sidebar().load_ams_list(obj->dev_id, obj);
        update_select_layout(obj);
    } else {
        BOOST_LOG_TRIVIAL(error) << "on_selection_changed dev_id not found";
        return;
    }


    //reset print status
    update_flow_cali_check(obj);

    show_status(PrintDialogStatus::PrintStatusInit);

    update_show_status();
}

void SelectMachineDialog::update_flow_cali_check(MachineObject* obj)
{
    auto bed_type = m_plater->get_partplate_list().get_curr_plate()->get_bed_type(true);
    auto show_cali_tips = true;

    if (obj && obj->get_printer_arch() == PrinterArch::ARCH_I3) { show_cali_tips = false; }

    set_flow_calibration_state(true, show_cali_tips);
}

void SelectMachineDialog::update_ams_check(MachineObject* obj)
{
    if (obj && obj->has_ams()) {
        select_use_ams->Show();
        if (obj->get_printer_ams_type() == "generic") {
            img_use_ams_tip->Show();
        }
        else {
            img_use_ams_tip->Hide();
        }
    } else {
        select_use_ams->Hide();
    }
}

void SelectMachineDialog::update_show_status()
{
    // refreshing return
    if (get_status() == PrintDialogStatus::PrintStatusRefreshingMachineList)
        return;

    if (get_status() == PrintDialogStatus::PrintStatusSending)
        return;

    if (get_status() == PrintDialogStatus::PrintStatusSendingCanceled)
        return;

    NetworkAgent* agent = Slic3r::GUI::wxGetApp().getAgent();
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!agent) {
        update_ams_check(nullptr);
        return;
    }
    if (!dev) return;
    dev->check_pushing();
    PartPlate* plate = m_plater->get_partplate_list().get_curr_plate();

    // blank plate has no valid gcode file
    if (m_print_type == PrintFromType::FROM_NORMAL) {
        if (plate && !plate->is_valid_gcode_file()) {
            show_status(PrintDialogStatus::PrintStatusBlankPlate);
            return;
        }
    }
    MachineObject* obj_ = dev->get_my_machine(m_printer_last_select);
    if (!obj_) {
        update_ams_check(nullptr);
        if (agent) {
            if (agent->is_user_login()) {
                show_status(PrintDialogStatus::PrintStatusInvalidPrinter);
            }
            else {
                show_status(PrintDialogStatus::PrintStatusNoUserLogin);
            }
        }
        return;
    }

    /* check cloud machine connections */
    if (!obj_->is_lan_mode_printer()) {
        if (!agent->is_server_connected()) {
            agent->refresh_connection();
            show_status(PrintDialogStatus::PrintStatusConnectingServer);
            reset_timeout();
            return;
        }
    }

    if (!obj_->is_info_ready()) {
        if (is_timeout()) {
            m_ams_mapping_result.clear();
            sync_ams_mapping_result(m_ams_mapping_result);
            show_status(PrintDialogStatus::PrintStatusReadingTimeout);
            return;
        }
        else {
            m_timeout_count++;
            show_status(PrintDialogStatus::PrintStatusReading);
            return;
        }
        return;
    }

    reset_timeout();

    if (!obj_->is_support_print_all && m_print_plate_idx == PLATE_ALL_IDX) {
        show_status(PrintDialogStatus::PrintStatusNotSupportedPrintAll);
        return;
    }


    // do ams mapping if no ams result
    bool clean_ams_mapping = false;
    if (m_ams_mapping_result.empty()) {
        if (m_checkbox_list["use_ams"]->GetValue()) {
            do_ams_mapping(obj_);
        } else {
            clean_ams_mapping = true;
        }
    }

    if (!obj_->has_ams() || !m_checkbox_list["use_ams"]->GetValue()) {
        clean_ams_mapping = true;
    }

    if (clean_ams_mapping) {
        m_ams_mapping_result.clear();
        sync_ams_mapping_result(m_ams_mapping_result);
    }

    // reading done
    if (wxGetApp().app_config && wxGetApp().app_config->get("internal_debug").empty()) {
        if (obj_->upgrade_force_upgrade) {
            show_status(PrintDialogStatus::PrintStatusNeedForceUpgrading);
            return;
        }

        if (obj_->upgrade_consistency_request) {
            show_status(PrintStatusNeedConsistencyUpgrading);
            return;
        }
    }

    if (is_blocking_printing(obj_)) {
        show_status(PrintDialogStatus::PrintStatusUnsupportedPrinter);
        return;
    }
    else if (obj_->is_in_upgrading()) {
        show_status(PrintDialogStatus::PrintStatusInUpgrading);
        return;
    }
    else if (obj_->is_system_printing()) {
        show_status(PrintDialogStatus::PrintStatusInSystemPrinting);
        return;
    }
    else if (obj_->is_in_printing() || obj_->ams_status_main == AMS_STATUS_MAIN_FILAMENT_CHANGE) {
        show_status(PrintDialogStatus::PrintStatusInPrinting);
        return;
    }
    else if (!obj_->is_support_print_without_sd && (obj_->get_sdcard_state() == MachineObject::SdcardState::NO_SDCARD)) {
        show_status(PrintDialogStatus::PrintStatusNoSdcard);
        return;
    }

    // check sdcard when if lan mode printer
    if (obj_->is_lan_mode_printer()) {
        if (obj_->get_sdcard_state() == MachineObject::SdcardState::NO_SDCARD) {
            show_status(PrintDialogStatus::PrintStatusLanModeNoSdcard);
            return;
        }
    }

    // no ams
    if (!obj_->has_ams() || !m_checkbox_list["use_ams"]->GetValue()) {
        if (!has_tips(obj_)) {
            if (has_timelapse_warning()) {
                show_status(PrintDialogStatus::PrintStatusTimelapseWarning);
            }
            else {
                show_status(PrintDialogStatus::PrintStatusReadingFinished);
            }
        }
        return;
    }

    if (!m_checkbox_list["use_ams"]->GetValue()) {
        m_ams_mapping_result.clear();
        sync_ams_mapping_result(m_ams_mapping_result);

        if (has_timelapse_warning()) {
            show_status(PrintDialogStatus::PrintStatusTimelapseWarning);
        } else {
            show_status(PrintDialogStatus::PrintStatusDisableAms);
        }

        return;
    }


    // do ams mapping if no ams result
    if (m_ams_mapping_result.empty()) {
        do_ams_mapping(obj_);
    }

    if (!obj_->is_support_ams_mapping()) {
        int exceed_index = -1;
        if (obj_->is_mapping_exceed_filament(m_ams_mapping_result, exceed_index)) {
            std::vector<wxString> params;
            params.push_back(wxString::Format("%02d", exceed_index+1));
            show_status(PrintDialogStatus::PrintStatusNeedUpgradingAms, params);
        } else {
            if (obj_->is_valid_mapping_result(m_ams_mapping_result)) {

                if (has_timelapse_warning()) {
                    show_status(PrintDialogStatus::PrintStatusTimelapseWarning);
                }
                else {
                    show_status(PrintDialogStatus::PrintStatusAmsMappingByOrder);
                }
                
            } else {
                int mismatch_index = -1;
                for (int i = 0; i < m_ams_mapping_result.size(); i++) {
                    if (m_ams_mapping_result[i].mapping_result == MappingResult::MAPPING_RESULT_TYPE_MISMATCH) {
                        mismatch_index = m_ams_mapping_result[i].id;
                        break;
                    }
                }
                std::vector<wxString> params;
                if (mismatch_index >= 0) {
                    params.push_back(wxString::Format("%02d", mismatch_index+1));
                    params.push_back(wxString::Format("%02d", mismatch_index+1));
                }
                show_status(PrintDialogStatus::PrintStatusAmsMappingU0Invalid, params);
            }
        }
        return;
    }

    if (m_ams_mapping_res) {
        if (has_timelapse_warning()) {
            show_status(PrintDialogStatus::PrintStatusTimelapseWarning);
        }
        else {
            show_status(PrintDialogStatus::PrintStatusAmsMappingSuccess);
        }
        return;
    }
    else {
        if (obj_->is_valid_mapping_result(m_ams_mapping_result)) {
            if (!has_tips(obj_)){
                if (has_timelapse_warning()) {
                    show_status(PrintDialogStatus::PrintStatusTimelapseWarning);
                }
                else {
                    show_status(PrintDialogStatus::PrintStatusAmsMappingValid);
                }
                return;
            }       
        }
        else {
            show_status(PrintDialogStatus::PrintStatusAmsMappingInvalid);
            return;
        }
    } 
}

bool SelectMachineDialog::has_timelapse_warning()
{
    PartPlate *plate = m_plater->get_partplate_list().get_curr_plate();
    for (auto warning : plate->get_slice_result()->warnings) {
        if (warning.msg == NOT_GENERATE_TIMELAPSE) {
            return true;
        }
    }

    return false;
}

void SelectMachineDialog::update_timelapse_enable_status()
{
    AppConfig *config = wxGetApp().app_config;
    if (!has_timelapse_warning()) {
        if (!config || config->get("print", "timelapse") == "0")
            m_checkbox_list["timelapse"]->SetValue(false);
        else
            m_checkbox_list["timelapse"]->SetValue(true);
        select_timelapse->Enable(true);
    } else {
        m_checkbox_list["timelapse"]->SetValue(false);
        select_timelapse->Enable(false);
        if (config) { config->set_str("print", "timelapse", "0"); }
    }
}


bool SelectMachineDialog::is_show_timelapse()
{
    auto compare_version = [](const std::string &version1, const std::string &version2) -> bool {
        int i = 0, j = 0;
        int max_size = std::max(version1.size(), version2.size());
        while (i < max_size || j < max_size) {
            int v1 = 0, v2 = 0;
            while (i < version1.size() && version1[i] != '.') v1 = 10 * v1 + (version1[i++] - '0');
            while (j < version2.size() && version2[j] != '.') v2 = 10 * v2 + (version2[j++] - '0');
            if (v1 > v2) return true;
            if (v1 < v2) return false;
            ++i;
            ++j;
        }
        return false;
    };

    std::string standard_version = "2.2.0";
    PartPlate *plate      = m_plater->get_partplate_list().get_curr_plate();
    fs::path   gcode_path = plate->get_tmp_gcode_path();

    std::string   line;
    std::ifstream gcode_file;
    gcode_file.open(gcode_path.string());
    if (gcode_file.fail()) {
    } else {
        bool is_version = false;
        while (gcode_file >> line) {
            if (is_version) {
                if (compare_version(standard_version, line)) {
                    gcode_file.close();
                    return false;
                }
                break;
            }
            if (line == "OrcaSlicer")
                is_version = true;
        }
    }
    gcode_file.close();
    return true;
}

void SelectMachineDialog::reset_ams_material()
{
    MaterialHash::iterator iter = m_materialList.begin();
    while (iter != m_materialList.end()) {
        int           id = iter->first;
        Material* item = iter->second;
        MaterialItem* m = item->item;
        wxString ams_id = "-";
        wxColour ams_col = wxColour(0xEE, 0xEE, 0xEE);
        m->set_ams_info(ams_col, ams_id);
        iter++;
    }
}

void SelectMachineDialog::Enable_Refresh_Button(bool en)
{
    if (!en) {
        if (m_button_refresh->IsEnabled()) {
            m_button_refresh->Disable();
            m_button_refresh->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
            m_button_refresh->SetBorderColor(wxColour(0x90, 0x90, 0x90));
        }
    } else {
        if (!m_button_refresh->IsEnabled()) {
            m_button_refresh->Enable();
            m_button_refresh->SetBackgroundColor(m_btn_bg_enable);
            m_button_refresh->SetBorderColor(m_btn_bg_enable);
        }
    }
}

void SelectMachineDialog::Enable_Send_Button(bool en)
{
    if (!en) {
        if (m_button_ensure->IsEnabled()) {
            m_button_ensure->Disable();
            m_button_ensure->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
            m_button_ensure->SetBorderColor(wxColour(0x90, 0x90, 0x90));
        }
    } else {
        if (!m_button_ensure->IsEnabled()) {
            m_button_ensure->Enable();
            m_button_ensure->SetBackgroundColor(m_btn_bg_enable);
            m_button_ensure->SetBorderColor(m_btn_bg_enable);
        }
    }
}

void SelectMachineDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    print_time->msw_rescale();
    timeimg->SetBitmap(print_time->bmp());
    print_weight->msw_rescale();
    weightimg->SetBitmap(print_weight->bmp());
    rename_editable->msw_rescale();
    rename_editable_light->msw_rescale();
    ams_mapping_help_icon->msw_rescale();
    img_amsmapping_tip->SetBitmap(ams_mapping_help_icon->bmp());
    enable_ams->msw_rescale();
    img_use_ams_tip->SetBitmap(enable_ams->bmp());

    m_button_refresh->SetMinSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_refresh->SetCornerRadius(FromDIP(12));
    m_button_ensure->SetMinSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_ensure->SetCornerRadius(FromDIP(12));
    m_status_bar->msw_rescale();

    for (auto checkpire : m_checkbox_list) {
        checkpire.second->Rescale();
    }

    for (auto material1 : m_materialList) {
        material1.second->item->msw_rescale();
    }

    Fit();
    Refresh();
}

wxImage *SelectMachineDialog::LoadImageFromBlob(const unsigned char *data, int size)
{
    if (data != NULL) {
        wxMemoryInputStream mi(data, size);
        wxImage *           img = new wxImage(mi, wxBITMAP_TYPE_ANY);
        if (img != NULL && img->IsOk()) return img;
        // wxLogDebug( wxT("DB::LoadImageFromBlob error: data=%p size=%d"), data, size);
        // caller is responsible for deleting the pointer
        delete img;
    }
    return NULL;
}

void SelectMachineDialog::set_flow_calibration_state(bool state, bool show_tips)
{
    if (!state) {
        m_checkbox_list["flow_cali"]->SetValue(state);
        auto tool_tip = _L("Caution to use! Flow calibration on Textured PEI Plate may fail due to the scattered surface.");
        m_checkbox_list["flow_cali"]->SetToolTip(tool_tip);
        m_checkbox_list["flow_cali"]->Enable();
        for (auto win : select_flow->GetWindowChildren()) {
            win->SetToolTip(tool_tip);
        }
        //select_flow->SetToolTip(tool_tip);
    }
    else {

        AppConfig* config = wxGetApp().app_config;
        if (config && config->get("print", "flow_cali") == "0") {
            m_checkbox_list["flow_cali"]->SetValue(false);
        }
        else {
            m_checkbox_list["flow_cali"]->SetValue(true);
        }

        m_checkbox_list["flow_cali"]->Enable();
        for (auto win : select_flow->GetWindowChildren()) {
            win->SetToolTip( _L("Automatic flow calibration using Micro Lidar"));
        }
    }

    if (!show_tips) {
        for (auto win : select_flow->GetWindowChildren()) {
            win->SetToolTip(wxEmptyString);
        }
    }
}

void SelectMachineDialog::set_default()
{
    if (m_print_type == PrintFromType::FROM_NORMAL) {
        m_stext_printer_title->Show(true);
        m_comboBox_printer->Show(true);
        m_button_refresh->Show(true);
        m_rename_normal_panel->Show(true);
        m_hyperlink->Show(true);
    }
    else if (m_print_type == PrintFromType::FROM_SDCARD_VIEW) {
        m_stext_printer_title->Show(false);
        m_comboBox_printer->Show(false);
        m_button_refresh->Show(false);
        m_rename_normal_panel->Show(false);
        m_hyperlink->Show(false);
    }

    //project name
    m_rename_switch_panel->SetSelection(0);

    wxString filename = m_plater->get_export_gcode_filename("", true, m_print_plate_idx == PLATE_ALL_IDX ? true : false);
    if (m_print_plate_idx == PLATE_ALL_IDX && filename.empty()) {
        filename = _L("Untitled");
    }

    if (filename.empty()) {
        filename = m_plater->get_export_gcode_filename("", true);
        if (filename.empty()) filename = _L("Untitled");
    }

    fs::path filename_path(filename.c_str());
    std::string file_name  = filename_path.filename().string();
    if (from_u8(file_name).find(_L("Untitled")) != wxString::npos) {
        PartPlate *part_plate = m_plater->get_partplate_list().get_plate(m_print_plate_idx);
        if (part_plate) {
            if (std::vector<ModelObject *> objects = part_plate->get_objects_on_this_plate(); objects.size() > 0) {
                file_name = objects[0]->name;
                for (int i = 1; i < objects.size(); i++) {
                    file_name += (" + " + objects[i]->name);
                }
            }
            if (file_name.size() > 100) {
                file_name = file_name.substr(0, 97) + "...";
            }
        }
    }
    m_current_project_name = wxString::FromUTF8(file_name);


    //unsupported character filter
    m_current_project_name = from_u8(filter_characters(m_current_project_name.ToUTF8().data(), "<>[]:/\\|?*\""));

    m_rename_text->SetLabelText(m_current_project_name);
    m_rename_normal_panel->Layout();

    //clear combobox
    m_list.clear();
    m_comboBox_printer->Clear();
    m_printer_last_select = "";
    m_print_info = "";
    m_comboBox_printer->SetValue(wxEmptyString);
    m_comboBox_printer->Enable();

    // rset status bar
    m_status_bar->reset();

    NetworkAgent* agent = wxGetApp().getAgent();
    if (agent) {
        if (agent->is_user_login()) {
            show_status(PrintDialogStatus::PrintStatusInit);
        }
        else {
            show_status(PrintDialogStatus::PrintStatusNoUserLogin);
        }
    }
    select_bed->Show();
    select_flow->Show();

    //reset checkbox
    select_bed->Show(false);
    select_flow->Show(false);
    select_timelapse->Show(false);
    select_use_ams->Show(false);

    // load checkbox values from app config
    AppConfig* config = wxGetApp().app_config;
    if (config && config->get("print", "bed_leveling") == "0") {
        m_checkbox_list["bed_leveling"]->SetValue(false);
    }
    else {
        m_checkbox_list["bed_leveling"]->SetValue(true);
    }
    if (config && config->get("print", "flow_cali") == "0") {
        m_checkbox_list["flow_cali"]->SetValue(false);
    }
    else {
        m_checkbox_list["flow_cali"]->SetValue(true);
    }
    if (config && config->get("print", "timelapse") == "0") {
        m_checkbox_list["timelapse"]->SetValue(false);
    }
    else {
        m_checkbox_list["timelapse"]->SetValue(true);
    }

    m_checkbox_list["use_ams"]->SetValue(true);

    if (m_print_type == PrintFromType::FROM_NORMAL) {
        reset_and_sync_ams_list();
        set_default_normal(m_plater->get_partplate_list().get_curr_plate()->thumbnail_data);
    }
    else if (m_print_type == PrintFromType::FROM_SDCARD_VIEW) {
        update_page_turn_state(true);
        set_default_from_sdcard();
    }

    Layout();
    Fit();
}

void SelectMachineDialog::reset_and_sync_ams_list()
{
    // for black list
    std::vector<std::string> materials;
    std::vector<std::string> brands;
    std::vector<std::string> display_materials;
    std::vector<std::string> m_filaments_id;
    auto                     preset_bundle = wxGetApp().preset_bundle;

    for (auto filament_name : preset_bundle->filament_presets) {
        for (int f_index = 0; f_index < preset_bundle->filaments.size(); f_index++) {
            PresetCollection *filament_presets = &wxGetApp().preset_bundle->filaments;
            Preset *          preset           = &filament_presets->preset(f_index);

            if (preset && filament_name.compare(preset->name) == 0) {
                std::string display_filament_type;
                std::string filament_type = preset->config.get_filament_type(display_filament_type);
                std::string m_filament_id = preset->filament_id;
                display_materials.push_back(display_filament_type);
                materials.push_back(filament_type);
                m_filaments_id.push_back(m_filament_id);

                std::string m_vendor_name = "";
                auto        vendor        = dynamic_cast<ConfigOptionStrings *>(preset->config.option("filament_vendor"));
                if (vendor && (vendor->values.size() > 0)) {
                    std::string vendor_name = vendor->values[0];
                    m_vendor_name           = vendor_name;
                }
                brands.push_back(m_vendor_name);
            }
        }
    }

    auto           extruders = wxGetApp().plater()->get_partplate_list().get_curr_plate()->get_used_extruders();
    BitmapCache    bmcache;
    MaterialHash::iterator iter = m_materialList.begin();
    while (iter != m_materialList.end()) {
        int       id   = iter->first;
        Material *item = iter->second;
        item->item->Destroy();
        delete item;
        iter++;
    }

    m_sizer_ams_mapping->Clear();
    m_materialList.clear();
    m_filaments.clear();

    for (auto i = 0; i < extruders.size(); i++) {
        auto          extruder = extruders[i] - 1;
        auto          colour   = wxGetApp().preset_bundle->project_config.opt_string("filament_colour", (unsigned int) extruder);
        unsigned char rgb[4];
        bmcache.parse_color4(colour, rgb);

        auto colour_rgb = wxColour((int) rgb[0], (int) rgb[1], (int) rgb[2], (int) rgb[3]);
        if (extruder >= materials.size() || extruder < 0 || extruder >= display_materials.size()) continue;

        MaterialItem *item = new MaterialItem(m_filament_panel, colour_rgb, _L(display_materials[extruder]));
        m_sizer_ams_mapping->Add(item, 0, wxALL, FromDIP(5));

        item->Bind(wxEVT_LEFT_UP, [this, item, materials, extruder](wxMouseEvent &e) {});
        item->Bind(wxEVT_LEFT_DOWN, [this, item, materials, extruder](wxMouseEvent &e) {
            MaterialHash::iterator iter = m_materialList.begin();
            while (iter != m_materialList.end()) {
                int           id   = iter->first;
                Material *    item = iter->second;
                MaterialItem *m    = item->item;
                m->on_normal();
                iter++;
            }

            m_current_filament_id = extruder;
            item->on_selected();

            auto    mouse_pos = ClientToScreen(e.GetPosition());
            wxPoint rect      = item->ClientToScreen(wxPoint(0, 0));

            // update ams data
            DeviceManager *dev_manager = Slic3r::GUI::wxGetApp().getDeviceManager();
            if (!dev_manager) return;
            MachineObject *obj_ = dev_manager->get_selected_machine();

            if (obj_ && obj_->is_support_ams_mapping()) {
                if (m_mapping_popup.IsShown()) return;
                wxPoint pos = item->ClientToScreen(wxPoint(0, 0));
                pos.y += item->GetRect().height;
                m_mapping_popup.Move(pos);

                if (obj_ && obj_->has_ams() && m_checkbox_list["use_ams"]->GetValue() && obj_->dev_id == m_printer_last_select) {
                    m_mapping_popup.set_parent_item(item);
                    m_mapping_popup.set_current_filament_id(extruder);
                    m_mapping_popup.set_tag_texture(materials[extruder]);
                    m_mapping_popup.update_ams_data(obj_->amsList);
                    m_mapping_popup.Popup();
                }
            }
        });

        Material *material_item = new Material();
        material_item->id       = extruder;
        material_item->item     = item;
        m_materialList[i]       = material_item;

        // build for ams mapping
        if (extruder < materials.size() && extruder >= 0) {
            FilamentInfo info;
            info.id          = extruder;
            info.type        = materials[extruder];
            info.brand       = brands[extruder];
            info.filament_id = m_filaments_id[extruder];
            info.color       = wxString::Format("#%02X%02X%02X%02X", colour_rgb.Red(), colour_rgb.Green(), colour_rgb.Blue(), colour_rgb.Alpha()).ToStdString();
            m_filaments.push_back(info);
        }
    }

    /*if (extruders.size() <= 10) {
        m_sizer_ams_mapping->SetCols(extruders.size());
    }
    else {
        m_sizer_ams_mapping->SetCols(10);
    }*/

    m_sizer_ams_mapping->SetCols(8);
    m_sizer_ams_mapping->Layout();
    m_filament_panel_sizer->Layout();
    // reset_ams_material();//show "-"
}

void SelectMachineDialog::clone_thumbnail_data() {
    //record preview_colors
    MaterialHash::iterator iter               = m_materialList.begin();
    if (m_preview_colors_in_thumbnail.size() != m_materialList.size()) {
        m_preview_colors_in_thumbnail.resize(m_materialList.size());
    }
    while (iter != m_materialList.end()) {
        int           id   = iter->first;
        Material *    item = iter->second;
        MaterialItem *m    = item->item;
        m_preview_colors_in_thumbnail[id] = m->m_material_coloul;
        if (item->id < m_cur_colors_in_thumbnail.size()) {
            m_cur_colors_in_thumbnail[item->id] = m->m_ams_coloul;
        }
        else {//exist empty or unrecognized type ams in machine
            m_cur_colors_in_thumbnail.resize(item->id + 1);
            m_cur_colors_in_thumbnail[item->id] = m->m_ams_coloul;
        }
        iter++;
    }
    //copy data
    auto &data   = m_cur_input_thumbnail_data;
    m_preview_thumbnail_data.reset();
    m_preview_thumbnail_data.set(data.width, data.height);
    if (data.width > 0 && data.height > 0) {
        for (unsigned int r = 0; r < data.height; ++r) {
            unsigned int rr = (data.height - 1 - r) * data.width;
            for (unsigned int c = 0; c < data.width; ++c) {
                unsigned char *origin_px   = (unsigned char *) data.pixels.data() + 4 * (rr + c);
                unsigned char *new_px      = (unsigned char *) m_preview_thumbnail_data.pixels.data() + 4 * (rr + c);
                for (size_t i = 0; i < 4; i++) {
                    new_px[i] = origin_px[i];
                }
            }
        }
    }
    //record_edge_pixels_data
    record_edge_pixels_data();
}

void SelectMachineDialog::record_edge_pixels_data()
{
    auto is_not_in_preview_colors = [this](unsigned char r, unsigned char g , unsigned char b , unsigned char a) {
        for (size_t i = 0; i < m_preview_colors_in_thumbnail.size(); i++) {
            wxColour  render_color  = adjust_color_for_render(m_preview_colors_in_thumbnail[i]);
            if (render_color.Red() == r && render_color.Green() == g && render_color.Blue() == b /*&& render_color.Alpha() == a*/) {
                return false;
            }
        }
        return true;
    };
    ThumbnailData &data = m_cur_no_light_thumbnail_data;
    ThumbnailData &origin_data = m_cur_input_thumbnail_data;
    if (data.width > 0 && data.height > 0) {
        m_edge_pixels.resize(data.width * data.height);
        for (unsigned int r = 0; r < data.height; ++r) {
            unsigned int rr        = (data.height - 1 - r) * data.width;
            for (unsigned int c = 0; c < data.width; ++c) {
                unsigned char *no_light_px = (unsigned char *) data.pixels.data() + 4 * (rr + c);
                unsigned char *origin_px          = (unsigned char *) origin_data.pixels.data() + 4 * (rr + c);
                m_edge_pixels[r * data.width + c] = false;
                if (origin_px[3] > 0) {
                    if (is_not_in_preview_colors(no_light_px[0], no_light_px[1], no_light_px[2], origin_px[3])) {
                        m_edge_pixels[r * data.width + c] = true;
                    }
                }
            }
        }
    }
}

wxColour SelectMachineDialog::adjust_color_for_render(const wxColour &color)
{
    ColorRGBA _temp_color_color(color.Red() / 255.0f, color.Green() / 255.0f, color.Blue() / 255.0f, color.Alpha() / 255.0f);
    auto                 _temp_color_color_ = adjust_color_for_rendering(_temp_color_color);
    wxColour             render_color((int) (_temp_color_color_[0] * 255.0f), (int) (_temp_color_color_[1] * 255.0f), (int) (_temp_color_color_[2] * 255.0f),
                          (int) (_temp_color_color_[3] * 255.0f));
    return render_color;
}

void SelectMachineDialog::final_deal_edge_pixels_data(ThumbnailData &data)
{
    if (data.width > 0 && data.height > 0 && m_edge_pixels.size() >0 ) {
        for (unsigned int r = 0; r < data.height; ++r) {
             unsigned int rr            = (data.height - 1 - r) * data.width;
             bool         exist_rr_up   = r >= 1 ? true : false;
             bool         exist_rr_down = r <= data.height - 2 ? true : false;
             unsigned int rr_up         = exist_rr_up ? (data.height - 1 - (r - 1)) * data.width : 0;
             unsigned int rr_down       = exist_rr_down ? (data.height - 1 - (r + 1)) * data.width : 0;
             for (unsigned int c = 0; c < data.width; ++c) {
                  bool         exist_c_left  = c >= 1 ? true : false;
                  bool         exist_c_right = c <= data.width - 2 ? true : false;
                  unsigned int c_left        = exist_c_left ? c - 1 : 0;
                  unsigned int c_right       = exist_c_right ? c + 1 : 0;
                  unsigned char *cur_px   = (unsigned char *) data.pixels.data() + 4 * (rr + c);
                  unsigned char *relational_pxs[8] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
                  if (exist_rr_up && exist_c_left) { relational_pxs[0] = (unsigned char *) data.pixels.data() + 4 * (rr_up + c_left); }
                  if (exist_rr_up) { relational_pxs[1] = (unsigned char *) data.pixels.data() + 4 * (rr_up + c); }
                  if (exist_rr_up && exist_c_right) { relational_pxs[2] = (unsigned char *) data.pixels.data() + 4 * (rr_up + c_right); }
                  if (exist_c_left) { relational_pxs[3] = (unsigned char *) data.pixels.data() + 4 * (rr + c_left); }
                  if (exist_c_right) { relational_pxs[4] = (unsigned char *) data.pixels.data() + 4 * (rr + c_right); }
                  if (exist_rr_down && exist_c_left) { relational_pxs[5] = (unsigned char *) data.pixels.data() + 4 * (rr_down + c_left); }
                  if (exist_rr_down) { relational_pxs[6] = (unsigned char *) data.pixels.data() + 4 * (rr_down + c); }
                  if (exist_rr_down && exist_c_right) { relational_pxs[7] = (unsigned char *) data.pixels.data() + 4 * (rr_down + c_right); }
                  if (cur_px[3] > 0 && m_edge_pixels[r * data.width + c]) {
                       int rgba_sum[4] = {0, 0, 0, 0};
                       int valid_count = 0;
                       for (size_t k = 0; k < 8; k++) {
                           if (relational_pxs[k]) {
                               if (k == 0 && m_edge_pixels[(r - 1) * data.width + c_left]) {
                                    continue;
                                }
                               if (k == 1 && m_edge_pixels[(r - 1) * data.width + c]) {
                                    continue;
                                }
                                if (k == 2 && m_edge_pixels[(r - 1) * data.width + c_right]) {
                                    continue;
                                }
                                if (k == 3 && m_edge_pixels[r * data.width + c_left]) {
                                    continue;
                                }
                                if (k == 4 && m_edge_pixels[r * data.width + c_right]) {
                                    continue;
                                }
                                if (k == 5 && m_edge_pixels[(r + 1) * data.width + c_left]) {
                                    continue;
                                }
                                if (k == 6 && m_edge_pixels[(r + 1) * data.width + c]) {
                                    continue;
                                }
                                if (k == 7 && m_edge_pixels[(r + 1) * data.width + c_right]) {
                                    continue;
                                }
                                for (size_t m = 0; m < 4; m++) {
                                    rgba_sum[m] += relational_pxs[k][m];
                                }
                                valid_count++;
                           }
                       }
                       if (valid_count > 0) {
                            for (size_t m = 0; m < 4; m++) {
                                cur_px[m] = std::clamp(int(rgba_sum[m] / (float)valid_count), 0, 255);
                            }
                       }
                  }
             }
        }
    }
}

void SelectMachineDialog::updata_thumbnail_data_after_connected_printer()
{
    // change thumbnail_data
    ThumbnailData &input_data          = m_plater->get_partplate_list().get_curr_plate()->thumbnail_data;
    ThumbnailData &no_light_data = m_plater->get_partplate_list().get_curr_plate()->no_light_thumbnail_data;
    if (input_data.width == 0 || input_data.height == 0 || no_light_data.width == 0 || no_light_data.height == 0) {
        wxGetApp().plater()->update_all_plate_thumbnails(false);
    }
    unify_deal_thumbnail_data(input_data, no_light_data);
}

void SelectMachineDialog::unify_deal_thumbnail_data(ThumbnailData &input_data, ThumbnailData &no_light_data) {
    if (input_data.width == 0 || input_data.height == 0 || no_light_data.width == 0 || no_light_data.height == 0) {
        BOOST_LOG_TRIVIAL(error) << "SelectMachineDialog::no_light_data is empty,error";
        return;
    }
    m_cur_input_thumbnail_data    = input_data;
    m_cur_no_light_thumbnail_data = no_light_data;
    clone_thumbnail_data();
    MaterialHash::iterator iter               = m_materialList.begin();
    bool                   is_connect_printer = true;
    while (iter != m_materialList.end()) {
        int           id   = iter->first;
        Material *    item = iter->second;
        MaterialItem *m    = item->item;
        if (m->m_ams_name == "-") {
            is_connect_printer = false;
            break;
        }
        iter++;
    }
    if (is_connect_printer) {
        change_default_normal(-1, wxColour());
        final_deal_edge_pixels_data(m_preview_thumbnail_data);
        set_default_normal(m_preview_thumbnail_data);
    }
}

void SelectMachineDialog::change_default_normal(int old_filament_id, wxColour temp_ams_color)
{
    if (m_cur_colors_in_thumbnail.size() == 0) {
        BOOST_LOG_TRIVIAL(error) << "SelectMachineDialog::change_default_normal:error:m_cur_colors_in_thumbnail.size() == 0";
        return;
    }
    if (old_filament_id >= 0) {
        if (old_filament_id < m_cur_colors_in_thumbnail.size()) {
            m_cur_colors_in_thumbnail[old_filament_id] = temp_ams_color;
        }
        else {
            BOOST_LOG_TRIVIAL(error) << "SelectMachineDialog::change_default_normal:error:old_filament_id > m_cur_colors_in_thumbnail.size()";
            return;
        }
    }
    ThumbnailData& data = m_cur_input_thumbnail_data;
    ThumbnailData& no_light_data = m_cur_no_light_thumbnail_data;
    if (data.width > 0 && data.height > 0 && data.width == no_light_data.width && data.height == no_light_data.height) {
        for (unsigned int r = 0; r < data.height; ++r) {
            unsigned int rr = (data.height - 1 - r) * data.width;
            for (unsigned int c = 0; c < data.width; ++c) {
                unsigned char *no_light_px   = (unsigned char *) no_light_data.pixels.data() + 4 * (rr + c);
                unsigned char *origin_px = (unsigned char *) data.pixels.data() + 4 * (rr + c);
                unsigned char *new_px        = (unsigned char *) m_preview_thumbnail_data.pixels.data() + 4 * (rr + c);
                if (origin_px[3]  > 0 && m_edge_pixels[r * data.width + c] == false) {
                    auto filament_id = 255 - no_light_px[3];
                    if (filament_id >= m_cur_colors_in_thumbnail.size()) {
                        continue;
                    }
                    wxColour temp_ams_color_in_loop = m_cur_colors_in_thumbnail[filament_id];
                    wxColour ams_color              = adjust_color_for_render(temp_ams_color_in_loop);
                    //change color
                    new_px[3] = origin_px[3]; // alpha
                    int origin_rgb = origin_px[0] + origin_px[1] + origin_px[2];
                    int no_light_px_rgb   = no_light_px[0] + no_light_px[1] + no_light_px[2];
                    unsigned char i               = 0;
                    if (origin_rgb >= no_light_px_rgb) {//Brighten up
                        unsigned char cur_single_color = ams_color.Red();
                        new_px[i]                      = std::clamp(cur_single_color + (origin_px[i] - no_light_px[i]), 0, 255);
                        i++;
                        cur_single_color = ams_color.Green();
                        new_px[i]                      = std::clamp(cur_single_color + (origin_px[i] - no_light_px[i]), 0, 255);
                        i++;
                        cur_single_color =  ams_color.Blue();
                        new_px[i]                      = std::clamp(cur_single_color + (origin_px[i] - no_light_px[i]), 0, 255);
                    } else {//Dimming
                        float         ratio            = origin_rgb / (float) no_light_px_rgb;
                        unsigned char cur_single_color = ams_color.Red();
                        new_px[i]                      = std::clamp((int)(cur_single_color * ratio), 0, 255);
                        i++;
                        cur_single_color = ams_color.Green();
                        new_px[i]        = std::clamp((int) (cur_single_color * ratio), 0, 255);
                        i++;
                        cur_single_color = ams_color.Blue();
                        new_px[i]        = std::clamp((int) (cur_single_color * ratio), 0, 255);
                    }
                }
            }
        }
    }
    else {
        BOOST_LOG_TRIVIAL(error) << "SelectMachineDialog::change_defa:no_light_data is empty,error";
    }
}

void SelectMachineDialog::set_default_normal(const ThumbnailData &data)
{
    update_page_turn_state(false);
    if (data.is_valid()) {
        wxImage image(data.width, data.height);
        image.InitAlpha();
        for (unsigned int r = 0; r < data.height; ++r) {
            unsigned int rr = (data.height - 1 - r) * data.width;
            for (unsigned int c = 0; c < data.width; ++c) {
                unsigned char *px = (unsigned char *) data.pixels.data() + 4 * (rr + c);
                image.SetRGB((int) c, (int) r, px[0], px[1], px[2]);
                image.SetAlpha((int) c, (int) r, px[3]);
            }
        }
        image = image.Rescale(FromDIP(198), FromDIP(198));
        m_thumbnailPanel->set_thumbnail(image);
    }

    m_basic_panel->Layout();
    m_basic_panel->Fit();

    // disable pei bed
    DeviceManager *dev_manager = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev_manager) return;
    MachineObject *obj_       = dev_manager->get_selected_machine();
    update_flow_cali_check(obj_);
    wxSize         screenSize = wxGetDisplaySize();
    auto           dialogSize = this->GetSize();

#ifdef __WINDOWS__

#endif // __WXOSX_MAC__
    // basic info
    auto       aprint_stats = m_plater->get_partplate_list().get_current_fff_print().print_statistics();
    wxString   time;
    PartPlate *plate = m_plater->get_partplate_list().get_curr_plate();
    if (plate) {
        if (plate->get_slice_result()) { time = wxString::Format("%s", short_time(get_time_dhms(plate->get_slice_result()->print_statistics.modes[0].time))); }
    }

    char weight[64];
    if (wxGetApp().app_config->get("use_inches") == "1") {
        ::sprintf(weight, "%.2f oz", aprint_stats.total_weight * 0.035274); // ORCA remove spacing before text
    } else {
        ::sprintf(weight, "%.2f g", aprint_stats.total_weight); // ORCA remove spacing before text
    }

    m_stext_time->SetLabel(time);
    m_stext_weight->SetLabel(weight);
}

void SelectMachineDialog::set_default_from_sdcard()
{
    m_print_plate_total = m_required_data_plate_data_list.size();
    update_page_turn_state(true);

    ThumbnailData& data = m_required_data_plate_data_list[m_print_plate_idx]->plate_thumbnail;

    if (data.pixels.size() > 0) {
        wxMemoryInputStream mis((unsigned char*)data.pixels.data(), data.pixels.size());
        wxImage image = wxImage(mis);
        image = image.Rescale(FromDIP(198), FromDIP(198));
        m_thumbnailPanel->set_thumbnail(image);
    }

    //for black list
    std::vector<std::string> materials;
    std::vector<std::string> brands;
    std::vector<std::string> display_materials;

    for (auto i = 0; i < m_required_data_plate_data_list[m_print_plate_idx]->slice_filaments_info.size(); i++) {
        FilamentInfo fo = m_required_data_plate_data_list[m_print_plate_idx]->slice_filaments_info[i];
        display_materials.push_back(fo.type);
        materials.push_back(fo.type);
        brands.push_back(fo.brand);
    }

    //init MaterialItem
    MaterialHash::iterator iter = m_materialList.begin();
    while (iter != m_materialList.end()) {
        int       id = iter->first;
        Material* item = iter->second;
        item->item->Destroy();
        delete item;
        iter++;
    }

    m_ams_mapping_result.clear();
    m_sizer_ams_mapping->Clear();
    m_materialList.clear();
    m_filaments.clear();

    for (auto i = 0; i < m_required_data_plate_data_list[m_print_plate_idx]->slice_filaments_info.size(); i++) {
        FilamentInfo fo = m_required_data_plate_data_list[m_print_plate_idx]->slice_filaments_info[i];

        MaterialItem* item = new MaterialItem(m_filament_panel,  wxColour(fo.color), fo.type);
        m_sizer_ams_mapping->Add(item, 0, wxALL, FromDIP(5));

        item->Bind(wxEVT_LEFT_UP, [this, item, materials](wxMouseEvent& e) {});
        item->Bind(wxEVT_LEFT_DOWN, [this, item, materials, fo](wxMouseEvent& e) {
            MaterialHash::iterator iter = m_materialList.begin();
            while (iter != m_materialList.end()) {
                int           id = iter->first;
                Material* item = iter->second;
                MaterialItem* m = item->item;
                m->on_normal();
                iter++;
            }

            try {
                m_current_filament_id = fo.id;
            }
            catch (...) {}
            item->on_selected();


            auto    mouse_pos = ClientToScreen(e.GetPosition());
            wxPoint rect = item->ClientToScreen(wxPoint(0, 0));
            // update ams data
            DeviceManager* dev_manager = Slic3r::GUI::wxGetApp().getDeviceManager();
            if (!dev_manager) return;
            MachineObject* obj_ = dev_manager->get_selected_machine();

            if (obj_ && obj_->is_support_ams_mapping()) {
                if (m_mapping_popup.IsShown()) return;
                wxPoint pos = item->ClientToScreen(wxPoint(0, 0));
                pos.y += item->GetRect().height;
                m_mapping_popup.Move(pos);

                if (obj_ &&
                    obj_->has_ams() &&
                    m_checkbox_list["use_ams"]->GetValue() &&
                    obj_->dev_id == m_printer_last_select)
                {
                    m_mapping_popup.set_parent_item(item);
                    m_mapping_popup.set_current_filament_id(fo.id);
                    m_mapping_popup.set_tag_texture(fo.type);
                    m_mapping_popup.update_ams_data(obj_->amsList);
                    m_mapping_popup.Popup();
                }
            }
            });

        Material* material_item = new Material();
        material_item->id = fo.id;
        material_item->item = item;
        m_materialList[i] = material_item;

        // build for ams mapping
        m_filaments.push_back(fo);
    }

    if (m_required_data_plate_data_list[m_print_plate_idx]->slice_filaments_info.size() <= 4) {
        m_sizer_ams_mapping->SetCols(m_required_data_plate_data_list[m_print_plate_idx]->slice_filaments_info.size());
    }
    else {
        m_sizer_ams_mapping->SetCols(4);
    }

    m_basic_panel->Layout();
    m_basic_panel->Fit();


    set_flow_calibration_state(true);

    wxSize screenSize = wxGetDisplaySize();
    auto dialogSize = this->GetSize();

    reset_ams_material();

    // basic info
    try {
        float float_time = std::stof(m_required_data_plate_data_list[m_print_plate_idx]->get_gcode_prediction_str());
        double float_weight = std::stof(m_required_data_plate_data_list[m_print_plate_idx]->get_gcode_weight_str());
        wxString   time;
        time = wxString::Format("%s", short_time(get_time_dhms(float_time)));
        char weight[64];
        ::sprintf(weight, "%.2f g", float_weight); // ORCA remove spacing before text
        m_stext_time->SetLabel(time);
        m_stext_weight->SetLabel(weight);
    }
    catch (...) {}
}

void SelectMachineDialog::update_page_turn_state(bool show)
{
     m_bitmap_last_plate->Show(show);
     m_bitmap_next_plate->Show(show);

     if (show) {
         if (m_print_plate_idx <= 0) { m_bitmap_last_plate->Disable(); }
         else { m_bitmap_last_plate->Enable(); }

         if ((m_print_plate_idx + 1) >= m_print_plate_total) { m_bitmap_next_plate->Disable(); }
         else { m_bitmap_next_plate->Enable(); }

         if (m_print_plate_total == 1) {
             m_bitmap_last_plate->Show(false);
             m_bitmap_next_plate->Show(false);
         }
     }
}

void SelectMachineDialog::sys_color_changed()
{
    if (wxGetApp(). dark_mode()) {
        //rename_button->SetIcon("ams_editable_light");
        m_rename_button->SetBitmap(rename_editable_light->bmp());

    }
    else {
        m_rename_button->SetBitmap(rename_editable->bmp());
    }
    m_rename_button->Refresh();
}

bool SelectMachineDialog::Show(bool show)
{
    if (show) {
        m_refresh_timer->Start(LIST_REFRESH_INTERVAL);
    } else {
        m_refresh_timer->Stop();

        DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
        if (dev) {
            MachineObject *obj_ = dev->get_selected_machine();
            if (obj_ && obj_->connection_type() == "cloud" /*&& m_print_type == FROM_SDCARD_VIEW*/) {
                if (obj_->is_connected()) { obj_->disconnect(); }
            }
        }

        return DPIDialog::Show(false);
    }

    show_status(PrintDialogStatus::PrintStatusInit);

    PresetBundle& preset_bundle = *wxGetApp().preset_bundle;
    const auto& project_config = preset_bundle.project_config;

    const t_config_enum_values &enum_keys_map = ConfigOptionEnum<BedType>::get_enum_values();
    const ConfigOptionEnum<BedType>* bed_type=project_config.option<ConfigOptionEnum<BedType>>("curr_bed_type");
    std::string plate_name;
    for (auto& elem : enum_keys_map) {
        if (elem.second == bed_type->value)
            plate_name = elem.first;
    }

    if (plate_name.empty()) {
        m_text_bed_type->Hide();
    }
    else {
        plate_name = "Plate: " + plate_name;
        m_text_bed_type->SetLabelText(plate_name);
        m_text_bed_type->Show();
    }

    // set default value when show this dialog
    wxGetApp().UpdateDlgDarkUI(this);
    wxGetApp().reset_to_active();
    set_default();
    update_user_machine_list();

    Layout();
    Fit();
    CenterOnParent();
    return DPIDialog::Show(show);
}

SelectMachineDialog::~SelectMachineDialog()
{
    delete m_refresh_timer;
}

void SelectMachineDialog::update_lan_machine_list()
{
    DeviceManager* dev = wxGetApp().getDeviceManager();
    if (!dev) return;
   auto  m_free_machine_list = dev->get_local_machine_list();

    BOOST_LOG_TRIVIAL(trace) << "SelectMachinePopup update_other_devices start";

    for (auto& elem : m_free_machine_list) {
        MachineObject* mobj = elem.second;

        /* do not show printer bind state is empty */
        if (!mobj->is_avaliable()) continue;
        if (!mobj->is_online()) continue;
        if (!mobj->is_lan_mode_printer()) continue;
        if (mobj->has_access_right()) {
            std::vector<std::string>               machine_list;
            wxArrayString                          machine_list_name;
            std::map<std::string, MachineObject *> option_list;
        }

    }
    BOOST_LOG_TRIVIAL(trace) << "SelectMachineDialog update_lan_devices end";
}


std::string SelectMachineDialog::get_print_status_info(PrintDialogStatus status)
{
    switch (status) {
    case PrintStatusInit: return "PrintStatusInit";
    case PrintStatusNoUserLogin: return "PrintStatusNoUserLogin";
    case PrintStatusInvalidPrinter: return "PrintStatusInvalidPrinter";
    case PrintStatusConnectingServer: return "PrintStatusConnectingServer";
    case PrintStatusReading: return "PrintStatusReading";
    case PrintStatusReadingFinished: return "PrintStatusReadingFinished";
    case PrintStatusReadingTimeout: return "PrintStatusReadingTimeout";
    case PrintStatusInUpgrading: return "PrintStatusInUpgrading";
    case PrintStatusNeedUpgradingAms: return "PrintStatusNeedUpgradingAms";
    case PrintStatusInSystemPrinting: return "PrintStatusInSystemPrinting";
    case PrintStatusInPrinting: return "PrintStatusInPrinting";
    case PrintStatusDisableAms: return "PrintStatusDisableAms";
    case PrintStatusAmsMappingSuccess: return "PrintStatusAmsMappingSuccess";
    case PrintStatusAmsMappingInvalid: return "PrintStatusAmsMappingInvalid";
    case PrintStatusAmsMappingU0Invalid: return "PrintStatusAmsMappingU0Invalid";
    case PrintStatusAmsMappingValid: return "PrintStatusAmsMappingValid";
    case PrintStatusAmsMappingByOrder: return "PrintStatusAmsMappingByOrder";
    case PrintStatusRefreshingMachineList: return "PrintStatusRefreshingMachineList";
    case PrintStatusSending: return "PrintStatusSending";
    case PrintStatusSendingCanceled: return "PrintStatusSendingCanceled";
    case PrintStatusLanModeNoSdcard: return "PrintStatusLanModeNoSdcard";
    case PrintStatusNoSdcard: return "PrintStatusNoSdcard";
    case PrintStatusUnsupportedPrinter: return "PrintStatusUnsupportedPrinter";
    case PrintStatusTimelapseNoSdcard: return "PrintStatusTimelapseNoSdcard";
    case PrintStatusNotSupportedPrintAll: return "PrintStatusNotSupportedPrintAll";
    }
    return "unknown";
}


 ThumbnailPanel::ThumbnailPanel(wxWindow *parent, wxWindowID winid, const wxPoint &pos, const wxSize &size)
     : wxPanel(parent, winid, pos, size)
 {
#ifdef __WINDOWS__
     SetDoubleBuffered(true);
#endif //__WINDOWS__

     SetBackgroundStyle(wxBG_STYLE_CUSTOM);
     wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
     m_staticbitmap    = new wxStaticBitmap(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize);
     m_background_bitmap = ScalableBitmap(this,"thumbnail_grid",256);
     sizer->Add(m_staticbitmap, 1, wxEXPAND, 0);
     Bind(wxEVT_PAINT, &ThumbnailPanel::OnPaint, this);
     SetSizer(sizer);
     Layout();
     Fit();
 }

 void ThumbnailPanel::set_thumbnail(wxImage &img)
 {
     m_brightness_value = get_brightness_value(img);
     m_bitmap = img;
     //Paint the background bitmap to the thumbnail bitmap with wxMemoryDC
     wxMemoryDC dc;
     bitmap_with_background.Create(wxSize(m_bitmap.GetWidth(), m_bitmap.GetHeight()));
     dc.SelectObject(bitmap_with_background);
     dc.DrawBitmap(m_background_bitmap.bmp(), 0, 0);
     dc.DrawBitmap(m_bitmap, 0, 0);
     dc.SelectObject(wxNullBitmap);
     Refresh();
 }

 void ThumbnailPanel::OnPaint(wxPaintEvent& event) {

     wxPaintDC dc(this);
     render(dc);
 }

 void ThumbnailPanel::render(wxDC& dc) {

     if (wxGetApp().dark_mode() && m_brightness_value < SHOW_BACKGROUND_BITMAP_PIXEL_THRESHOLD) {
         #ifdef __WXMSW__
             wxMemoryDC memdc;
             wxBitmap bmp(GetSize());
             memdc.SelectObject(bmp);
             memdc.DrawBitmap(bitmap_with_background, 0, 0);
             dc.Blit(0, 0, GetSize().GetWidth(), GetSize().GetHeight(), &memdc, 0, 0);
        #else
             dc.DrawBitmap(bitmap_with_background, 0, 0);
        #endif
     }
     else
         dc.DrawBitmap(m_bitmap, 0, 0);

 }

 ThumbnailPanel::~ThumbnailPanel() {}



 }} // namespace Slic3r::GUI
