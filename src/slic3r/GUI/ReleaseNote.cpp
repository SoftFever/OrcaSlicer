#include "ReleaseNote.hpp"
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
#include "Widgets/WebView.hpp"
#include "Jobs/BoostThreadWorker.hpp"
#include "Jobs/PlaterWorker.hpp"

#include <wx/regex.h>
#include <wx/progdlg.h>
#include <wx/clipbrd.h>
#include <wx/dcgraph.h>
#include <miniz.h>
#include <algorithm>
#include "Plater.hpp"
#include "BitmapCache.hpp"
#include "slic3r/GUI/GUI_App.hpp"

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_SECONDARY_CHECK_CONFIRM, wxCommandEvent);
wxDEFINE_EVENT(EVT_SECONDARY_CHECK_CANCEL, wxCommandEvent);
wxDEFINE_EVENT(EVT_SECONDARY_CHECK_DONE, wxCommandEvent);
wxDEFINE_EVENT(EVT_SECONDARY_CHECK_RESUME, wxCommandEvent);
wxDEFINE_EVENT(EVT_LOAD_VAMS_TRAY, wxCommandEvent);
wxDEFINE_EVENT(EVT_CHECKBOX_CHANGE, wxCommandEvent);
wxDEFINE_EVENT(EVT_ENTER_IP_ADDRESS, wxCommandEvent);
wxDEFINE_EVENT(EVT_CLOSE_IPADDRESS_DLG, wxCommandEvent);
wxDEFINE_EVENT(EVT_CHECK_IP_ADDRESS_FAILED, wxCommandEvent);
wxDEFINE_EVENT(EVT_SECONDARY_CHECK_RETRY, wxCommandEvent);
wxDEFINE_EVENT(EVT_PRINT_ERROR_STOP, wxCommandEvent);
wxDEFINE_EVENT(EVT_UPDATE_NOZZLE, wxCommandEvent);
wxDEFINE_EVENT(EVT_JUMP_TO_HMS, wxCommandEvent);
wxDEFINE_EVENT(EVT_JUMP_TO_LIVEVIEW, wxCommandEvent);

ReleaseNoteDialog::ReleaseNoteDialog(Plater *plater /*= nullptr*/)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Release Note"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    std::string icon_path = (boost::format("%1%/images/OrcaSlicerTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top   = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(30));

    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_body->Add(0, 0, 0, wxLEFT, FromDIP(38));

    auto sm = create_scaled_bitmap("OrcaSlicer", nullptr,  70);
    auto brand = new wxStaticBitmap(this, wxID_ANY, sm, wxDefaultPosition, wxSize(FromDIP(70), FromDIP(70)));

    m_sizer_body->Add(brand, 0, wxALL, 0);

    m_sizer_body->Add(0, 0, 0, wxRIGHT, FromDIP(25));

    wxBoxSizer *m_sizer_right = new wxBoxSizer(wxVERTICAL);

    m_text_up_info = new Label(this, Label::Head_14, wxEmptyString, LB_AUTO_WRAP);
    m_text_up_info->SetForegroundColour(wxColour(0x26, 0x2E, 0x30));
    m_sizer_right->Add(m_text_up_info, 0, wxEXPAND, 0);

    m_sizer_right->Add(0, 0, 1, wxTOP, FromDIP(15));

    m_vebview_release_note = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(560), FromDIP(430)), wxVSCROLL);
    m_vebview_release_note->SetScrollRate(5, 5);
    m_vebview_release_note->SetBackgroundColour(wxColour(0xF8, 0xF8, 0xF8));
    m_vebview_release_note->SetMaxSize(wxSize(FromDIP(560), FromDIP(430)));

    m_sizer_right->Add(m_vebview_release_note, 0, wxEXPAND | wxRIGHT, FromDIP(20));
    m_sizer_body->Add(m_sizer_right, 1, wxBOTTOM | wxEXPAND, FromDIP(30));
    m_sizer_main->Add(m_sizer_body, 0, wxEXPAND, 0);

    SetSizer(m_sizer_main);
    Layout();
    m_sizer_main->Fit(this);

    Centre(wxBOTH);
    wxGetApp().UpdateDlgDarkUI(this);
}

ReleaseNoteDialog::~ReleaseNoteDialog() {}


void ReleaseNoteDialog::on_dpi_changed(const wxRect &suggested_rect)
{
}

void ReleaseNoteDialog::update_release_note(wxString release_note, std::string version)
{
    m_text_up_info->SetLabel(wxString::Format(_L("version %s update information :"), version));
    wxBoxSizer * sizer_text_release_note = new wxBoxSizer(wxVERTICAL);
    auto        m_staticText_release_note = new ::Label(m_vebview_release_note, release_note, LB_AUTO_WRAP);
    m_staticText_release_note->SetMinSize(wxSize(FromDIP(530), -1));
    m_staticText_release_note->SetMaxSize(wxSize(FromDIP(530), -1));
    sizer_text_release_note->Add(m_staticText_release_note, 0, wxALL, 5);
    m_vebview_release_note->SetSizer(sizer_text_release_note);
    m_vebview_release_note->Layout();
    m_vebview_release_note->Fit();
    wxGetApp().UpdateDlgDarkUI(this);
}

UpdatePluginDialog::UpdatePluginDialog(wxWindow* parent /*= nullptr*/)
    : DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, _L("Network plug-in update"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    std::string icon_path = (boost::format("%1%/images/OrcaSlicerTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    wxBoxSizer* m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(30));

    wxBoxSizer* m_sizer_body = new wxBoxSizer(wxHORIZONTAL);



    auto sm = create_scaled_bitmap("OrcaSlicer", nullptr, 55);
    auto brand = new wxStaticBitmap(this, wxID_ANY, sm, wxDefaultPosition, wxSize(FromDIP(55), FromDIP(55)));

    wxBoxSizer* m_sizer_right = new wxBoxSizer(wxVERTICAL);

    m_text_up_info = new Label(this, Label::Head_13, wxEmptyString, LB_AUTO_WRAP);
    m_text_up_info->SetMaxSize(wxSize(FromDIP(260), -1));
    m_text_up_info->SetForegroundColour(wxColour(0x26, 0x2E, 0x30));


    operation_tips = new ::Label(this, Label::Body_12, _L("Click OK to update the Network plug-in when Orca Slicer launches next time."), LB_AUTO_WRAP);
    operation_tips->SetMinSize(wxSize(FromDIP(260), -1));
    operation_tips->SetMaxSize(wxSize(FromDIP(260), -1));

    m_vebview_release_note = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_vebview_release_note->SetScrollRate(5, 5);
    m_vebview_release_note->SetBackgroundColour(wxColour(0xF8, 0xF8, 0xF8));
    m_vebview_release_note->SetMinSize(wxSize(FromDIP(260), FromDIP(150)));
    m_vebview_release_note->SetMaxSize(wxSize(FromDIP(260), FromDIP(150)));

    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);

    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    auto m_button_ok = new Button(this, _L("OK"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(wxColour(0xFFFFFE));
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetCornerRadius(FromDIP(12));

    m_button_ok->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        EndModal(wxID_OK);
        });

    auto m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetCornerRadius(FromDIP(12));

    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        EndModal(wxID_NO);
        });

    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_ok, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_cancel, 0, wxALL, FromDIP(5));

    m_sizer_right->Add(m_text_up_info, 0, wxEXPAND, 0);
    m_sizer_right->Add(0, 0, 0, wxTOP, FromDIP(5));
    m_sizer_right->Add(m_vebview_release_note, 0, wxEXPAND | wxRIGHT, FromDIP(20));
    m_sizer_right->Add(0, 0, 0, wxTOP, FromDIP(5));
    m_sizer_right->Add(operation_tips, 1, wxEXPAND | wxRIGHT, FromDIP(20));
    m_sizer_right->Add(0, 0, 0, wxTOP, FromDIP(5));
    m_sizer_right->Add(sizer_button, 0, wxEXPAND | wxRIGHT, FromDIP(20));

    m_sizer_body->Add(0, 0, 0, wxLEFT, FromDIP(24));
    m_sizer_body->Add(brand, 0, wxALL, 0);
    m_sizer_body->Add(0, 0, 0, wxRIGHT, FromDIP(20));
    m_sizer_body->Add(m_sizer_right, 1, wxBOTTOM | wxEXPAND, FromDIP(18));
    m_sizer_main->Add(m_sizer_body, 0, wxEXPAND, 0);

    SetSizer(m_sizer_main);
    Layout();
    m_sizer_main->Fit(this);

    Centre(wxBOTH);
    wxGetApp().UpdateDlgDarkUI(this);
}

UpdatePluginDialog::~UpdatePluginDialog() {}


void UpdatePluginDialog::on_dpi_changed(const wxRect& suggested_rect)
{
}

