#include "SendToPrinter.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/Thread.hpp"
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
#include <miniz.h>
#include <algorithm>
#include "BitmapCache.hpp"

namespace Slic3r {
namespace GUI {

#define INITIAL_NUMBER_OF_MACHINES 0
#define LIST_REFRESH_INTERVAL 200
#define MACHINE_LIST_REFRESH_INTERVAL 2000

wxDEFINE_EVENT(EVT_UPDATE_USER_MACHINE_LIST, wxCommandEvent);
wxDEFINE_EVENT(EVT_PRINT_JOB_CANCEL, wxCommandEvent);
wxDEFINE_EVENT(EVT_SEND_JOB_SUCCESS, wxCommandEvent);
wxDEFINE_EVENT(EVT_CLEAR_IPADDRESS, wxCommandEvent);


void SendToPrinterDialog::stripWhiteSpace(std::string& str)
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

wxString SendToPrinterDialog::format_text(wxString &m_msg)
{

	if (wxGetApp().app_config->get("language") != "zh_CN") { return m_msg; }

	wxString out_txt = m_msg;
	wxString count_txt = "";
	int      new_line_pos = 0;

	for (int i = 0; i < m_msg.length(); i++) {
		auto text_size = m_statictext_printer_msg->GetTextExtent(count_txt);
		if (text_size.x < (FromDIP(400))) {
			count_txt += m_msg[i];
		}
		else {
			out_txt.insert(i - 1, '\n');
			count_txt = "";
		}
	}
	return out_txt;
}

void SendToPrinterDialog::check_focus(wxWindow* window)
{
    if (window == m_rename_input || window == m_rename_input->GetTextCtrl()) {
        on_rename_enter();
    }
}

void SendToPrinterDialog::check_fcous_state(wxWindow* window)
{
    check_focus(window);
    auto children = window->GetChildren();
    for (auto child : children) {
        check_fcous_state(child);
    }
}

void SendToPrinterDialog::on_rename_click(wxCommandEvent& event)
{
    m_is_rename_mode = true;
    m_rename_input->GetTextCtrl()->SetValue(m_current_project_name);
    m_rename_switch_panel->SetSelection(1);
    m_rename_input->GetTextCtrl()->SetFocus();
    m_rename_input->GetTextCtrl()->SetInsertionPointEnd();
}

void SendToPrinterDialog::on_rename_enter()
{
    if (m_is_rename_mode == false) {
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
    new_file_name = temp;

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
    m_rename_text->SetLabel(m_current_project_name);
    m_rename_normal_panel->Layout();
}

SendToPrinterDialog::SendToPrinterDialog(Plater *plater)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Send to Printer SD card"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    , m_plater(plater), m_export_3mf_cancel(false)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    // bind
    Bind(wxEVT_CLOSE_WINDOW, &SendToPrinterDialog::on_cancel, this);

    // font
    SetFont(wxGetApp().normal_font());

    // icon
    std::string icon_path = (boost::format("%1%/images/OrcaSlicerTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    Freeze();
    SetBackgroundColour(m_colour_def_color);

    m_sizer_main = new wxBoxSizer(wxVERTICAL);

    m_sizer_main->SetMinSize(wxSize(0, -1));
    m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));

    m_scrollable_region       = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_sizer_scrollable_region = new wxBoxSizer(wxVERTICAL);

    m_panel_image = new wxPanel(m_scrollable_region, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_panel_image->SetBackgroundColour(m_colour_def_color);

    sizer_thumbnail = new wxBoxSizer(wxVERTICAL);
    m_thumbnailPanel = new ThumbnailPanel(m_panel_image);
    m_thumbnailPanel->SetSize(wxSize(FromDIP(256), FromDIP(256)));
    m_thumbnailPanel->SetMinSize(wxSize(FromDIP(256), FromDIP(256)));
    m_thumbnailPanel->SetMaxSize(wxSize(FromDIP(256), FromDIP(256)));
    sizer_thumbnail->Add(m_thumbnailPanel, 0, wxEXPAND, 0);
    m_panel_image->SetSizer(sizer_thumbnail);
    m_panel_image->Layout();

    wxBoxSizer *m_sizer_basic        = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_sizer_basic_weight = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_sizer_basic_time   = new wxBoxSizer(wxHORIZONTAL);

    auto timeimg = new wxStaticBitmap(m_scrollable_region, wxID_ANY, create_scaled_bitmap("print-time", this, 18), wxDefaultPosition, wxSize(FromDIP(18), FromDIP(18)), 0);
    m_sizer_basic_weight->Add(timeimg, 1, wxEXPAND | wxALL, FromDIP(5));
    m_stext_time = new wxStaticText(m_scrollable_region, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
    m_sizer_basic_weight->Add(m_stext_time, 0, wxALL, FromDIP(5));
    m_sizer_basic->Add(m_sizer_basic_weight, 0, wxALIGN_CENTER, 0);
    m_sizer_basic->Add(0, 0, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));

    auto weightimg = new wxStaticBitmap(m_scrollable_region, wxID_ANY, create_scaled_bitmap("print-weight", this, 18), wxDefaultPosition, wxSize(FromDIP(18), FromDIP(18)), 0);
    m_sizer_basic_time->Add(weightimg, 1, wxEXPAND | wxALL, FromDIP(5));
    m_stext_weight = new wxStaticText(m_scrollable_region, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    m_sizer_basic_time->Add(m_stext_weight, 0, wxALL, FromDIP(5));
    m_sizer_basic->Add(m_sizer_basic_time, 0, wxALIGN_CENTER, 0);

    m_line_materia = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_materia->SetForegroundColour(wxColour(238, 238, 238));
    m_line_materia->SetBackgroundColour(wxColour(238, 238, 238));

    wxBoxSizer *m_sizer_printer = new wxBoxSizer(wxHORIZONTAL);

    m_stext_printer_title = new wxStaticText(this, wxID_ANY, _L("Printer"), wxDefaultPosition, wxSize(-1, -1), 0);
    m_stext_printer_title->SetFont(::Label::Head_14);
    m_stext_printer_title->Wrap(-1);
    m_stext_printer_title->SetForegroundColour(m_colour_bold_color);
    m_stext_printer_title->SetBackgroundColour(m_colour_def_color);

    m_sizer_printer->Add(m_stext_printer_title, 0, wxALL | wxLEFT, FromDIP(5));
    m_sizer_printer->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(12));

    m_comboBox_printer = new ::ComboBox(this, wxID_ANY, "", wxDefaultPosition, wxSize(FromDIP(250), -1), 0, nullptr, wxCB_READONLY);
    m_comboBox_printer->Bind(wxEVT_COMBOBOX, &SendToPrinterDialog::on_selection_changed, this);

    m_sizer_printer->Add(m_comboBox_printer, 1, wxEXPAND | wxRIGHT, FromDIP(5));
    btn_bg_enable = StateColor(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
                               std::pair<wxColour, int>(wxColour(105, 75, 124), StateColor::Normal));

    m_button_refresh = new Button(this, _L("Refresh"));
    m_button_refresh->SetBackgroundColor(btn_bg_enable);
    m_button_refresh->SetBorderColor(btn_bg_enable);
    m_button_refresh->SetTextColor(StateColor::darkModeColorFor("#FFFFFE"));
    m_button_refresh->SetSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_refresh->SetMinSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_refresh->SetCornerRadius(FromDIP(10));
    m_button_refresh->Bind(wxEVT_BUTTON, &SendToPrinterDialog::on_refresh, this);
    m_sizer_printer->Add(m_button_refresh, 0, wxALL | wxLEFT, FromDIP(5));

    m_statictext_printer_msg = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
    m_statictext_printer_msg->SetFont(::Label::Body_13);
    m_statictext_printer_msg->SetForegroundColour(*wxBLACK);
    m_statictext_printer_msg->Hide();

    // line schedule
    m_line_schedule = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_schedule->SetBackgroundColour(wxColour(238, 238, 238));
    m_simplebook   = new wxSimplebook(this, wxID_ANY, wxDefaultPosition, SELECT_MACHINE_DIALOG_SIMBOOK_SIZE, 0);

    // perpare mode
    m_panel_prepare = new wxPanel(m_simplebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_panel_prepare->SetBackgroundColour(m_colour_def_color);
    // m_panel_prepare->SetBackgroundColour(wxColour(135,206,250));
    wxBoxSizer *m_sizer_prepare = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_pcont   = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_prepare->Add(0, 0, 1, wxTOP, FromDIP(22));
    m_sizer_pcont->Add(0, 0, 1, wxEXPAND, 0);
    m_button_ensure = new Button(m_panel_prepare, _L("Send"));
    m_button_ensure->SetBackgroundColor(btn_bg_enable);
    m_button_ensure->SetBorderColor(btn_bg_enable);
    m_button_ensure->SetTextColor(StateColor::darkModeColorFor("#FFFFFE"));
    m_button_ensure->SetSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_ensure->SetMinSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_ensure->SetCornerRadius(FromDIP(12));

    m_button_ensure->Bind(wxEVT_BUTTON, &SendToPrinterDialog::on_ok, this);
    m_sizer_pcont->Add(m_button_ensure, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
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
    m_statictext_finish->SetForegroundColour(wxColour(105, 75, 124));
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
    auto st_title_error_code_doc = new wxStaticText(m_sw_print_failed_info, wxID_ANY, ": ");
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
    auto st_title_error_desc_doc = new wxStaticText(m_sw_print_failed_info, wxID_ANY, ": ");
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
    m_link_network_state->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {wxGetApp().link_to_network_check(); });
    m_link_network_state->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {m_link_network_state->SetCursor(wxCURSOR_HAND); });
    m_link_network_state->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {m_link_network_state->SetCursor(wxCURSOR_ARROW); });

    sizer_print_failed_info->Add(m_link_network_state, 0, wxLEFT, 5);
    sizer_print_failed_info->Add(sizer_error_code, 0, wxLEFT, 5);
    sizer_print_failed_info->Add(0, 0, 0, wxTOP, FromDIP(3));
    sizer_print_failed_info->Add(sizer_error_desc, 0, wxLEFT, 5);
    sizer_print_failed_info->Add(0, 0, 0, wxTOP, FromDIP(3));
    sizer_print_failed_info->Add(sizer_extra_info, 0, wxLEFT, 5);

    // bind
    Bind(EVT_SHOW_ERROR_INFO, [this](auto& e) {
        show_print_failed_info(true);
    });

    // bind
    Bind(EVT_UPDATE_USER_MACHINE_LIST, &SendToPrinterDialog::update_printer_combobox, this);
    Bind(EVT_PRINT_JOB_CANCEL, &SendToPrinterDialog::on_print_job_cancel, this);


    m_sizer_scrollable_region->Add(m_panel_image, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    m_sizer_scrollable_region->Add(0, 0, 0, wxTOP, FromDIP(10));
    m_sizer_scrollable_region->Add(m_sizer_basic, 0, wxALIGN_CENTER_HORIZONTAL, 0);
	m_scrollable_region->SetSizer(m_sizer_scrollable_region);
	m_scrollable_region->Layout();

    //file name
    //rename normal
    m_rename_switch_panel = new wxSimplebook(this);
    m_rename_switch_panel->SetSize(wxSize(FromDIP(420), FromDIP(25)));
    m_rename_switch_panel->SetMinSize(wxSize(FromDIP(420), FromDIP(25)));
    m_rename_switch_panel->SetMaxSize(wxSize(FromDIP(420), FromDIP(25)));

    m_rename_normal_panel = new wxPanel(m_rename_switch_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_rename_normal_panel->SetBackgroundColour(*wxWHITE);
    rename_sizer_v = new wxBoxSizer(wxVERTICAL);
    rename_sizer_h = new wxBoxSizer(wxHORIZONTAL);

    m_rename_text = new wxStaticText(m_rename_normal_panel, wxID_ANY, wxT("MyLabel"), wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    m_rename_text->SetForegroundColour(*wxBLACK);
    m_rename_text->SetFont(::Label::Body_13);
    m_rename_text->SetMaxSize(wxSize(FromDIP(390), -1));
    m_rename_button = new Button(m_rename_normal_panel, "", "ams_editable", wxBORDER_NONE, FromDIP(10));
    m_rename_button->SetBackgroundColor(*wxWHITE);
    m_rename_button->SetBackgroundColour(*wxWHITE);

    rename_sizer_h->Add(m_rename_text, 0, wxALIGN_CENTER, 0);
    rename_sizer_h->Add(m_rename_button, 0, wxALIGN_CENTER, 0);
    rename_sizer_v->Add(rename_sizer_h, 1, wxALIGN_CENTER, 0);
    m_rename_normal_panel->SetSizer(rename_sizer_v);
    m_rename_normal_panel->Layout();
    rename_sizer_v->Fit(m_rename_normal_panel);

    //rename edit
    auto m_rename_edit_panel = new wxPanel(m_rename_switch_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_rename_edit_panel->SetBackgroundColour(*wxWHITE);
    auto rename_edit_sizer_v = new wxBoxSizer(wxVERTICAL);

    m_rename_input = new ::TextInput(m_rename_edit_panel, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    m_rename_input->GetTextCtrl()->SetFont(::Label::Body_13);
    m_rename_input->SetSize(wxSize(FromDIP(380), FromDIP(24)));
    m_rename_input->SetMinSize(wxSize(FromDIP(380), FromDIP(24)));
    m_rename_input->SetMaxSize(wxSize(FromDIP(380), FromDIP(24)));
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

    m_rename_button->Bind(wxEVT_BUTTON, &SendToPrinterDialog::on_rename_click, this);
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

    m_panel_prepare->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        check_fcous_state(this);
        e.Skip();
        });

    m_scrollable_region->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        check_fcous_state(this);
        e.Skip();
        });

    Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        check_fcous_state(this);
        e.Skip();
        });

    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(10));
    m_sizer_main->Add(m_scrollable_region, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(6));
    m_sizer_main->Add(m_rename_switch_panel, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(6));
    m_sizer_main->Add(m_line_materia, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(12));
    m_sizer_main->Add(m_sizer_printer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(11));
    m_sizer_main->Add(m_statictext_printer_msg, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    m_sizer_main->Add(0, 1, 0, wxTOP, FromDIP(22));
    m_sizer_main->Add(m_line_schedule, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));
    m_sizer_main->Add(m_simplebook, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    m_sizer_main->Add(m_sw_print_failed_info, 0, wxALIGN_CENTER, 0);
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(13));

