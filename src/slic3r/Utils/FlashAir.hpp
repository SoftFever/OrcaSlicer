#ifndef slic3r_FlashAir_hpp_
#define slic3r_FlashAir_hpp_

#include <string>
#include <wx/string.h>

#include "PrintHost.hpp"


namespace Slic3r {


class DynamicPrintConfig;
class Http;

class FlashAir : public PrintHost
{
public:
	FlashAir(DynamicPrintConfig *config);
	virtual ~FlashAir();

	virtual const char* get_name() const;

	virtual bool test(wxString &curl_msg) const;
	virtual wxString get_test_ok_msg () const;
	virtual wxString get_test_failed_msg (wxString &msg) const;
	virtual bool upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn) const;
	virtual bool has_auto_discovery() const;
	virtual bool can_test() const;
	virtual bool can_start_print() const;
	virtual std::string get_host() const { return host; }

private:
	std::string host;

	std::string timestamp_str() const;
	std::string make_url(const std::string &path) const;
	std::string make_url(const std::string &path, const std::string &arg, const std::string &val) const;
};


}

#endif
