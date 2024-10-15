#include "NetworkTestDialog.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "slic3r/Utils/Http.hpp"
#include "libslic3r/AppConfig.hpp"
#include <boost/asio/ip/address.hpp>
#include <boost/log/trivial.hpp>

namespace Slic3r {
namespace GUI {

wxDECLARE_EVENT(EVT_UPDATE_RESULT, wxCommandEvent);

wxDEFINE_EVENT(EVT_UPDATE_RESULT, wxCommandEvent);

static wxString NA_STR = _L("N/A");

NetworkTestDialog::NetworkTestDialog(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style)
    : DPIDialog(parent,wxID_ANY,from_u8((boost::format(_utf8(L("Network Test")))).str()),wxDefaultPosition,
            wxSize(1000, 700),
            /*wxCAPTION*/wxDEFAULT_DIALOG_STYLE|wxMAXIMIZE_BOX|wxMINIMIZE_BOX|wxRESIZE_BORDER)
{
    this->SetBackgroundColour(wxColour(255, 255, 255));

	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* main_sizer;
	main_sizer = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* top_sizer = create_top_sizer(this);
	main_sizer->Add(top_sizer, 0, wxEXPAND, 5);

	wxBoxSizer* info_sizer = create_info_sizer(this);
	main_sizer->Add(info_sizer, 0, wxEXPAND, 5);

	wxBoxSizer* content_sizer = create_content_sizer(this);
	main_sizer->Add(content_sizer, 0, wxEXPAND, 5);

	wxBoxSizer* result_sizer = create_result_sizer(this);
	main_sizer->Add(result_sizer, 1, wxEXPAND, 5);

	set_default();

	init_bind();

	this->SetSizer(main_sizer);
	this->Layout();

	this->Centre(wxBOTH);
    wxGetApp().UpdateDlgDarkUI(this);
}

wxBoxSizer* NetworkTestDialog::create_top_sizer(wxWindow* parent)
{
    StateColor btn_bg(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered), std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Enabled));
	auto sizer = new wxBoxSizer(wxVERTICAL);

	auto line_sizer = new wxBoxSizer(wxHORIZONTAL);
	btn_start = new Button(this, _L("Start Test Multi-Thread"));
    btn_start->SetBackgroundColor(btn_bg);
	line_sizer->Add(btn_start, 0, wxALL, 5);

	btn_start_sequence = new Button(this, _L("Start Test Single-Thread"));
    btn_start_sequence->SetBackgroundColor(btn_bg);

	line_sizer->Add(btn_start_sequence, 0, wxALL, 5);

	btn_download_log = new Button(this, _L("Export Log"));
    btn_download_log->SetBackgroundColor(btn_bg);
	line_sizer->Add(btn_download_log, 0, wxALL, 5);
	btn_download_log->Hide();

	btn_start->Bind(wxEVT_BUTTON, [this](wxCommandEvent &evt) {
			start_all_job();
		});
	btn_start_sequence->Bind(wxEVT_BUTTON, [this](wxCommandEvent &evt) {
			start_all_job_sequence();
		});
	sizer->Add(line_sizer, 0, wxEXPAND, 5);
	return sizer;
}

wxBoxSizer* NetworkTestDialog::create_info_sizer(wxWindow* parent)
{
	auto sizer = new wxBoxSizer(wxVERTICAL);

	text_basic_info = new wxStaticText(this, wxID_ANY, _L("Basic Info"), wxDefaultPosition, wxDefaultSize, 0);
	text_basic_info->Wrap(-1);
	sizer->Add(text_basic_info, 0, wxALL, 5);

	wxBoxSizer* version_sizer = new wxBoxSizer(wxHORIZONTAL);
	text_version_title = new wxStaticText(this, wxID_ANY, _L("Studio Version:"), wxDefaultPosition, wxDefaultSize, 0);
	text_version_title->Wrap(-1);
	version_sizer->Add(text_version_title, 0, wxALL, 5);

	wxString text_version = get_studio_version();
	text_version_val = new wxStaticText(this, wxID_ANY, text_version, wxDefaultPosition, wxDefaultSize, 0);
	text_version_val->Wrap(-1);
	version_sizer->Add(text_version_val, 0, wxALL, 5);
	sizer->Add(version_sizer, 1, wxEXPAND, 5);

	wxBoxSizer* sys_sizer = new wxBoxSizer(wxHORIZONTAL);

	txt_sys_info_title = new wxStaticText(this, wxID_ANY, _L("System Version:"), wxDefaultPosition, wxDefaultSize, 0);
	txt_sys_info_title->Wrap(-1);
	sys_sizer->Add(txt_sys_info_title, 0, wxALL, 5);

	txt_sys_info_value = new wxStaticText(this, wxID_ANY, get_os_info(), wxDefaultPosition, wxDefaultSize, 0);
	txt_sys_info_value->Wrap(-1);
	sys_sizer->Add(txt_sys_info_value, 0, wxALL, 5);

	sizer->Add(sys_sizer, 1, wxEXPAND, 5);

	wxBoxSizer* line_sizer = new wxBoxSizer(wxHORIZONTAL);
	txt_dns_info_title = new wxStaticText(this, wxID_ANY, _L("DNS Server:"), wxDefaultPosition, wxDefaultSize, 0);
	txt_dns_info_title->Wrap(-1);
	txt_dns_info_title->Hide();
	line_sizer->Add(txt_dns_info_title, 0, wxALL, 5);

	txt_dns_info_value = new wxStaticText(this, wxID_ANY, get_dns_info(), wxDefaultPosition, wxDefaultSize, 0);
	txt_dns_info_value->Hide();
	line_sizer->Add(txt_dns_info_value, 0, wxALL, 5);
	sizer->Add(line_sizer, 1, wxEXPAND, 5);

	return sizer;
}