void UpdatePluginDialog::update_info(std::string json_path)
{
    std::string version_str, description_str;
    wxString version;
    wxString description;

    try {
        boost::nowide::ifstream ifs(json_path);
        json j;
        ifs >> j;

        version_str = j["version"];
        description_str = j["description"];
    }
    catch (nlohmann::detail::parse_error& err) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse " << json_path << " got a nlohmann::detail::parse_error, reason = " << err.what();
        return;
    }

    version = from_u8(version_str);
    description = from_u8(description_str);

    m_text_up_info->SetLabel(wxString::Format(_L("A new Network plug-in(%s) available, Do you want to install it?"), version));
    m_text_up_info->SetMinSize(wxSize(FromDIP(260), -1));
    m_text_up_info->SetMaxSize(wxSize(FromDIP(260), -1));
    wxBoxSizer* sizer_text_release_note = new wxBoxSizer(wxVERTICAL);
    auto        m_text_label            = new ::Label(m_vebview_release_note, Label::Body_13, description, LB_AUTO_WRAP);
    m_text_label->SetMinSize(wxSize(FromDIP(235), -1));
    m_text_label->SetMaxSize(wxSize(FromDIP(235), -1));

    sizer_text_release_note->Add(m_text_label, 0, wxALL, 5);
    m_vebview_release_note->SetSizer(sizer_text_release_note);
    m_vebview_release_note->Layout();
    m_vebview_release_note->Fit();
    wxGetApp().UpdateDlgDarkUI(this);
    Layout();
    Fit();
}

UpdateVersionDialog::UpdateVersionDialog(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, _L("New version of Orca Slicer"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
{
    std::string icon_path = (boost::format("%1%/images/OrcaSlicerTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);

    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top   = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    

    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxHORIZONTAL);

    

    auto sm    = create_scaled_bitmap("OrcaSlicer", nullptr, 70);
    m_brand = new wxStaticBitmap(this, wxID_ANY, sm, wxDefaultPosition, wxSize(FromDIP(70), FromDIP(70)));

    

    wxBoxSizer *m_sizer_right = new wxBoxSizer(wxVERTICAL);

    m_text_up_info = new Label(this, Label::Head_14, wxEmptyString, LB_AUTO_WRAP);
    m_text_up_info->SetForegroundColour(wxColour(0x26, 0x2E, 0x30));

    m_simplebook_release_note = new wxSimplebook(this);
    m_simplebook_release_note->SetSize(wxSize(FromDIP(560), FromDIP(430)));
    m_simplebook_release_note->SetMinSize(wxSize(FromDIP(560), FromDIP(430)));
    m_simplebook_release_note->SetBackgroundColour(wxColour(0xF8, 0xF8, 0xF8));

    m_scrollwindows_release_note = new wxScrolledWindow(m_simplebook_release_note, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(560), FromDIP(430)), wxVSCROLL);
    m_scrollwindows_release_note->SetScrollRate(5, 5);
    m_scrollwindows_release_note->SetBackgroundColour(wxColour(0xF8, 0xF8, 0xF8));

    //webview
    m_vebview_release_note = CreateTipView(m_simplebook_release_note);
    m_vebview_release_note->SetBackgroundColour(wxColour(0xF8, 0xF8, 0xF8));
    m_vebview_release_note->SetSize(wxSize(FromDIP(560), FromDIP(430)));
    m_vebview_release_note->SetMinSize(wxSize(FromDIP(560), FromDIP(430)));
    //m_vebview_release_note->SetMaxSize(wxSize(FromDIP(560), FromDIP(430)));
    m_vebview_release_note->Bind(wxEVT_WEBVIEW_NAVIGATING,[=](wxWebViewEvent& event){
        static bool load_url_first = false;
        if(load_url_first){
            // Orca: not used in Orca Slicer
            // wxLaunchDefaultBrowser(url_line);
            event.Veto();
        }else{
            load_url_first = true;
        }
        
    });

	fs::path ph(data_dir());
	ph /= "resources/tooltip/releasenote.html";
	if (!fs::exists(ph)) {
		ph = resources_dir();
		ph /= "tooltip/releasenote.html";
	}
	auto url = ph.string();
	std::replace(url.begin(), url.end(), '\\', '/');
	url = "file:///" + url;
    m_vebview_release_note->LoadURL(from_u8(url));

    m_simplebook_release_note->AddPage(m_scrollwindows_release_note, wxEmptyString, false);
    m_simplebook_release_note->AddPage(m_vebview_release_note, wxEmptyString, false);


    
    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);


    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
                            std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    m_button_download = new Button(this, _L("Download"));
    m_button_download->SetBackgroundColor(btn_bg_green);
    m_button_download->SetBorderColor(*wxWHITE);
    m_button_download->SetTextColor(wxColour("#FFFFFE"));
    m_button_download->SetFont(Label::Body_12);
    m_button_download->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_download->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_download->SetCornerRadius(FromDIP(12));

    m_button_download->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        EndModal(wxID_YES);
    });

    m_button_skip_version = new Button(this, _L("Skip this Version"));
    m_button_skip_version->SetBackgroundColor(btn_bg_white);
    m_button_skip_version->SetBorderColor(wxColour(38, 46, 48));
    m_button_skip_version->SetFont(Label::Body_12);
    m_button_skip_version->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_skip_version->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_skip_version->SetCornerRadius(FromDIP(12));

    m_button_skip_version->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { 
        wxGetApp().set_skip_version(true);
        EndModal(wxID_NO);
    });

    m_cb_stable_only = new CheckBox(this);
    m_cb_stable_only->SetValue(wxGetApp().app_config->get_bool("check_stable_update_only"));
    m_cb_stable_only->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& e) {
        wxGetApp().app_config->set_bool("check_stable_update_only", m_cb_stable_only->GetValue());
        e.Skip();
    });

    auto stable_only_label = new Label(this, _L("Check for stable updates only"));
    stable_only_label->SetFont(Label::Body_13);
    stable_only_label->SetForegroundColour(wxColour(38, 46, 48));
    stable_only_label->SetFont(Label::Body_12);

    m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetCornerRadius(FromDIP(12));

    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        EndModal(wxID_NO);
    });

    m_sizer_main->Add(m_line_top, 0, wxEXPAND | wxBOTTOM, 0);
    
    //sizer_button->Add(m_remind_choice, 0, wxALL | wxEXPAND, FromDIP(5));
    sizer_button->AddStretchSpacer();
    sizer_button->Add(stable_only_label, 0, wxALIGN_CENTER | wxLEFT, FromDIP(7));
    sizer_button->Add(m_cb_stable_only, 0, wxALIGN_CENTER | wxLEFT, FromDIP(5));
    sizer_button->Add(m_button_download, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_skip_version, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_cancel, 0, wxALL, FromDIP(5));

    m_sizer_right->Add(m_text_up_info, 0, wxEXPAND | wxBOTTOM | wxTOP, FromDIP(15));
    m_sizer_right->Add(m_simplebook_release_note, 1, wxEXPAND | wxRIGHT, 0);
    m_sizer_right->Add(sizer_button, 0, wxEXPAND | wxRIGHT, FromDIP(20));

    m_sizer_body->Add(m_brand, 0, wxTOP|wxRIGHT|wxLEFT, FromDIP(15));
    m_sizer_body->Add(0, 0, 0, wxRIGHT, 0);
    m_sizer_body->Add(m_sizer_right, 1, wxBOTTOM | wxEXPAND, FromDIP(8));
    m_sizer_main->Add(m_sizer_body, 1, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxBOTTOM, 10);

    SetSizer(m_sizer_main);
    Layout();
    Fit();

    SetMinSize(GetSize());

    Centre(wxBOTH);
    wxGetApp().UpdateDlgDarkUI(this);
}

UpdateVersionDialog::~UpdateVersionDialog() {}


wxWebView* UpdateVersionDialog::CreateTipView(wxWindow* parent)
{
	wxWebView* tipView = WebView::CreateWebView(parent, "");
	tipView->Bind(wxEVT_WEBVIEW_LOADED, &UpdateVersionDialog::OnLoaded, this);
	tipView->Bind(wxEVT_WEBVIEW_NAVIGATED, &UpdateVersionDialog::OnTitleChanged, this);
	tipView->Bind(wxEVT_WEBVIEW_ERROR, &UpdateVersionDialog::OnError, this);
	return tipView;
}

void UpdateVersionDialog::OnLoaded(wxWebViewEvent& event)
{
    event.Skip();
}

void UpdateVersionDialog::OnTitleChanged(wxWebViewEvent& event)
{
    //ShowReleaseNote();
    event.Skip();
}
void UpdateVersionDialog::OnError(wxWebViewEvent& event)
{
    event.Skip();
}

static std::string url_encode(const std::string& value) {
	std::ostringstream escaped;
	escaped.fill('0');
	escaped << std::hex;
	for (std::string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
		std::string::value_type c = (*i);

		// Keep alphanumeric and other accepted characters intact
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			escaped << c;
			continue;
		}

		// Any other characters are percent-encoded
		escaped << std::uppercase;
		escaped << '%' << std::setw(2) << int((unsigned char)c);
		escaped << std::nouppercase;
	}
	return escaped.str();
}

bool UpdateVersionDialog::ShowReleaseNote(std::string content)
{
	auto script = "window.showMarkdown('" + url_encode(content) + "', true);";
    RunScript(script);
    return true;
}

