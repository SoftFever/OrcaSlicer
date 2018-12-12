#ifndef slic3r_PrintHost_hpp_
#define slic3r_PrintHost_hpp_

#include <memory>
#include <string>
#include <wx/string.h>


namespace Slic3r {

class DynamicPrintConfig;


struct PrintHostUpload
{
    boost::filesystem::path source_path;
    boost::filesystem::path upload_path;
    bool start_print = false;
};


class PrintHost
{
public:
    virtual ~PrintHost();

    virtual bool test(wxString &curl_msg) const = 0;
    virtual wxString get_test_ok_msg () const = 0;
    virtual wxString get_test_failed_msg (wxString &msg) const = 0;
    // Send gcode file to print host, filename is expected to be in UTF-8
    virtual bool send_gcode(const std::string &filename) const = 0;         // XXX: remove in favor of upload()
    virtual bool upload(PrintHostUpload upload_data) const = 0;
    virtual bool has_auto_discovery() const = 0;
    virtual bool can_test() const = 0;

    static PrintHost* get_print_host(DynamicPrintConfig *config);
};


struct PrintHostJob
{
    PrintHostUpload upload_data;
    std::unique_ptr<PrintHost> printhost;

    PrintHostJob() {}
    PrintHostJob(const PrintHostJob&) = delete;
    PrintHostJob(PrintHostJob &&other)
        : upload_data(std::move(other.upload_data))
        , printhost(std::move(other.printhost))
    {}

    PrintHostJob(DynamicPrintConfig *config)
        : printhost(PrintHost::get_print_host(config))
    {}

    PrintHostJob& operator=(const PrintHostJob&) = delete;
    PrintHostJob& operator=(PrintHostJob &&other)
    {
        upload_data = std::move(other.upload_data);
        printhost = std::move(other.printhost);
        return *this;
    }

    bool empty() const { return !printhost; }
    operator bool() const { return !!printhost; }
};


class PrintHostJobQueue
{
public:
    PrintHostJobQueue();
    PrintHostJobQueue(const PrintHostJobQueue &) = delete;
    PrintHostJobQueue(PrintHostJobQueue &&other) = delete;
    ~PrintHostJobQueue();

    PrintHostJobQueue& operator=(const PrintHostJobQueue &) = delete;
    PrintHostJobQueue& operator=(PrintHostJobQueue &&other) = delete;

private:
    struct priv;
    std::shared_ptr<priv> p;
};



}

#endif
