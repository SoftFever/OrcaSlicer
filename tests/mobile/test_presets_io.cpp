#include <catch2/catch_test_macros.hpp>

#include "test_utils.hpp"

#include "mobile/core/OrcaMobile.hpp"
#include "mobile/core/Session.hpp"

#include <nlohmann/json.hpp>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <algorithm>
#include <string>

using namespace Slic3r;
using namespace Slic3r::Mobile;

namespace {

namespace fs = boost::filesystem;

struct ScopedDataDir
{
    fs::path path;
    ScopedDataDir() : path(fs::temp_directory_path() / fs::unique_path("orca-mobile-io-%%%%-%%%%")) { fs::create_directories(path); }
    ~ScopedDataDir() { boost::system::error_code ec; fs::remove_all(path, ec); }
};

// One data directory per process; the façade's directories are process-global.
const ScopedDataDir& data_directory()
{
    static ScopedDataDir dir;
    static bool          once = [] {
        set_directories(std::string(RESOURCES_DIR), dir.path.string());
        return true;
    }();
    (void) once;
    return dir;
}

const std::string& first_system_containing(const std::vector<PresetInfo>& presets, const std::string& needle)
{
    for (const PresetInfo& p : presets)
        if (p.is_system && p.name.find(needle) != std::string::npos)
            return p.name;
    FAIL("no system preset containing '" << needle << "'");
    static const std::string none;
    return none;
}

const std::string& first_compatible(const std::vector<PresetInfo>& presets)
{
    for (const PresetInfo& p : presets)
        if (p.is_system && p.is_compatible)
            return p.name;
    FAIL("no compatible system preset");
    static const std::string none;
    return none;
}

bool has_user_preset(Session& session, PresetKind kind, const std::string& name)
{
    const std::vector<PresetInfo> presets = session.presets(kind);
    return std::any_of(presets.begin(), presets.end(), [&](const PresetInfo& p) { return p.name == name && p.is_user; });
}

// Loads the Voron 2.4 350 0.4 printer and saves one user printer, filament and process
// derived from the compatible system presets, with one option changed each.
struct UserPresets
{
    std::string printer, filament, process;
};
UserPresets make_user_presets(Session& session, const std::string& suffix)
{
    REQUIRE(session.load_presets().empty());
    REQUIRE(session.select_preset(PresetKind::Printer, first_system_containing(session.presets(PresetKind::Printer), "Voron 2.4 350 0.4 nozzle")));
    REQUIRE(session.select_preset(PresetKind::Filament, first_compatible(session.presets(PresetKind::Filament))));
    REQUIRE(session.select_preset(PresetKind::Process, first_compatible(session.presets(PresetKind::Process))));

    UserPresets u { "Test printer " + suffix, "Test filament " + suffix, "Test process " + suffix };
    REQUIRE(session.set_option("printer_notes", "exported from the test"));
    REQUIRE(session.save_preset_as(PresetKind::Printer, u.printer));
    REQUIRE(session.set_option("filament_cost", "42"));
    REQUIRE(session.save_preset_as(PresetKind::Filament, u.filament));
    REQUIRE(session.set_option("layer_height", "0.3"));
    REQUIRE(session.save_preset_as(PresetKind::Process, u.process));
    return u;
}

void write_text(const fs::path& path, const std::string& text)
{
    boost::nowide::ofstream out(path.string());
    out << text;
}

} // namespace