void UpdateVersionDialog::RunScript(std::string script)
{
    WebView::RunScript(m_vebview_release_note, script);
    script.clear();
}

void UpdateVersionDialog::on_dpi_changed(const wxRect &suggested_rect) {
    m_button_download->Rescale();
    m_button_skip_version->Rescale();
    m_button_cancel->Rescale();
}

std::vector<std::string> UpdateVersionDialog::splitWithStl(std::string str,std::string pattern)
{
    std::string::size_type pos;
    std::vector<std::string> result;
    str += pattern;
    int size = str.size();
    for (int i = 0; i < size; i++)
    {
        pos = str.find(pattern, i);
        if (pos < size)
        {
            std::string s = str.substr(i, pos - i);
            result.push_back(s);
            i = pos + pattern.size() - 1;
        }
    }
    return result;
}

void UpdateVersionDialog::update_version_info(wxString release_note, wxString version)
{
    //bbs check whether the web display is used
    bool use_web_link       = false;
    url_line                = "";
    auto split_array        =  splitWithStl(release_note.ToStdString(), "###");

    if (split_array.size() >= 3) {
        for (auto i = 0; i < split_array.size(); i++) {
            std::string url = split_array[i];
            if (std::strstr(url.c_str(), "http://") != NULL || std::strstr(url.c_str(), "https://") != NULL) {
                use_web_link = true;
                url_line = url;
                break;
            }
        }
    }
   

    if (use_web_link) {
        m_brand->Hide();
        m_text_up_info->Hide();
        m_simplebook_release_note->SetSelection(1);
        m_vebview_release_note->LoadURL(from_u8(url_line));
    }
    else {
        m_simplebook_release_note->SetMaxSize(wxSize(FromDIP(560), FromDIP(430)));
        m_simplebook_release_note->SetSelection(0);
        m_text_up_info->SetLabel(wxString::Format(_L("Click to download new version in default browser: %s"), version));
        wxBoxSizer* sizer_text_release_note = new wxBoxSizer(wxVERTICAL);
        auto        m_staticText_release_note = new ::Label(m_scrollwindows_release_note, release_note, LB_AUTO_WRAP);
        m_staticText_release_note->SetMinSize(wxSize(FromDIP(560), -1));
        m_staticText_release_note->SetMaxSize(wxSize(FromDIP(560), -1));
        sizer_text_release_note->Add(m_staticText_release_note, 0, wxALL, 5);
        m_scrollwindows_release_note->SetSizer(sizer_text_release_note);
        m_scrollwindows_release_note->Layout();
        m_scrollwindows_release_note->Fit();
        SetMinSize(GetSize());
        SetMaxSize(GetSize());
    }

    wxGetApp().UpdateDlgDarkUI(this);
    Layout();
    Fit();
}

SecondaryCheckDialog::SecondaryCheckDialog(wxWindow* parent, wxWindowID id, const wxString& title, enum ButtonStyle btn_style, const wxPoint& pos, const wxSize& size, long style, bool not_show_again_check)
    :DPIFrame(parent, id, title, pos, size, style)
{
    m_button_style = btn_style;
    std::string icon_path = (boost::format("%1%/images/OrcaSlicerTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(400), 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(5));

    wxBoxSizer* m_sizer_right = new wxBoxSizer(wxVERTICAL);

    m_sizer_right->Add(0, 0, 1, wxTOP, FromDIP(15));

    m_vebview_release_note = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_vebview_release_note->SetScrollRate(0, 5);
    m_vebview_release_note->SetBackgroundColour(*wxWHITE);
    m_vebview_release_note->SetMinSize(wxSize(FromDIP(400), FromDIP(380)));
    m_sizer_right->Add(m_vebview_release_note, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(15));


    auto bottom_sizer = new wxBoxSizer(wxVERTICAL);
    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);
    btn_bg_green = StateColor(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    btn_bg_white = StateColor(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));


    if (not_show_again_check) {
        auto checkbox_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_show_again_checkbox = new wxCheckBox(this, wxID_ANY, _L("Don't show again"), wxDefaultPosition, wxDefaultSize, 0);
        m_show_again_checkbox->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, [this](wxCommandEvent& e) {
            not_show_again = !not_show_again;
            m_show_again_checkbox->SetValue(not_show_again);
        });
        checkbox_sizer->Add(FromDIP(15), 0, 0, 0);
        checkbox_sizer->Add(m_show_again_checkbox, 0, wxALL, FromDIP(5));
        bottom_sizer->Add(checkbox_sizer, 0, wxBOTTOM | wxEXPAND, 0);
    }
    m_button_ok = new Button(this, _L("Confirm"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(wxColour("#FFFFFE"));
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_ok->SetMaxSize(wxSize(-1, FromDIP(24)));
    m_button_ok->SetCornerRadius(FromDIP(12));

    m_button_ok->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        wxCommandEvent evt(EVT_SECONDARY_CHECK_CONFIRM, GetId());
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
        this->on_hide();
    });

    m_button_retry = new Button(this, _L("Retry"));
    m_button_retry->SetBackgroundColor(btn_bg_green);
    m_button_retry->SetBorderColor(*wxWHITE);
    m_button_retry->SetTextColor(wxColour("#FFFFFE"));
    m_button_retry->SetFont(Label::Body_12);
    m_button_retry->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_retry->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_retry->SetMaxSize(wxSize(-1, FromDIP(24)));
    m_button_retry->SetCornerRadius(FromDIP(12));

    m_button_retry->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        wxCommandEvent evt(EVT_SECONDARY_CHECK_RETRY, GetId());
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
        this->on_hide();
    });

    m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_cancel->SetMaxSize(wxSize(-1, FromDIP(24)));
    m_button_cancel->SetCornerRadius(FromDIP(12));

    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
            wxCommandEvent evt(EVT_SECONDARY_CHECK_CANCEL);
            e.SetEventObject(this);
            GetEventHandler()->ProcessEvent(evt);
            this->on_hide();
        });

    m_button_fn = new Button(this, _L("Done"));
    m_button_fn->SetBackgroundColor(btn_bg_white);
    m_button_fn->SetBorderColor(wxColour(38, 46, 48));
    m_button_fn->SetFont(Label::Body_12);
    m_button_fn->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_fn->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_fn->SetMaxSize(wxSize(-1, FromDIP(24)));
    m_button_fn->SetCornerRadius(FromDIP(12));

    m_button_fn->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
            post_event(wxCommandEvent(EVT_SECONDARY_CHECK_DONE));
            e.Skip();
        });

    m_button_resume = new Button(this, _L("resume"));
    m_button_resume->SetBackgroundColor(btn_bg_white);
    m_button_resume->SetBorderColor(wxColour(38, 46, 48));
    m_button_resume->SetFont(Label::Body_12);
    m_button_resume->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_resume->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_resume->SetMaxSize(wxSize(-1, FromDIP(24)));
    m_button_resume->SetCornerRadius(FromDIP(12));

    m_button_resume->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        post_event(wxCommandEvent(EVT_SECONDARY_CHECK_RESUME));
        e.Skip();
        });
    m_button_resume->Hide();

    if (btn_style == CONFIRM_AND_CANCEL) {
        m_button_cancel->Show();
        m_button_fn->Hide();
        m_button_retry->Hide();
    } else if (btn_style == CONFIRM_AND_DONE) {
        m_button_cancel->Hide();
        m_button_fn->Show();
        m_button_retry->Hide();
    } else if (btn_style == CONFIRM_AND_RETRY) {
        m_button_retry->Show();
        m_button_cancel->Hide();
        m_button_fn->Hide();
    } else if (style == DONE_AND_RETRY) {
        m_button_retry->Show();
        m_button_fn->Show();
        m_button_cancel->Hide();
    }
    else {
        m_button_retry->Hide();
        m_button_cancel->Hide();
        m_button_fn->Hide();
    }

    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_resume, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_retry, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_fn, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_ok, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_cancel, 0, wxALL, FromDIP(5));
    sizer_button->Add(FromDIP(5),0, 0, 0);
    bottom_sizer->Add(sizer_button, 0, wxEXPAND | wxRIGHT | wxLEFT, 0);


    m_sizer_right->Add(bottom_sizer, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(15));
    m_sizer_right->Add(0, 0, 0, wxTOP,FromDIP(10));

    m_sizer_main->Add(m_sizer_right, 0, wxBOTTOM | wxEXPAND, FromDIP(5));

    Bind(wxEVT_CLOSE_WINDOW, [this](auto& e) {this->on_hide();});
    Bind(wxEVT_ACTIVATE, [this](auto& e) { if (!e.GetActive()) this->RequestUserAttention(wxUSER_ATTENTION_ERROR); });

    SetSizer(m_sizer_main);
    Layout();
    m_sizer_main->Fit(this);

    CenterOnParent();
    wxGetApp().UpdateFrameDarkUI(this);
}

void SecondaryCheckDialog::post_event(wxCommandEvent&& event)
{
    if (event_parent) {
        event.SetString("");
        event.SetEventObject(event_parent);
        wxPostEvent(event_parent, event);
        event.Skip();
    }
}