wxBoxSizer* NetworkTestDialog::create_content_sizer(wxWindow* parent)
{
	auto sizer = new wxBoxSizer(wxVERTICAL);

	wxFlexGridSizer* grid_sizer;
	grid_sizer = new wxFlexGridSizer(0, 3, 0, 0);
	grid_sizer->SetFlexibleDirection(wxBOTH);
	grid_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    StateColor btn_bg(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered), std::pair<wxColour, int>(wxColour(255,255,255), StateColor::Enabled));
	btn_link = new Button(this, _L("Test BambuLab"));
    btn_link->SetBackgroundColor(btn_bg);
	grid_sizer->Add(btn_link, 0, wxEXPAND | wxALL, 5);

	text_link_title = new wxStaticText(this, wxID_ANY, _L("Test BambuLab:"), wxDefaultPosition, wxDefaultSize, 0);
	text_link_title->Wrap(-1);
	grid_sizer->Add(text_link_title, 0, wxALIGN_RIGHT | wxALL, 5);

	text_link_val = new wxStaticText(this, wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0);
	text_link_val->Wrap(-1);
	grid_sizer->Add(text_link_val, 0, wxALL, 5);

	btn_bing = new Button(this, _L("Test Bing.com"));
    btn_bing->SetBackgroundColor(btn_bg);
	grid_sizer->Add(btn_bing, 0, wxEXPAND | wxALL, 5);

    text_bing_title = new wxStaticText(this, wxID_ANY, _L("Test bing.com:"), wxDefaultPosition, wxDefaultSize, 0);

	text_bing_title->Wrap(-1);
	grid_sizer->Add(text_bing_title, 0, wxALIGN_RIGHT | wxALL, 5);

	text_bing_val = new wxStaticText(this, wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0);
	text_bing_val->Wrap(-1);
	grid_sizer->Add(text_bing_val, 0, wxALL, 5);

	btn_iot = new Button(this, _L("Test HTTP"));
    btn_iot->SetBackgroundColor(btn_bg);
	grid_sizer->Add(btn_iot, 0, wxEXPAND | wxALL, 5);

	text_iot_title = new wxStaticText(this, wxID_ANY, _L("Test HTTP Service:"), wxDefaultPosition, wxDefaultSize, 0);
	text_iot_title->Wrap(-1);
	grid_sizer->Add(text_iot_title, 0, wxALIGN_RIGHT | wxALL, 5);

	text_iot_value = new wxStaticText(this, wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0);
	text_iot_value->Wrap(-1);
	grid_sizer->Add(text_iot_value, 0, wxALL, 5);

	btn_oss = new Button(this, _L("Test storage"));
    btn_oss->SetBackgroundColor(btn_bg);
	grid_sizer->Add(btn_oss, 0, wxEXPAND | wxALL, 5);

	text_oss_title = new wxStaticText(this, wxID_ANY, _L("Test Storage Upload:"), wxDefaultPosition, wxDefaultSize, 0);
	text_oss_title->Wrap(-1);
	grid_sizer->Add(text_oss_title, 0, wxALIGN_RIGHT | wxALL, 5);

	text_oss_value = new wxStaticText(this, wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0);
	text_oss_value->Wrap(-1);
	grid_sizer->Add(text_oss_value, 0, wxALL, 5);

	btn_oss_upgrade = new Button(this, _L("Test storage upgrade"));
    btn_oss_upgrade->SetBackgroundColor(btn_bg);
	grid_sizer->Add(btn_oss_upgrade, 0, wxEXPAND | wxALL, 5);

	text_oss_upgrade_title = new wxStaticText(this, wxID_ANY, _L("Test Storage Upgrade:"), wxDefaultPosition, wxDefaultSize, 0);
	text_oss_upgrade_title->Wrap(-1);
	grid_sizer->Add(text_oss_upgrade_title, 0, wxALIGN_RIGHT | wxALL, 5);

	text_oss_upgrade_value = new wxStaticText(this, wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0);
	text_oss_upgrade_value->Wrap(-1);
	grid_sizer->Add(text_oss_upgrade_value, 0, wxALL, 5);

	btn_oss_download = new Button(this, _L("Test storage download"));
    btn_oss_download->SetBackgroundColor(btn_bg);
	grid_sizer->Add(btn_oss_download, 0, wxEXPAND | wxALL, 5);

	text_oss_download_title = new wxStaticText(this, wxID_ANY, _L("Test Storage Download:"), wxDefaultPosition, wxDefaultSize, 0);
	text_oss_download_title->Wrap(-1);
	grid_sizer->Add(text_oss_download_title, 0, wxALIGN_RIGHT | wxALL, 5);

	text_oss_download_value = new wxStaticText(this, wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0);
	text_oss_download_value->Wrap(-1);
	grid_sizer->Add(text_oss_download_value, 0, wxALL, 5);

	btn_network_plugin=new Button(this, _L("Test plugin download"));
    btn_network_plugin->SetBackgroundColor(btn_bg);
	grid_sizer->Add(btn_network_plugin, 0, wxEXPAND | wxALL, 5);

	text_network_plugin_title=new wxStaticText(this, wxID_ANY, _L("Test Plugin Download:"), wxDefaultPosition, wxDefaultSize, 0);
	text_network_plugin_title->Wrap(-1);
	grid_sizer->Add(text_network_plugin_title, 0, wxALIGN_RIGHT | wxALL, 5);

	text_network_plugin_value=new wxStaticText(this, wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0);
	text_network_plugin_value->Wrap(-1);
	grid_sizer->Add(text_network_plugin_value, 0, wxALL, 5);


	btn_oss_upload = new Button(this, _L("Test Storage Upload"));
    btn_oss_upload->SetBackgroundColor(btn_bg);
	grid_sizer->Add(btn_oss_upload, 0, wxEXPAND | wxALL, 5);

	text_oss_upload_title = new wxStaticText(this, wxID_ANY, _L("Test Storage Upload:"), wxDefaultPosition, wxDefaultSize, 0);
	text_oss_upload_title->Wrap(-1);
	grid_sizer->Add(text_oss_upload_title, 0, wxALIGN_RIGHT | wxALL, 5);

	text_oss_upload_value = new wxStaticText(this, wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0);
	text_oss_upload_value->Wrap(-1);
	grid_sizer->Add(text_oss_upload_value, 0, wxALL, 5);

	btn_oss_upload->Hide();
	text_oss_upload_title->Hide();
	text_oss_upload_value->Hide();

	sizer->Add(grid_sizer, 1, wxEXPAND, 5);

	btn_link->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
		start_test_bambulab_thread();
	});

	btn_bing->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
		start_test_bing_thread();
	});

	btn_iot->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
		start_test_iot_thread();
	});

	btn_oss->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
		start_test_oss_thread();
	});

	btn_oss_upgrade->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
		start_test_oss_upgrade_thread();
	});

	btn_oss_download->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
		start_test_oss_download_thread();
	});

	btn_network_plugin->Bind(wxEVT_BUTTON, [this](wxCommandEvent &evt) {
		start_test_plugin_download_thread(); 
	});

	return sizer;
}
wxBoxSizer* NetworkTestDialog::create_result_sizer(wxWindow* parent)
{
	auto sizer = new wxBoxSizer(wxVERTICAL);
	text_result = new wxStaticText(this, wxID_ANY, _L("Log Info"), wxDefaultPosition, wxDefaultSize, 0);
	text_result->Wrap(-1);
	sizer->Add(text_result, 0, wxALL, 5);

	txt_log = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
	sizer->Add(txt_log, 1, wxALL | wxEXPAND, 5);
	return sizer;
}

