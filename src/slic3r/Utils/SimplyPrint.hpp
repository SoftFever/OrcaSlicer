#ifndef slic3r_SimplyPrint_hpp_
#define slic3r_SimplyPrint_hpp_

#include "PrintHost.hpp"
#include "slic3r/GUI/Jobs/OAuthJob.hpp"

namespace Slic3r {

class DynamicPrintConfig;
class Http;
class SimplyPrint : public PrintHost
{
    std::string cred_file;
    std::map<std::string, std::string> cred;

    void load_oauth_credential();

    bool do_api_call(std::function<Http(bool /*is_retry*/)>                                                           build_request,
                     std::function<bool(std::string /* body */, unsigned /* http_status */)>                          on_complete,
                     std::function<bool(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error) const;

public:
    SimplyPrint(DynamicPrintConfig* config);
    ~SimplyPrint() override = default;

    const char* get_name() const override { return "SimplyPrint"; }
    bool can_test() const override { return true; }
    bool has_auto_discovery() const override { return false; }
    bool is_cloud() const override { return true; }
    std::string get_host() const override { return "https://simplyprint.io"; }

    GUI::OAuthParams get_oauth_params() const;
    void             save_oauth_credential(const GUI::OAuthResult& cred) const;

    wxString                   get_test_ok_msg() const override;
    wxString                   get_test_failed_msg(wxString& msg) const override;
    bool                       test(wxString& curl_msg) const override;
    PrintHostPostUploadActions get_post_upload_actions() const override { return PrintHostPostUploadAction::QueuePrint; }
    bool                       upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn, InfoFn info_fn) const override;
    bool                       is_logged_in() const override { return !cred.empty(); }
    void                       log_out() const override;
};
} // namespace Slic3r

#endif
