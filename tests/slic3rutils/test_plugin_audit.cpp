#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <catch2/catch_all.hpp>

#include <libslic3r/Utils.hpp>
#include <libslic3r/libslic3r.h>         // GCODEVIEWER_APP_KEY, SLIC3R_APP_KEY (via libslic3r_version.h)
#include <slic3r/plugin/PluginAuditManager.hpp>
#include <slic3r/Utils/OrcaCloudServiceAgent.hpp> // secret_constants::USER_SECRET_FILENAME

#include <catch2/catch_test_macros.hpp>
#include "libslic3r_version.h"
#include "plugin_test_utils.hpp"

#include <boost/filesystem.hpp>

#include <string>
#include <vector>

using namespace Slic3r;
namespace fs = boost::filesystem;

namespace {

// Seed the deny registry with the same list install_hook() uses. Both draw from
// PluginAuditManager::default_denied_filenames(), so the test and production seeding cannot
// drift apart. The registry is a process singleton, so repeated seeding only appends harmless
// duplicates; matching is unaffected.
void seed_denied_names()
{
    PluginAuditManager& mgr = PluginAuditManager::instance();
    for (const auto& name : PluginAuditManager::default_denied_filenames())
        mgr.add_denied_filename(name);
}

// Seed the keyword registry with the same list install_hook() uses. Same rationale as
// seed_denied_names(): a process-singleton registry, seeded from the single shared source so
// production and tests cannot drift apart.
void seed_denied_keywords()
{
    PluginAuditManager& mgr = PluginAuditManager::instance();
    for (const auto& keyword : PluginAuditManager::default_denied_path_keywords())
        mgr.add_denied_path_keyword(keyword);
}

} // namespace

TEST_CASE("Plugin audit denies app config and token filenames anywhere", "[audit]")
{
    seed_denied_names();
    const PluginAuditManager& mgr = PluginAuditManager::instance();

    SECTION("the seeded names are denied by their base name")
    {
        CHECK(mgr.is_denied_filename(fs::path(SLIC3R_APP_KEY ".conf")));
        CHECK(mgr.is_denied_filename(fs::path(GCODEVIEWER_APP_KEY ".conf")));
        CHECK(mgr.is_denied_filename(fs::path(SLIC3R_APP_KEY ".ini")));
        CHECK(mgr.is_denied_filename(fs::path(GCODEVIEWER_APP_KEY ".ini")));
        CHECK(mgr.is_denied_filename(fs::path(secret_constants::USER_SECRET_FILENAME)));
    }

    SECTION("companions holding the same secrets are denied by the prefix rule")
    {
        CHECK(mgr.is_denied_filename(fs::path(SLIC3R_APP_KEY ".conf.bak")));
        CHECK(mgr.is_denied_filename(fs::path(std::string(secret_constants::USER_SECRET_FILENAME) + ".tmp")));
        // Windows alternate data streams share the same base name.
        CHECK(mgr.is_denied_filename(fs::path(SLIC3R_APP_KEY ".conf:stream")));
    }

    SECTION("the denial ignores the directory the file lives in")
    {
        CHECK(mgr.is_denied_filename(fs::path("/tmp") / (SLIC3R_APP_KEY ".conf")));
        CHECK(mgr.is_denied_filename(fs::path("/some/plugin/dir") / (SLIC3R_APP_KEY ".conf")));
        // Traversal is handled for free: filename() of the path below is already the denied name.
        CHECK(mgr.is_denied_filename(fs::path(data_dir()) / "plugins" / ".." / (SLIC3R_APP_KEY ".conf")));
    }

    SECTION("matching is case-insensitive on every platform")
    {
        CHECK(mgr.is_denied_filename(fs::path("orcaslicer.conf")));
        CHECK(mgr.is_denied_filename(fs::path("ORCASLICER.CONF")));
        CHECK(mgr.is_denied_filename(fs::path("ORCA_REFRESH_TOKEN.SEC")));
    }

    SECTION("an unrelated name that merely shares a stem is not denied")
    {
        // The prefix is the full registered name ("OrcaSlicer.conf"), not the stem "OrcaSlicer",
        // so a sibling file with a different extension/suffix stays allowed.
        CHECK_FALSE(mgr.is_denied_filename(fs::path(data_dir()) / (SLIC3R_APP_KEY "_other.txt")));
        CHECK_FALSE(mgr.is_denied_filename(fs::path(data_dir()) / (SLIC3R_APP_KEY ".json")));
        CHECK_FALSE(mgr.is_denied_filename(fs::path("orca_refresh_token.txt")));
    }

    SECTION("an empty path is not denied")
    {
        CHECK_FALSE(mgr.is_denied_filename(fs::path()));
    }
}

