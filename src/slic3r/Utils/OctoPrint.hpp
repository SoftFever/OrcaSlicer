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
	bool upload(PrintHostUpload upload_data, Http::ProgressFn prorgess_fn, Http::ErrorFn error_fn) const;
	bool has_auto_discovery() const;
	bool can_test() const;
	virtual std::string get_host() const { return host; }
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