NetworkTestDialog::~NetworkTestDialog()
{
    ;
}

void NetworkTestDialog::init_bind()
{
	Bind(EVT_UPDATE_RESULT, [this](wxCommandEvent& evt) {
		if (evt.GetInt() == TEST_BAMBULAB_JOB) {
			text_link_val->SetLabelText(evt.GetString());
		} else if (evt.GetInt() == TEST_BING_JOB) {
			text_bing_val->SetLabelText(evt.GetString());
		} else if (evt.GetInt() == TEST_IOT_JOB) {
			text_iot_value->SetLabelText(evt.GetString());
		} else if (evt.GetInt() == TEST_OSS_JOB) {
			text_oss_value->SetLabelText(evt.GetString());
		} else if (evt.GetInt() == TEST_OSS_UPGRADE_JOB) {
			text_oss_upgrade_value->SetLabelText(evt.GetString());
		} else if (evt.GetInt() == TEST_OSS_DOWNLOAD_JOB) {
			text_oss_download_value->SetLabelText(evt.GetString());
		} else if (evt.GetInt() == TEST_OSS_UPLOAD_JOB) {
			text_oss_upload_value->SetLabelText(evt.GetString());
		} else if (evt.GetInt() == TEST_PLUGIN_JOB){
			text_network_plugin_value->SetLabelText(evt.GetString());
        }

		std::time_t t = std::time(0);
		std::tm* now_time = std::localtime(&t);
		std::stringstream buf;
		buf << std::put_time(now_time, "%a %b %d %H:%M:%S");
		wxString info = wxString::Format("%s:", buf.str()) + evt.GetString() + "\n";
		try {
			txt_log->AppendText(info);
		}
		catch (std::exception& e) {
			BOOST_LOG_TRIVIAL(error) << "Unkown Exception in print_log, exception=" << e.what();
			return;
		}
		catch (...) {
			BOOST_LOG_TRIVIAL(error) << "Unkown Exception in print_log";
			return;
		}
		return;
	});

	Bind(wxEVT_CLOSE_WINDOW, &NetworkTestDialog::on_close, this);
}

