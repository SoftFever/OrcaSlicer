#ifndef slic3r_Duet_hpp_
#define slic3r_Duet_hpp_

#include <string>
#include <wx/string.h>

#include "PrintHost.hpp"


namespace Slic3r {


class DynamicPrintConfig;
class Http;

class Duet : public PrintHost
{
public:
	Duet(DynamicPrintConfig *config);
	virtual ~Duet();

	bool test(wxString &curl_msg) const;
	wxString get_test_ok_msg () const;
	wxString get_test_failed_msg (wxString &msg) const;
	bool upload(PrintHostUpload upload_data, Http::ProgressFn prorgess_fn, Http::ErrorFn error_fn) const;
	bool has_auto_discovery() const;
	bool can_test() const;
	virtual std::string get_host() const { return host; }
private:
	std::string host;
	std::string password;

	std::string get_upload_url(const std::string &filename) const;
	std::string get_connect_url() const;
	std::string get_base_url() const;
	std::string timestamp_str() const;
	bool connect(wxString &msg) const;
	void disconnect() const;
	bool start_print(wxString &msg, const std::string &filename) const;
	int get_err_code_from_body(const std::string &body) const;
	static wxString format_error(const std::string &body, const std::string &error, unsigned status);
};


}

#endif
