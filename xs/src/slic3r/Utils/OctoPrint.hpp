#ifndef slic3r_OctoPrint_hpp_
#define slic3r_OctoPrint_hpp_

#include <string>
#include <wx/string.h>


namespace Slic3r {


class DynamicPrintConfig;
class Http;

class OctoPrint
{
public:
	OctoPrint(DynamicPrintConfig *config);

	bool test(wxString &curl_msg) const;
	bool send_gcode(const std::string &filename, bool print = false) const;
private:
	std::string host;
	std::string apikey;
	std::string cafile;

	void set_auth(Http &http) const;
	std::string make_url(const std::string &path) const;
	static wxString format_error(std::string error, unsigned status);
};


}

#endif
