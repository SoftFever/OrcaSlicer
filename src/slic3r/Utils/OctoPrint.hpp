#ifndef slic3r_OctoPrint_hpp_
#define slic3r_OctoPrint_hpp_

#include <string>
#include <wx/string.h>
#include <boost/optional.hpp>

#include "PrintHost.hpp"


namespace Slic3r {


class DynamicPrintConfig;
class Http;

class OctoPrint : public PrintHost
{
public:
    OctoPrint(DynamicPrintConfig *config);
    virtual ~OctoPrint();

    virtual bool test(wxString &curl_msg) const;
    virtual wxString get_test_ok_msg () const;
    virtual wxString get_test_failed_msg (wxString &msg) const;
    virtual bool upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn) const;
    virtual bool has_auto_discovery() const;
    virtual bool can_test() const;
    virtual bool can_start_print() const;
    virtual std::string get_host() const { return host; }

protected:
    virtual bool validate_version_text(const boost::optional<std::string> &version_text) const;

private:
    std::string host;
    std::string apikey;
    std::string cafile;

    void set_auth(Http &http) const;
    std::string make_url(const std::string &path) const;
};


class SLAHost: public OctoPrint
{
public:
    SLAHost(DynamicPrintConfig *config) : OctoPrint(config) {}
    virtual ~SLAHost();

    virtual wxString get_test_ok_msg () const;
    virtual wxString get_test_failed_msg (wxString &msg) const;
    virtual bool can_start_print() const ;
protected:
    virtual bool validate_version_text(const boost::optional<std::string> &version_text) const;
};


}

#endif