wxString NetworkTestDialog::get_os_info()
{
	int major = 0, minor = 0, micro = 0;
	wxGetOsVersion(&major, &minor, &micro);
	std::string os_version = (boost::format("%1%.%2%.%3%") % major % minor % micro).str();
	wxString text_sys_version = wxGetOsDescription() + wxString::Format("%d.%d.%d", major, minor, micro);
	return text_sys_version;
}


wxString NetworkTestDialog::get_dns_info()
{
	return NA_STR;
}

void NetworkTestDialog::start_all_job()
{
	start_test_bambulab_thread();
	start_test_bing_thread();
	
	start_test_iot_thread();
	start_test_oss_thread();
	start_test_oss_upgrade_thread();
	start_test_oss_download_thread();
	start_test_plugin_download_thread();
	start_test_ping_thread();
}

void NetworkTestDialog::start_all_job_sequence()
{
	m_sequence_job = new boost::thread([this] {
		update_status(-1, "start_test_sequence");
		start_test_bing();
		if (m_closing) return;
		start_test_bambulab();
		if (m_closing) return;
		start_test_oss();
		if (m_closing) return;
		start_test_oss_upgrade();
		if (m_closing) return;
		start_test_oss_download();
		if (m_closing) return;
		start_test_plugin_download();
		update_status(-1, "end_test_sequence");
	});
}

void NetworkTestDialog::start_test_bing()
{
	m_in_testing[TEST_BING_JOB] = true;
	update_status(TEST_BING_JOB, "test bing start...");

	std::string url = "http://www.bing.com/";
	Slic3r::Http http = Slic3r::Http::get(url);
	update_status(-1, "[test_bing]: url=" + url);

	int result = -1;
	http.timeout_max(10)
		.on_complete([this, &result](std::string body, unsigned status) {
			try {
				if (status == 200) {
					result = 0;
				}
			}
			catch (...) {
				;
			}
		})
		.on_ip_resolve([this](std::string ip) {
			wxString ip_report = wxString::Format("test bing ip resolved = %s", ip);
			update_status(TEST_BING_JOB, ip_report);
		})
		.on_error([this](std::string body, std::string error, unsigned int status) {
		wxString info = wxString::Format("status=%u, body=%s, error=%s", status, body, error);
		this->update_status(TEST_BING_JOB, "test bing failed");
		this->update_status(-1, info);
	}).perform_sync();
	if (result == 0) {
		update_status(TEST_BING_JOB, "test bing ok");
	}
	m_in_testing[TEST_BING_JOB] = false;
}

void NetworkTestDialog::start_test_bambulab()
{
	m_in_testing[TEST_BAMBULAB_JOB] = true;
	update_status(TEST_BAMBULAB_JOB, "test bambulab start...");

	std::string platform = "windows";

#ifdef __WINDOWS__
	platform = "windows";
#endif
#ifdef __APPLE__
	platform = "macos";
#endif
#ifdef __LINUX__
	platform = "linux";
#endif
	std::string query_params = (boost::format("?name=slicer&version=%1%&guide_version=%2%")
		% VersionInfo::convert_full_version(SLIC3R_VERSION)
		% VersionInfo::convert_full_version("0.0.0.1")
		).str();

	AppConfig* app_config = wxGetApp().app_config;
	std::string url = wxGetApp().get_http_url(app_config->get_country_code()) + query_params;
	Slic3r::Http http = Slic3r::Http::get(url);
	update_status(-1, "[test_bambulab]: url=" + url);
	int result = -1;
	http.header("accept", "application/json")
		.timeout_max(10)
		.on_complete([this, &result](std::string body, unsigned status) {
			try {
				if (status == 200) {
					result = 0;
				}
			}
			catch (...) {
				;
			}
		})
		.on_ip_resolve([this](std::string ip) {
			wxString ip_report = wxString::Format("test bambulab ip resolved = %s", ip);
			update_status(TEST_BAMBULAB_JOB, ip_report);
		})
		.on_error([this](std::string body, std::string error, unsigned int status) {
			wxString info = wxString::Format("status=%u, body=%s, error=%s", status, body, error);
			this->update_status(TEST_BAMBULAB_JOB, "test bambulab failed");
			this->update_status(-1, info);
		}).perform_sync();
	if (result == 0) {
		update_status(TEST_BAMBULAB_JOB, "test bambulab ok");
	}
	m_in_testing[TEST_BAMBULAB_JOB] = false;
}