TEST_CASE("Plugin audit deny beats allowed roots", "[audit]")
{
    ScopedDataDir data_dir_guard("plugin-audit-deny");
    seed_denied_names();

    PluginAuditManager& mgr = PluginAuditManager::instance();
    // Reproduce install_hook()'s grant: data_dir() is a global allowed root, so both the app
    // config and the token would otherwise be reachable simply by living inside it.
    mgr.add_global_allowed_root(data_dir());

    // Enter a plugin context. The deny must hold even inside a globally allowed root.
    ScopedPluginAuditContext ctx("test_plugin", "");

    const fs::path conf  = fs::path(data_dir()) / (SLIC3R_APP_KEY ".conf");
    const fs::path token = fs::path(data_dir()) / secret_constants::USER_SECRET_FILENAME;

    SECTION("a non-denied file inside the allowed root is writable (root really grants writes)")
    {
        AuditDecision decision = mgr.check_open((fs::path(data_dir()) / "plugin_data.txt").string(), "w");
        CHECK(decision.allowed);
    }

    SECTION("a file outside the allowed root is blocked for reads as well as writes")
    {
        AuditDecision decision = mgr.check_open((fs::path(data_dir()).parent_path() / "outside.txt").string(), "r");
        CHECK_FALSE(decision.allowed);
        CHECK(decision.reason == "outside allowed root");
    }

    SECTION("writing the app config is blocked despite data_dir() being allowed")
    {
        AuditDecision decision = mgr.check_open(conf.string(), "w");
        CHECK_FALSE(decision.allowed);
        CHECK(decision.reason == "denied filename");
    }

    SECTION("reading the app config is blocked despite the allowed root")
    {
        AuditDecision decision = mgr.check_open(conf.string(), "r");
        CHECK_FALSE(decision.allowed);
        CHECK(decision.reason == "denied filename");
    }

    SECTION("reading the cloud refresh token is blocked")
    {
        AuditDecision decision = mgr.check_open(token.string(), "r");
        CHECK_FALSE(decision.allowed);
    }

    SECTION("the token staging companion (.tmp) is blocked too")
    {
        AuditDecision decision = mgr.check_open((token.string() + ".tmp"), "w");
        CHECK_FALSE(decision.allowed);
    }

    SECTION("a traversal path resolving to the config is blocked")
    {
        const fs::path traversal = fs::path(data_dir()) / "plugins" / ".." / (SLIC3R_APP_KEY ".conf");
        AuditDecision  decision  = mgr.check_open(traversal.string(), "r");
        CHECK_FALSE(decision.allowed);
    }
}

TEST_CASE("Plugin audit deny beats a plugin's own scoped root", "[audit]")
{
    ScopedDataDir data_dir_guard("plugin-audit-scoped");
    seed_denied_names();

    PluginAuditManager& mgr = PluginAuditManager::instance();

    // A plugin's private directory, granted as a scoped root while it runs.
    const fs::path plugin_dir = fs::path(data_dir()) / "plugins" / "test_plugin";
    fs::create_directories(plugin_dir);

    ScopedPluginAuditContext ctx("test_plugin", "");
    mgr.add_scoped_allowed_root(plugin_dir);

    SECTION("the plugin's own non-denied file opens for read and write")
    {
        const std::string own_file = (plugin_dir / "state.json").string();
        CHECK(mgr.check_open(own_file, "r").allowed);
        CHECK(mgr.check_open(own_file, "w").allowed);
    }

    SECTION("a denied name stashed inside the plugin's own root is still blocked")
    {
        const std::string smuggled = (plugin_dir / (SLIC3R_APP_KEY ".conf")).string();
        AuditDecision      decision = mgr.check_open(smuggled, "w");
        CHECK_FALSE(decision.allowed);
        CHECK(decision.reason == "denied filename");
    }
}