TEST_CASE("A printer bundle round-trips through export and import as user presets", "[PresetsIO][slow]")
{
    const ScopedDataDir& dir = data_directory();
    Session              source;
    const UserPresets    u = make_user_presets(source, "rt");

    const std::string bundle = (dir.path / "export" / (u.printer + ".orca_printer")).string();
    REQUIRE(source.export_presets({ { PresetKind::Printer, u.printer }, { PresetKind::Filament, u.filament }, { PresetKind::Process, u.process } }, bundle) == bundle);
    REQUIRE(fs::exists(bundle));

    // Delete the originals so the import has something to add.
    REQUIRE(source.delete_preset(PresetKind::Process, u.process));
    REQUIRE(source.delete_preset(PresetKind::Filament, u.filament));
    REQUIRE(source.delete_preset(PresetKind::Printer, u.printer));
    CHECK_FALSE(has_user_preset(source, PresetKind::Printer, u.printer));

    ImportPresetsResult result = source.import_presets(bundle, /*overwrite=*/false, /*as_bundle=*/false);
    CHECK(result.conflicts.empty());
    CHECK(result.skipped.empty());
    REQUIRE(result.imported.size() == 3);
    REQUIRE(source.reload_presets().empty());

    CHECK(has_user_preset(source, PresetKind::Printer, u.printer));
    CHECK(has_user_preset(source, PresetKind::Filament, u.filament));
    CHECK(has_user_preset(source, PresetKind::Process, u.process));
    // Imported as plain user presets: editable, with a sidecar, and the changed value survived.
    const std::string file = source.preset_file(PresetKind::Process, u.process);
    REQUIRE_FALSE(file.empty());
    CHECK(fs::exists(fs::path(file).replace_extension(".info")));
    REQUIRE(source.select_preset(PresetKind::Process, u.process));
    CHECK(source.option("layer_height") == "0.3");

    SECTION("a second import reports the clashes and leaves the presets alone")
    {
        ImportPresetsResult again = source.import_presets(bundle, false, false);
        CHECK(again.imported.empty());
        CHECK(again.conflicts.size() == 3);

        ImportPresetsResult forced = source.import_presets(bundle, true, false);
        CHECK(forced.imported.size() == 3);
    }
}

TEST_CASE("Exporting a built-in preset is refused", "[PresetsIO][slow]")
{
    const ScopedDataDir& dir = data_directory();
    Session              session;
    REQUIRE(session.load_presets().empty());
    const std::string system_printer = first_system_containing(session.presets(PresetKind::Printer), "Voron 2.4 350 0.4 nozzle");
    CHECK_THROWS(session.export_presets({ { PresetKind::Printer, system_printer } }, (dir.path / "refused.orca_printer").string()));
}

TEST_CASE("Import reports why a preset file was skipped", "[PresetsIO][slow]")
{
    const ScopedDataDir& dir = data_directory();
    Session              session;
    REQUIRE(session.load_presets().empty());
    const std::string system_process = first_system_containing(session.presets(PresetKind::Process), "Voron");

    SECTION("a parent that is not installed")
    {
        const fs::path file = dir.path / "orphan.json";
        write_text(file, nlohmann::json({ { "name", "Orphan process" }, { "version", "2.0.0.0" }, { "inherits", "No such preset @Nowhere" },
                                          { "print_settings_id", "Orphan process" }, { "layer_height", "0.3" } })
                             .dump());
        const ImportPresetsResult r = session.import_presets(file.string(), false, false);
        CHECK(r.imported.empty());
        REQUIRE(r.skipped.size() == 1);
        CHECK(r.skipped[0].reason == "missing_parent");
        CHECK(r.skipped[0].inherits == "No such preset @Nowhere");
    }
    SECTION("a name that belongs to a built-in preset")
    {
        const fs::path file = dir.path / "system-name.json";
        write_text(file, nlohmann::json({ { "name", system_process }, { "version", "2.0.0.0" }, { "print_settings_id", system_process }, { "layer_height", "0.3" } }).dump());
        const ImportPresetsResult r = session.import_presets(file.string(), false, false);
        CHECK(r.imported.empty());
        REQUIRE(r.skipped.size() == 1);
        CHECK(r.skipped[0].reason == "system_name");
    }
    SECTION("a file that is not a preset")
    {
        const fs::path file = dir.path / "not-a-preset.json";
        write_text(file, "{\"hello\": \"world\"}");
        const ImportPresetsResult r = session.import_presets(file.string(), false, false);
        REQUIRE(r.skipped.size() == 1);
        CHECK(r.skipped[0].reason == "invalid");
    }
}

