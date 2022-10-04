#include "PrinterFileSystem.h"
#include "libslic3r/Utils.hpp"

#include "../../Utils/NetworkAgent.hpp"

#include <boost/algorithm/hex.hpp>
#include <boost/uuid/detail/md5.hpp>

#include "nlohmann/json.hpp"

#include <cstring>

wxDEFINE_EVENT(EVT_STATUS_CHANGED, wxCommandEvent);
wxDEFINE_EVENT(EVT_MODE_CHANGED, wxCommandEvent);
wxDEFINE_EVENT(EVT_FILE_CHANGED, wxCommandEvent);
wxDEFINE_EVENT(EVT_THUMBNAIL, wxCommandEvent);
wxDEFINE_EVENT(EVT_DOWNLOAD, wxCommandEvent);

wxDEFINE_EVENT(EVT_FILE_CALLBACK, wxCommandEvent);

static wxBitmap default_thumbnail;

struct StaticBambuLib : BambuLib {
    static StaticBambuLib & get();
    static int Fake_Bambu_Open(Bambu_Session * session, char const* uid) { return -2; }
};

PrinterFileSystem::PrinterFileSystem()
    : BambuLib(StaticBambuLib::get())
{
    if (!default_thumbnail.IsOk())
        default_thumbnail = wxImage(Slic3r::encode_path(Slic3r::var("live_stream_default.png").c_str()));
    m_session.owner = this;
    //auto time = wxDateTime::Now();
    //for (int i = 0; i < 240; ++i) {
    //    m_file_list.push_back({"", time.GetTicks(), 0, default_thumbnail, FF_DOWNLOAD, i - 130});
    //    time.Add(wxDateSpan::Days(-1));
    //}
    //BuildGroups();
}

PrinterFileSystem::~PrinterFileSystem()
{
    m_recv_thread.detach();
}

void PrinterFileSystem::SetFileType(FileType type)
{
    if (this->m_file_type == type)
        return;
    this->m_file_type = type;
    m_file_list.swap(m_file_list2);
    m_lock_start = m_lock_end = 0;
    SendChangedEvent(EVT_FILE_CHANGED);
    m_status = Status::ListSyncing;
    SendChangedEvent(EVT_STATUS_CHANGED, m_status);
    ListAllFiles();
}

void PrinterFileSystem::SetGroupMode(GroupMode mode)
{
    if (this->m_group_mode == mode)
        return;
    this->m_group_mode = mode;
    m_lock_start = m_lock_end = 0;
    SendChangedEvent(EVT_MODE_CHANGED);
}

size_t PrinterFileSystem::EnterSubGroup(size_t index)
{
    if (m_group_mode == G_NONE)
        return index;
    index = m_group_mode == G_YEAR ? m_group_year[index] : m_group_month[index];
    SetGroupMode((GroupMode)(m_group_mode - 1));
    return index;
}

void PrinterFileSystem::ListAllFiles()
{
    json req;
    req["type"] = m_file_type == F_VIDEO ? "video" : "timelapse";
    req["notify"] = "DETAIL";
    SendRequest<FileList>(LIST_INFO, req, [this](json const& resp, FileList & list, auto) {
        json files = resp["file_lists"];
        for (auto& f : files) {
            std::string name = f["name"];
            time_t time = f.value("time", 0);
            boost::uint64_t size = f["size"];
            File ff = {name, time, size, default_thumbnail};
            list.push_back(ff);
        }
        return 0;
    }, [this](int result, FileList list) {
        m_file_list.swap(list);
        std::sort(m_file_list.begin(), m_file_list.end());
        auto iter1 = m_file_list.begin();
        auto end1  = m_file_list.end();
        auto iter2 = list.begin();
        auto end2  = list.end();
        while (iter1 != end1 && iter2 != end2) {
            if (iter1->name == iter2->name) {
                iter1->thumbnail = iter2->thumbnail;
                iter1->flags = iter2->flags;
                iter1->progress = iter2->progress;
                ++iter1; ++iter2;
            } else if (*iter1 < *iter2) {
                ++iter1;
            } else if (*iter2 < *iter1) {
                ++iter2;
            } else {
                ++iter1;
                ++iter2;
            }
        }
        BuildGroups();
        m_status = Status::ListReady;
        SendChangedEvent(EVT_STATUS_CHANGED, m_status);
        SendChangedEvent(EVT_FILE_CHANGED);
        return 0;
    });
}