void SecondaryCheckDialog::update_text(wxString text)
{
    wxBoxSizer* sizer_text_release_note = new wxBoxSizer(wxVERTICAL);

    if (!m_staticText_release_note) {
        m_staticText_release_note = new Label(m_vebview_release_note, text, LB_AUTO_WRAP);
        wxBoxSizer* top_blank_sizer = new wxBoxSizer(wxVERTICAL);
        wxBoxSizer* bottom_blank_sizer = new wxBoxSizer(wxVERTICAL);
        top_blank_sizer->Add(FromDIP(5), 0, wxALIGN_CENTER | wxALL, FromDIP(5));
        bottom_blank_sizer->Add(FromDIP(5), 0, wxALIGN_CENTER | wxALL, FromDIP(5));

        sizer_text_release_note->Add(top_blank_sizer, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
        sizer_text_release_note->Add(m_staticText_release_note, 0, wxALIGN_CENTER, FromDIP(5));
        sizer_text_release_note->Add(bottom_blank_sizer, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
        m_vebview_release_note->SetSizer(sizer_text_release_note);
    }
    m_staticText_release_note->SetMaxSize(wxSize(FromDIP(330), -1));
    m_staticText_release_note->SetMinSize(wxSize(FromDIP(330), -1));
    m_staticText_release_note->SetLabelText(text);
    m_vebview_release_note->Layout();

    auto text_size = m_staticText_release_note->GetBestSize();
    if (text_size.y < FromDIP(360))
        m_vebview_release_note->SetMinSize(wxSize(FromDIP(360), text_size.y + FromDIP(25)));
    else {
        m_vebview_release_note->SetMinSize(wxSize(FromDIP(360), FromDIP(360)));
    }

    Layout();
    Fit();
}

void SecondaryCheckDialog::on_show()
{
    wxGetApp().UpdateFrameDarkUI(this);
    // recover button color
    wxMouseEvent evt_ok(wxEVT_LEFT_UP);
    m_button_ok->GetEventHandler()->ProcessEvent(evt_ok);
    wxMouseEvent evt_cancel(wxEVT_LEFT_UP);
    m_button_cancel->GetEventHandler()->ProcessEvent(evt_cancel);

    this->Show();
    this->Raise();
}

void SecondaryCheckDialog::on_hide()
{
    if (m_show_again_checkbox != nullptr && not_show_again && show_again_config_text != "")
        wxGetApp().app_config->set(show_again_config_text, "1");

    this->Hide();
    if (wxGetApp().mainframe != nullptr) {
        wxGetApp().mainframe->Show();
        wxGetApp().mainframe->Raise();
    }
}

void SecondaryCheckDialog::update_title_style(wxString title, SecondaryCheckDialog::ButtonStyle style, wxWindow* parent)
{
    if (m_button_style == style && title == GetTitle() == title) return;

    SetTitle(title);

    event_parent = parent;

    if (style == CONFIRM_AND_CANCEL) {
        m_button_cancel->Show();
        m_button_fn->Hide();
        m_button_retry->Hide();
        m_button_resume->Hide();
    }
    else if (style == CONFIRM_AND_DONE) {
        m_button_cancel->Hide();
        m_button_fn->Show();
        m_button_retry->Hide();
        m_button_resume->Hide();
    }
    else if (style == CONFIRM_AND_RETRY) {
        m_button_retry->Show();
        m_button_cancel->Hide();
        m_button_fn->Hide();
        m_button_resume->Hide();
    }
    else if (style == DONE_AND_RETRY) {
        m_button_retry->Show();
        m_button_fn->Show();
        m_button_cancel->Hide();
        m_button_resume->Hide();
    }
    else if(style == CONFIRM_AND_RESUME)
    {
        m_button_retry->Hide();
        m_button_fn->Hide();
        m_button_cancel->Hide();
        m_button_resume->Show();
    }
    else {
        m_button_retry->Hide();
        m_button_cancel->Hide();
        m_button_fn->Hide();
        m_button_resume->Hide();

    }


    Layout();
}

void SecondaryCheckDialog::update_btn_label(wxString ok_btn_text, wxString cancel_btn_text)
{
    m_button_ok->SetLabel(ok_btn_text);
    m_button_cancel->SetLabel(cancel_btn_text);
    rescale();
}

SecondaryCheckDialog::~SecondaryCheckDialog()
{

}

void SecondaryCheckDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    rescale(); 
}

void SecondaryCheckDialog::msw_rescale() { 
    wxGetApp().UpdateFrameDarkUI(this); 
    Refresh();
}

void SecondaryCheckDialog::rescale()
{
    m_button_ok->Rescale();
    m_button_cancel->Rescale();
}

PrintErrorDialog::PrintErrorDialog(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style)
    :DPIFrame(parent, id, title, pos, size, style)
{
    std::string icon_path = (boost::format("%1%/images/OrcaSlicerTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));
    SetBackgroundColour(*wxWHITE);

    btn_bg_white = StateColor(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(350), 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(5));

    wxBoxSizer* m_sizer_right = new wxBoxSizer(wxVERTICAL);

    m_sizer_right->Add(0, 0, 1, wxTOP, FromDIP(5));

    m_vebview_release_note = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_vebview_release_note->SetScrollRate(0, 5);
    m_vebview_release_note->SetBackgroundColour(*wxWHITE);
    m_vebview_release_note->SetMinSize(wxSize(FromDIP(320), FromDIP(250)));
    m_sizer_right->Add(m_vebview_release_note, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(15));

    m_error_prompt_pic_static = new wxStaticBitmap(m_vebview_release_note, wxID_ANY, wxBitmap(), wxDefaultPosition, wxSize(FromDIP(300), FromDIP(180)));

    auto bottom_sizer = new wxBoxSizer(wxVERTICAL);
    m_sizer_button = new wxBoxSizer(wxVERTICAL);

    bottom_sizer->Add(m_sizer_button, 0, wxEXPAND | wxRIGHT | wxLEFT, 0);

    m_sizer_right->Add(bottom_sizer, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(15));
    m_sizer_right->Add(0, 0, 0, wxTOP, FromDIP(10));

    m_sizer_main->Add(m_sizer_right, 0, wxBOTTOM | wxEXPAND, FromDIP(5));

    Bind(wxEVT_CLOSE_WINDOW, [this](auto& e) {this->on_hide(); });
    Bind(wxEVT_ACTIVATE, [this](auto& e) { if (!e.GetActive()) this->RequestUserAttention(wxUSER_ATTENTION_ERROR); });
    Bind(wxEVT_WEBREQUEST_STATE, &PrintErrorDialog::on_webrequest_state, this);


    SetSizer(m_sizer_main);
    Layout();
    m_sizer_main->Fit(this);

    init_button_list();

    CenterOnParent();
    wxGetApp().UpdateFrameDarkUI(this);
}

void PrintErrorDialog::post_event(wxCommandEvent&& event)
{
    if (event_parent) {
        event.SetString("");
        event.SetEventObject(event_parent);
        wxPostEvent(event_parent, event);
        event.Skip();
    }
}

void PrintErrorDialog::on_webrequest_state(wxWebRequestEvent& evt)
{
    BOOST_LOG_TRIVIAL(trace) << "monitor: monitor_panel web request state = " << evt.GetState();
    switch (evt.GetState()) {
    case wxWebRequest::State_Completed: {
            wxImage img(*evt.GetResponse().GetStream());
            wxImage resize_img = img.Scale(FromDIP(320), FromDIP(180), wxIMAGE_QUALITY_HIGH);
            wxBitmap error_prompt_pic = resize_img;
            m_error_prompt_pic_static->SetBitmap(error_prompt_pic);
            Layout();
            Fit();

        break;
    }
    case wxWebRequest::State_Failed:
    case wxWebRequest::State_Cancelled:
    case wxWebRequest::State_Unauthorized: {
        m_error_prompt_pic_static->SetBitmap(wxBitmap());
        break;
    }
    case wxWebRequest::State_Active:
    case wxWebRequest::State_Idle: break;
    default: break;
    }
}

void PrintErrorDialog::update_text_image(wxString text, wxString image_url)
{
    //if (!m_sizer_text_release_note) {
    //    m_sizer_text_release_note = new wxBoxSizer(wxVERTICAL);
    //}
    wxBoxSizer* sizer_text_release_note = new wxBoxSizer(wxVERTICAL);

    
    if (!m_staticText_release_note) {
        m_staticText_release_note = new Label(m_vebview_release_note, text, LB_AUTO_WRAP);
        sizer_text_release_note->Add(m_error_prompt_pic_static, 0, wxALIGN_CENTER, FromDIP(5));
        sizer_text_release_note->Add(m_staticText_release_note, 0, wxALIGN_CENTER , FromDIP(5));
        m_vebview_release_note->SetSizer(sizer_text_release_note);
    }
    if (!image_url.empty()) {
        web_request = wxWebSession::GetDefault().CreateRequest(this, image_url);
        BOOST_LOG_TRIVIAL(trace) << "monitor: create new webrequest, state = " << web_request.GetState() << ", url = " << image_url;
        if (web_request.GetState() == wxWebRequest::State_Idle)
            web_request.Start();
        BOOST_LOG_TRIVIAL(trace) << "monitor: start new webrequest, state = " << web_request.GetState() << ", url = " << image_url;
        m_error_prompt_pic_static->Show();

    }
    else {
        m_error_prompt_pic_static->Hide();
    }
    sizer_text_release_note->Layout();
    m_staticText_release_note->SetMaxSize(wxSize(FromDIP(300), -1));
    m_staticText_release_note->SetMinSize(wxSize(FromDIP(300), -1));
    m_staticText_release_note->SetLabelText(text);
    m_vebview_release_note->Layout();

    auto text_size = m_staticText_release_note->GetBestSize();
    if (text_size.y < FromDIP(360))
        if (!image_url.empty()) {
            m_vebview_release_note->SetMinSize(wxSize(FromDIP(320), text_size.y + FromDIP(220)));
        }
        else {
            m_vebview_release_note->SetMinSize(wxSize(FromDIP(320), text_size.y + FromDIP(25)));
        }
    else {
        m_vebview_release_note->SetMinSize(wxSize(FromDIP(320), FromDIP(340)));
    }

    Layout();
    Fit();
}

void PrintErrorDialog::on_show()
{
    wxGetApp().UpdateFrameDarkUI(this);

    this->Show();
    this->Raise();
}

void PrintErrorDialog::on_hide()
{
    //m_sizer_button->Clear();
    //m_sizer_button->Layout();
    //m_used_button.clear();
    this->Hide();
    if (web_request.IsOk() && web_request.GetState() == wxWebRequest::State_Active) {
        BOOST_LOG_TRIVIAL(info) << "web_request: cancelled";
        web_request.Cancel();
    }
    m_error_prompt_pic_static->SetBitmap(wxBitmap());

    if (wxGetApp().mainframe != nullptr) {
        wxGetApp().mainframe->Show();
        wxGetApp().mainframe->Raise();
    }
}

void PrintErrorDialog::update_title_style(wxString title, std::vector<int> button_style, wxWindow* parent)
{
    SetTitle(title);
    event_parent = parent;
    for (int used_button_id : m_used_button) {
        if (m_button_list.find(used_button_id) != m_button_list.end()) {
            m_button_list[used_button_id]->Hide();
        }
    }
    m_sizer_button->Clear();
    m_used_button = button_style;
    for (int button_id : button_style) {
        if (m_button_list.find(button_id) != m_button_list.end()) {
            m_sizer_button->Add(m_button_list[button_id], 0, wxALL, FromDIP(5));
            m_button_list[button_id]->Show();
        }
    }
    Layout();
    Fit();

}

void PrintErrorDialog::init_button(PrintErrorButton style,wxString buton_text) {
    Button* print_error_button = new Button(this, buton_text);
    print_error_button->SetBackgroundColor(btn_bg_white);
    print_error_button->SetBorderColor(wxColour(38, 46, 48));
    print_error_button->SetFont(Label::Body_14);
    print_error_button->SetSize(wxSize(FromDIP(300), FromDIP(30)));
    print_error_button->SetMinSize(wxSize(FromDIP(300), FromDIP(30)));
    print_error_button->SetMaxSize(wxSize(-1, FromDIP(30)));
    print_error_button->SetCornerRadius(FromDIP(5));
    print_error_button->Hide();
    m_button_list[style] = print_error_button;

}

void PrintErrorDialog::init_button_list() {
    
    init_button(RESUME_PRINTING, _L("Resume Printing"));
    m_button_list[RESUME_PRINTING]->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        post_event(wxCommandEvent(EVT_SECONDARY_CHECK_RESUME));
        e.Skip();
    });

    init_button(RESUME_PRINTING_DEFECTS, _L("Resume Printing(defects acceptable)"));
    m_button_list[RESUME_PRINTING_DEFECTS]->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        post_event(wxCommandEvent(EVT_SECONDARY_CHECK_RESUME));
        e.Skip();
    });


    init_button(RESUME_PRINTING_PROBELM_SOLVED, _L("Resume Printing(problem solved)"));
    m_button_list[RESUME_PRINTING_PROBELM_SOLVED]->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        post_event(wxCommandEvent(EVT_SECONDARY_CHECK_RESUME));
        e.Skip();
    });

    init_button(STOP_PRINTING, _L("Stop Printing"));
    m_button_list[STOP_PRINTING]->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        post_event(wxCommandEvent(EVT_PRINT_ERROR_STOP));
        e.Skip();
    });

    init_button(CHECK_ASSISTANT, _L("Check Assistant"));
    m_button_list[CHECK_ASSISTANT]->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        post_event(wxCommandEvent(EVT_JUMP_TO_HMS));
        this->on_hide();
    });

    init_button(FILAMENT_EXTRUDED, _L("Filament Extruded, Continue"));
    m_button_list[FILAMENT_EXTRUDED]->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        post_event(wxCommandEvent(EVT_SECONDARY_CHECK_DONE));
        e.Skip();
    });

    init_button(RETRY_FILAMENT_EXTRUDED, _L("Not Extruded Yet, Retry"));
    m_button_list[RETRY_FILAMENT_EXTRUDED]->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        wxCommandEvent evt(EVT_SECONDARY_CHECK_RETRY, GetId());
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
        this->on_hide();
    });

    init_button(CONTINUE, _L("Finished, Continue"));
    m_button_list[CONTINUE]->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        post_event(wxCommandEvent(EVT_SECONDARY_CHECK_DONE));
        e.Skip();
    });

    init_button(LOAD_VIRTUAL_TRAY, _L("Load Filament"));
    m_button_list[LOAD_VIRTUAL_TRAY]->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        post_event(wxCommandEvent(EVT_LOAD_VAMS_TRAY));
        e.Skip();
    });

    init_button(OK_BUTTON, _L("OK"));
    m_button_list[OK_BUTTON]->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        wxCommandEvent evt(EVT_SECONDARY_CHECK_CONFIRM, GetId());
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
        this->on_hide();
    });

    init_button(FILAMENT_LOAD_RESUME, _L("Filament Loaded, Resume"));
    m_button_list[FILAMENT_LOAD_RESUME]->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        post_event(wxCommandEvent(EVT_SECONDARY_CHECK_RESUME));
        e.Skip();
    });

    init_button(JUMP_TO_LIVEVIEW, _L("View Liveview"));
    m_button_list[JUMP_TO_LIVEVIEW]->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        post_event(wxCommandEvent(EVT_JUMP_TO_LIVEVIEW));
        e.Skip();
    });
}

