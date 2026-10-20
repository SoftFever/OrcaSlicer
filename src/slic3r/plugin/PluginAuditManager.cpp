#include "PluginAuditManager.hpp"
#include "PluginAuditManagerInternal.hpp"

#include "../Utils/OrcaCloudServiceAgent.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/libslic3r.h" // GCODEVIEWER_APP_KEY, and SLIC3R_APP_KEY via libslic3r_version.h
#include "libslic3r_version.h"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <Python.h>

namespace Slic3r {

// extensive list of audit events can be found at https://docs.python.org/3/library/audit_events.html
static const std::unordered_map<std::string, AuditEventCategory> audit_event_categories{
    // fsread
    {"glob.glob", AuditEventCategory::FsRead},
    {"glob.glob/2", AuditEventCategory::FsRead},
    {"os.fwalk", AuditEventCategory::FsRead},
    {"os.getxattr", AuditEventCategory::FsRead},
    {"os.listdir", AuditEventCategory::FsRead},
    {"os.listdrives", AuditEventCategory::FsRead},
    {"os.listmounts", AuditEventCategory::FsRead},
    {"os.listvolumes", AuditEventCategory::FsRead},
    {"os.listxattr", AuditEventCategory::FsRead},
    {"os.scandir", AuditEventCategory::FsRead},
    {"os.walk", AuditEventCategory::FsRead},
    {"pathlib.Path.glob", AuditEventCategory::FsRead},
    {"pathlib.Path.rglob", AuditEventCategory::FsRead},

    // fsreadwrite
    {"os.chflags", AuditEventCategory::FsReadWrite},
    {"os.chmod", AuditEventCategory::FsReadWrite},
    {"os.chown", AuditEventCategory::FsReadWrite},
    {"os.removexattr", AuditEventCategory::FsReadWrite},
    {"os.rename", AuditEventCategory::FsReadWrite},
    {"os.setxattr", AuditEventCategory::FsReadWrite},
    {"os.truncate", AuditEventCategory::FsReadWrite},
    {"os.utime", AuditEventCategory::FsReadWrite},
    {"shutil.chown", AuditEventCategory::FsReadWrite},
    {"shutil.copymode", AuditEventCategory::FsReadWrite},
    {"shutil.copystat", AuditEventCategory::FsReadWrite},
    {"shutil.copyfile", AuditEventCategory::FsReadWrite},
    {"shutil.copytree", AuditEventCategory::FsReadWrite},
    {"shutil.make_archive", AuditEventCategory::FsReadWrite},
    {"shutil.move", AuditEventCategory::FsReadWrite},
    {"shutil.unpack_archive", AuditEventCategory::FsReadWrite},

    // fscreate
    {"os.link", AuditEventCategory::FsCreate},
    {"os.mkdir", AuditEventCategory::FsCreate},
    {"os.symlink", AuditEventCategory::FsCreate},
    {"tempfile.mkdtemp", AuditEventCategory::FsCreate},
    {"tempfile.mkstemp", AuditEventCategory::FsCreate},
    {"_winapi.CreateJunction", AuditEventCategory::FsCreate},

    // fsdelete
    {"os.remove", AuditEventCategory::FsDelete},
    {"os.rmdir", AuditEventCategory::FsDelete},
    {"shutil.rmtree", AuditEventCategory::FsDelete},

    // http
    {"http.client.connect", AuditEventCategory::Http},
    {"http.client.send", AuditEventCategory::Http},
    {"urllib.Request", AuditEventCategory::Http},

    // socket
    {"socket.__new__", AuditEventCategory::Socket},
    {"socket.bind", AuditEventCategory::Socket},
    {"socket.connect", AuditEventCategory::Socket},
    {"socket.getaddrinfo", AuditEventCategory::Socket},
    {"socket.gethostbyaddr", AuditEventCategory::Socket},
    {"socket.gethostbyname", AuditEventCategory::Socket},
    {"socket.gethostname", AuditEventCategory::Socket},
    {"socket.getnameinfo", AuditEventCategory::Socket},
    {"socket.getservbyname", AuditEventCategory::Socket},
    {"socket.getservbyport", AuditEventCategory::Socket},
    {"socket.sendmsg", AuditEventCategory::Socket},
    {"socket.sendto", AuditEventCategory::Socket},

    // processcreate
    {"os.fork", AuditEventCategory::ProcessCreate},
    {"os.forkpty", AuditEventCategory::ProcessCreate},
    {"os.posix_spawn", AuditEventCategory::ProcessCreate},
    {"os.spawn", AuditEventCategory::ProcessCreate},
    {"os.system", AuditEventCategory::ProcessCreate},
    {"os.startfile", AuditEventCategory::ProcessCreate},
    {"os.startfile/2", AuditEventCategory::ProcessCreate},
    {"pty.spawn", AuditEventCategory::ProcessCreate},
    {"subprocess.Popen", AuditEventCategory::ProcessCreate},
    {"_winapi.CreateProcess", AuditEventCategory::ProcessCreate},
    {"_posixsubprocess.fork_exec", AuditEventCategory::ProcessCreate},

    // threading
    {"_thread.start_new_thread", AuditEventCategory::Threading},
    // processreplace: exec* replaces the current process image rather than spawning a child
    {"os.exec", AuditEventCategory::ProcessReplace},
};

// Returns the category event_name belongs to, or AuditEventCategory::None when it isn't audited.
static AuditEventCategory event_category(const std::string& event_name)
{
    const auto it = audit_event_categories.find(event_name);
    return it == audit_event_categories.end() ? AuditEventCategory::None : it->second;
}

// Events the 3.12 stdlib raises with sys.audit() inside the function that performs the whole
// operation, so the raising frame bounds everything the operation does. Every other mapped event
// is a primitive raised from C, whose current frame is whatever Python code called it -- possibly
// a library loop over many targets -- so it never marks an operation. glob.glob and glob.glob/2
// are raised in iglob(), which returns its lazy generator before any directory is listed.
static const std::unordered_set<std::string> operation_root_events{
    "urllib.Request",      "http.client.connect",   "os.walk",          "os.fwalk",
    "glob.glob",           "glob.glob/2",           "pathlib.Path.glob", "pathlib.Path.rglob",
    "subprocess.Popen",    "pty.spawn",             "tempfile.mkstemp", "tempfile.mkdtemp",
    "shutil.copyfile",     "shutil.copymode",       "shutil.copystat",  "shutil.copytree",
    "shutil.chown",        "shutil.move",           "shutil.rmtree",    "shutil.make_archive",
    "shutil.unpack_archive",
};

// True for the categories whose targets are filesystem paths, as opposed to a network
// address or a process command line -- the deny-path and allowed-root checks only make sense
// against a path.
static bool is_fs_category(AuditEventCategory category)
{
    switch (category) {
    case AuditEventCategory::FsRead:
    case AuditEventCategory::FsReadWrite:
    case AuditEventCategory::FsCreate:
    case AuditEventCategory::FsDelete:
        return true;
    default:
        return false;
    }
}

namespace plugin_audit {

// The category's name in a saved event grant, e.g. "FsRead" in "FsRead:open".
const char* category_name(AuditEventCategory category)
{
    switch (category) {
    case AuditEventCategory::FsRead:         return "FsRead";
    case AuditEventCategory::FsReadWrite:    return "FsReadWrite";
    case AuditEventCategory::FsCreate:       return "FsCreate";
    case AuditEventCategory::FsDelete:       return "FsDelete";
    case AuditEventCategory::Http:           return "Http";
    case AuditEventCategory::Socket:         return "Socket";
    case AuditEventCategory::ProcessCreate:  return "ProcessCreate";
    case AuditEventCategory::ProcessReplace: return "ProcessReplace";
    case AuditEventCategory::Threading:      return "Threading";
    case AuditEventCategory::None:           break;
    }
    return "None";
}

int report_denied(PluginAuditManager&       mgr,
                  const std::string&        event_name,
                  const AuditDecision&      decision,
                  const std::string&        target)
{
    AuditViolation violation;
    violation.plugin_key = mgr.current_plugin();
    violation.event_name = event_name;
    violation.reason     = decision.reason;
    mgr.report_violation(violation);

    std::string message = "Plugin attempted audited operation \"" + event_name + "\" without permission";
    if (!decision.reason.empty())
        message += ": " + decision.reason;
    if (!target.empty())
        message += ": " + target;

    PyErr_SetString(PyExc_PermissionError, message.c_str());
    return -1;
}

} // namespace plugin_audit

// ---------------------------------------------------------------------------
// Path safety
// ---------------------------------------------------------------------------

bool is_inside_allowed_root(const boost::filesystem::path& candidate, const boost::filesystem::path& allowed_root)
{
    namespace fs = boost::filesystem;

    boost::system::error_code ec;

    // Canonicalize both paths.  weakly_canonical resolves symlinks but does
    // NOT require the path to exist — it canonicalizes the prefix that exists
    // and appends the non-existing tail lexically.
    fs::path canon_candidate = fs::weakly_canonical(candidate, ec);
    if (ec) {
        // Fall back to lexically_normal + absolute
        canon_candidate = fs::absolute(candidate, ec).lexically_normal();
        if (ec)
            canon_candidate = candidate;
    }

    fs::path canon_root = fs::weakly_canonical(allowed_root, ec);
    if (ec) {
        canon_root = fs::absolute(allowed_root, ec).lexically_normal();
        if (ec)
            canon_root = allowed_root;
    }

    // Component-wise comparison: the root must be a prefix of candidate,
    // and the next component must not be ".." or missing.
    auto cand_it  = canon_candidate.begin();
    auto cand_end = canon_candidate.end();
    auto root_it  = canon_root.begin();
    auto root_end = canon_root.end();

    const auto same_component = [](const fs::path& lhs, const fs::path& rhs) {
#ifdef _WIN32
        return boost::algorithm::iequals(lhs.native(), rhs.native());
#else
        return lhs == rhs;
#endif
    };

    // Consume matching components
    while (root_it != root_end && cand_it != cand_end && same_component(*root_it, *cand_it)) {
        ++root_it;
        ++cand_it;
    }

    // If we didn't consume the entire root, candidate is not inside it.
    if (root_it != root_end)
        return false;

    // The remaining path components must not traverse upward.
    for (auto it = cand_it; it != cand_end; ++it) {
        if (*it == "..")
            return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// ScopedPluginAuditContext
// ---------------------------------------------------------------------------

thread_local std::string PluginAuditManager::m_current_plugin_key = "";
thread_local std::string PluginAuditManager::m_current_capability_name = "";
thread_local std::string PluginAuditManager::m_current_audit_phase = "";
thread_local std::vector<AllowedRoot> PluginAuditManager::m_scoped_allowed_roots;
thread_local std::vector<PyObject*> PluginAuditManager::m_root_frames;
thread_local int PluginAuditManager::m_releasing_frames = 0;
thread_local bool PluginAuditManager::m_audit_denial_pending = false;
thread_local bool PluginAuditManager::m_has_last_violation = false;
thread_local AuditViolation PluginAuditManager::m_last_violation;

ScopedPluginAuditContext::ScopedPluginAuditContext(const std::string& plugin_key,
                                                   const std::string& capability_name)
    : m_plugin_key(plugin_key)
    , m_capability(capability_name)
    , m_previous_id(PluginAuditManager::instance().current_plugin())
    , m_previous_capability(PluginAuditManager::instance().current_capability())
    , m_previous_scoped_roots(PluginAuditManager::m_scoped_allowed_roots)
    , m_previous_root_frames(std::move(PluginAuditManager::m_root_frames))
{
    PluginAuditManager::instance().set_current_plugin(plugin_key);
    PluginAuditManager::instance().set_current_capability(capability_name);
    PluginAuditManager::m_scoped_allowed_roots.clear();
    PluginAuditManager::m_root_frames.clear();
}

ScopedPluginAuditContext::~ScopedPluginAuditContext()
{
    PluginAuditManager& mgr = PluginAuditManager::instance();

    // Let this scope's root frames go while it is still the current context, and as its own
    // plugin: the trampoline's denial path clears the key before the scope ends, and a finalizer
    // must never be judged as the outer plugin, or bypass the audit when there is none. A
    // finalizer may record a new root frame, so repeat until none is held.
    if (!PluginAuditManager::m_root_frames.empty()) {
        assert(PyGILState_Check());
        mgr.set_current_plugin(m_plugin_key);
        mgr.set_current_capability(m_capability);
        while (!PluginAuditManager::m_root_frames.empty()) {
            std::vector<PyObject*> frames = std::move(PluginAuditManager::m_root_frames);
            PluginAuditManager::m_root_frames.clear();
            mgr.release_frames(frames);
        }
    }

    mgr.set_current_plugin(m_previous_id);
    mgr.set_current_capability(m_previous_capability);
    PluginAuditManager::m_scoped_allowed_roots = std::move(m_previous_scoped_roots);
    PluginAuditManager::m_root_frames          = std::move(m_previous_root_frames);
}

// ---------------------------------------------------------------------------
// PluginAuditManager
// ---------------------------------------------------------------------------

PluginAuditManager& PluginAuditManager::instance()
{
    static PluginAuditManager mgr;
    return mgr;
}

void PluginAuditManager::set_current_plugin(const std::string& plugin_key) { m_current_plugin_key = plugin_key; }

std::string PluginAuditManager::current_plugin() const { return m_current_plugin_key; }

void PluginAuditManager::clear_current_plugin() { m_current_plugin_key.clear(); }

void PluginAuditManager::set_current_capability(const std::string& capability_name) { m_current_capability_name = capability_name; }

std::string PluginAuditManager::current_capability() const { return m_current_capability_name; }

void PluginAuditManager::clear_current_capability() { m_current_capability_name.clear(); }

void PluginAuditManager::set_current_audit_phase(const std::string& phase) { m_current_audit_phase = phase; }

const std::string& PluginAuditManager::current_audit_phase() const { return m_current_audit_phase; }

void PluginAuditManager::clear_current_audit_phase() { m_current_audit_phase.clear(); }

void PluginAuditManager::set_observation_observer(AuditObserver observer)
{
    std::lock_guard<std::mutex> lock(m_observer_mutex);
    m_observation_observer = std::move(observer);
}

void PluginAuditManager::clear_observation_observer()
{
    std::lock_guard<std::mutex> lock(m_observer_mutex);
    m_observation_observer = nullptr;
}

bool PluginAuditManager::observation_mode() const
{
    std::lock_guard<std::mutex> lock(m_observer_mutex);
    return static_cast<bool>(m_observation_observer);
}

// The audit hook holds the GIL here. Copy the observer out before invoking it so a callback that
// reads the manager cannot deadlock on m_observer_mutex.
bool PluginAuditManager::record_observation(const std::string&              event_name,
                                            const std::string&              category,
                                            const std::vector<std::string>& targets)
{
    AuditObserver observer;
    {
        std::lock_guard<std::mutex> lock(m_observer_mutex);
        observer = m_observation_observer;
    }
    if (!observer)
        return false;

    // The observer is called from a CPython C callback; a C++ exception must not unwind through
    // the audit hook. The probe's collector only allocates, so a throw here is a lost observation,
    // not a reason to abort the audited operation.
    try {
        AuditObservation observation;
        observation.plugin_key = m_current_plugin_key;
        observation.capability = m_current_capability_name;
        observation.phase      = m_current_audit_phase;
        observation.event_name = event_name;
        observation.category   = category;
        observation.targets    = targets;
        // Probe mode never denies: the hook returns 0 after recording. A later failure of the
        // surrounding Python operation is reported by the probe's callback/load result, not here.
        observation.continued = true;
        observer(observation);
    } catch (...) {
    }
    return true;
}

ScopedPluginAuditPhase::ScopedPluginAuditPhase(std::string phase)
    : m_phase(std::move(phase))
    , m_previous_phase(PluginAuditManager::instance().current_audit_phase())
{
    PluginAuditManager::instance().set_current_audit_phase(m_phase);
}

ScopedPluginAuditPhase::~ScopedPluginAuditPhase()
{
    PluginAuditManager::instance().set_current_audit_phase(m_previous_phase);
}

void PluginAuditManager::add_global_allowed_root(const boost::filesystem::path& root, bool allow_write)
{
    if (root.empty())
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_global_allowed_roots.push_back({root, allow_write});
    BOOST_LOG_TRIVIAL(info) << "[AUDIT] Global allowed root: " << root.string() << " allow_write=" << allow_write;
}

void PluginAuditManager::add_scoped_allowed_root(const boost::filesystem::path& root, bool allow_write)
{
    if (root.empty())
        return;

    m_scoped_allowed_roots.push_back({root, allow_write});
    BOOST_LOG_TRIVIAL(info) << "[AUDIT] Scoped allowed root for plugin " << current_plugin() << ": " << root.string()
                            << " allow_write=" << allow_write;
}

// ---------------------------------------------------------------------------
// Denied filenames
// ---------------------------------------------------------------------------

void PluginAuditManager::add_denied_filename(const std::string& filename)
{
    if (filename.empty())
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_denied_filenames.push_back(filename);
    BOOST_LOG_TRIVIAL(info) << "[AUDIT] Denied filename: " << filename;
}

std::vector<std::string> PluginAuditManager::default_denied_filenames()
{
    // AppConfig::config_path() picks .conf vs .ini on the USE_JSON_CONFIG ifdef and the app key
    // on its mode, and is a non-static member we cannot call without an instance.  Denying all
    // four names is cheaper and more robust than replicating that; the two unused names cost one
    // string comparison each.  Single-sourced here so install_hook() and the tests seed from the
    // same list and cannot drift apart.
    return {
        SLIC3R_APP_KEY ".conf",
        GCODEVIEWER_APP_KEY ".conf",
        SLIC3R_APP_KEY ".ini",
        GCODEVIEWER_APP_KEY ".ini",
        secret_constants::USER_SECRET_FILENAME,
    };
}

bool PluginAuditManager::is_denied_filename(const boost::filesystem::path& candidate) const
{
    // Match on the base name alone, with no path resolution.  Traversal is handled for free,
    // because filename() of data_dir()/plugins/../OrcaSlicer.conf is already "OrcaSlicer.conf",
    // and the prefix rule covers the .bak/.tmp companions that hold the same secrets plus Windows
    // alternate data streams ("OrcaSlicer.conf:stream").  A plugin that launders a denied file
    // through a symlink, a hardlink, a subprocess, or a Windows 8.3 short name is out of scope
    // (see the design doc): this blocks direct access, not an actively evasive plugin.
    const std::string filename = candidate.filename().string();
    if (filename.empty())
        return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& denied : m_denied_filenames) {
        if (boost::algorithm::istarts_with(filename, denied))
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Denied path keywords
// ---------------------------------------------------------------------------

void PluginAuditManager::add_denied_path_keyword(const std::string& keyword)
{
    if (keyword.empty())
        return;

    std::string lower = keyword;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });

    std::lock_guard<std::mutex> lock(m_mutex);
    m_denied_path_keywords.push_back(lower);
    BOOST_LOG_TRIVIAL(info) << "[AUDIT] Denied path keyword: " << lower;
}

std::vector<std::string> PluginAuditManager::default_denied_path_keywords()
{
    // Broad, categorical rules on top of the exact-name is_denied_filename registry: a plugin
    // must never be able to reach a secret, a certificate, or a configuration file just because
    // it happens to live inside an otherwise-allowed root (e.g. the bundled TLS client cert at
    // resources_dir()/cert/..., which would become reachable the moment resources_dir() is
    // granted as a read-only allowed root). Match as whole path components, not substrings, so
    // imports such as numpy/__config__.py and stdlib configparser.py remain usable.
    return {"secret", "secrets", "cert", "certs", "certificate", "certificates", "conf", "config"};
}

static bool has_denied_config_extension(std::string name)
{
    const size_t stream_pos = name.find(':');
    if (stream_pos != std::string::npos)
        name.erase(stream_pos);

    const boost::filesystem::path path(name);
    const std::string extension = path.extension().string();
    return extension == ".conf" || extension == ".ini";
}

bool PluginAuditManager::is_denied_path_keyword(const boost::filesystem::path& candidate) const
{
    namespace fs = boost::filesystem;

    boost::system::error_code ec;
    fs::path canon = fs::weakly_canonical(candidate, ec);
    if (ec) {
        canon = fs::absolute(candidate, ec).lexically_normal();
        if (ec)
            canon = candidate;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_denied_path_keywords.empty())
        return false;

    for (const auto& component : canon) {
        std::string name = component.string();
        if (name.empty())
            continue;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
        for (const auto& keyword : m_denied_path_keywords) {
            if (name == keyword || (keyword == "conf" && has_denied_config_extension(name)))
                return true;
        }
    }
    return false;
}

bool PluginAuditManager::is_denied_path(const boost::filesystem::path& candidate) const
{
    return is_denied_filename(candidate) || is_denied_path_keyword(candidate);
}

// ---------------------------------------------------------------------------
// Policy checks
// ---------------------------------------------------------------------------

AuditDecision PluginAuditManager::check_path_access(const boost::filesystem::path& path, bool is_write)
{
    if (path.empty())
        return {true, ""};

    std::string plugin_key = current_plugin();
    if (plugin_key.empty())
        return {true, ""}; // not running inside a plugin context

    // Denied filenames/keywords are checked before the allowed roots.  The app config and the
    // cloud refresh token live directly inside data_dir(), which is a global allowed root, and
    // the bundled TLS client cert lives inside resources_dir(), a read-only global allowed
    // root, so a deny placed any lower would be unreachable.
    if (is_denied_filename(path)) {
        BOOST_LOG_TRIVIAL(warning) << "[AUDIT] block path=" << path.string() << " is_write=" << is_write
                                   << " plugin=" << plugin_key << " reason=denied filename";
        return {false, "denied filename"};
    }
    if (is_denied_path_keyword(path)) {
        BOOST_LOG_TRIVIAL(warning) << "[AUDIT] block path=" << path.string() << " is_write=" << is_write
                                   << " plugin=" << plugin_key << " reason=denied path keyword";
        return {false, "denied path keyword"};
    }

    namespace fs = boost::filesystem;
    fs::path candidate = path;

    // Resolve relative paths against the current working directory
    if (candidate.is_relative()) {
        boost::system::error_code ec;
        fs::path absolute_candidate = fs::absolute(candidate, ec);
        if (!ec)
            candidate = absolute_candidate;
    }

    // A root that doesn't allow writes only matches a read-shaped request; a write/create/
    // delete-shaped one falls through to "outside allowed root" for that root even though the
    // path is physically inside it.
    for (const auto& root : m_scoped_allowed_roots) {
        if ((!is_write || root.allow_write) && is_inside_allowed_root(candidate, root.path)) {
            return {true, ""};
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& root : m_global_allowed_roots) {
            if ((!is_write || root.allow_write) && is_inside_allowed_root(candidate, root.path)) {
                return {true, ""};
            }
        }
    }

    BOOST_LOG_TRIVIAL(warning) << "[AUDIT] block path=" << candidate.string() << " is_write=" << is_write
                               << " plugin=" << plugin_key;
    return {false, "outside allowed root"};
}

AuditDecision PluginAuditManager::check_open(const std::string& path_str, const std::string& mode)
{
    const bool is_write = mode.find('w') != std::string::npos || mode.find('a') != std::string::npos ||
                          mode.find('+') != std::string::npos || mode.find('x') != std::string::npos;
    return check_path_access(boost::filesystem::path(path_str), is_write);
}

bool PluginAuditManager::request_filesystem_read_permissions(const std::string&              plugin_key,
                                                              const std::vector<std::string>& paths)
{
    if (plugin_key.empty() || paths.empty())
        return true;

    // Probe mode records the declaration and proceeds. It must not consult the install state,
    // open the permission dialog, or persist a grant.
    if (observation_mode()) {
        record_observation("orca.request_permissions", "FsRead", paths);
        return true;
    }

    return plugin_audit::request_filesystem_read_permissions(plugin_key, paths);
}

void PluginAuditManager::report_violation(const AuditViolation& violation)
{
    m_last_violation     = violation;
    m_has_last_violation = true;
    m_audit_denial_pending = true;

    BOOST_LOG_TRIVIAL(warning) << "[AUDIT BLOCKED] plugin=" << violation.plugin_key << " event=" << violation.event_name
                               << " reason=" << violation.reason;
}

bool PluginAuditManager::audit_denial_pending() const { return m_audit_denial_pending; }

void PluginAuditManager::clear_audit_denial() { m_audit_denial_pending = false; }

void PluginAuditManager::clear_last_violation()
{
    m_has_last_violation = false;
    m_last_violation     = AuditViolation{};
}

bool PluginAuditManager::last_violation(AuditViolation& violation) const
{
    if (!m_has_last_violation)
        return false;

    violation = m_last_violation;
    return true;
}

// ---------------------------------------------------------------------------
// The C-level audit hook
// ---------------------------------------------------------------------------

namespace PluginAuditDetail {

PyObject* tuple_item(PyObject* args, Py_ssize_t index)
{
    if (!args || !PyTuple_Check(args) || index < 0 || index >= PyTuple_GET_SIZE(args))
        return nullptr;
    return PyTuple_GET_ITEM(args, index);
}

std::string python_path(PyObject* object)
{
    if (!object)
        return {};

    PyObject* path_object = PyOS_FSPath(object);
    if (!path_object) {
        PyErr_Clear();
        return {};
    }

    const char* path = PyUnicode_Check(path_object) ? PyUnicode_AsUTF8(path_object) : PyBytes_AsString(path_object);
    std::string result = path ? path : "";
    Py_DECREF(path_object);
    if (!path)
        PyErr_Clear();
    return result;
}

std::string python_unicode(PyObject* object)
{
    if (!object || !PyUnicode_Check(object))
        return {};
    const char* text = PyUnicode_AsUTF8(object);
    if (!text) {
        PyErr_Clear();
        return {};
    }
    return text;
}

std::string python_str(PyObject* object)
{
    if (!object)
        return {};

    PyObject* str_object = PyObject_Str(object);
    if (!str_object) {
        PyErr_Clear();
        return {};
    }

    const std::string result = python_unicode(str_object);
    Py_DECREF(str_object);
    return result;
}

// The interpreter's stdlib folders, and the site-packages folders inside them that hold
// third-party code, both resolved. Set by install_hook(); like the verdict cache below, only
// touched while the GIL is held.
static std::vector<boost::filesystem::path>   s_stdlib_dirs;
static std::vector<boost::filesystem::path>   s_site_package_dirs;
static std::unordered_map<std::string, bool>  s_stdlib_file_verdicts; // co_filename -> is stdlib

void collect_stdlib_dirs()
{
    s_stdlib_dirs.clear();
    s_site_package_dirs.clear();
    s_stdlib_file_verdicts.clear();

    PyObject* sysconfig = PyImport_ImportModule("sysconfig");
    PyObject* paths     = sysconfig ? PyObject_CallMethod(sysconfig, "get_paths", nullptr) : nullptr;
    if (paths && PyDict_Check(paths)) {
        const auto collect = [paths](const char* key, std::vector<boost::filesystem::path>& out) {
            const std::string dir = python_unicode(PyDict_GetItemString(paths, key)); // borrowed
            if (dir.empty())
                return;
            boost::system::error_code ec;
            const boost::filesystem::path resolved = boost::filesystem::weakly_canonical(dir, ec);
            out.push_back(ec ? boost::filesystem::path(dir) : resolved);
        };
        collect("stdlib", s_stdlib_dirs);
        collect("platstdlib", s_stdlib_dirs);
        collect("purelib", s_site_package_dirs);
        collect("platlib", s_site_package_dirs);
    }
    Py_XDECREF(paths);
    Py_XDECREF(sysconfig);
    if (PyErr_Occurred()) {
        PyErr_Clear();
        BOOST_LOG_TRIVIAL(error) << "[AUDIT] Failed to read the stdlib folders from sysconfig";
    }
}

bool is_stdlib_frame(PyObject* frame)
{
    PyCodeObject* code = PyFrame_GetCode(reinterpret_cast<PyFrameObject*>(frame)); // new reference
    const std::string filename = python_unicode(code->co_filename);
    Py_DECREF(code);
    return PluginAuditManager::is_stdlib_file(filename);
}

// The frames on this thread's Python stack, innermost first. The pointers stay valid while the
// caller runs, because a running activation keeps its frame object alive.
std::vector<PyObject*> stack_frames()
{
    std::vector<PyObject*> frames;
    PyFrameObject* frame = PyEval_GetFrame(); // borrowed reference
    Py_XINCREF(frame);                        // normalize to an owned reference for the loop below
    while (frame) {
        frames.push_back(reinterpret_cast<PyObject*>(frame));
        PyFrameObject* back = PyFrame_GetBack(frame); // new reference, or nullptr at the top of the stack
        Py_DECREF(frame);
        frame = back;
    }
    return frames;
}

// Moves the held root frames whose operation has returned from `held` to `dropped`. A suspended
// generator's frame (os.walk between two items) is off the stack, but its operation resumes, so
// it stays held until the scope ends.
void take_returned_frames(std::vector<PyObject*>& held, std::vector<PyObject*>& dropped)
{
    if (held.empty())
        return;

    const std::vector<PyObject*> stack = stack_frames();
    const auto running = [&stack](PyObject* frame) {
        if (std::find(stack.begin(), stack.end(), frame) != stack.end())
            return true;
        // NULL once the generator has finished or been deleted: its frame then owns its own data.
        PyObject* generator = PyFrame_GetGenerator(reinterpret_cast<PyFrameObject*>(frame)); // new reference
        const bool suspended = generator != nullptr;
        Py_XDECREF(generator);
        return suspended;
    };
    const auto returned = std::stable_partition(held.begin(), held.end(), running);
    dropped.insert(dropped.end(), returned, held.end());
    held.erase(returned, held.end());
}

static const std::unordered_set<std::string> two_path_fs_events{
    "os.rename",       "os.link",          "os.symlink",         "shutil.copyfile",
    "shutil.copytree", "shutil.copymode",  "shutil.copystat",    "shutil.move",
    "shutil.unpack_archive", "_winapi.CreateJunction",
};

static const std::unordered_map<std::string, std::vector<Py_ssize_t>> audit_target_arg_indices{
    {"http.client.connect", {1}},
    {"urllib.Request", {0}},

    {"socket.connect", {1}},
    {"socket.bind", {1}},
    {"socket.getaddrinfo", {0}},
    {"socket.gethostbyname", {0}},
    {"socket.gethostbyaddr", {0}},
    {"socket.getnameinfo", {0}},
    {"socket.getservbyname", {0}},
    {"socket.getservbyport", {0}},
    {"socket.sendto", {1}},
    {"socket.sendmsg", {1}},

    {"os.system", {0}},
    {"subprocess.Popen", {1, 0}},
    {"os.posix_spawn", {1, 0}},
    {"os.spawn", {2, 1}},
    {"os.startfile", {0}},
    {"os.startfile/2", {0}},
    {"pty.spawn", {0}},
    {"_winapi.CreateProcess", {1, 0}},
    {"_posixsubprocess.fork_exec", {0}},
    {"os.exec", {0}},
};

AuditEventCategory open_category(PyObject* args)
{
    const std::string mode = python_unicode(tuple_item(args, 1));
    if (!mode.empty() && mode.find_first_of("wax+") == std::string::npos)
        return AuditEventCategory::FsRead;
    return AuditEventCategory::FsReadWrite;
}

std::vector<std::string> audit_targets(const std::string& event_name, AuditEventCategory category, PyObject* args)
{
    std::vector<std::string> targets;

    switch (category) {
    case AuditEventCategory::FsRead:
    case AuditEventCategory::FsReadWrite:
    case AuditEventCategory::FsCreate:
    case AuditEventCategory::FsDelete: {
        const Py_ssize_t path_count = two_path_fs_events.count(event_name) ? 2 : 1;
        for (Py_ssize_t index = 0; index < path_count; ++index) {
            const std::string path = python_path(tuple_item(args, index));
            if (!path.empty())
                targets.push_back(path);
        }
        return targets;
    }
    case AuditEventCategory::Threading:
        // Thread creation exposes no user-supplied target. Use a fixed sentinel so the grant
        // persists per plugin: the permission list matches targets by exact string, and the
        // started function's repr embeds an address that changes every run.
        targets.emplace_back("thread");
        return targets;
    default:
        break;
    }

    const auto it = audit_target_arg_indices.find(event_name);
    if (it != audit_target_arg_indices.end()) {
        for (const Py_ssize_t index : it->second) {
            // None is an argument left out (sendmsg without an address), not a target named "None".
            PyObject* const item = tuple_item(args, index);
            if (item == Py_None)
                continue;
            std::string value = python_str(item);
            if (!value.empty()) {
                targets.push_back(std::move(value));
                break;
            }
        }
    }

    return targets;
}

// Judges an audited event raised while a plugin context is active.
int audit_plugin_event(PluginAuditManager& mgr, const std::string& event_name, AuditEventCategory event_type, PyObject* args)
{
    const bool                      fs_category = is_fs_category(event_type);
    const std::vector<std::string>  targets      = audit_targets(event_name, event_type, args);

    // Probe mode: report the classified event and let it through. Deliberately before the deny
    // registry and allowed-root checks, so the report covers operations that production would
    // block as well as those it would allow. No grant is read or written.
    if (mgr.record_observation(event_name, plugin_audit::category_name(event_type), targets))
        return 0;

    // A denied path (secrets, certificates, config files -- see is_denied_path) is an
    // unconditional block: checked before the running-operation check below, so no granted
    // operation can launder access to one, and before the allowed-root shortcut further down,
    // so an allowed root (e.g. the read-only resources folder) cannot make a denied path
    // underneath it reachable.
    if (fs_category) {
        for (const auto& target : targets) {
            if (mgr.is_denied_path(boost::filesystem::path(target)))
                return plugin_audit::report_denied(mgr, event_name, {false, "denied path"}, target);
        }
    }

    // A nested event of an operation already granted, e.g. socket.connect inside an approved
    // urlopen(), is not asked again. urllib.Request is always judged on its own URL, so a
    // redirect or a nested urlopen() inside a running request is not covered by it.
    if (event_name != "urllib.Request" && mgr.covered_by_root_frame())
        return 0;

    // A filesystem target that resolves entirely inside a pre-determined allowed root -- the
    // plugin system's own data_dir() tree (which holds each plugin's storage folder and the
    // installed/system profile cache), the read-only bundled resources folder, or a per-call
    // scoped root such as the current G-code folder -- is part of the plugin system's normal
    // workflow and does not need a prompt.
    if (fs_category && !targets.empty()) {
        const bool is_write = event_type != AuditEventCategory::FsRead;
        const bool all_inside_allowed_root =
            std::all_of(targets.begin(), targets.end(), [&](const std::string& target) {
                return mgr.check_path_access(boost::filesystem::path(target), is_write).allowed;
            });
        if (all_inside_allowed_root)
            return 0;
    }

    const int result = plugin_audit::decide_event(mgr, event_name, event_type, targets);

    // A granted operation covers its nested events while it runs. A targetless grant names no
    // operation, so it covers nothing else.
    if (result == 0 && !targets.empty() && operation_root_events.count(event_name))
        mgr.record_root_frame();
    return result;
}

} // namespace PluginAuditDetail

void PluginAuditManager::record_root_frame()
{
    PyObject* frame = reinterpret_cast<PyObject*>(PyEval_GetFrame()); // borrowed reference
    if (!frame || !PluginAuditDetail::is_stdlib_frame(frame))
        return;
    if (std::find(m_root_frames.begin(), m_root_frames.end(), frame) != m_root_frames.end())
        return; // glob.glob and glob.glob/2 share a frame
    Py_INCREF(frame);
    m_root_frames.push_back(frame);
}

bool PluginAuditManager::covered_by_root_frame() const
{
    if (m_root_frames.empty())
        return false;

    // Only stdlib frames may sit between the event and the operation: plugin or library code
    // the operation calls back into (an os.walk onerror, a urllib handler) acts on its own.
    for (PyObject* frame : PluginAuditDetail::stack_frames()) {
        if (std::find(m_root_frames.begin(), m_root_frames.end(), frame) != m_root_frames.end())
            return true;
        if (!PluginAuditDetail::is_stdlib_frame(frame))
            return false;
    }
    return false;
}

bool PluginAuditManager::releasing_frames() const { return m_releasing_frames > 0; }

bool PluginAuditManager::is_stdlib_file(const std::string& filename)
{
    // Frozen stdlib modules (os, posixpath, codecs, ...) carry a pseudo filename.
    if (boost::algorithm::starts_with(filename, "<frozen "))
        return true;
    if (filename.empty() || filename.front() == '<')
        return false;

    // Both sides are resolved: co_filename follows the Python home as it was given, which may
    // be a symlink.
    const auto [it, inserted] = PluginAuditDetail::s_stdlib_file_verdicts.try_emplace(filename, false);
    if (inserted) {
        const boost::filesystem::path file(filename);
        const auto inside_any = [&file](const std::vector<boost::filesystem::path>& dirs) {
            return std::any_of(dirs.begin(), dirs.end(),
                               [&file](const boost::filesystem::path& dir) { return is_inside_allowed_root(file, dir); });
        };
        it->second = inside_any(PluginAuditDetail::s_stdlib_dirs) && !inside_any(PluginAuditDetail::s_site_package_dirs);
    }
    return it->second;
}

void PluginAuditManager::release_frames(std::vector<PyObject*>& frames)
{
    if (frames.empty())
        return;

    // A finalizer's own denial must neither replace the PermissionError of the event being
    // decided nor leave a stale denial behind for the next call.
    PyObject* const      error          = PyErr_GetRaisedException();
    const bool           denial_pending = m_audit_denial_pending;
    const bool           has_violation  = m_has_last_violation;
    const AuditViolation violation      = m_last_violation;

    std::vector<PyObject*> released;
    released.swap(frames);
    ++m_releasing_frames;
    for (PyObject* frame : released)
        Py_DECREF(frame);
    --m_releasing_frames;

    m_audit_denial_pending = denial_pending;
    m_has_last_violation   = has_violation;
    m_last_violation       = violation;
    PyErr_SetRaisedException(error);
}

int PluginAuditManager::audit_hook(const char* event, PyObject* args, void* user_data)
{
    auto* mgr = static_cast<PluginAuditManager*>(user_data);
    if (!mgr)
        return 0;

    std::string event_name(event ? event : "");

    if (event_name.empty())
        return 0;

    // Verbose logging of every audit event (can be noisy)
    if (mgr->verbose_events) {
        BOOST_LOG_TRIVIAL(debug) << "[AUDIT EVENT] " << event_name;
    }

    if (mgr->current_plugin().empty()) {
        BOOST_LOG_TRIVIAL(trace) << "[AUDIT] event=" << event_name << " bypassed (no plugin context)";
        return 0;
    }

    // the open function can take in different flags that determine if it is a read or readwrite.
    const AuditEventCategory event_type =
        event_name == "open" ? PluginAuditDetail::open_category(args) : event_category(event_name);
    if (event_type == AuditEventCategory::None)
        return 0;

    // Root frames whose operation has returned are let go once this event is decided, so that a
    // finalizer they run re-enters a hook whose held frames are already consistent.
    std::vector<PyObject*> returned_frames;
    PluginAuditDetail::take_returned_frames(m_root_frames, returned_frames);
    const int result = PluginAuditDetail::audit_plugin_event(*mgr, event_name, event_type, args);
    mgr->release_frames(returned_frames);
    return result;
}

void PluginAuditManager::install_hook()
{
    // Tells the stdlib's own frames apart, for the operation-root frames (see record_root_frame).
    PluginAuditDetail::collect_stdlib_dirs();

    // data_dir() is the primary globally-allowed root during plugin execution: read+write. It
    // covers the plugin system's own workflow needs -- each plugin's storage folder
    // (data_dir()/orca_plugins) and the installed/system profile cache (data_dir()/system) --
    // without a separate, narrower grant for either (G-code plugins additionally get the temp
    // G-code folder via a scoped root, see SlicingPipelinePluginCapabilityTrampoline).
    add_global_allowed_root(data_dir());

    // resources_dir() holds the app's bundled, shared assets (installed system profiles, the
    // bundled TLS client cert, web assets). Plugins may read from it -- e.g. inspecting bundled
    // profiles -- but must never write into the shared, potentially multi-user app install, so
    // it is granted read-only. The bundled cert itself stays unreachable regardless, via the
    // "cert" denied-path keyword seeded below.
    add_global_allowed_root(resources_dir(), /*allow_write=*/false);

    // The user's app config and cloud credentials live directly inside data_dir(), so the
    // root just granted would otherwise expose them to any plugin.  Deny them by name.
    //
    // Seeded here rather than by each secret's owner because install_hook() runs during lazy
    // interpreter init and therefore provably precedes any plugin bytecode, whereas
    // OrcaCloudServiceAgent::set_config_dir runs during networking init — neither strictly
    // precedes the other, and if Orca cloud never initializes an owner-registered token deny
    // would never exist at all.  default_denied_filenames() is the single source of that list
    // (see its comment for why all four config names are denied); the tests seed from it too.
    for (const auto& name : default_denied_filenames())
        add_denied_filename(name);

    // Categorical denies on top of the exact-name list above: no path a plugin can reach may
    // contain a "secret", "cert"(ificate), or "conf"(ig) path component, regardless of which
    // allowed root it happens to sit inside. default_denied_path_keywords() is the single
    // source of this list; the tests seed from it too.
    for (const auto& keyword : default_denied_path_keywords())
        add_denied_path_keyword(keyword);

    if (PySys_AddAuditHook(audit_hook, this) < 0) {
        BOOST_LOG_TRIVIAL(error) << "[AUDIT] Failed to install CPython audit hook";
        return;
    }
    BOOST_LOG_TRIVIAL(info) << "[AUDIT] CPython audit hook installed successfully";
}

} // namespace Slic3r