void PrinterFileSystem::DeleteFiles(size_t index)
{
    if (index == size_t(-1)) {
        size_t n = 0;
        for (size_t i = 0; i < m_file_list.size(); ++i) {
            auto &file = m_file_list[i];
            if ((file.flags & FF_SELECT) != 0 && (file.flags & FF_DELETED) == 0) {
                file.flags |= FF_DELETED;
                ++n;
            }
            if (n == 0) return;
        }
    } else {
        if (index >= m_file_list.size())
            return;
        auto &file = m_file_list[index];
        if ((file.flags & FF_DELETED) != 0)
            return;
        file.flags |= FF_DELETED;
    }
    if ((m_task_flags & FF_DELETED) == 0)
        DeleteFilesContinue();
}

void PrinterFileSystem::DownloadFiles(size_t index, std::string const &path)
{
    if (index == (size_t) -1) {
        size_t n = 0;
        for (size_t i = 0; i < m_file_list.size(); ++i) {
            auto &file = m_file_list[i];
            if ((file.flags & FF_SELECT) == 0) continue;
            if ((file.flags & FF_DOWNLOAD) != 0 && file.progress >= 0) continue;
            file.flags |= FF_DOWNLOAD;
            file.progress = -1;
            ++n;
        }
        if (n == 0) return;
    } else {
        if (index >= m_file_list.size())
            return;
        auto &file = m_file_list[index];
        if ((file.flags & FF_DOWNLOAD) != 0 && file.progress >= 0)
            return;
        file.flags |= FF_DOWNLOAD;
        file.progress = -1;
    }
    if ((m_task_flags & FF_DOWNLOAD) == 0)
        DownloadNextFile(path);
}

void PrinterFileSystem::DownloadCancel(size_t index)
{
    if (index == (size_t) -1) return;
    if (index >= m_file_list.size()) return;
    auto &file = m_file_list[index];
    if ((file.flags & FF_DOWNLOAD) == 0) return;
    if (file.progress >= 0)
        CancelRequest(m_download_seq);
}

size_t PrinterFileSystem::GetCount() const
{
    if (m_group_mode == G_NONE)
        return m_file_list.size();
    return m_group_mode == G_YEAR ? m_group_year.size() : m_group_month.size();
}

size_t PrinterFileSystem::GetIndexAtTime(boost::uint32_t time)
{
    auto iter = std::upper_bound(m_file_list.begin(), m_file_list.end(), File{"", time});
    size_t n = std::distance(m_file_list.begin(), iter) - 1;
    if (m_group_mode == G_NONE) {
        return n;
    }
    auto & group = m_group_mode == G_YEAR ? m_group_year : m_group_month;
    auto iter2 = std::upper_bound(group.begin(), group.end(), n);
    return std::distance(group.begin(), iter2) - 1;
}

void PrinterFileSystem::ToggleSelect(size_t index)
{
    if (index < m_file_list.size()) m_file_list[index].flags ^= FF_SELECT;
}

void PrinterFileSystem::SelectAll(bool select)
{
    if (select)
        for (auto &f : m_file_list) f.flags |= FF_SELECT;
    else
        for (auto &f : m_file_list) f.flags &= ~FF_SELECT;
}

void PrinterFileSystem::SetFocusRange(size_t start, size_t count)
{
    m_lock_start = start;
    m_lock_end = start + count;
    if (!m_stopped && (m_task_flags & FF_THUMNAIL) == 0)
        UpdateFocusThumbnail();
}

PrinterFileSystem::File const &PrinterFileSystem::GetFile(size_t index)
{
    if (m_group_mode == G_NONE)
        return m_file_list[index];
    if (m_group_mode == G_YEAR)
        index = m_group_year[index];
    return m_file_list[m_group_month[index]];
}

