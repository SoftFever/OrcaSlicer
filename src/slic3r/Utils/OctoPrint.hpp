#ifndef slic3r_OctoPrint_hpp_
#define slic3r_OctoPrint_hpp_

#include <string>
#include <wx/string.h>

#include "PrintHost.hpp"


namespace Slic3r {


class DynamicPrintConfig;
class Http;

class OctoPrint : public PrintHost
{
public:
	OctoPrint(DynamicPrintConfig *config);
	virtual ~OctoPrint();

	bool test(wxString &curl_msg) const;
	wxString get_test_ok_msg () const;
	wxString get_test_failed_msg (wxString &msg) const;
	// Send gcode file to octoprint, filename is expected to be in UTF-8
	bool send_gcode(const std::string &filename) const;
	bool has_auto_discovery() const;
	bool can_test() const;
private:
	std::string host;
	std::string apikey;
	std::string cafile;

	void set_auth(Http &http) const;
	std::string make_url(const std::string &path) const;
	static wxString format_error(const std::string &body, const std::string &error, unsigned status);
};


}

#endif