TEST_CASE("Cloud payloads carry the difference from the parent and pulls recreate the preset", "[PresetsIO][slow]")
{
    const ScopedDataDir& dir = data_directory();
    (void) dir;
    Session session;
    session.set_cloud_user("11111111-2222-3333-4444-555555555555");
    const UserPresets u = make_user_presets(session, "cloud");

    // Never-synced presets are dirty.
    const std::vector<CloudPresetState> dirty = session.cloud_dirty_presets();
    CHECK(std::any_of(dirty.begin(), dirty.end(), [&](const CloudPresetState& s) { return s.name == u.filament; }));

    const nlohmann::json payload = nlohmann::json::parse(session.cloud_push_payload(PresetKind::Filament, u.filament));
    const nlohmann::json content = payload.at("content");
    CHECK(content.at("name") == u.filament);
    CHECK(content.at("type") == "filament");
    CHECK(content.contains("version"));
    CHECK_FALSE(content.value("inherits", std::string()).empty());
    CHECK(content.at("filament_cost") == "42");
    CHECK_FALSE(content.contains("filament_density")); // untouched, so not part of the diff
    CHECK(session.cloud_push_payload(PresetKind::Printer, "Voron 2.4 350 0.4 nozzle").empty());

    // Marking synced clears the flag and stores the id.
    session.cloud_mark_synced(PresetKind::Filament, u.filament, "aaaaaaaa-bbbb-5ccc-8ddd-eeeeeeeeeeee", 1700000000, "");
    const std::vector<CloudPresetState> after = session.cloud_dirty_presets();
    CHECK(std::none_of(after.begin(), after.end(), [&](const CloudPresetState& s) { return s.name == u.filament; }));

    // A pull with a newer copy of the same preset updates it; a pull without it removes it.
    nlohmann::json newer = content;
    newer["filament_cost"] = "43";
    const nlohmann::json upserts = nlohmann::json::array({ { { "id", "aaaaaaaa-bbbb-5ccc-8ddd-eeeeeeeeeeee" }, { "name", u.filament }, { "updated_time", 1800000000 }, { "content", newer } } });
    CloudApplyResult applied = session.cloud_apply_pull(upserts.dump());
    CHECK(applied.error.empty());
    CHECK(applied.unresolved.empty());
    REQUIRE(session.select_preset(PresetKind::Filament, u.filament));
    CHECK(session.option("filament_cost") == "43");

    applied = session.cloud_apply_pull("[]");
    CHECK(applied.error.empty());
    CHECK_FALSE(has_user_preset(session, PresetKind::Filament, u.filament));

    SECTION("a pulled preset whose parent is missing is reported, not loaded")
    {
        nlohmann::json orphan = { { "name", "Cloud orphan" }, { "type", "filament" }, { "version", "2.0.0.0" }, { "inherits", "No such filament @Nowhere" }, { "filament_cost", "1" } };
        const nlohmann::json pull = nlohmann::json::array({ { { "id", "ffffffff-0000-5000-8000-000000000000" }, { "name", "Cloud orphan" }, { "updated_time", 1800000001 }, { "content", orphan } } });
        const CloudApplyResult r = session.cloud_apply_pull(pull.dump());
        REQUIRE(r.unresolved.size() == 1);
        CHECK(r.unresolved[0].reason == "missing_parent");
        CHECK_FALSE(has_user_preset(session, PresetKind::Filament, "Cloud orphan"));
    }
}

TEST_CASE("Deleting a synced preset leaves a pending cloud deletion until confirmed", "[PresetsIO][slow]")
{
    const ScopedDataDir& dir = data_directory();
    (void) dir;
    Session session;
    session.set_cloud_user("11111111-2222-3333-4444-555555555555");
    const UserPresets u = make_user_presets(session, "del");
    session.cloud_mark_synced(PresetKind::Process, u.process, "12121212-3434-5656-8787-909090909090", 1700000000, "");
    REQUIRE(session.delete_preset(PresetKind::Process, u.process));

    std::vector<PendingDelete> pending = session.cloud_pending_deletes();
    REQUIRE(std::any_of(pending.begin(), pending.end(), [](const PendingDelete& d) { return d.setting_id == "12121212-3434-5656-8787-909090909090"; }));

    session.cloud_confirm_deleted("12121212-3434-5656-8787-909090909090");
    pending = session.cloud_pending_deletes();
    CHECK(std::none_of(pending.begin(), pending.end(), [](const PendingDelete& d) { return d.setting_id == "12121212-3434-5656-8787-909090909090"; }));
}