PrintErrorDialog::~PrintErrorDialog()
{

}

void PrintErrorDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    rescale();
}

void PrintErrorDialog::msw_rescale() {
    wxGetApp().UpdateFrameDarkUI(this);
    Refresh();
}

void PrintErrorDialog::rescale()
{
    for(auto used_button:m_used_button)
        m_button_list[used_button]->Rescale();
}

ConfirmBeforeSendDialog::ConfirmBeforeSendDialog(wxWindow* parent, wxWindowID id, const wxString& title, enum ButtonStyle btn_style, const wxPoint& pos, const wxSize& size, long style, bool not_show_again_check)
    :DPIDialog(parent, id, title, pos, size, style)
{
    std::string icon_path = (boost::format("%1%/images/OrcaSlicerTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(400), 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(5));

    wxBoxSizer* m_sizer_right = new wxBoxSizer(wxVERTICAL);

    m_sizer_right->Add(0, 0, 1, wxTOP, FromDIP(15));

    m_vebview_release_note = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_vebview_release_note->SetScrollRate(0, 5);
    m_vebview_release_note->SetBackgroundColour(*wxWHITE);
    m_vebview_release_note->SetMinSize(wxSize(FromDIP(400), FromDIP(380)));
    m_sizer_right->Add(m_vebview_release_note, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(15));


    auto bottom_sizer = new wxBoxSizer(wxVERTICAL);
    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));


    if (not_show_again_check) {
        auto checkbox_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_show_again_checkbox = new wxCheckBox(this, wxID_ANY, _L("Don't show again"), wxDefaultPosition, wxDefaultSize, 0);
        m_show_again_checkbox->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, [this](wxCommandEvent& e) {
            not_show_again = !not_show_again;
            m_show_again_checkbox->SetValue(not_show_again);
        });
        checkbox_sizer->Add(FromDIP(15), 0, 0, 0);
        checkbox_sizer->Add(m_show_again_checkbox, 0, wxALL, FromDIP(5));
        bottom_sizer->Add(checkbox_sizer, 0, wxBOTTOM | wxEXPAND, 0);
    }
    m_button_ok = new Button(this, _L("Confirm"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(wxColour("#FFFFFE"));
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(-1, FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetCornerRadius(FromDIP(12));

    m_button_ok->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        wxCommandEvent evt(EVT_SECONDARY_CHECK_CONFIRM, GetId());
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
        this->on_hide();
    });

    m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(wxSize(-1, FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetCornerRadius(FromDIP(12));

    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        wxCommandEvent evt(EVT_SECONDARY_CHECK_CANCEL);
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
        this->on_hide();
        });

    if (btn_style != CONFIRM_AND_CANCEL)
        m_button_cancel->Hide();
    else
        m_button_cancel->Show();
    
    m_button_update_nozzle = new Button(this, _L("Confirm and Update Nozzle"));
    m_button_update_nozzle->SetBackgroundColor(btn_bg_white);
    m_button_update_nozzle->SetBorderColor(wxColour(38, 46, 48));
    m_button_update_nozzle->SetFont(Label::Body_12);
    m_button_update_nozzle->SetSize(wxSize(-1, FromDIP(24)));
    m_button_update_nozzle->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_update_nozzle->SetCornerRadius(FromDIP(12));

    m_button_update_nozzle->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        wxCommandEvent evt(EVT_UPDATE_NOZZLE);
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
        this->on_hide();
    });

    m_button_update_nozzle->Hide();

    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_ok, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_update_nozzle, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_cancel, 0, wxALL, FromDIP(5));
    sizer_button->Add(FromDIP(5),0, 0, 0);
    bottom_sizer->Add(sizer_button, 0, wxEXPAND | wxRIGHT | wxLEFT, 0);


    m_sizer_right->Add(bottom_sizer, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(20));
    m_sizer_right->Add(0, 0, 0, wxTOP, FromDIP(10));

    m_sizer_main->Add(m_sizer_right, 0, wxBOTTOM | wxEXPAND, FromDIP(5));

    Bind(wxEVT_CLOSE_WINDOW, [this](auto& e) {this->on_hide(); });

    SetSizer(m_sizer_main);
    Layout();
    m_sizer_main->Fit(this);

    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void ConfirmBeforeSendDialog::update_text(wxString text)
{
    wxBoxSizer* sizer_text_release_note = new wxBoxSizer(wxVERTICAL);
    if (!m_staticText_release_note){
        m_staticText_release_note = new Label(m_vebview_release_note, text, LB_AUTO_WRAP);
        wxBoxSizer* top_blank_sizer = new wxBoxSizer(wxVERTICAL);
        wxBoxSizer* bottom_blank_sizer = new wxBoxSizer(wxVERTICAL);
        top_blank_sizer->Add(FromDIP(5), 0, wxALIGN_CENTER | wxALL, FromDIP(5));
        bottom_blank_sizer->Add(FromDIP(5), 0, wxALIGN_CENTER | wxALL, FromDIP(5));

        sizer_text_release_note->Add(top_blank_sizer, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
        sizer_text_release_note->Add(m_staticText_release_note, 0, wxALIGN_CENTER, FromDIP(5));
        sizer_text_release_note->Add(bottom_blank_sizer, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
        m_vebview_release_note->SetSizer(sizer_text_release_note);
    }
    m_staticText_release_note->SetMaxSize(wxSize(FromDIP(380), -1));
    m_staticText_release_note->SetMinSize(wxSize(FromDIP(380), -1));
    m_staticText_release_note->SetLabelText(text);
    m_vebview_release_note->Layout();

    auto text_size = m_staticText_release_note->GetBestSize();
    if (text_size.y < FromDIP(380))
        m_vebview_release_note->SetMinSize(wxSize(FromDIP(400), text_size.y + FromDIP(25)));
    else {
        m_vebview_release_note->SetMinSize(wxSize(FromDIP(400), FromDIP(380)));
    }

    Layout();
    Fit();
}

void ConfirmBeforeSendDialog::update_text(std::vector<ConfirmBeforeSendInfo> texts)
{
    wxBoxSizer* sizer_text_release_note = new wxBoxSizer(wxVERTICAL);
    m_vebview_release_note->SetSizer(sizer_text_release_note);

    auto height = 0;
    for (auto text : texts) {
        auto label_item = new Label(m_vebview_release_note, text.text, LB_AUTO_WRAP);
        if (text.level == ConfirmBeforeSendInfo::InfoLevel::Warning) {
            label_item->SetForegroundColour(wxColour(0xFF, 0x6F, 0x00));
        }
        label_item->SetMaxSize(wxSize(FromDIP(380), -1));
        label_item->SetMinSize(wxSize(FromDIP(380), -1));
        label_item->Wrap(FromDIP(380));
        label_item->Layout();
        sizer_text_release_note->Add(label_item, 0, wxALIGN_CENTER | wxALL, FromDIP(3));
        height += label_item->GetSize().y;
    }
    
    m_vebview_release_note->Layout();
    if (height < FromDIP(380))
        m_vebview_release_note->SetMinSize(wxSize(FromDIP(400), height + FromDIP(25)));
    else {
        m_vebview_release_note->SetMinSize(wxSize(FromDIP(400), FromDIP(380)));
    }

    Layout();
    Fit();
}

void ConfirmBeforeSendDialog::on_show()
{
    wxGetApp().UpdateDlgDarkUI(this);
    // recover button color
    wxMouseEvent evt_ok(wxEVT_LEFT_UP);
    m_button_ok->GetEventHandler()->ProcessEvent(evt_ok);
    wxMouseEvent evt_cancel(wxEVT_LEFT_UP);
    m_button_cancel->GetEventHandler()->ProcessEvent(evt_cancel);
    this->ShowModal();
}

void ConfirmBeforeSendDialog::on_hide()
{
    if (m_show_again_checkbox != nullptr && not_show_again && show_again_config_text != "")
        wxGetApp().app_config->set(show_again_config_text, "1");
    EndModal(wxID_OK);
}

void ConfirmBeforeSendDialog::update_btn_label(wxString ok_btn_text, wxString cancel_btn_text)
{
    m_button_ok->SetLabel(ok_btn_text);
    m_button_cancel->SetLabel(cancel_btn_text);
    rescale();
}

wxString ConfirmBeforeSendDialog::format_text(wxString str, int warp)
{
    Label st (this, str);
    wxString out_txt      = str;
    wxString count_txt    = "";
    int      new_line_pos = 0;

    for (int i = 0; i < str.length(); i++) {
        auto text_size = st.GetTextExtent(count_txt);
        if (text_size.x < warp) {
            count_txt += str[i];
        } else {
            out_txt.insert(i - 1, '\n');
            count_txt = "";
        }
    }
    return out_txt;
}

ConfirmBeforeSendDialog::~ConfirmBeforeSendDialog()
{

}

void ConfirmBeforeSendDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    rescale();
}

void ConfirmBeforeSendDialog::show_update_nozzle_button(bool show)
{
    m_button_update_nozzle->Show(show);
    Layout();
}

void ConfirmBeforeSendDialog::hide_button_ok()
{
    m_button_ok->Hide();
}

void ConfirmBeforeSendDialog::edit_cancel_button_txt(wxString txt)
{
    m_button_cancel->SetLabel(txt);
}

void ConfirmBeforeSendDialog::disable_button_ok()
{
    m_button_ok->Disable();
    m_button_ok->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
    m_button_ok->SetBorderColor(wxColour(0x90, 0x90, 0x90));
}

void ConfirmBeforeSendDialog::enable_button_ok()
{
    m_button_ok->Enable();
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(btn_bg_green);
}

void ConfirmBeforeSendDialog::rescale()
{
    m_button_ok->Rescale();
    m_button_cancel->Rescale();
}

InputIpAddressDialog::InputIpAddressDialog(wxWindow* parent)
    :DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, _L("LAN Connection Failed (Sending print file)"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    std::string icon_path = (boost::format("%1%/images/OrcaSlicerTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    m_result = -1;
    wxBoxSizer* m_sizer_body = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* m_sizer_main = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer* m_sizer_main_left = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* m_sizer_main_right = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* m_sizer_msg = new wxBoxSizer(wxHORIZONTAL);
    auto        m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));

    comfirm_before_enter_text = _L("Step 1, please confirm Orca Slicer and your printer are in the same LAN.");
    comfirm_after_enter_text = _L("Step 2, if the IP and Access Code below are different from the actual values on your printer, please correct them.");


    m_tip1 = new Label(this, ::Label::Body_13, comfirm_before_enter_text, LB_AUTO_WRAP);
    m_tip1->SetMinSize(wxSize(FromDIP(352), -1));
    m_tip1->SetMaxSize(wxSize(FromDIP(352), -1));
    m_tip1->Wrap(FromDIP(352));

    auto        m_line_tips = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_tips->SetBackgroundColour(wxColour(0xEEEEEE));

    m_tip2 = new Label(this, ::Label::Body_13, comfirm_after_enter_text, LB_AUTO_WRAP);
    m_tip2->SetMinSize(wxSize(FromDIP(352), -1));
    m_tip2->SetMaxSize(wxSize(FromDIP(352), -1));

    auto m_input_tip_area = new wxBoxSizer(wxHORIZONTAL);
    auto m_input_area = new wxBoxSizer(wxHORIZONTAL);

    m_tips_ip = new Label(this, _L("IP"));
    m_tips_ip->SetMinSize(wxSize(FromDIP(168), -1));
    m_tips_ip->SetMaxSize(wxSize(FromDIP(168), -1));

    m_input_ip = new TextInput(this, wxEmptyString, wxEmptyString);
    m_input_ip->Bind(wxEVT_TEXT, &InputIpAddressDialog::on_text, this);
    m_input_ip->SetMinSize(wxSize(FromDIP(168), FromDIP(28)));
    m_input_ip->SetMaxSize(wxSize(FromDIP(168), FromDIP(28)));

    m_tips_access_code = new Label(this, _L("Access Code"));
    m_tips_access_code->SetMinSize(wxSize(FromDIP(168),-1));
    m_tips_access_code->SetMaxSize(wxSize(FromDIP(168),-1));

    m_input_access_code = new TextInput(this, wxEmptyString, wxEmptyString);
    m_input_access_code->Bind(wxEVT_TEXT, &InputIpAddressDialog::on_text, this);
    m_input_access_code->SetMinSize(wxSize(FromDIP(168), FromDIP(28)));
    m_input_access_code->SetMaxSize(wxSize(FromDIP(168), FromDIP(28)));

    m_input_tip_area->Add(m_tips_ip, 0, wxALIGN_CENTER, 0);
    m_input_tip_area->Add(0, 0, 0, wxLEFT, FromDIP(16));
    m_input_tip_area->Add(m_tips_access_code, 0, wxALIGN_CENTER, 0);
   
    m_input_area->Add(m_input_ip, 0, wxALIGN_CENTER, 0);
    m_input_area->Add(0, 0, 0, wxLEFT, FromDIP(16));
    m_input_area->Add(m_input_access_code, 0, wxALIGN_CENTER, 0);

    m_test_right_msg = new Label(this, Label::Body_13, wxEmptyString, LB_AUTO_WRAP);
    m_test_right_msg->SetForegroundColour(wxColour(38, 166, 154));
    m_test_right_msg->Hide();

    
    m_test_wrong_msg = new Label(this, Label::Body_13, wxEmptyString, LB_AUTO_WRAP);
    m_test_wrong_msg->SetForegroundColour(wxColour(208, 27, 27));
    m_test_wrong_msg->Hide();

    m_tip3 = new Label(this, Label::Body_12, _L("Where to find your printer's IP and Access Code?"), LB_AUTO_WRAP);
    m_tip3->SetMinSize(wxSize(FromDIP(352), -1));
    m_tip3->SetMaxSize(wxSize(FromDIP(352), -1));

    m_tip4 = new Label(this, ::Label::Body_13, _L("Step 3: Ping the IP address to check for packet loss and latency."), LB_AUTO_WRAP);
    m_tip4->SetMinSize(wxSize(FromDIP(352), -1));
    m_tip4->SetMaxSize(wxSize(FromDIP(352), -1));
    
    m_trouble_shoot = new wxHyperlinkCtrl(this, wxID_ANY, "How to trouble shooting", "");

    m_img_help = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("input_access_code_x1_en", this, 198), wxDefaultPosition, wxSize(FromDIP(352), -1), 0);
    

    auto m_sizer_button = new wxBoxSizer(wxHORIZONTAL);

    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    m_button_ok = new Button(this, _L("Test"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(wxColour(0xFFFFFE));
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetCornerRadius(FromDIP(12));
   

    m_button_ok->Bind(wxEVT_LEFT_DOWN, &InputIpAddressDialog::on_ok, this);

    auto m_button_cancel = new Button(this, _L("Close"));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetCornerRadius(FromDIP(12));

    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
         on_cancel();
    });

    m_sizer_button->AddStretchSpacer();
    //m_sizer_button->Add(m_button_ok, 0, wxALL, FromDIP(5));
    m_sizer_button->Add(m_button_cancel, 0, wxALL, FromDIP(5));
    m_sizer_button->Layout();


    m_status_bar    = std::make_shared<BBLStatusBarSend>(this);
    m_status_bar->get_panel()->Hide();

    m_worker = std::make_unique<PlaterWorker<BoostThreadWorker>>(this, m_status_bar, "send_worker");

    auto m_step_icon_panel1 = new wxWindow(this, wxID_ANY);
    auto m_step_icon_panel2 = new wxWindow(this, wxID_ANY);
   
    m_step_icon_panel1->SetBackgroundColour(*wxWHITE);
    m_step_icon_panel2->SetBackgroundColour(*wxWHITE);
   

    auto m_sizer_step_icon_panel1 = new wxBoxSizer(wxVERTICAL);
    auto m_sizer_step_icon_panel2 = new wxBoxSizer(wxVERTICAL);
  


    m_img_step1 = new wxStaticBitmap(m_step_icon_panel1, wxID_ANY, create_scaled_bitmap("ip_address_step", this, 6), wxDefaultPosition, wxSize(FromDIP(6), FromDIP(6)), 0);

    auto        m_line_tips_left = new wxPanel(m_step_icon_panel1, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_tips_left->SetBackgroundColour(wxColour(0xEEEEEE));
    m_img_step2 = new wxStaticBitmap(m_step_icon_panel2, wxID_ANY, create_scaled_bitmap("ip_address_step", this, 6), wxDefaultPosition, wxSize(FromDIP(6), FromDIP(6)), 0);
    m_img_step3 = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("ip_address_step", this, 6), wxDefaultPosition, wxSize(FromDIP(6), FromDIP(6)), 0);

    m_sizer_step_icon_panel1->Add(m_img_step1, 0, wxALIGN_CENTER|wxALL, FromDIP(5));
   

    m_step_icon_panel1->SetSizer(m_sizer_step_icon_panel1);
    m_step_icon_panel1->Layout();
    m_step_icon_panel1->Fit();

    m_step_icon_panel2->SetSizer(m_sizer_step_icon_panel2);
    m_step_icon_panel2->Layout();
    m_step_icon_panel2->Fit();



    m_sizer_step_icon_panel2->Add(m_img_step2, 0, wxALIGN_CENTER|wxALL, FromDIP(5));
    //m_sizer_step_icon_panel3->Add(m_img_step3, 0, wxALIGN_CENTER|wxALL, FromDIP(5));

    m_step_icon_panel1->SetMinSize(wxSize(-1, m_tip1->GetBestSize().y));
    m_step_icon_panel1->SetMaxSize(wxSize(-1, m_tip1->GetBestSize().y));

    m_sizer_msg->Add(0, 0, 0, wxALIGN_CENTER, FromDIP(20));
    m_sizer_msg->Add(m_img_step3, 0, wxALL, FromDIP(5));
    m_sizer_msg->Add(0, 0, 0, wxALIGN_CENTER, FromDIP(8));
    m_sizer_msg->Add(m_tip4, 0, wxALIGN_CENTER | wxEXPAND | wxLEFT, FromDIP(5));
    m_img_step3->Hide();
    m_tip4->Hide();
    m_sizer_msg->Layout();

    m_sizer_main_left->Add(m_step_icon_panel1, 0, wxEXPAND, 0);
    m_sizer_main_left->Add(0, 0, 0, wxTOP, FromDIP(20));
    m_sizer_main_left->Add(m_line_tips_left, 1, wxEXPAND, 0);
    m_sizer_main_left->Add(0, 0, 0, wxTOP, FromDIP(20));
    m_sizer_main_left->Add(m_step_icon_panel2, 0, wxEXPAND, 0);

    m_sizer_main_left->Layout();
    
    m_trouble_shoot->Hide();
   
    m_sizer_main_right->Add(m_tip1, 0, wxRIGHT|wxEXPAND, FromDIP(18));
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(20));
    m_sizer_main_right->Add(m_line_tips, 0, wxRIGHT|wxEXPAND, FromDIP(18));
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(20));
    m_sizer_main_right->Add(m_tip2, 0, wxRIGHT|wxEXPAND, FromDIP(18));
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(12));
    m_sizer_main_right->Add(m_tip3, 0, wxRIGHT | wxEXPAND, FromDIP(18));
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_sizer_main_right->Add(m_img_help, 0, 0, 0);
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(12));
    m_sizer_main_right->Add(m_input_tip_area, 0, wxRIGHT|wxEXPAND, FromDIP(18));
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_sizer_main_right->Add(m_input_area, 0, wxRIGHT|wxEXPAND, FromDIP(18));
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_sizer_main_right->Add(m_button_ok, 0,  wxRIGHT, FromDIP(18));
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_sizer_main_right->Add(m_test_right_msg, 0, wxRIGHT|wxEXPAND, FromDIP(18));
    m_sizer_main_right->Add(m_test_wrong_msg, 0, wxRIGHT|wxEXPAND, FromDIP(18));

    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(16));
    m_sizer_main_right->Add(m_status_bar->get_panel(), 0,wxRIGHT|wxEXPAND, FromDIP(18));
    m_sizer_main_right->Layout();
   
    m_sizer_main->Add(m_sizer_main_left, 0, wxLEFT, FromDIP(18));
    m_sizer_main->Add(m_sizer_main_right, 0, wxLEFT|wxEXPAND, FromDIP(4));
    m_sizer_main->Layout();

    m_sizer_body->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(20));
    m_sizer_body->Add(m_sizer_main, 0, wxEXPAND, 0);
    m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_sizer_body->Add(m_sizer_msg, 0, wxLEFT|wxEXPAND, FromDIP(18));
    m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_sizer_body->Add(m_trouble_shoot, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(40));

    m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(8));
    m_sizer_body->Add(m_sizer_button, 0, wxRIGHT | wxEXPAND, FromDIP(25));
    m_sizer_body->Layout();
    
    SetSizer(m_sizer_body);
    Layout();
    Fit();

    CentreOnParent(wxBOTH);
    Move(wxPoint(GetScreenPosition().x, GetScreenPosition().y - FromDIP(50)));
    wxGetApp().UpdateDlgDarkUI(this);

    Bind(EVT_CHECK_IP_ADDRESS_FAILED, &InputIpAddressDialog::on_check_ip_address_failed, this);

    Bind(EVT_CLOSE_IPADDRESS_DLG, [this](auto& e) {
        m_status_bar->reset();
        EndModal(wxID_YES);
    });
    Bind(wxEVT_CLOSE_WINDOW, [this](auto& e) {on_cancel();});

}

