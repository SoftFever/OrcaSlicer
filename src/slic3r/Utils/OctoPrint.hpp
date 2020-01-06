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
    ~OctoPrint() override = default;

    const char* get_name() const;

    bool test(wxString &curl_msg) const override;
    wxString get_test_ok_msg () const override;
    wxString get_test_failed_msg (wxString &msg) const override;
    bool upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn) const override;
    bool has_auto_discovery() const override { return true; }
    bool can_test() const override { return true; }
    bool can_start_print() const override { return true; }
    std::string get_host() const override { return host; }

protected:
    virtual bool validate_version_text(const boost::optional<std::string> &version_text) const;

private:
    std::string host;
    std::string apikey;
    std::string cafile;

    void set_auth(Http &http) const;
    std::string make_url(const std::string &path) const;
};

class SL1Host: public OctoPrint
{
public:
    SL1Host(DynamicPrintConfig *config) : OctoPrint(config) {}
    ~SL1Host() override = default;

    const char* get_name() const override;

    wxString get_test_ok_msg() const override;
    wxString get_test_failed_msg(wxString &msg) const override;
    bool can_start_print() const override { return false; }

protected:
    bool validate_version_text(const boost::optional<std::string> &version_text) const override;
};

}

#endif
