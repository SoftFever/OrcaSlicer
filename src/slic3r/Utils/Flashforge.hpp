#ifndef slic3r_FlashForge_hpp_
#define slic3r_FlashForge_hpp_

#include <string>
#include <wx/string.h>
#include "PrintHost.hpp"
#include "SerialMessage.hpp"
#include "SerialMessageType.hpp"
#include "../../libslic3r/PrintConfig.hpp"
#include "TCPConsole.hpp"

namespace Slic3r {
class DynamicPrintConfig;
class Http;

class Flashforge : public PrintHost
{
public:
    explicit Flashforge(DynamicPrintConfig *config);
    ~Flashforge() override = default;

    const char *get_name() const override;

    bool                       test(wxString &curl_msg) const override;
    wxString                   get_test_ok_msg() const override;
    wxString                   get_test_failed_msg(wxString &msg) const override;
    bool                       upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn, InfoFn info_fn) const override;
    bool                       has_auto_discovery() const override { return false; }
    bool                       can_test() const override { return true; }
    PrintHostPostUploadActions get_post_upload_actions() const override { return PrintHostPostUploadAction::StartPrint; }
    std::string                get_host() const override { return m_host; }

private:
    std::string m_host;
    std::string m_port;
    std::string m_serial;
    std::string m_printer_id;
    std::string make_url(const std::string &path) const;
};

} // namespace Slic3r

#endif