void InputIpAddressDialog::on_cancel()
{
    m_worker->cancel_all();
    if (m_result == 0){
        this->EndModal(wxID_YES);
    }else {
        this->EndModal(wxID_CANCEL);
    }
}


void InputIpAddressDialog::update_title(wxString title)
{
    SetTitle(title);
}

void InputIpAddressDialog::set_machine_obj(MachineObject* obj)
{
    m_obj = obj;
    m_input_ip->GetTextCtrl()->SetLabelText(m_obj->dev_ip);
    m_input_access_code->GetTextCtrl()->SetLabelText(m_obj->get_access_code());

    std::string img_str = DeviceManager::get_printer_diagram_img(m_obj->printer_type);
    auto diagram_bmp = create_scaled_bitmap(img_str + "_en", this, 198);
    m_img_help->SetBitmap(diagram_bmp);

    
    auto str_ip = m_input_ip->GetTextCtrl()->GetValue();
    auto str_access_code = m_input_access_code->GetTextCtrl()->GetValue();
    if (isIp(str_ip.ToStdString()) && str_access_code.Length() == 8) {
        m_button_ok->Enable(true);
        StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
            std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
        m_button_ok->SetTextColor(StateColor::darkModeColorFor("#FFFFFE"));
        m_button_ok->SetBackgroundColor(btn_bg_green);
    }
    else {
        m_button_ok->Enable(false);
        m_button_ok->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
        m_button_ok->SetBorderColor(wxColour(0x90, 0x90, 0x90));
    }

    Layout();
    Fit();
}

