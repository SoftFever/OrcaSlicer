#ifndef slic3r_GUI_NetworkTestDialog_hpp_
#define slic3r_GUI_NetworkTestDialog_hpp_

#include <wx/wx.h>
#include <boost/thread.hpp>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include <slic3r/GUI/Widgets/Button.hpp>
#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/button.h>
#include <wx/string.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/grid.h>
#include <wx/dialog.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>
#include <iomanip>
#include <sys/timeb.h>
#include <time.h>
#include <vector>
#include <algorithm>

namespace Slic3r { 
namespace GUI {

enum TestJob {
	TEST_BING_JOB = 0,
	TEST_BAMBULAB_JOB = 1,
	TEST_IOT_JOB = 2,
	TEST_OSS_JOB = 3,
	TEST_OSS_UPGRADE_JOB = 4,
	TEST_OSS_DOWNLOAD_JOB = 5,
	TEST_OSS_UPLOAD_JOB = 6,
	TEST_PING_JOB = 7,
	TEST_PLUGIN_JOB = 8,
	TEST_JOB_MAX = 9
};

class NetworkTestDialog : public DPIDialog
{
protected:
	Button* btn_start;
	Button* btn_start_sequence;
	Button* btn_download_log;
	wxStaticText* text_basic_info;
	wxStaticText* text_version_title;
	wxStaticText* text_version_val;
	wxStaticText* txt_sys_info_title;
	wxStaticText* txt_sys_info_value;
	wxStaticText* txt_dns_info_title;
	wxStaticText* txt_dns_info_value;
	Button*     btn_link;
	wxStaticText* text_link_title;
	wxStaticText* text_link_val;
	Button*     btn_bing;
	wxStaticText* text_bing_title;
	wxStaticText* text_bing_val;
	Button*     btn_iot;
	wxStaticText* text_iot_title;
	wxStaticText* text_iot_value;
	Button*     btn_oss;
	wxStaticText* text_oss_title;
	wxStaticText* text_oss_value;
	Button*     btn_oss_upgrade;
	wxStaticText* text_oss_upgrade_title;
	wxStaticText* text_oss_upgrade_value;
	Button*     btn_oss_download;
	wxStaticText* text_oss_download_title;
	wxStaticText* text_oss_download_value;
	Button*     btn_oss_upload;
	wxStaticText* text_oss_upload_title;
	wxStaticText* text_oss_upload_value;
	Button*     btn_network_plugin;
	wxStaticText* text_network_plugin_title;
	wxStaticText* text_network_plugin_value;
	wxStaticText* text_ping_title;
	wxStaticText* text_ping_value;
	wxStaticText* text_result;
	wxTextCtrl* txt_log;

	wxBoxSizer* create_top_sizer(wxWindow* parent);
	wxBoxSizer* create_info_sizer(wxWindow* parent);
	wxBoxSizer* create_content_sizer(wxWindow* parent);
	wxBoxSizer* create_result_sizer(wxWindow* parent);

	boost::thread* test_job[TEST_JOB_MAX];
	boost::thread* m_sequence_job { nullptr };
	bool		   m_in_testing[TEST_JOB_MAX];
	bool           m_download_cancel = false;
	bool           m_closing = false;

	void init_bind();

public:
	NetworkTestDialog(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = wxEmptyString, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(605, 375), long style = wxDEFAULT_DIALOG_STYLE);

	~NetworkTestDialog();

	void on_dpi_changed(const wxRect& suggested_rect);

	void set_default();
	wxString get_studio_version();
	wxString get_os_info();
	wxString get_dns_info();

	void start_all_job();
	void start_all_job_sequence();
	void start_test_bing_thread();
	void start_test_bambulab_thread();
	void start_test_iot_thread();
	void start_test_oss_thread();
	void start_test_oss_upgrade_thread();
	void start_test_oss_download_thread();
	void start_test_oss_upload_thread();
	void start_test_ping_thread();
	void start_test_plugin_download_thread();

	void start_test_bing();
	void start_test_bambulab();
	void start_test_iot();
	void start_test_oss();
	void start_test_oss_upgrade();
	void start_test_oss_download();
	void start_test_oss_upload();
	void start_test_plugin_download();

	void on_close(wxCloseEvent& event);

	void update_status(int job_id, wxString info);
};

} // namespace GUI
} // namespace Slic3r

#endif