void NetworkTestDialog::start_test_iot()
{
	m_in_testing[TEST_IOT_JOB] = true;
	update_status(TEST_IOT_JOB, "test http start...");
	NetworkAgent* agent = wxGetApp().getAgent();
	if (agent) {
		unsigned int http_code;
		std::string http_body;
		if (!agent->is_user_login()) {
			update_status(TEST_IOT_JOB, "please login first");
		} else {
			int result = agent->get_user_print_info(&http_code, &http_body);
			if (result == 0) {
				update_status(TEST_IOT_JOB, "test http ok");
			} else {
				update_status(TEST_IOT_JOB, "test http failed");
				wxString info = wxString::Format("test http failed, status = %u, error = %s", http_code, http_body);
				update_status(-1, info);
			}
		}
	} else {
		update_status(TEST_IOT_JOB, "please install network module first");
	}
	m_in_testing[TEST_IOT_JOB] = false;
}

void NetworkTestDialog::start_test_oss()
{
	m_in_testing[TEST_OSS_JOB] = true;
	update_status(TEST_OSS_JOB, "test storage start...");

	std::string url = "http://upload-file.bambulab.com";

	AppConfig* config = wxGetApp().app_config;
	if (config) {
		if (config->get_country_code() == "CN")
			url = "http://upload-file.bambulab.cn";
	}

	Slic3r::Http http = Slic3r::Http::get(url);
	update_status(-1, "[test_oss]: url=" + url);

	int result = -1;
	http.timeout_max(15)
		.on_complete([this, &result](std::string body, unsigned status) {
		try {
			if (status == 200) {
				result = 0;
			}
		}
		catch (...) {
			;
		}
			})
		.on_ip_resolve([this](std::string ip) {
			wxString ip_report = wxString::Format("test oss ip resolved = %s", ip);
			update_status(TEST_OSS_JOB, ip_report);
		})
		.on_error([this, &result](std::string body, std::string error, unsigned int status) {
			if (status == 403) {
				result = 0;
			} else {
				wxString info = wxString::Format("status=%u, body=%s, error=%s", status, body, error);
				this->update_status(TEST_OSS_JOB, "test storage failed");
				this->update_status(-1, info);
			}
		}).perform_sync();
		if (result == 0) {
			update_status(TEST_OSS_JOB, "test storage ok");
		}
	m_in_testing[TEST_OSS_JOB] = false;
}

void NetworkTestDialog::start_test_oss_upgrade()
{
	m_in_testing[TEST_OSS_UPGRADE_JOB] = true;
	update_status(TEST_OSS_UPGRADE_JOB, "test storage upgrade start...");

	std::string url = "http://upgrade-file.bambulab.com";

	AppConfig* config = wxGetApp().app_config;
	if (config) {
		if (config->get_country_code() == "CN")
			url = "http://upgrade-file.bambulab.cn";
	}

	Slic3r::Http http = Slic3r::Http::get(url);
	update_status(-1, "[test_oss_upgrade]: url=" + url);

	int result = -1;
	http.timeout_max(15)
		.on_complete([this, &result](std::string body, unsigned status) {
		try {
			if (status == 200) {
				result = 0;
			}
		}
		catch (...) {
			;
		}
			})
		.on_ip_resolve([this](std::string ip) {
				wxString ip_report = wxString::Format("test storage upgrade ip resolved = %s", ip);
				update_status(TEST_OSS_UPGRADE_JOB, ip_report);
			})
		.on_error([this, &result](std::string body, std::string error, unsigned int status) {
			if (status == 403) {
				result = 0;
			}
			else {
				wxString info = wxString::Format("status=%u, body=%s, error=%s", status, body, error);
				this->update_status(TEST_OSS_UPGRADE_JOB, "test storage upgrade failed");
				this->update_status(-1, info);
			}
		}).perform_sync();

	if (result == 0) {
		update_status(TEST_OSS_UPGRADE_JOB, "test storage upgrade ok");
	}
	m_in_testing[TEST_OSS_UPGRADE_JOB] = false;
}

