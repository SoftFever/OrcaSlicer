#ifndef slic3r_HMS_hpp_
#define slic3r_HMS_hpp_

#include "GUI_App.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/StepCtrl.hpp"
#include "BitmapCache.hpp"
#include "slic3r/Utils/Http.hpp"
#include "libslic3r/Thread.hpp"
#include "nlohmann/json.hpp"

namespace Slic3r {
namespace GUI {

#define HMS_INFO_FILE	"hms.json"

class HMSQuery {
protected:
	json m_hms_json;
	int download_hms_info();
	int load_from_local(std::string& version_info);
	int save_to_local(std::string lang);
	std::string get_hms_file(std::string lang);
	wxString _query_hms_msg(std::string long_error_code, std::string lang_code = "en");
	wxString _query_error_msg(std::string long_error_code, std::string lang_code = "en");
public:
	HMSQuery() {}
	int check_hms_info();
	wxString query_hms_msg(std::string long_error_code);
	wxString query_print_error_msg(int print_error);
	static std::string hms_language_code();
	static std::string build_query_params(std::string& lang);
};

int get_hms_info_version(std::string &version);

std::string get_hms_wiki_url(std::string code);

std::string get_error_message(int error_code);

}
}


#endif