    show_print_failed_info(false);
    SetSizer(m_sizer_main);
    Layout();
    Fit();
    Thaw();

    init_bind();
    init_timer();
    // CenterOnParent();
    Centre(wxBOTH);
    wxGetApp().UpdateDlgDarkUI(this);
}

void SendToPrinterDialog::update_print_error_info(int code, std::string msg, std::string extra)
{
    m_print_error_code = code;
    m_print_error_msg = msg;
    m_print_error_extra = extra;
}

void SendToPrinterDialog::show_print_failed_info(bool show, int code, wxString description, wxString extra)
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
        if (!m_sw_print_failed_info->IsShown()) { return; }
        m_sw_print_failed_info->Show(false);
        m_st_txt_error_code->SetLabelText(wxEmptyString);
        m_st_txt_error_desc->SetLabelText(wxEmptyString);
        m_st_txt_extra_info->SetLabelText(wxEmptyString);
        Layout();
        Fit();
    }
}

void SendToPrinterDialog::prepare_mode()
{
    m_is_in_sending_mode = false;
    m_comboBox_printer->Enable();
    m_worker->wait_for_idle();

    if (wxIsBusy())
        wxEndBusyCursor();
    Enable_Send_Button(true);
    show_print_failed_info(false);

    m_status_bar->reset();
    if (m_simplebook->GetSelection() != 0) {
        m_simplebook->SetSelection(0);
    }
}