void NetworkTestDialog::start_test_oss_download()
{
	int result = 0;
	// get country_code
	AppConfig* app_config = wxGetApp().app_config;
	if (!app_config) {
		update_status(TEST_OSS_DOWNLOAD_JOB, "app config is nullptr");
		return;
	}

	m_in_testing[TEST_OSS_DOWNLOAD_JOB] = true;
	update_status(TEST_OSS_DOWNLOAD_JOB, "test storage download start...");
	m_download_cancel = false;
	// get temp path
	fs::path target_file_path = (fs::temp_directory_path() / "test_storage_download.zip");
	fs::path tmp_path = target_file_path;
	tmp_path += (boost::format(".%1%%2%") % get_current_pid() % ".tmp").str();

	// get_url
	std::string url = wxGetApp().get_plugin_url("plugins", app_config->get_country_code());
	std::string download_url;
	Slic3r::Http http_url = Slic3r::Http::get(url);
	update_status(-1, "[test_oss_download]: url=" + url);

	http_url.on_complete(
		[&download_url](std::string body, unsigned status) {
			try {
				json j = json::parse(body);
				std::string message = j["message"].get<std::string>();
                
				if (message == "success") {
					json resource = j.at("resources");
					if (resource.is_array()) {
						for (auto iter = resource.begin(); iter != resource.end(); iter++) {
							Semver version;
							std::string url;
							std::string type;
							std::string vendor;
							std::string description;
							for (auto sub_iter = iter.value().begin(); sub_iter != iter.value().end(); sub_iter++) {
								if (boost::iequals(sub_iter.key(), "type")) {
									type = sub_iter.value();
									BOOST_LOG_TRIVIAL(info) << "[test_storage_download]: get version of settings's type, " << sub_iter.value();
								}
								else if (boost::iequals(sub_iter.key(), "version")) {
									version = *(Semver::parse(sub_iter.value()));
								}
								else if (boost::iequals(sub_iter.key(), "description")) {
									description = sub_iter.value();
								}
								else if (boost::iequals(sub_iter.key(), "url")) {
									url = sub_iter.value();
								}
							}
							BOOST_LOG_TRIVIAL(info) << "[test_storage_download]: get type " << type << ", version " << version.to_string() << ", url " << url;
							download_url = url;
						}
					}
				}
				else {
					BOOST_LOG_TRIVIAL(info) << "[test_storage_download]: get version of plugin failed, body=" << body;
				}
			}
			catch (...) {
				BOOST_LOG_TRIVIAL(error) << "[test_storage_download]: catch unknown exception";
				;
			}
		}).on_error(
			[&result, this](std::string body, std::string error, unsigned int status) {
				BOOST_LOG_TRIVIAL(error) << "[test_storage_download] on_error: " << error << ", body = " << body;
				wxString info = wxString::Format("status=%u, body=%s, error=%s", status, body, error);
				this->update_status(TEST_OSS_DOWNLOAD_JOB, "test storage download failed");
				this->update_status(-1, info);
				result = -1;
		}).perform_sync();

	if (result < 0) {
		this->update_status(TEST_OSS_DOWNLOAD_JOB, "test storage download failed");
		m_in_testing[TEST_OSS_DOWNLOAD_JOB] = false;
		return;
	}

	if (download_url.empty()) {
		BOOST_LOG_TRIVIAL(info) << "[test_oss_download]: no availaible plugin found for this app version: " << SLIC3R_VERSION;
		this->update_status(TEST_OSS_DOWNLOAD_JOB, "test storage download failed");
		m_in_testing[TEST_OSS_DOWNLOAD_JOB] = false;
		return;
	}
	if (m_download_cancel) {
		this->update_status(TEST_OSS_DOWNLOAD_JOB, "test storage download canceled");
		m_in_testing[TEST_OSS_DOWNLOAD_JOB] = false;
		return;
	}

	bool cancel = false;
	BOOST_LOG_TRIVIAL(info) << "[test_storage_download] get_url = " << download_url;

	// download
	Slic3r::Http http = Slic3r::Http::get(download_url);
	int reported_percent = 0;
	http.on_progress(
		[this, &result, &reported_percent](Slic3r::Http::Progress progress, bool& cancel) {
			int percent = 0;
			if (progress.dltotal != 0) {
				percent = progress.dlnow * 100 / progress.dltotal;
			}
			if (percent - reported_percent >= 10) {
				reported_percent = percent;
				std::string download_progress_info = (boost::format("downloading %1%%%") % percent).str();
				this->update_status(TEST_OSS_DOWNLOAD_JOB, download_progress_info);
			}

			BOOST_LOG_TRIVIAL(info) << "[test_storage_download] progress: " << reported_percent;
			cancel = m_download_cancel;

			if (cancel)
				result = -1;
		})
		.on_complete([this, tmp_path, target_file_path](std::string body, unsigned status) {
			BOOST_LOG_TRIVIAL(info) << "[test_storage_download] completed";
			bool cancel = false;
			fs::fstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
			file.write(body.c_str(), body.size());
			file.close();
			fs::rename(tmp_path, target_file_path);
			//this->update_status(TEST_OSS_DOWNLOAD_JOB, "test storage download ok");
		})
		.on_error([this, &result](std::string body, std::string error, unsigned int status) {
			BOOST_LOG_TRIVIAL(error) << "[test_oss_download] downloading... on_error: " << error << ", body = " << body;
			wxString info = wxString::Format("status=%u, body=%s, error=%s", status, body, error);
			this->update_status(TEST_OSS_DOWNLOAD_JOB, "test storage download failed");
			this->update_status(-1, info);
			result = -1;
		});
	http.perform_sync();
	if (result < 0) {
		this->update_status(TEST_OSS_DOWNLOAD_JOB, "test storage download failed");
	} else {
		this->update_status(TEST_OSS_DOWNLOAD_JOB, "test storage download ok");
	}
	m_in_testing[TEST_OSS_DOWNLOAD_JOB] = false;
	return;
}