int PrinterFileSystem::RecvData(std::function<int(Bambu_Sample& sample)> const & callback)
{
    int result = 0;
    while (true) {
        Bambu_Sample sample;
        result = Bambu_ReadSample(&m_session, &sample);
        if (result == Bambu_success) {
            result = callback(sample);
            if (result == 1)
                continue;
        } else if (result == Bambu_would_block) {
            boost::this_thread::sleep(boost::posix_time::seconds(1));
            continue;
        } else if (result == Bambu_stream_end) {
            result = 0;
        } else {
            result = ERROR_PIPE;
        }
        break;
    }
    return result;
}

void PrinterFileSystem::Attached()
{
    boost::unique_lock lock(m_mutex);
    m_recv_thread = std::move(boost::thread([w = weak_from_this()] {
        boost::shared_ptr<PrinterFileSystem> s = w.lock();
        if (s) s->RecvMessageThread();
    }));
}

void PrinterFileSystem::Start()
{
    boost::unique_lock l(m_mutex);
    if (!m_stopped) return;
    m_stopped = false;
    m_cond.notify_all();
}

void PrinterFileSystem::Retry()
{
    boost::unique_lock l(m_mutex);
    m_cond.notify_all();
}

void PrinterFileSystem::SetUrl(std::string const &url)
{
    boost::unique_lock l(m_mutex);
    m_messages.push_back(url);
    m_cond.notify_all();
}

void PrinterFileSystem::Stop(bool quit)
{
    boost::unique_lock l(m_mutex);
    if (quit) {
        m_session.owner = nullptr;
    } else if (m_stopped) {
        return;
    }
    m_stopped = true;
    m_cond.notify_all();
}

void PrinterFileSystem::BuildGroups()
{
    if (m_file_list.empty())
        return;
    m_group_year.clear();
    m_group_month.clear();
    wxDateTime t = wxDateTime((time_t) m_file_list.front().time);
    m_group_year.push_back(0);
    m_group_month.push_back(0);
    for (size_t i = 0; i < m_file_list.size(); ++i) {
        wxDateTime s = wxDateTime((time_t) m_file_list[i].time);
        if (s.GetYear() != t.GetYear()) {
            m_group_year.push_back(m_group_month.size());
            m_group_month.push_back(i);
        } else if (s.GetMonth() != t.GetMonth()) {
            m_group_month.push_back(i);
        }
        t = s;
    }
}

void PrinterFileSystem::DeleteFilesContinue()
{
    std::vector<size_t> indexes;
    std::vector<std::string> names;
    for (size_t i = 0; i < m_file_list.size(); ++i)
        if ((m_file_list[i].flags & FF_SELECT) && !m_file_list[i].name.empty()) {
            indexes.push_back(i);
            names.push_back(m_file_list[i].name);
            if (names.size() >= 64)
                break;
        }
    m_task_flags &= ~FF_DELETED;
    if (names.empty())
        return;
    json req;
    json arr;
    for (auto &name : names) arr.push_back(name);
    req["delete"] = arr;
    m_task_flags |= FF_DELETED;
    SendRequest<Void>(
        FILE_DEL, req, nullptr,
        [indexes, names, this](int, Void const &) {
            // TODO:
            for (size_t i = indexes.size() - 1; i >= 0; --i)
                FileRemoved(indexes[i], names[i]);
            SendChangedEvent(EVT_FILE_CHANGED);
            DeleteFilesContinue();
        });
}

