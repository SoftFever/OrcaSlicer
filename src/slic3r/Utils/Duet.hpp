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
	~Duet() override = default;

	const char* get_name() const override;

	bool test(wxString &curl_msg) const override;
	wxString get_test_ok_msg() const override;
	wxString get_test_failed_msg(wxString &msg) const override;
	bool upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn) const override;
	bool has_auto_discovery() const override { return false; }
	bool can_test() const override { return true; }
	bool can_start_print() const override { return true; }
	std::string get_host() const override { return host; }

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
};

}

#endif