void SendToPrinterDialog::sending_mode()
{
    m_is_in_sending_mode = true;
    m_comboBox_printer->Disable();
    if (m_simplebook->GetSelection() != 1){
        m_simplebook->SetSelection(1);
        Layout();
        Fit();
    }
}

void SendToPrinterDialog::prepare(int print_plate_idx)
{
    m_print_plate_idx = print_plate_idx;
}

void SendToPrinterDialog::update_priner_status_msg(wxString msg, bool is_warning)
{
    auto colour = is_warning ? wxColour(0xFF, 0x6F, 0x00) : wxColour(0x6B, 0x6B, 0x6B);
    m_statictext_printer_msg->SetForegroundColour(colour);

    if (msg.empty()) {
        if (!m_statictext_printer_msg->GetLabel().empty()) {
            m_statictext_printer_msg->SetLabel(wxEmptyString);
            m_statictext_printer_msg->Hide();
            Layout();
            Fit();
        }
    } else {
        msg          = format_text(msg);

        auto str_new = msg.ToStdString();
        stripWhiteSpace(str_new);

        auto str_old = m_statictext_printer_msg->GetLabel().ToStdString();
        stripWhiteSpace(str_old);

        if (str_new != str_old) {
            if (m_statictext_printer_msg->GetLabel() != msg) {
                m_statictext_printer_msg->SetLabel(msg);
                m_statictext_printer_msg->SetMinSize(wxSize(FromDIP(400), -1));
                m_statictext_printer_msg->SetMaxSize(wxSize(FromDIP(400), -1));
                m_statictext_printer_msg->Wrap(FromDIP(400));
                m_statictext_printer_msg->Show();
                Layout();
                Fit();
            }
        }
    }
}