void InputIpAddressDialog::update_test_msg(wxString msg,bool connected)
{
    if (msg.empty()) {
        m_test_right_msg->Hide();
        m_test_wrong_msg->Hide();
    }
    else {
         if(connected){
             m_test_right_msg->Show();
             m_test_right_msg->SetLabelText(msg);
             m_test_right_msg->SetMinSize(wxSize(FromDIP(352), -1));
             m_test_right_msg->SetMaxSize(wxSize(FromDIP(352), -1));
         }
         else{
             m_test_wrong_msg->Show();
             m_test_wrong_msg->SetLabelText(msg);
             m_test_wrong_msg->SetMinSize(wxSize(FromDIP(352), -1));
             m_test_wrong_msg->SetMaxSize(wxSize(FromDIP(352), -1));
         }

         
    }

    Layout();
    Fit();
}

bool InputIpAddressDialog::isIp(std::string ipstr)
{
    istringstream ipstream(ipstr);
    int num[4];
    char point[3];
    string end;
    ipstream >> num[0] >> point[0] >> num[1] >> point[1] >> num[2] >> point[2] >> num[3] >> end;
    for (int i = 0; i < 3; ++i) {
        if (num[i] < 0 || num[i]>255) return false;
        if (point[i] != '.') return false;
    }
    if (num[3] < 0 || num[3]>255) return false;
    if (!end.empty()) return false;
    return true;
}

