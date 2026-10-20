#ifndef slic3r_PluginAuditManager_hpp_
#define slic3r_PluginAuditManager_hpp_

// Via pybind11 so this file requests the same python3xx.lib as everything else.
#include <boost/filesystem/path.hpp>
#include <pybind11/conduit/wrap_include_python_h.h>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <boost/filesystem.hpp>

namespace Slic3r {

struct AuditDecision {
    bool        allowed = true;
    std::string reason;
};

struct AuditViolation {
    std::string            plugin_key;
    std::string            event_name;
    std::string            reason;
};

// One classified CPython audit event observed while the probe is in record-only mode. This is
// evidence about the code path that ran, not a grant request: the hook allowed the operation to
// continue and did not consult the permission dialog or the persisted install state.
struct AuditObservation {
    std::string              plugin_key;
    std::string              capability;
    std::string              phase;
    std::string              event_name;
    std::string              category;
    std::vector<std::string> targets;
    bool                     continued = true;
};

// A filesystem root a plugin may access while an audit context is active. allow_write is
// false for a root that only grants reads (e.g. the bundled, shared resources folder) --
// a write-shaped event never matches such a root, even though a read-shaped one does.
struct AllowedRoot {
    boost::filesystem::path path;
    bool                    allow_write = true;
};

// The set of CPython audit events PluginAuditManager recognizes, grouped by the kind of
// operation they represent. None means the event isn't one audit_hook() acts on at all.
enum class AuditEventCategory {
    None,
    FsRead,
    FsReadWrite,
    FsCreate,
    FsDelete,
    Http,
    Socket,
    ProcessCreate,
    ProcessReplace,
    Threading,
};

// Returns true if candidate resolves to a path inside allowed_root.
// Uses weakly_canonical and component-wise comparison to reject traversal attacks.
bool is_inside_allowed_root(const boost::filesystem::path& candidate,
                            const boost::filesystem::path& allowed_root);

class PluginAuditManager
{
public:
    static PluginAuditManager& instance();

    // Call once after Py_Initialize to install the global audit hook.
    void install_hook();

    // --- current-plugin context (thread_local) ---
    void        set_current_plugin(const std::string& plugin_key);
    std::string current_plugin() const;
    void        clear_current_plugin();

    // --- current-capability context (thread_local) ---
    // The capability whose method is currently executing, within the current plugin. Empty
    // while a plugin-wide call runs, and during capture (get_name/get_type), where the
    // capability has no cached name yet.
    void        set_current_capability(const std::string& capability_name);
    std::string current_capability() const;
    void        clear_current_capability();

    // --- allowed-roots registry ---
    // allow_write = false registers a read-only root: a read-shaped event inside it is allowed,
    // but a write/create/delete-shaped event is not, so it falls through to the normal
    // prompt-or-deny path instead.
    void add_global_allowed_root(const boost::filesystem::path& root, bool allow_write = true);
    void add_scoped_allowed_root(const boost::filesystem::path& root, bool allow_write = true);

    // --- denied-filenames registry ---
    // Filenames a plugin may never touch, in any directory, regardless of the enclosing allowed
    // root.  A candidate is denied when its filename starts with a
    // registered name, so .bak/.tmp companions are covered by the same entry.
    //
    // The comparison is case-insensitive on every platform, unlike the _WIN32-only iequals
    // in is_inside_allowed_root: the default macOS APFS configuration is case-insensitive
    // too, so `orcaslicer.conf` reaches the real file there.  Over-blocking a genuinely
    // distinct name on Linux is the fail-safe direction and costs nothing real.
    void add_denied_filename(const std::string& filename);

    // The list install_hook() seeds into the deny registry: the app config (both app keys and
    // both extensions) and the cloud refresh token. Exposed so tests seed the exact same set
    // without a live interpreter, so the test and production seeding cannot drift apart.
    static std::vector<std::string> default_denied_filenames();