void NetworkTestDialog::start_test_oss_upload()
{
	
}

void NetworkTestDialog:: start_test_plugin_download(){
    int result = 0;
    // get country_code
    AppConfig *app_config = wxGetApp().app_config;
    if (!app_config) {
        update_status(TEST_PLUGIN_JOB, "app config is nullptr");
        return;
    }

    m_in_testing[TEST_PLUGIN_JOB] = true;
    update_status(TEST_PLUGIN_JOB, "test plugin download start...");
    m_download_cancel = false;
    // get temp path
    fs::path target_file_path = (fs::temp_directory_path() / "test_plugin_download.zip");
    fs::path tmp_path         = target_file_path;
    tmp_path += (boost::format(".%1%%2%") % get_current_pid() % ".tmp").str();

    // get_url
    std::string  url = wxGetApp().get_plugin_url("plugins", app_config->get_country_code());
    std::string  download_url;
    Slic3r::Http http_url = Slic3r::Http::get(url);
    http_url
        .on_complete([&download_url,this](std::string body, unsigned status) {
            try {
                json        j       = json::parse(body);
                std::string message = j["message"].get<std::string>();

                if (message == "success") {
                    json resource = j.at("resources");
                    if (resource.is_array()) {
                        for (auto iter = resource.begin(); iter != resource.end(); iter++) {
                            Semver      version;
                            std::string url;
                            std::string type;
                            std::string vendor;
                            std::string description;
                            for (auto sub_iter = iter.value().begin(); sub_iter != iter.value().end(); sub_iter++) {
                                if (boost::iequals(sub_iter.key(), "type")) {
                                    type = sub_iter.value();
                                    BOOST_LOG_TRIVIAL(info) << "[test_plugin_download]: get version of settings's type, " << sub_iter.value();
                                } else if (boost::iequals(sub_iter.key(), "version")) {
                                    version = *(Semver::parse(sub_iter.value()));
                                } else if (boost::iequals(sub_iter.key(), "description")) {
                                    description = sub_iter.value();
                                } else if (boost::iequals(sub_iter.key(), "url")) {
                                    url = sub_iter.value();
                                }
                            }
                            BOOST_LOG_TRIVIAL(info) << "[test_plugin_download]: get type " << type << ", version " << version.to_string() << ", url " << url;
                            download_url = url;
                           this->update_status(-1, "[test_plugin_download]: downloadurl=" + download_url);
                        }
                    }
                } else {
                    BOOST_LOG_TRIVIAL(info) << "[test_plugin_download]: get version of plugin failed, body=" << body;
                }
            } catch (...) {
                BOOST_LOG_TRIVIAL(error) << "[test_plugin_download]: catch unknown exception";
                ;
            }
        })
        .on_error([&result, this](std::string body, std::string error, unsigned int status) {
            BOOST_LOG_TRIVIAL(error) << "[test_plugin_download] on_error: " << error << ", body = " << body;
            wxString info = wxString::Format("status=%u, body=%s, error=%s", status, body, error);
            this->update_status(TEST_PLUGIN_JOB, "test plugin download failed");
            this->update_status(-1, info);
            result = -1;
        })
        .perform_sync();
     

    if (result < 0) {
        this->update_status(TEST_PLUGIN_JOB, "test plugin download failed");
        m_in_testing[TEST_PLUGIN_JOB] = false;
        return;
    }

    if (download_url.empty()) {
        BOOST_LOG_TRIVIAL(info) << "[test_plugin_download]: no availaible plugin found for this app version: " << SLIC3R_VERSION;
        this->update_status(TEST_PLUGIN_JOB, "test plugin download failed");
        m_in_testing[TEST_PLUGIN_JOB] = false;
        return;
    }
    if (m_download_cancel) {
        this->update_status(TEST_PLUGIN_JOB, "test plugin download canceled");
        m_in_testing[TEST_PLUGIN_JOB] = false;
        return;
    }

    bool cancel = false;
    BOOST_LOG_TRIVIAL(info) << "[test_plugin_download] get_url = " << download_url;

    // download
    Slic3r::Http http             = Slic3r::Http::get(download_url);
    int          reported_percent = 0;
    http.on_progress([this, &result, &reported_percent](Slic3r::Http::Progress progress, bool &cancel) {
            int percent = 0;
            if (progress.dltotal != 0) { percent = progress.dlnow * 100 / progress.dltotal; }
            if (percent - reported_percent >= 5) {
                reported_percent                   = percent;
                std::string download_progress_info = (boost::format("downloading %1%%%") % percent).str();
                this->update_status(TEST_PLUGIN_JOB, download_progress_info);
            }

            BOOST_LOG_TRIVIAL(info) << "[test_plugin_download] progress: " << reported_percent;
            cancel = m_download_cancel;

            if (cancel) result = -1;
        })
        .on_complete([this, tmp_path, target_file_path](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << "[test_plugin_download] completed";
            bool        cancel = false;
            fs::fstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
            file.write(body.c_str(), body.size());
            file.close();
            fs::rename(tmp_path, target_file_path);
        })
        .on_error([this, &result](std::string body, std::string error, unsigned int status) {
            BOOST_LOG_TRIVIAL(error) << "[test_plugin_download] downloading... on_error: " << error << ", body = " << body;
            wxString info = wxString::Format("status=%u, body=%s, error=%s", status, body, error);
            this->update_status(TEST_PLUGIN_JOB, "test plugin download failed");
            this->update_status(-1, info);
            result = -1;
        });
    http.perform_sync();
    if (result < 0) {
        this->update_status(TEST_PLUGIN_JOB, "test plugin download failed");
    } else {
        this->update_status(TEST_PLUGIN_JOB, "test plugin download ok");
    }
    m_in_testing[TEST_PLUGIN_JOB] = false;
    return;
}