void InputIpAddressDialog::on_ok(wxMouseEvent& evt)
{
    m_test_right_msg->Hide();
    m_test_wrong_msg->Hide();
    m_img_step3->Hide();
    m_tip4->Hide();
    m_trouble_shoot->Hide();
    Layout();
    Fit();
    wxString ip = m_input_ip->GetTextCtrl()->GetValue();
    wxString str_access_code = m_input_access_code->GetTextCtrl()->GetValue();

    //check support function
    if (!m_obj) return;
    if (!m_obj->is_support_send_to_sdcard) {
        wxString input_str = wxString::Format("%s|%s", ip, str_access_code);
        auto event = wxCommandEvent(EVT_ENTER_IP_ADDRESS);
        event.SetString(input_str);
        event.SetEventObject(this);
        wxPostEvent(this, event);

        auto event_close = wxCommandEvent(EVT_CLOSE_IPADDRESS_DLG);
        event_close.SetEventObject(this);
        wxPostEvent(this, event_close);
        return;
    }

    m_button_ok->Enable(false);
    m_button_ok->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
    m_button_ok->SetBorderColor(wxColour(0x90, 0x90, 0x90));

    m_worker->wait_for_idle();

    m_status_bar->reset();
    m_status_bar->set_prog_block();
    m_status_bar->set_cancel_callback_fina([this]() {
        BOOST_LOG_TRIVIAL(info) << "print_job: enter canceled";
        m_worker->cancel_all();
   });


    auto m_send_job = std::make_unique<SendJob>(m_obj->dev_id);
    m_send_job->m_dev_ip = ip.ToStdString();
    m_send_job->m_access_code = str_access_code.ToStdString();


#if !BBL_RELEASE_TO_PUBLIC
    m_send_job->m_local_use_ssl_for_mqtt = wxGetApp().app_config->get("enable_ssl_for_mqtt") == "true" ? true : false;
    m_send_job->m_local_use_ssl_for_ftp = wxGetApp().app_config->get("enable_ssl_for_ftp") == "true" ? true : false;
#else
    m_send_job->m_local_use_ssl_for_mqtt = m_obj->local_use_ssl_for_mqtt;
    m_send_job->m_local_use_ssl_for_ftp = m_obj->local_use_ssl_for_ftp;
#endif

    m_send_job->connection_type = m_obj->connection_type();
    m_send_job->cloud_print_only = true;
    m_send_job->has_sdcard = m_obj->has_sdcard();
    m_send_job->set_check_mode();
    m_send_job->set_project_name("verify_job");

    m_send_job->on_check_ip_address_fail([this](int result) {
        this->check_ip_address_failed(result);
    });

    m_send_job->on_check_ip_address_success([this, ip, str_access_code]() {
        wxString input_str = wxString::Format("%s|%s", ip, str_access_code);
        auto event = wxCommandEvent(EVT_ENTER_IP_ADDRESS);
        event.SetString(input_str);
        event.SetEventObject(this);
        wxPostEvent(this, event);
        m_result = 0;
        
        update_test_msg(_L("IP and Access Code Verified! You may close the window"), true);
       
    });

    replace_job(*m_worker, std::move(m_send_job));
}

void InputIpAddressDialog::check_ip_address_failed(int result)
{
    auto evt = new wxCommandEvent(EVT_CHECK_IP_ADDRESS_FAILED);
    evt->SetInt(result);
    wxQueueEvent(this, evt);
}

void InputIpAddressDialog::on_check_ip_address_failed(wxCommandEvent& evt)
{
    m_result = evt.GetInt();
    if (m_result == -2) {
        update_test_msg(_L("Connection failed, please double check IP and Access Code"), false);
    }
    else {
        update_test_msg(_L("Connection failed! If your IP and Access Code is correct, \nplease move to step 3 for troubleshooting network issues"), false);
        m_img_step3->Show();
        m_tip4->Show();
        //m_trouble_shoot->Show();
        Layout();
        Fit();
    }
    
    m_button_ok->Enable(true);
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
    m_button_ok->SetTextColor(StateColor::darkModeColorFor("#FFFFFE"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
}

void InputIpAddressDialog::on_text(wxCommandEvent& evt)
{
    auto str_ip = m_input_ip->GetTextCtrl()->GetValue();
    auto str_access_code = m_input_access_code->GetTextCtrl()->GetValue();

    if (isIp(str_ip.ToStdString()) && str_access_code.Length() == 8) {
        m_button_ok->Enable(true);
        StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
            std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
        m_button_ok->SetTextColor(StateColor::darkModeColorFor("#FFFFFE"));
        m_button_ok->SetBackgroundColor(btn_bg_green);
    }
    else {
        m_button_ok->Enable(false);
        m_button_ok->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
        m_button_ok->SetBorderColor(wxColour(0x90, 0x90, 0x90));
    }
}

InputIpAddressDialog::~InputIpAddressDialog()
{

}

void InputIpAddressDialog::on_dpi_changed(const wxRect& suggested_rect)
{

}


 }} // namespace Slic3r::GUI