void PrinterFileSystem::DownloadNextFile(std::string const &path)
{
    size_t index = size_t(-1);
    for (size_t i = 0; i < m_file_list.size(); ++i) {
        if ((m_file_list[i].flags & FF_DOWNLOAD) != 0 && m_file_list[i].progress == -1) {
            index = i;
            break;
        }
    }
    m_task_flags &= ~FF_DOWNLOAD;
    if (index >= m_file_list.size())
        return;
    json req;
    req["file"] = m_file_list[index].name;
    m_file_list[index].progress = 0;
    SendChangedEvent(EVT_DOWNLOAD, index, m_file_list[index].name);
    struct Download
    {
        int                       index;
        std::string               name;
        std::string               path;
        boost::filesystem::ofstream ofs;
        boost::uuids::detail::md5 boost_md5;
    };
    std::shared_ptr<Download> download(new Download);
    download->index = index;
    download->name  = m_file_list[index].name;
    download->path  = path;
    m_task_flags |= FF_DOWNLOAD;
    m_download_seq = SendRequest<Progress>(
        FILE_DOWNLOAD, req,
        [this, download](json const &resp, Progress &prog, unsigned char const *data) -> int {
            // in work thread, continue recv
            size_t size = resp["size"];
            prog.size   = resp["offset"];
            prog.total  = resp["total"];
            if (prog.size == 0) {
                download->ofs.open(download->path + "/" + download->name, std::ios::binary);
                if (!download->ofs) return FILE_OPEN_ERR;
            }
            // receive data
            download->ofs.write((char const *) data, size);
            download->boost_md5.process_bytes(data, size);
            prog.size += size;
            if (prog.size < prog.total) { return CONTINUE; }
            download->ofs.close();
            int         result = 0;
            std::string md5    = resp["file_md5"];
            // check size and md5
            if (prog.size == prog.total) {
                boost::uuids::detail::md5::digest_type digest;
                download->boost_md5.get_digest(digest);
                for (int i = 0; i < 4; ++i) digest[i] = boost::endian::endian_reverse(digest[i]);
                std::string str_md5;
                const auto  char_digest = reinterpret_cast<const char *>(&digest[0]);
                boost::algorithm::hex(char_digest, char_digest + sizeof(digest), std::back_inserter(str_md5));
                if (!boost::iequals(str_md5, md5)) result = FILE_CHECK_ERR;
            } else {
                result = FILE_SIZE_ERR;
            }
            if (result != 0) {
                boost::system::error_code ec;
                boost::filesystem::remove(download->path, ec);
            }
            return result;
        },
        [this, download](int result, Progress const &data) {
            if (download->index >= 0)
                download->index = FindFile(download->index, download->name);
            if (download->index >= 0) {
                int progress = data.size * 100 / data.total;
                if (result > CONTINUE)
                    progress = -2;
                auto & file = m_file_list[download->index];
                if (result == ERROR_CANCEL)
                    file.flags &= ~FF_DOWNLOAD;
                else if (file.progress != progress) {
                    file.progress = progress;
                    SendChangedEvent(EVT_DOWNLOAD, download->index, m_file_list[download->index].name, data.size);
                }
            }
            if (result != CONTINUE) DownloadNextFile(download->path);
        });
}

void PrinterFileSystem::UpdateFocusThumbnail()
{
    m_task_flags &= ~FF_THUMNAIL;
    if (m_lock_start >= m_file_list.size() || m_lock_start >= m_lock_end)
        return;
    size_t start = m_lock_start;
    size_t end   = std::min(m_lock_end, GetCount());
    std::vector<std::string> names;
    for (; start < end; ++start) {
        auto &file = GetFile(start);
        if ((file.flags & FF_THUMNAIL) == 0) {
            names.push_back(file.name);
            if (names.size() >= 5)
                break;
        }
    }
    if (names.empty())
        return;
    json req;
    json arr;
    for (auto &name : names) arr.push_back(name);
    req["files"] = arr;
    m_task_flags |= FF_THUMNAIL;
    SendRequest<Thumbnail>(
        THUMBNAIL, req,
        [this](json const &resp, Thumbnail &thumb, unsigned char const *data) -> int {
            // in work thread, continue recv
            // receive data
            wxString            mimetype  = resp.value("mimetype", "image/jpeg");
            std::string         thumbnail = resp["thumbnail"];
            boost::uint32_t     size      = resp["size"];
            thumb.name = thumbnail;
            if (size > 0) {
                wxMemoryInputStream mis(data, size);
                thumb.thumbnail = wxImage(mis, mimetype);
            }
            return 0;
        },
        [this](int result, Thumbnail const &thumb) {
            auto n = thumb.name.find_last_of('.');
            auto name = n == std::string::npos ? thumb.name : thumb.name.substr(0, n);
            auto iter = std::find_if(m_file_list.begin(), m_file_list.end(), [&name](auto &f) { return boost::algorithm::starts_with(f.name, name); });
            if (iter != m_file_list.end()) {
                iter->flags |= FF_THUMNAIL; // DOTO: retry on fail
                if (thumb.thumbnail.IsOk()) {
                    iter->thumbnail = thumb.thumbnail;
                    int index = iter - m_file_list.begin();
                    SendChangedEvent(EVT_THUMBNAIL, index, thumb.name);
                }
            }
            if (result == 0)
                UpdateFocusThumbnail();
        });
}

