#ifndef slic3r_GUI_PrinterFileSystem_h_
#define slic3r_GUI_PrinterFileSystem_h_

#define BAMBU_DYNAMIC
#include "BambuTunnel.h"

#include <wx/event.h>

#include <boost/thread.hpp>
#include <boost/enable_shared_from_this.hpp>

#include "nlohmann/json_fwd.hpp"
using nlohmann::json;

#include <functional>
#include <deque>

wxDECLARE_EVENT(EVT_STATUS_CHANGED, wxCommandEvent);
wxDECLARE_EVENT(EVT_MODE_CHANGED, wxCommandEvent);
wxDECLARE_EVENT(EVT_FILE_CHANGED, wxCommandEvent);
wxDECLARE_EVENT(EVT_THUMBNAIL, wxCommandEvent);
wxDECLARE_EVENT(EVT_DOWNLOAD, wxCommandEvent);

class PrinterFileSystem : public wxEvtHandler, public boost::enable_shared_from_this<PrinterFileSystem>, BambuLib
{
    static const int CTRL_TYPE     = 0x3001;

    enum {
        LIST_INFO       = 0x0001,
        THUMBNAIL       = 0x0002,
        FILE_DEL        = 0x0003,
        FILE_DOWNLOAD   = 0X0004,
        NOTIFY_FIRST    = 0x0100, 
        LIST_CHANGE_NOTIFY = 0x0100,
        LIST_RESYNC_NOTIFY = 0x0101,
        TASK_CANCEL     = 0x1000
    };

    enum {
        SUCCESS             = 0,
        CONTINUE            = 1,
        ERROR_JSON          = 2,
        ERROR_PIPE          = 3,
        ERROR_CANCEL        = 4,
        FILE_NO_EXIST       = 10,
        FILE_NAME_INVALID   = 11,
        FILE_SIZE_ERR       = 12,
        FILE_OPEN_ERR       = 13,
        FILE_READ_ERR       = 14,
        FILE_CHECK_ERR      = 15,
    };


public:
    PrinterFileSystem();

    ~PrinterFileSystem();

public:
    enum FileType {
        F_TIMELAPSE,
        F_VIDEO,
    };

    enum GroupMode {
        G_NONE,
        G_MONTH,
        G_YEAR,
    };

    void SetFileType(FileType type);

    void SetGroupMode(GroupMode mode);

    size_t EnterSubGroup(size_t index);

    GroupMode GetFileType() const { return m_group_mode; }

    GroupMode GetGroupMode() const { return m_group_mode; }

    template<typename T> using Callback = std::function<void(int, T)>;

    enum Flags {
        FF_SELECT = 1,
        FF_THUMNAIL = 2,    // Thumbnail ready
        FF_DOWNLOAD = 4,    // Request download
        FF_DELETED = 8,     // Request delete
    };

    struct File
    {
        std::string name;
        time_t time = 0;
        boost::uint64_t size = 0;
        wxBitmap    thumbnail;
        int         flags = 0;
        int         progress = -1; // -1: waiting

        bool IsSelect() const { return flags & FF_SELECT; }
        bool IsDownload() const { return flags & FF_DOWNLOAD; }

        friend bool operator<(File const & l, File const & r) { return l.time > r.time; }
    };

    struct Void {};

    typedef std::vector<File> FileList;

    struct Thumbnail
    {
        std::string name;
        wxBitmap    thumbnail;
    };

    struct Progress
    {
        wxInt64 size;
        wxInt64 total;
    };

    void ListAllFiles();

    void DeleteFiles(size_t index);

    void DownloadFiles(size_t index, std::string const &path);

    void DownloadCancel(size_t index);

    size_t GetCount() const;

    size_t GetIndexAtTime(boost::uint32_t time);

    void ToggleSelect(size_t index);
    
    void SelectAll(bool select);

    void SetFocusRange(size_t start, size_t count);

    File const &GetFile(size_t index);

    enum Status {
        Initializing,
        Connecting, 
        ListSyncing,
        ListReady,
        Failed,
    };
    