    // True when candidate's base name starts with a denied name (case-insensitive). No path
    // resolution: laundering a denied file through a symlink, hardlink, subprocess, or Windows
    // 8.3 short name is out of scope (see the design doc). This blocks direct access only.
    bool is_denied_filename(const boost::filesystem::path& candidate) const;

    // --- denied-path-keyword registry ---
    // Keywords that categorically deny a path if ANY component matches one case-insensitively --
    // e.g. a "secrets" subfolder, a "certificates" folder, a "conf"/"config" directory, or a
    // .conf/.ini file anywhere the plugin can otherwise reach, including inside an allowed root.
    // This is broader than the exact-name is_denied_filename registry, but it is not a substring
    // match: importable modules such as numpy/__config__.py, configparser.py, sysconfig.py, or
    // user folders such as "Conference" and "Concert" are unrelated names and must stay promptable.
    void add_denied_path_keyword(const std::string& keyword);

    // The list install_hook() seeds into the keyword registry. Exposed so tests seed the exact
    // same set without a live interpreter.
    static std::vector<std::string> default_denied_path_keywords();

    // True when any component of candidate's (canonicalized) path matches a registered keyword,
    // case-insensitively. A registered "conf" keyword also blocks .conf/.ini file components.
    bool is_denied_path_keyword(const boost::filesystem::path& candidate) const;

    // is_denied_filename(candidate) || is_denied_path_keyword(candidate). Convenience for
    // call sites that only need to know whether a path is categorically off-limits, not which
    // specific rule fired.
    bool is_denied_path(const boost::filesystem::path& candidate) const;

    // --- policy checks ---
    // Shared core for every audited filesystem event.  The deny checks are consulted above the
    // allowed roots, so a denied path is blocked even when it sits inside an allowed root (e.g.
    // data_dir(), which is a global allowed root).
    AuditDecision check_path_access(const boost::filesystem::path& candidate, bool is_write);
    AuditDecision check_open(const std::string& path, const std::string& mode);

    // Ask the user to grant the requested filesystem-read paths. The request may originate on a
    // plugin load worker, so the implementation marshals the modal dialog to the wx main thread.
    // Returns true only when every missing path was granted and persisted; denial aborts the plugin
    // load without adding a permission.
    bool request_filesystem_read_permissions(const std::string& plugin_key,
                                             const std::vector<std::string>& paths);

    // --- probe observation mode (thread-safe, never used by the GUI) ---
    // When an observer is installed, every classified event raised by plugin-authored code is
    // reported to it and then allowed through (the hook returns 0). The permission dialogs, saved
    // grants and .install_state.json are never consulted, so this is not a containment boundary.
    // The observer must not touch Python objects: it runs inside the audit hook with the GIL held.
    using AuditObserver = std::function<void(const AuditObservation&)>;
    void set_observation_observer(AuditObserver observer);
    void clear_observation_observer();
    bool observation_mode() const;

    // Stop after classification/target extraction; true means an observer consumed the event.
    // Exposed for the probe and its tests, not for production callers.
    bool record_observation(const std::string& event_name,
                            const std::string& category,
                            const std::vector<std::string>& targets);

    // --- probe phase attribution (thread_local) ---
    // Labels events for the probe's report. Production leaves it empty. A Python thread spawned by
    // plugin code inherits no phase, so its events are reported as unattributed rather than being
    // guessed at.
    void               set_current_audit_phase(const std::string& phase);
    const std::string& current_audit_phase() const;
    void               clear_current_audit_phase();

    void report_violation(const AuditViolation& violation);
    bool audit_denial_pending() const;
    void clear_audit_denial();
    void clear_last_violation();
    bool last_violation(AuditViolation& violation) const;