size_t PrinterFileSystem::FindFile(size_t index, std::string const &name)
{
    if (index >= m_file_list.size() || m_file_list[index].name != name) {
        auto iter = std::find_if(m_file_list.begin(), m_file_list.end(), [name](File &f) { return f.name == name; });
        if (iter == m_file_list.end()) return -1;
        index = std::distance(m_file_list.begin(), iter);
    }
    return index;
}

void PrinterFileSystem::FileRemoved(size_t index, std::string const &name)
{
    index = FindFile(index, name);
    if (index == size_t(-1))
        return;
    auto removeFromGroup = [](std::vector<size_t> &group, size_t index, int total) {
        for (auto iter = group.begin(); iter != group.end(); ++iter) {
            size_t index2 = -1;
            if (*iter < index) continue;
            if (*iter == index) {
                auto iter2 = iter + 1;
                if (iter2 == group.end() ? index == total - 1 : *iter2 == index + 1) {
                    index2 = std::distance(group.begin(), iter);
                }
                ++iter;
            }
            for (; iter != group.end(); ++iter) {
                --*iter;
            }
            return index2;
        }
        return size_t(-1);
    };
    size_t index2 = removeFromGroup(m_group_month, index, m_file_list.size());
    if (index2 < m_group_month.size()) {
        int index3 = removeFromGroup(m_group_year, index, m_group_month.size());
        if (index3 < m_group_year.size())
            m_group_year.erase(m_group_year.begin() + index3);
        m_group_month.erase(m_group_month.begin() + index2);
    }
    m_file_list.erase(m_file_list.begin() + index);
}

struct CallbackEvent : wxCommandEvent
{
    CallbackEvent(std::function<void(void)> const &callback, boost::weak_ptr<PrinterFileSystem> owner) : wxCommandEvent(EVT_FILE_CALLBACK), callback(callback), owner(owner) {}
    ~CallbackEvent(){ if (!owner.expired()) callback(); }
    std::function<void(void)> const callback;
    boost::weak_ptr<PrinterFileSystem> owner;
};

void PrinterFileSystem::PostCallback(std::function<void(void)> const& callback)
{
    wxCommandEvent *e = new CallbackEvent(callback, boost::weak_ptr(shared_from_this()));
    wxQueueEvent(this, e);
}

void PrinterFileSystem::SendChangedEvent(wxEventType type, size_t index, std::string const &str, long extra)
{
    wxCommandEvent event(type);
    event.SetEventObject(this);
    event.SetInt(index);
    if (!str.empty())
        event.SetString(wxString::FromUTF8(str.c_str()));
    event.SetExtraLong(extra);
    if (wxThread::IsMain())
        ProcessEventLocally(event);
    else
        wxPostEvent(this, event);
}

void PrinterFileSystem::DumpLog(Bambu_Session *session, int level, Bambu_Message const *msg)
{
    BOOST_LOG_TRIVIAL(info) << "PrinterFileSystem: " << msg;
    StaticBambuLib::get().Bambu_FreeLogMsg(msg);
}

boost::uint32_t PrinterFileSystem::SendRequest(int type, json const &req, callback_t2 const &callback)
{
    if (m_session.gSID < 0) {
        boost::unique_lock l(m_mutex);
        m_cond.notify_all();
        return 0;
    }
    boost::uint32_t seq = m_sequence + m_callbacks.size();
    json root;
    root["cmdtype"] = type;
    root["sequence"] = seq;
    root["req"] = req;
    std::ostringstream oss;
    oss << root;
    auto msg = oss.str();
    //OutputDebugStringA(msg.c_str());
    //OutputDebugStringA("\n");
    BOOST_LOG_TRIVIAL(info) << "PrinterFileSystem::SendRequest: " << type << " msg: " << msg;
    boost::unique_lock l(m_mutex);
    m_messages.push_back(msg);
    m_callbacks.push_back(callback);
    m_cond.notify_all();
    return seq;
}