TEST_CASE("Plugin audit does not constrain non-plugin code", "[audit]")
{
    ScopedDataDir data_dir_guard("plugin-audit-noplugin");
    seed_denied_names();

    PluginAuditManager& mgr = PluginAuditManager::instance();
    mgr.clear_current_plugin(); // no plugin context: this is OrcaSlicer's own C++/internal Python

    const fs::path conf = fs::path(data_dir()) / (SLIC3R_APP_KEY ".conf");

    // The name is still recognised as denied...
    CHECK(mgr.is_denied_filename(conf));
    // ...but with no current plugin the access check allows it: denies constrain plugin code only.
    CHECK(mgr.check_open(conf.string(), "w").allowed);
    CHECK(mgr.check_open(conf.string(), "r").allowed);
}

TEST_CASE("Plugin audit denies secret/certificate/config-like paths by keyword", "[audit]")
{
    seed_denied_keywords();
    const PluginAuditManager& mgr = PluginAuditManager::instance();

    SECTION("a 'secrets' directory component is denied")
    {
        CHECK(mgr.is_denied_path_keyword(fs::path("/plugin/secrets/api_key.json")));
        CHECK(mgr.is_denied_path_keyword(fs::path("/plugin/secret/token.txt")));
    }

    SECTION("a 'certificate(s)' directory component is denied")
    {
        CHECK(mgr.is_denied_path_keyword(fs::path("/resources/cert/slicer_base64.cer")));
        CHECK(mgr.is_denied_path_keyword(fs::path("/resources/certificates/ca.pem")));
    }

    SECTION("a 'conf'/'config' directory or config file component is denied")
    {
        CHECK(mgr.is_denied_path_keyword(fs::path("/plugin/conf/settings.json")));
        CHECK(mgr.is_denied_path_keyword(fs::path("/plugin/config/settings.json")));
        CHECK(mgr.is_denied_path_keyword(fs::path("/plugin/plugin.conf")));
        CHECK(mgr.is_denied_path_keyword(fs::path("/plugin/plugin.ini")));
    }

    SECTION("matching is case-insensitive")
    {
        CHECK(mgr.is_denied_path_keyword(fs::path("/plugin/SECRETS/token.txt")));
        CHECK(mgr.is_denied_path_keyword(fs::path("/resources/Certificates/ca.pem")));
        CHECK(mgr.is_denied_path_keyword(fs::path("/plugin/PLUGIN.CONF")));
    }

    SECTION("matching is not limited to the base name -- any ancestor component counts")
    {
        CHECK(mgr.is_denied_path_keyword(fs::path("/data/secrets/nested/deep/file.txt")));
    }

    SECTION("an unrelated path is not denied")
    {
        CHECK_FALSE(mgr.is_denied_path_keyword(fs::path("/plugin/output/model.gcode")));
        CHECK_FALSE(mgr.is_denied_path_keyword(fs::path("/plugin/storage/state.json")));
        CHECK_FALSE(mgr.is_denied_path_keyword(fs::path("/python/packages/cp312/numpy/__config__.py")));
        CHECK_FALSE(mgr.is_denied_path_keyword(fs::path("/python/packages/cp312/numpy/_core/_ufunc_config.py")));
        CHECK_FALSE(mgr.is_denied_path_keyword(fs::path("/python/Lib/configparser.py")));
        CHECK_FALSE(mgr.is_denied_path_keyword(fs::path("/python/Lib/sysconfig.py")));
        CHECK_FALSE(mgr.is_denied_path_keyword(fs::path("/python/Lib/logging/config.py")));
        CHECK_FALSE(mgr.is_denied_path_keyword(fs::path("/python/packages/cp312/certifi/cacert.pem")));
        CHECK_FALSE(mgr.is_denied_path_keyword(fs::path("/users/Conference/output.txt")));
        CHECK_FALSE(mgr.is_denied_path_keyword(fs::path("/users/Concert/output.txt")));
    }

    SECTION("an empty path is not denied")
    {
        CHECK_FALSE(mgr.is_denied_path_keyword(fs::path()));
    }
}

TEST_CASE("Plugin audit is_denied_path combines the filename and keyword registries", "[audit]")
{
    seed_denied_names();
    seed_denied_keywords();
    const PluginAuditManager& mgr = PluginAuditManager::instance();

    SECTION("a filename-registry match is denied")
    {
        CHECK(mgr.is_denied_path(fs::path(SLIC3R_APP_KEY ".conf")));
    }

    SECTION("a keyword-registry match is denied")
    {
        CHECK(mgr.is_denied_path(fs::path("/plugin/secrets/token.txt")));
    }

    SECTION("a path matching neither registry is not denied")
    {
        CHECK_FALSE(mgr.is_denied_path(fs::path("/plugin/output/model.gcode")));
    }
}