void SendToPrinterDialog::update_print_status_msg(wxString msg, bool is_warning, bool is_printer_msg)
{
    if (is_printer_msg) {
        update_priner_status_msg(msg, is_warning);
    } else {
        update_priner_status_msg(wxEmptyString, false);
    }
}


void SendToPrinterDialog::init_bind()
{
    Bind(wxEVT_TIMER, &SendToPrinterDialog::on_timer, this);
    Bind(EVT_CLEAR_IPADDRESS, &SendToPrinterDialog::clear_ip_address_config, this);
}

void SendToPrinterDialog::init_timer()
{
    m_refresh_timer = new wxTimer();
    m_refresh_timer->SetOwner(this);
}

void SendToPrinterDialog::on_cancel(wxCloseEvent &event)
{
    m_worker->cancel_all();
    this->EndModal(wxID_CANCEL);
}

void SendToPrinterDialog::on_ok(wxCommandEvent &event)
{
    BOOST_LOG_TRIVIAL(info) << "print_job: on_ok to send";
    m_is_canceled = false;
    Enable_Send_Button(false);
    if (m_is_in_sending_mode)
        return;

    int result = 0;
    if (m_printer_last_select.empty()) {
        return;
    }

    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;

    MachineObject *obj_ = dev->get_selected_machine();

    if (obj_ == nullptr) {
        m_printer_last_select = "";
        m_comboBox_printer->SetTextLabel("");
        return;
    }
    assert(obj_->dev_id == m_printer_last_select);


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
        BOOST_LOG_TRIVIAL(info) << "send_job: m_is_canceled";
        //m_status_bar->set_status_text(task_canceled_text);
        return;
    }

    // enter sending mode
    sending_mode();

    result = m_plater->send_gcode(m_print_plate_idx, [this](int export_stage, int current, int total, bool &cancel) {
        if (this->m_is_canceled) return;
        bool     cancelled = false;
        wxString msg       = _L("Preparing print job");
        m_status_bar->update_status(msg, cancelled, 10, true);
        m_export_3mf_cancel = cancel = cancelled;
    });

    if (m_is_canceled || m_export_3mf_cancel) {
        BOOST_LOG_TRIVIAL(info) << "send_job: m_export_3mf_cancel or m_is_canceled";
        //m_status_bar->set_status_text(task_canceled_text);
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
        BOOST_LOG_TRIVIAL(info) << "send_job: m_export_3mf_cancel or m_is_canceled";
        //m_status_bar->set_status_text(task_canceled_text);
        return;
    }

   /* std::string  file_name       = "";
	auto default_output_file    = wxGetApp().plater()->get_export_gcode_filename(".3mf");
    if (!default_output_file.empty()) {
		fs::path default_output_file_path = boost::filesystem::path(default_output_file.c_str());
		file_name = default_output_file_path.filename().string();
    }*/



    auto m_send_job                 = std::make_unique<SendJob>(m_printer_last_select);
    m_send_job->m_dev_ip            = obj_->dev_ip;
    m_send_job->m_access_code       = obj_->get_access_code();