void PrinterFileSystem::InstallNotify(int type, callback_t2 const &callback)
{
    type -= NOTIFY_FIRST;
    if (m_notifies.size() <= size_t(type)) m_notifies.resize(type + 1);
    m_notifies[type] = callback;
}

void PrinterFileSystem::CancelRequest(boost::uint32_t seq)
{
    json req;
    json arr;
    arr.push_back(seq);
    req["tasks"] = arr;
    SendRequest(TASK_CANCEL, req, [this](int result, json const &resp, unsigned char const *) {
        if (result != 0) return;
        json tasks = resp["tasks"];
        std::deque<callback_t2> callbacks;
        boost::unique_lock      l(m_mutex);
        for (auto &f : tasks) {
            boost::uint32_t seq = f;
            seq -= m_sequence;
            if (size_t(seq) >= m_callbacks.size()) continue;
            auto & c = m_callbacks[seq];
            if (c == nullptr) continue;
            callbacks.push_back(c);
            m_callbacks[seq] = callback_t2();
        }
        while (!m_callbacks.empty() && m_callbacks.front() == nullptr) {
            m_callbacks.pop_front();
            ++m_sequence;
        }
        l.unlock();
        for (auto &c : callbacks) c(ERROR_CANCEL, json(), nullptr);
    });
}

void PrinterFileSystem::RecvMessageThread()
{
    Bambu_Sample sample;
    boost::unique_lock l(m_mutex);
    Reconnect(l, 0);
    while (true) {
        if (m_stopped && (m_session.owner == nullptr || (m_messages.empty() && m_callbacks.empty()))) {
            Reconnect(l, 0); // Close and wait start again
            if (m_session.owner == nullptr) {
                // clear callbacks first
                auto callbacks(std::move(m_callbacks));
                break;
            }
        }
        if (!m_messages.empty()) {
            auto & msg = m_messages.front();
            l.unlock();
            int n = Bambu_SendMessage(&m_session, CTRL_TYPE, msg.c_str(), msg.length());
            l.lock();
            if (n == 0)
                m_messages.pop_front();
            else if (n != Bambu_would_block) {
                Reconnect(l, n);
                continue;
            }
        }
        l.unlock();
        int n = Bambu_ReadSample(&m_session, &sample);
        l.lock();
        if (n == 0) {
            HandleResponse(l, sample);
        } else if (n == Bambu_would_block) {
            m_cond.timed_wait(l, boost::posix_time::milliseconds(m_messages.empty() && m_callbacks.empty() ? 1000 : 20));
        } else {
            Reconnect(l, n);
        }
    } // while
}

void PrinterFileSystem::HandleResponse(boost::unique_lock<boost::mutex> &l, Bambu_Sample const &sample)
{
    unsigned char const *end      = sample.buffer + sample.size;
    unsigned char const *json_end = (unsigned char const *) memchr(sample.buffer, '\n', sample.size);
    while (json_end && json_end + 3 < end && json_end[1] != '\n') json_end = (unsigned char const *) memchr(json_end + 2, '\n', end - json_end - 2);
    if (json_end)
        json_end += 2;
    else
        json_end = end;
    std::string msg((char const *) sample.buffer, json_end - sample.buffer);
    json        root;
    // OutputDebugStringA(msg.c_str());
    // OutputDebugStringA("\n");
    std::istringstream iss(msg);
    int                cmd    = 0;
    int                seq    = -1;
    int                result = 0;
    json               resp;
    try {
        iss >> root;
        if (!root["result"].is_null()) {
            result = root["result"];
            seq    = root["sequence"];
            resp   = root["reply"];
        } else {
            // maybe notify
            cmd  = root["cmdtype"];
            seq  = root["sequence"];
            resp = root["notify"];
        }
    } catch (...) {
        result = ERROR_JSON;
        return;
    }
    if (cmd > 0) {
        if (cmd < NOTIFY_FIRST) return;
        cmd -= NOTIFY_FIRST;
        if (size_t(cmd) >= m_notifies.size()) return;
        auto n = m_notifies[cmd];
        l.unlock();
        n(result, resp, json_end);
        l.lock();
    } else {
        seq -= m_sequence;
        if (size_t(seq) >= m_callbacks.size()) return;
        auto c = m_callbacks[seq];
        if (c == nullptr) return;
        if (result != CONTINUE) {
            m_callbacks[seq] = callback_t2();
            if (seq == 0) {
                while (!m_callbacks.empty() && m_callbacks.front() == nullptr) {
                    m_callbacks.pop_front();
                    ++m_sequence;
                }
            }
        }
        l.unlock();
        c(result, resp, json_end);
        l.lock();
    }
}