    Status GetStatus() const { return m_status; }
    int GetLastError() const { return m_last_error; }

    void Attached();

    void Start();

    void Retry();

    void SetUrl(std::string const &url);

    void Stop(bool quit = false);

private:
    void BuildGroups();

    void DeleteFilesContinue();

    void DownloadNextFile(std::string const &path);

    void UpdateFocusThumbnail();

    void FileRemoved(size_t index, std::string const &name);

    size_t FindFile(size_t index, std::string const &name);

    void SendChangedEvent(wxEventType type, size_t index = (size_t)-1, std::string const &str = {}, long extra = 0);

    static void DumpLog(Bambu_Session *session, int level, Bambu_Message const *msg);

private:
    template<typename T> using Translator = std::function<int(json const &, T &, unsigned char const *)>;

    typedef std::function<void(int, json const & resp)> callback_t;

    typedef std::function<void(int, json const &resp, unsigned char const *data)> callback_t2;

    template <typename T>
    boost::uint32_t SendRequest(int type, json const& req, Translator<T> const& translator, Callback<T> const& callback)
    {
        auto c = [translator, callback, this](int result, json const &resp, unsigned char const *data)
        {
            T t;
            if (result == 0 || result == CONTINUE) {
                try {
                    int n  = (translator != nullptr) ? translator(resp, t, data) : 0;
                    result = n == 0 ? result : n;
                }
                catch (...) {
                    result = ERROR_JSON;
                }
            }
            PostCallback<T>(callback, result, t);
        };
        return SendRequest(type, req, c);
    }

    template<typename T> using Applier = std::function<void(T const &)>;

    template<typename T>
    void InstallNotify(int type, Translator<T> const& translator, Applier<T> const& applier)
    {
        auto c = [translator, applier, this](int result, json const &resp, unsigned char const *data)
        {
            T t;
            if (result == 0 || result == CONTINUE) {
                try {
                    int n  = (translator != nullptr) ? translator(resp, t, data) : 0;
                    result = n == 0 ? result : n;
                }
                catch (...) {
                    result = ERROR_JSON;
                }
            }
            if (result == 0 && applier) {
                PostCallback<T>([applier](int, T const & t) {
                    applier(t);
                }, 0, t);
            }
        };
        InstallNotify(type, c);
    }

    boost::uint32_t SendRequest(int type, json const &req, callback_t2 const &callback);

    void InstallNotify(int type, callback_t2 const &callback);

    void CancelRequest(boost::uint32_t seq);

    void RecvMessageThread();

    void HandleResponse(boost::unique_lock<boost::mutex> &l, Bambu_Sample const &sample);

    void Reconnect(boost::unique_lock<boost::mutex> & l, int result);

    int RecvData(std::function<int(Bambu_Sample & sample)> const & callback);

    template <typename T>
    void PostCallback(Callback<T> const& callback, int result, T const& resp)
    {
        PostCallback([=] { callback(result, resp); });
    }

    void PostCallback(std::function<void(void)> const & callback);

protected:
    FileType m_file_type = F_TIMELAPSE;
    GroupMode m_group_mode = G_NONE;
    FileList m_file_list;
    FileList m_file_list2;
    std::vector<size_t> m_group_year;
    std::vector<size_t> m_group_month;

private:
    size_t m_lock_start = 0;
    size_t m_lock_end   = 0;
    int m_task_flags = 0;

private:
    struct Session : Bambu_Session
    {
        PrinterFileSystem * owner;
    };
    Session m_session;
    boost::uint32_t m_sequence = 0;
    boost::uint32_t m_download_seq = 0;
    std::deque<std::string> m_messages;
    std::deque<callback_t2> m_callbacks;
    std::deque<callback_t2> m_notifies;
    bool m_stopped = true;
    boost::mutex m_mutex;
    boost::condition_variable m_cond;
    boost::thread m_recv_thread;
    Status m_status;
    int m_last_error = 0;
};

#endif // !slic3r_GUI_PrinterFileSystem_h_