#if !BBL_RELEASE_TO_PUBLIC
    m_send_job->m_local_use_ssl_for_ftp = wxGetApp().app_config->get("enable_ssl_for_ftp") == "true" ? true : false;
    m_send_job->m_local_use_ssl_for_mqtt = wxGetApp().app_config->get("enable_ssl_for_mqtt") == "true" ? true : false;
#else
    m_send_job->m_local_use_ssl_for_ftp = obj_->local_use_ssl_for_ftp;
    m_send_job->m_local_use_ssl_for_mqtt = obj_->local_use_ssl_for_mqtt;
#endif

    m_send_job->connection_type     = obj_->connection_type();
    m_send_job->cloud_print_only    = true;
    m_send_job->has_sdcard          = obj_->has_sdcard();
    m_send_job->set_project_name(m_current_project_name.utf8_string());

    enable_prepare_mode = false;

    m_send_job->on_check_ip_address_fail([this](int result) {
        wxCommandEvent* evt = new wxCommandEvent(EVT_CLEAR_IPADDRESS);
        wxQueueEvent(this, evt);
        wxGetApp().show_ip_address_enter_dialog();
    });

    if (obj_->is_lan_mode_printer()) {
        m_send_job->set_check_mode();
        m_send_job->check_and_continue();
    }

    replace_job(*m_worker, std::move(m_send_job));

    BOOST_LOG_TRIVIAL(info) << "send_job: send print job";
}