TEST_CASE("Plugin audit a read-only allowed root blocks writes but not reads", "[audit]")
{
    ScopedDataDir      data_dir_guard("plugin-audit-readonly");
    ScopedResourcesDir resources_dir_guard("plugin-audit-readonly-resources");
    seed_denied_names();
    seed_denied_keywords();

    PluginAuditManager& mgr = PluginAuditManager::instance();
    mgr.add_global_allowed_root(resources_dir(), /*allow_write=*/false);

    ScopedPluginAuditContext ctx("test_plugin", "");

    const fs::path readonly_file = fs::path(resources_dir()) / "profiles" / "vendor.json";

    SECTION("a read inside the read-only root is allowed")
    {
        CHECK(mgr.check_open(readonly_file.string(), "r").allowed);
    }

    SECTION("a write inside the read-only root is blocked")
    {
        AuditDecision decision = mgr.check_open(readonly_file.string(), "w");
        CHECK_FALSE(decision.allowed);
        CHECK(decision.reason == "outside allowed root");
    }

    SECTION("a create inside the read-only root is blocked")
    {
        AuditDecision decision = mgr.check_path_access(readonly_file, /*is_write=*/true);
        CHECK_FALSE(decision.allowed);
    }

    SECTION("the bundled cert underneath the read-only root is denied even for reads")
    {
        const fs::path cert = fs::path(resources_dir()) / "cert" / "slicer_base64.cer";
        AuditDecision  decision = mgr.check_open(cert.string(), "r");
        CHECK_FALSE(decision.allowed);
        CHECK(decision.reason == "denied path keyword");
    }
}

TEST_CASE("Plugin audit network permissions match by scheme and host", "[audit]")
{
    SECTION("a URL is reduced to its scheme and host")
    {
        CHECK(PluginAuditManager::normalize_network_url("https://API.Example.com:8443/v1/items?page=2#top") ==
              "https://api.example.com");
        CHECK(PluginAuditManager::normalize_network_url("http://example.com") == "http://example.com");
        CHECK(PluginAuditManager::normalize_network_url("https://[2001:db8::1]:443/path") == "https://[2001:db8::1]");
    }

    SECTION("non-http schemes and targets without a scheme are not network URLs")
    {
        CHECK(PluginAuditManager::normalize_network_url("ftp://example.com").empty());
        CHECK(PluginAuditManager::normalize_network_url("api.example.com").empty());
        CHECK(PluginAuditManager::normalize_network_url("https://").empty());
    }

    SECTION("a URL target matches the same scheme and host regardless of path and port")
    {
        const std::vector<std::string> granted{"https://api.example.com"};
        CHECK(PluginAuditManager::network_permission_matches(granted, "https://api.example.com/v1/items?page=2"));
        CHECK(PluginAuditManager::network_permission_matches(granted, "https://api.example.com:8443/upload"));
    }

    SECTION("a URL target does not match a different host or a different scheme")
    {
        const std::vector<std::string> granted{"https://api.example.com"};
        CHECK_FALSE(PluginAuditManager::network_permission_matches(granted, "https://evil.example.com/v1"));
        CHECK_FALSE(PluginAuditManager::network_permission_matches(granted, "http://api.example.com/v1"));
    }

    SECTION("a bare host target matches the host of a grant")
    {
        const std::vector<std::string> granted{"https://api.example.com"};
        CHECK(PluginAuditManager::network_permission_matches(granted, "api.example.com"));
        CHECK(PluginAuditManager::network_permission_matches(granted, "api.example.com:443"));
        CHECK_FALSE(PluginAuditManager::network_permission_matches(granted, "other.example.com"));
    }

    SECTION("legacy persisted address tuples keep matching their host")
    {
        const std::vector<std::string> legacy_granted{"('api.example.com', 443)"};
        CHECK(PluginAuditManager::network_permission_matches(legacy_granted, "api.example.com"));

        const std::vector<std::string> granted{"https://api.example.com"};
        CHECK(PluginAuditManager::network_permission_matches(granted, "('api.example.com', 443)"));
        CHECK_FALSE(PluginAuditManager::network_permission_matches(granted, "('93.184.216.34', 443)"));
    }

    SECTION("an IPv6 literal matches across bracketed and bare forms")
    {
        const std::vector<std::string> granted{"https://[2001:db8::1]"};
        CHECK(PluginAuditManager::network_permission_matches(granted, "2001:db8::1"));
        CHECK(PluginAuditManager::network_permission_matches(granted, "[2001:db8::1]:443"));
        CHECK_FALSE(PluginAuditManager::network_permission_matches(granted, "2001:db8::2"));
    }

    SECTION("a non-http URL only matches an exact grant")
    {
        const std::vector<std::string> granted{"ftp://example.com/file"};
        CHECK(PluginAuditManager::network_permission_matches(granted, "ftp://example.com/file"));
        CHECK_FALSE(PluginAuditManager::network_permission_matches(granted, "ftp://example.com/other"));

        const std::vector<std::string> https{"https://api.example.com"};
        CHECK_FALSE(PluginAuditManager::network_permission_matches(https, "ftp://api.example.com/file"));
    }

    SECTION("an empty grant list or an empty target never matches")
    {
        const std::vector<std::string> granted{};
        CHECK_FALSE(PluginAuditManager::network_permission_matches(granted, "https://api.example.com"));

        const std::vector<std::string> any{"https://api.example.com"};
        CHECK_FALSE(PluginAuditManager::network_permission_matches(any, ""));
    }
}

