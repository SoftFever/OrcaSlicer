#ifndef slic3r_AstroBox_hpp_
#define slic3r_AstroBox_hpp_

#include <string>
#include <wx/string.h>
#include <boost/optional.hpp>

#include "PrintHost.hpp"


namespace Slic3r {


class DynamicPrintConfig;
class Http;

class AstroBox : public PrintHost
{
public:
    AstroBox(DynamicPrintConfig *config);
    virtual ~AstroBox();

    virtual const char* get_name() const;

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


class SL1Host: public AstroBox
{
public:
    SL1Host(DynamicPrintConfig *config) : AstroBox(config) {}
    virtual ~SL1Host();

    virtual const char* get_name() const;

    virtual wxString get_test_ok_msg () const;
    virtual wxString get_test_failed_msg (wxString &msg) const;
    virtual bool can_start_print() const ;
protected:
    virtual bool validate_version_text(const boost::optional<std::string> &version_text) const;
};


}

#endif