void PrinterFileSystem::Reconnect(boost::unique_lock<boost::mutex> &l, int result)
{
    if (m_session.gSID >= 0) {
        l.unlock();
        Bambu_Close(&m_session);
        l.lock();
    }
    if (m_session.owner == nullptr)
        return;
    json r;
    while (!m_callbacks.empty()) {
        auto c = m_callbacks.front();
        m_callbacks.pop_front();
        ++m_sequence;
        if (c) c(result, r, nullptr);
    }
    m_messages.clear();
    while (true) {
        while (m_stopped) {
            if (m_session.owner == nullptr)
                return;
            m_cond.wait(l);
        }
        m_status = Status::Initializing;
        SendChangedEvent(EVT_STATUS_CHANGED, m_status);
        // wait for url
        while (!m_stopped && m_messages.empty())
            m_cond.wait(l);
        if (m_stopped || m_messages.empty()) continue;
        std::string url = m_messages.front();
        m_messages.clear();
        if (url.empty()) {
            m_last_error = 1;
        } else {
            l.unlock();
            m_status = Status::Connecting;
            SendChangedEvent(EVT_STATUS_CHANGED, m_status);
            m_session.logger = &PrinterFileSystem::DumpLog;
            int ret          = Bambu_Open(&m_session, url.c_str() + 9); // skip bambu:/// sync
            if (ret == 0)
                ret = Bambu_StartStream(&m_session);
            l.lock();
            if (ret == 0)
                break;
            m_last_error = ret;
        }
        m_status     = Status::Failed;
        SendChangedEvent(EVT_STATUS_CHANGED, m_status);
        m_cond.timed_wait(l, boost::posix_time::seconds(10));
    }
    m_status = Status::ListSyncing;
    SendChangedEvent(EVT_STATUS_CHANGED, m_status);
    PostCallback([this] { ListAllFiles(); });
}


#include <stdlib.h>
#if defined(_MSC_VER) || defined(_WIN32)
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#if defined(_MSC_VER) || defined(_WIN32)
static HMODULE module = NULL;
#else
static void* module = NULL;
#endif

static void* get_function(const char* name)
{
    void* function = nullptr;

    if (!module)
        return function;

#if defined(_MSC_VER) || defined(_WIN32)
    function = GetProcAddress(module, name);
#else
    function = dlsym(module, name);
#endif

    if (!function) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", can not find function %1%") % name;
    }
    return function;
}

#define GET_FUNC(x) lib.x = reinterpret_cast<decltype(lib.x)>(get_function(#x))

StaticBambuLib &StaticBambuLib::get()
{
    static StaticBambuLib lib;
    // first load the library

    if (!module) {
        module = Slic3r::NetworkAgent::get_bambu_source_entry();
    }

    if (!module) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", can not Load Library";
    }

    GET_FUNC(Bambu_Open);
    GET_FUNC(Bambu_StartStream);
    GET_FUNC(Bambu_SendMessage);
    GET_FUNC(Bambu_ReadSample);
    GET_FUNC(Bambu_Close);
    GET_FUNC(Bambu_FreeLogMsg);

    if (!lib.Bambu_Open)
        lib.Bambu_Open = Fake_Bambu_Open;
    return lib;
}