TEST_CASE("Plugin audit limits network ancestor approvals to resolved addresses", "[audit]")
{
    SECTION("IP literals are distinguished from hostnames and URLs")
    {
        CHECK(PluginAuditManager::is_ip_address_target("127.0.0.1"));
        CHECK(PluginAuditManager::is_ip_address_target("93.184.216.34"));
        CHECK(PluginAuditManager::is_ip_address_target("::1"));
        CHECK(PluginAuditManager::is_ip_address_target("2001:db8::1"));
        CHECK(PluginAuditManager::is_ip_address_target("[2001:db8::1]"));

        CHECK_FALSE(PluginAuditManager::is_ip_address_target("api.example.com"));
        CHECK_FALSE(PluginAuditManager::is_ip_address_target("localhost"));
        CHECK_FALSE(PluginAuditManager::is_ip_address_target("https://api.example.com"));
        CHECK_FALSE(PluginAuditManager::is_ip_address_target(""));
    }

    SECTION("a network approval is kept apart from the general call-site cascade")
    {
        PluginAuditManager& mgr = PluginAuditManager::instance();
        const std::string   plugin_key = "plugin-audit-network-ancestor";
        const std::string   other_key  = "plugin-audit-network-ancestor-other";

        mgr.record_approved_network_call_sites(plugin_key, {"frame:network"});
        mgr.record_approved_call_sites(plugin_key, {"frame:general"});

        CHECK(mgr.has_approved_network_ancestor(plugin_key, {"frame:network"}));
        CHECK_FALSE(mgr.has_approved_network_ancestor(plugin_key, {"frame:general"}));
        CHECK_FALSE(mgr.has_approved_network_ancestor(other_key, {"frame:network"}));

        // The network approval must not leak into the general cascade used by other categories.
        CHECK_FALSE(mgr.has_approved_ancestor(plugin_key, {"frame:network"}));
    }
}

TEST_CASE("Plugin audit a scoped root can also be registered read-only", "[audit]")
{
    ScopedDataDir data_dir_guard("plugin-audit-scoped-readonly");
    seed_denied_names();
    seed_denied_keywords();

    PluginAuditManager& mgr = PluginAuditManager::instance();

    const fs::path readonly_dir = fs::path(data_dir()) / "readonly_scope";
    fs::create_directories(readonly_dir);

    ScopedPluginAuditContext ctx("test_plugin", "");
    mgr.add_scoped_allowed_root(readonly_dir, /*allow_write=*/false);

    SECTION("a read inside the scoped read-only root is allowed")
    {
        CHECK(mgr.check_open((readonly_dir / "vendor.json").string(), "r").allowed);
    }

    SECTION("a write inside the scoped read-only root is blocked")
    {
        CHECK_FALSE(mgr.check_open((readonly_dir / "vendor.json").string(), "w").allowed);
    }
}