void NetworkTestDialog::start_test_ping_thread()
{
	test_job[TEST_PING_JOB] = new boost::thread([this] {
		m_in_testing[TEST_PING_JOB] = true;

		m_in_testing[TEST_PING_JOB] = false;
	});
}

void NetworkTestDialog::start_test_bing_thread()
{
	test_job[TEST_BING_JOB] = new boost::thread([this] {
		start_test_bing();
	});
}

void NetworkTestDialog::start_test_bambulab_thread()
{
	if (m_in_testing[TEST_BAMBULAB_JOB]) return;
	test_job[TEST_BAMBULAB_JOB] = new boost::thread([this] {
		start_test_bambulab();
	});
}

void NetworkTestDialog::start_test_iot_thread()
{
	if (m_in_testing[TEST_IOT_JOB]) return;
	test_job[TEST_IOT_JOB] = new boost::thread([this] {
		start_test_iot();
	});
}

void NetworkTestDialog::start_test_oss_thread()
{
	test_job[TEST_OSS_JOB] = new boost::thread([this] {
		start_test_oss();
	});
}

void NetworkTestDialog::start_test_oss_upgrade_thread()
{
	test_job[TEST_OSS_UPGRADE_JOB] = new boost::thread([this] {
		start_test_oss_upgrade();
	});
}

void NetworkTestDialog::start_test_oss_download_thread()
{
	test_job[TEST_OSS_DOWNLOAD_JOB] = new boost::thread([this] {
		start_test_oss_download();
	});
}

void NetworkTestDialog::start_test_oss_upload_thread()
{
	test_job[TEST_OSS_UPLOAD_JOB] = new boost::thread([this] {
		start_test_oss_upload();
	});
}

void NetworkTestDialog:: start_test_plugin_download_thread(){

	test_job[TEST_PLUGIN_JOB] = new boost::thread([this] { 
		start_test_plugin_download(); 
	});
}

void NetworkTestDialog::on_close(wxCloseEvent& event)
{
	m_download_cancel = true;
	m_closing = true;
	for (int i = 0; i < TEST_JOB_MAX; i++) {
		if (test_job[i]) {
			test_job[i]->join();
			test_job[i] = nullptr;
		}
	}

	event.Skip();
}


wxString NetworkTestDialog::get_studio_version()
{
	return wxString(SLIC3R_VERSION);
}

void NetworkTestDialog::set_default()
{
	for (int i = 0; i < TEST_JOB_MAX; i++) {
		test_job[i] = nullptr;
		m_in_testing[i] = false;
	}

	m_sequence_job = nullptr;

	text_version_val->SetLabelText(get_studio_version());
	txt_sys_info_value->SetLabelText(get_os_info());
	txt_dns_info_value->SetLabelText(get_dns_info());
	text_link_val->SetLabelText(NA_STR);
	text_bing_val->SetLabelText(NA_STR);
	text_iot_value->SetLabelText(NA_STR);
	text_oss_value->SetLabelText(NA_STR);
	text_oss_upgrade_value->SetLabelText(NA_STR);
	text_oss_download_value->SetLabelText(NA_STR);
	text_oss_upload_value->SetLabelText(NA_STR);
	text_network_plugin_value->SetLabelText(NA_STR);
	//text_ping_value->SetLabelText(NA_STR);
	m_download_cancel = false;
	m_closing = false;
}


void NetworkTestDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    ;
}

void NetworkTestDialog::update_status(int job_id, wxString info)
{
	auto evt = new wxCommandEvent(EVT_UPDATE_RESULT, this->GetId());
	evt->SetString(info);
	evt->SetInt(job_id);
	wxQueueEvent(this, evt);
}


} // namespace GUI
} // namespace Slic3r