void SendToPrinterDialog::clear_ip_address_config(wxCommandEvent& e)
{
    enable_prepare_mode = true;
    prepare_mode();
}

void SendToPrinterDialog::update_user_machine_list()
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

void SendToPrinterDialog::on_refresh(wxCommandEvent &event)
{
    BOOST_LOG_TRIVIAL(info) << "m_printer_last_select: on_refresh";
    show_status(PrintDialogStatus::PrintStatusRefreshingMachineList);

    update_user_machine_list();
}

void SendToPrinterDialog::on_print_job_cancel(wxCommandEvent &evt)
{
    BOOST_LOG_TRIVIAL(info) << "print_job: canceled";
    show_status(PrintDialogStatus::PrintStatusSendingCanceled);
    // enter prepare mode
    prepare_mode();
}

std::vector<std::string> SendToPrinterDialog::sort_string(std::vector<std::string> strArray)
{
    std::vector<std::string> outputArray;
    std::sort(strArray.begin(), strArray.end());
    std::vector<std::string>::iterator st;
    for (st = strArray.begin(); st != strArray.end(); st++) { outputArray.push_back(*st); }

    return outputArray;
}

bool  SendToPrinterDialog::is_timeout()
{
    if (timeout_count > 15 * 1000 / LIST_REFRESH_INTERVAL) {
        return true;
    }
    return false;
}

void  SendToPrinterDialog::reset_timeout()
{
    timeout_count = 0;
}

void SendToPrinterDialog::update_user_printer()
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

    option_list = dev->get_my_machine_list();

    // same machine only appear once
    for (auto it = option_list.begin(); it != option_list.end(); it++) {
        if (it->second && (it->second->is_online() || it->second->is_connected())) {
            machine_list.push_back(it->second->dev_name);
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
    if (obj) {
        m_printer_last_select = obj->dev_id;
    } else {
        m_printer_last_select = "";
    }

    if (m_list.size() > 0) {
        // select a default machine
        if (m_printer_last_select.empty()) {
            m_printer_last_select = m_list[0]->dev_id;
            m_comboBox_printer->SetSelection(0);
            wxCommandEvent event(wxEVT_COMBOBOX);
            event.SetEventObject(m_comboBox_printer);
            wxPostEvent(m_comboBox_printer, event);
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
        m_comboBox_printer->SetTextLabel("");
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "for send task, current printer id =  " << m_printer_last_select << std::endl;
}

void SendToPrinterDialog::update_printer_combobox(wxCommandEvent &event)
{
    show_status(PrintDialogStatus::PrintStatusInit);
    update_user_printer();
}

void SendToPrinterDialog::on_timer(wxTimerEvent &event)
{
    wxGetApp().reset_to_active();
    update_show_status();
}

void SendToPrinterDialog::on_selection_changed(wxCommandEvent &event)
{
    /* reset timeout and reading printer info */
    //m_status_bar->reset();
    timeout_count      = 0;

    auto selection = m_comboBox_printer->GetSelection();
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;

    MachineObject* obj = nullptr;
    for (int i = 0; i < m_list.size(); i++) {
        if (i == selection) {
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
    }
    else {
        BOOST_LOG_TRIVIAL(error) << "on_selection_changed dev_id not found";
        return;
    }

    update_show_status();
}

void SendToPrinterDialog::update_show_status()
{
    NetworkAgent* agent = Slic3r::GUI::wxGetApp().getAgent();
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!agent) return;
    if (!dev) return;
    MachineObject* obj_ = dev->get_my_machine(m_printer_last_select);
    if (!obj_) {
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
            show_status(PrintDialogStatus::PrintStatusReadingTimeout);
            return;
        }
        else {
            timeout_count++;
            show_status(PrintDialogStatus::PrintStatusReading);
            return;
        }
        return;
    }

    reset_timeout();

    // reading done
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

    // check sdcard when if lan mode printer
   /* if (obj_->is_lan_mode_printer()) {
    }*/
	if (obj_->get_sdcard_state() == MachineObject::SdcardState::NO_SDCARD) {
		show_status(PrintDialogStatus::PrintStatusNoSdcard);
		return;
	}


    if (!obj_->is_support_send_to_sdcard) {
        show_status(PrintDialogStatus::PrintStatusNotSupportedSendToSDCard);
        return;
    }

    if (!m_is_in_sending_mode) {
        show_status(PrintDialogStatus::PrintStatusReadingFinished);
        return;
    }
}

bool SendToPrinterDialog::is_blocking_printing(MachineObject* obj_)
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return true;

    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    auto source_model = preset_bundle->printers.get_edited_preset().get_printer_type(preset_bundle);
    auto target_model = obj_->printer_type;

    if (source_model != target_model) {
        std::vector<std::string> compatible_machine = dev->get_compatible_machine(target_model);
        vector<std::string>::iterator it = find(compatible_machine.begin(), compatible_machine.end(), source_model);
        if (it == compatible_machine.end()) {
            return true;
        }
    }

    return false;
}

void SendToPrinterDialog::Enable_Refresh_Button(bool en)
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
            m_button_refresh->SetBackgroundColor(btn_bg_enable);
            m_button_refresh->SetBorderColor(btn_bg_enable);
        }
    }
}

