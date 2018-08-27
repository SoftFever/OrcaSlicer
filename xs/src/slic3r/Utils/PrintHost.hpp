#ifndef slic3r_PrintHost_hpp_
#define slic3r_PrintHost_hpp_

#include <memory>
#include <string>
#include <wx/string.h>


namespace Slic3r {


class DynamicPrintConfig;

class PrintHost
{
public:
	virtual ~PrintHost();

	virtual bool test(wxString &curl_msg) const = 0;
	virtual wxString get_test_ok_msg () const = 0;
	virtual wxString get_test_failed_msg (wxString &msg) const = 0;
	// Send gcode file to print host, filename is expected to be in UTF-8
	virtual bool send_gcode(const std::string &filename) const = 0;
	virtual bool has_auto_discovery() const = 0;
	virtual bool can_test() const = 0;

	static PrintHost* get_print_host(DynamicPrintConfig *config);
};




}

#endif