    // --- operation-root frames ---
    // One stdlib operation fires several nested audit events as it passes through lower layers:
    // urlopen() raises urllib.Request, then http.client and socket events. When an operation's
    // own root event is granted (by a saved grant or the user), the stdlib frame that raised it
    // is held, and a later event whose stack reaches that frame through stdlib frames only is
    // part of the same operation, so it is allowed without a prompt of its own. Plugin or
    // library code the operation calls back into is judged on its own. A frame is let go once it
    // returns, a suspended generator's (os.walk) when the audit scope ends. Letting a frame go may
    // run finalizers of objects its locals held; they are judged as the plugin and never prompt.
    void record_root_frame();
    bool covered_by_root_frame() const;
    bool releasing_frames() const;

    // True when code from `filename` (a code object's co_filename) is the interpreter's own
    // stdlib: the only code a root frame's coverage passes through. Valid after install_hook().
    static bool is_stdlib_file(const std::string& filename);

    // Answers permission prompts in place of the dialog, for tests. Production never sets it.
    using PermissionPrompt = std::function<bool(const std::string& event_name, const std::vector<std::string>& targets)>;
    static void set_permission_prompt_for_testing(PermissionPrompt prompt);

    bool verbose_events = true;

private:
    friend class ScopedPluginAuditContext;

    PluginAuditManager() = default;

    static int audit_hook(const char* event, PyObject* args, void* user_data);

    // Drops the references in `frames`, keeping the caller's error and denial state intact.
    void release_frames(std::vector<PyObject*>& frames);

    static thread_local std::string                   m_current_plugin_key;
    static thread_local std::string                   m_current_capability_name;
    static thread_local std::string                   m_current_audit_phase;
    static thread_local std::vector<AllowedRoot>       m_scoped_allowed_roots;
    static thread_local std::vector<PyObject*>         m_root_frames; // strong references
    static thread_local int                            m_releasing_frames;
    static thread_local bool                           m_audit_denial_pending;
    static thread_local bool                           m_has_last_violation;
    static thread_local AuditViolation                 m_last_violation;

    // mutable: is_denied_filename() is a const query that must lock.
    mutable std::mutex m_mutex;
    std::vector<AllowedRoot>             m_global_allowed_roots;
    std::vector<std::string>             m_denied_filenames;
    std::vector<std::string>             m_denied_path_keywords;

    // Kept apart from m_mutex: observation_mode() is queried from the audit hook while the hook
    // already holds the GIL, and the setter runs before plugin code starts.
    mutable std::mutex m_observer_mutex;
    AuditObserver      m_observation_observer;
};

// RAII guard that sets the current plugin key and capability name, restoring the previous
// pair on scope exit. `capability_name` may be empty for calls that are not scoped to a
// single capability.
//
// Each scope owns the root frames recorded inside it and lets them go, as its own plugin, before
// restoring the outer context. That needs the GIL whenever a frame is still held, so construct
// the scope after the GIL guard, to be destroyed before it.
class ScopedPluginAuditContext
{
public:
    explicit ScopedPluginAuditContext(
        const std::string&                   plugin_key,
        const std::string&                   capability_name = {});

    ~ScopedPluginAuditContext();

    ScopedPluginAuditContext(const ScopedPluginAuditContext&)            = delete;
    ScopedPluginAuditContext& operator=(const ScopedPluginAuditContext&) = delete;

private:
    std::string                   m_plugin_key;
    std::string                   m_capability;
    std::string                   m_previous_id;
    std::string                   m_previous_capability;
    std::vector<AllowedRoot>      m_previous_scoped_roots;
    std::vector<PyObject*>        m_previous_root_frames;
};

// Sets the probe's phase label on the current thread and restores the previous one on scope exit.
// Empty in production, where no phase is ever set.
class ScopedPluginAuditPhase
{
public:
    explicit ScopedPluginAuditPhase(std::string phase);
    ~ScopedPluginAuditPhase();

    ScopedPluginAuditPhase(const ScopedPluginAuditPhase&)            = delete;
    ScopedPluginAuditPhase& operator=(const ScopedPluginAuditPhase&) = delete;

private:
    std::string m_phase;
    std::string m_previous_phase;
};

} // namespace Slic3r

#endif // slic3r_PluginAuditManager_hpp_