void SendToPrinterDialog::show_status(PrintDialogStatus status, std::vector<wxString> params)
{
	if (m_print_status != status)
		BOOST_LOG_TRIVIAL(info) << "select_machine_dialog: show_status = " << status;
	m_print_status = status;

	// m_comboBox_printer
	if (status == PrintDialogStatus::PrintStatusRefreshingMachineList)
		m_comboBox_printer->Disable();
	else
		m_comboBox_printer->Enable();

	// m_panel_warn m_simplebook
	if (status == PrintDialogStatus::PrintStatusSending) {
		sending_mode();
	}

	// other
	if (status == PrintDialogStatus::PrintStatusInit) {
		update_print_status_msg(wxEmptyString, false, false);
		Enable_Send_Button(false);
		Enable_Refresh_Button(true);
	}
	else if (status == PrintDialogStatus::PrintStatusNoUserLogin) {
		wxString msg_text = _L("No login account, only printers in LAN mode are displayed");
		update_print_status_msg(msg_text, false, true);
		Enable_Send_Button(false);
		Enable_Refresh_Button(true);
	}
	else if (status == PrintDialogStatus::PrintStatusInvalidPrinter) {
		update_print_status_msg(wxEmptyString, true, true);
		Enable_Send_Button(false);
		Enable_Refresh_Button(true);
	}
	else if (status == PrintDialogStatus::PrintStatusConnectingServer) {
		wxString msg_text = _L("Connecting to server");
		update_print_status_msg(msg_text, true, true);
		Enable_Send_Button(true);
		Enable_Refresh_Button(true);
	}
	else if (status == PrintDialogStatus::PrintStatusReading) {
		wxString msg_text = _L("Synchronizing device information");
		update_print_status_msg(msg_text, false, true);
		Enable_Send_Button(false);
		Enable_Refresh_Button(true);
	}
	else if (status == PrintDialogStatus::PrintStatusReadingFinished) {
		update_print_status_msg(wxEmptyString, false, true);
		Enable_Send_Button(true);
		Enable_Refresh_Button(true);
	}
	else if (status == PrintDialogStatus::PrintStatusReadingTimeout) {
		wxString msg_text = _L("Synchronizing device information time out");
		update_print_status_msg(msg_text, true, true);
		Enable_Send_Button(true);
		Enable_Refresh_Button(true);
	}
	else if (status == PrintDialogStatus::PrintStatusInUpgrading) {
		wxString msg_text = _L("Cannot send the print task when the upgrade is in progress");
		update_print_status_msg(msg_text, true, true);
		Enable_Send_Button(false);
		Enable_Refresh_Button(true);
	}
    else if (status == PrintDialogStatus::PrintStatusUnsupportedPrinter) {
        wxString msg_text = _L("The selected printer is incompatible with the chosen printer presets.");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    }
	else if (status == PrintDialogStatus::PrintStatusRefreshingMachineList) {
		update_print_status_msg(wxEmptyString, false, true);
		Enable_Send_Button(false);
		Enable_Refresh_Button(false);
	}
	else if (status == PrintDialogStatus::PrintStatusSending) {
		Enable_Send_Button(false);
		Enable_Refresh_Button(false);
	}
	else if (status == PrintDialogStatus::PrintStatusSendingCanceled) {
		Enable_Send_Button(true);
		Enable_Refresh_Button(true);
	}
	else if (status == PrintDialogStatus::PrintStatusNoSdcard) {
		wxString msg_text = _L("An SD card needs to be inserted before send to printer SD card.");
		update_print_status_msg(msg_text, true, true);
		Enable_Send_Button(false);
		Enable_Refresh_Button(true);
    }
    else if (status == PrintDialogStatus::PrintStatusNotOnTheSameLAN) {
        wxString msg_text = _L("The printer is required to be in the same LAN as Orca Slicer.");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    }
    else if (status == PrintDialogStatus::PrintStatusNotSupportedSendToSDCard) {
        wxString msg_text = _L("The printer does not support sending to printer SD card.");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    }
    else {
		Enable_Send_Button(true);
		Enable_Refresh_Button(true);
    }
}


void SendToPrinterDialog::Enable_Send_Button(bool en)
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
            m_button_ensure->SetBackgroundColor(btn_bg_enable);
            m_button_ensure->SetBorderColor(btn_bg_enable);
        }
    }
}

void SendToPrinterDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    m_button_refresh->SetMinSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_refresh->SetCornerRadius(FromDIP(12));
    m_button_ensure->SetMinSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_ensure->SetCornerRadius(FromDIP(12));
    m_status_bar->msw_rescale();
    Fit();
    Refresh();
}

void SendToPrinterDialog::set_default()
{
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
    m_current_project_name = wxString::FromUTF8(filename_path.filename().string());

    //unsupported character filter
    m_current_project_name = from_u8(filter_characters(m_current_project_name.ToUTF8().data(), "<>[]:/\\|?*\""));

    m_rename_text->SetLabelText(m_current_project_name);
    m_rename_normal_panel->Layout();

    enable_prepare_mode = true;
    prepare_mode();

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
        } else {
            show_status(PrintDialogStatus::PrintStatusNoUserLogin);
        }
    }

    // thumbmail
    //wxBitmap bitmap;
    ThumbnailData &data   = m_plater->get_partplate_list().get_curr_plate()->thumbnail_data;
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
        image  = image.Rescale(FromDIP(256), FromDIP(256));
        m_thumbnailPanel->set_thumbnail(image);
    }

    std::vector<std::string> materials;
    std::vector<std::string> display_materials;
    {
        auto preset_bundle = wxGetApp().preset_bundle;
        for (auto filament_name : preset_bundle->filament_presets) {
            for (auto iter = preset_bundle->filaments.lbegin(); iter != preset_bundle->filaments.end(); iter++) {
                if (filament_name.compare(iter->name) == 0) {
                    std::string display_filament_type;
                    std::string filament_type = iter->config.get_filament_type(display_filament_type);
                    display_materials.push_back(display_filament_type);
                    materials.push_back(filament_type);
                }
            }
        }
    }

    m_scrollable_region->Layout();
    m_scrollable_region->Fit();
    Layout();
    Fit();


    wxSize screenSize = wxGetDisplaySize();
    auto dialogSize = this->GetSize();


    // basic info
    auto       aprint_stats = m_plater->get_partplate_list().get_current_fff_print().print_statistics();
    wxString   time;
    PartPlate *plate = m_plater->get_partplate_list().get_curr_plate();
    if (plate) {
        if (plate->get_slice_result()) { time = wxString::Format("%s", short_time(get_time_dhms(plate->get_slice_result()->print_statistics.modes[0].time))); }
    }

    char weight[64];
    if (wxGetApp().app_config->get("use_inches") == "1") {
        ::sprintf(weight, "  %.2f oz", aprint_stats.total_weight*0.035274);
    }else{
        ::sprintf(weight, "  %.2f g", aprint_stats.total_weight);
    }

    m_stext_time->SetLabel(time);
    m_stext_weight->SetLabel(weight);
}

bool SendToPrinterDialog::Show(bool show)
{
    show_status(PrintDialogStatus::PrintStatusInit);

    // set default value when show this dialog
    if (show) {
        wxGetApp().reset_to_active();
        set_default();
        update_user_machine_list();
    }

    if (show) {
        m_refresh_timer->Start(LIST_REFRESH_INTERVAL);
    } else {
        m_refresh_timer->Stop();
    }

    Layout();
    Fit();
    if (show) { CenterOnParent(); }
    return DPIDialog::Show(show);
}

SendToPrinterDialog::~SendToPrinterDialog()
{
    delete m_refresh_timer;
}

}
}
