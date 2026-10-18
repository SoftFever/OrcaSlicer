#include "PresetArchive.hpp"
#include "OrcaMobile.hpp"

#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Semver.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
#include <miniz/miniz.h>
#include <nlohmann/json.hpp>

#include <ctime>
#include <stdexcept>

namespace Slic3r { namespace Mobile {

namespace fs = boost::filesystem;
using nlohmann::json;

namespace {

// Reads the header keys of one preset JSON. `valid` stays false when the text is not
// a preset: no *_settings_id key, or a version Semver cannot parse.
ArchiveEntry entry_from_json_text(const std::string& file_name, const std::string& text)
{
    ArchiveEntry e;
    e.file_name = file_name;
    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception&) {
        return e;
    }
    if (! j.is_object())
        return e;
    auto str = [&](const char* key) -> std::string {
        auto it = j.find(key);
        return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    e.name     = str(BBL_JSON_KEY_NAME);
    e.inherits = str(BBL_JSON_KEY_INHERITS);
    if (j.contains("printer_settings_id"))
        e.kind = PresetKind::Printer;
    else if (j.contains("print_settings_id"))
        e.kind = PresetKind::Process;
    else if (j.contains("filament_settings_id"))
        e.kind = PresetKind::Filament;
    else
        return e;
    e.valid = ! e.name.empty() && static_cast<bool>(Semver::parse(str(BBL_JSON_KEY_VERSION)));
    return e;
}

bool is_archive(const std::string& path)
{
    return boost::iends_with(path, ".zip") || boost::iends_with(path, ".orca_printer") ||
           boost::iends_with(path, ".orca_filament") || boost::iends_with(path, ".orca_bundle");
}

const char* kind_dir(PresetKind kind)
{
    switch (kind) {
    case PresetKind::Printer:  return "printer";
    case PresetKind::Filament: return "filament";
    default:                   return "process";
    }
}

std::string timestamp_now()
{
    std::time_t t = std::time(nullptr);
    char        buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", std::localtime(&t));
    return buf;
}

struct ZipWriter
{
    mz_zip_archive zip;
    bool           open = false;
    explicit ZipWriter(const std::string& path)
    {
        mz_zip_zero_struct(&zip);
        open = mz_zip_writer_init_file(&zip, encode_path(path.c_str()).c_str(), 0) == MZ_TRUE;
    }
    ~ZipWriter()
    {
        if (open)
            mz_zip_writer_end(&zip);
    }
    void add_file(const std::string& entry, const std::string& src)
    {
        if (mz_zip_writer_add_file(&zip, entry.c_str(), encode_path(src.c_str()).c_str(), nullptr, 0, MZ_DEFAULT_COMPRESSION) != MZ_TRUE)
            throw std::runtime_error("Could not add " + entry + " to the archive");
    }
    void add_text(const std::string& entry, const std::string& text)
    {
        if (mz_zip_writer_add_mem(&zip, entry.c_str(), text.data(), text.size(), MZ_DEFAULT_COMPRESSION) != MZ_TRUE)
            throw std::runtime_error("Could not add " + entry + " to the archive");
    }
    void finish()
    {
        if (mz_zip_writer_finalize_archive(&zip) != MZ_TRUE)
            throw std::runtime_error("Could not finalize the archive");
    }
};

} // namespace

std::vector<ArchiveEntry> scan_preset_archive(const std::string& path)
{
    std::vector<ArchiveEntry> out;
    if (is_json_file(path)) {
        boost::nowide::ifstream in(path);
        if (! in)
            return out;
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        out.push_back(entry_from_json_text(fs::path(path).filename().string(), text));
        return out;
    }
    if (! is_archive(path))
        return out;

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    FILE* f = boost::nowide::fopen(path.c_str(), "rb");
    if (f == nullptr)
        return out;
    if (mz_zip_reader_init_cfile(&zip, f, 0, MZ_ZIP_FLAG_CASE_SENSITIVE) != MZ_TRUE) {
        fclose(f);
        return out;
    }
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (mz_zip_reader_file_stat(&zip, i, &st) != MZ_TRUE || st.m_is_directory)
            continue;
        std::string file_name = st.m_filename;
        if (const size_t slash = file_name.find_last_of("/\\"); slash != std::string::npos)
            file_name = file_name.substr(slash + 1);
        if (file_name == BUNDLE_STRUCTURE_JSON_NAME || ! boost::iends_with(file_name, ".json"))
            continue;
        size_t size = 0;
        void*  mem  = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (mem == nullptr)
            continue;
        out.push_back(entry_from_json_text(file_name, std::string(static_cast<const char*>(mem), size)));
        mz_free(mem);
    }
    mz_zip_reader_end(&zip);
    fclose(f);
    return out;
}

std::string write_preset_archive(PresetBundle& bundle, const std::vector<PresetRef>& refs, const std::string& out_path)
{
    const bool structured = boost::iends_with(out_path, ".orca_printer") || boost::iends_with(out_path, ".orca_bundle");
    const bool one_printer = boost::iends_with(out_path, ".orca_printer");
    if (! structured && ! boost::iends_with(out_path, ".zip"))
        throw std::runtime_error("Unsupported archive extension; use .orca_printer, .orca_bundle or .zip");
    if (refs.empty())
        throw std::runtime_error("Nothing to export");

    // Resolve every preset first so a bad request fails before any file is written.
    struct Item
    {
        PresetKind  kind;
        std::string name;
        std::string src;   // file to copy into the archive
    };
    std::vector<Item> items;
    int               printers = 0;
    const fs::path    temp_dir = fs::path(data_dir()) / PRESET_USER_DIR / "Temp";
    for (const PresetRef& ref : refs) {
        PresetCollection& c = ref.kind == PresetKind::Printer ? bundle.printers :
                              ref.kind == PresetKind::Filament ? bundle.filaments : bundle.prints;
        const Preset* p = c.find_preset(ref.name, false);
        if (p == nullptr)
            throw std::runtime_error("Preset \"" + ref.name + "\" does not exist");
        if (! p->is_user())
            throw std::runtime_error("\"" + ref.name + "\" is a built-in preset and cannot be exported; save it under your own name first");
        if (p->file.empty() || ! fs::exists(p->file))
            throw std::runtime_error("Preset \"" + ref.name + "\" has no file on disk");
        Item item { ref.kind, p->name, p->file };
        if (ref.kind == PresetKind::Printer) {
            ++printers;
            // Like the desktop, ship a copy of the printer without its network
            // credentials. The copy is fully flattened (saved with no parent) but
            // keeps `inherits`, so the importer re-diffs it against the vendor preset.
            Preset copy = *p;
            boost::system::error_code ec;
            fs::create_directories(temp_dir, ec);
            copy.file = (temp_dir / (get_preset_bare_name(p->name) + ".json")).make_preferred().string();
            for (const char* key : { "print_host", "print_host_webui", "printhost_apikey", "printhost_cafile",
                                     "printhost_user", "printhost_password", "printhost_port" })
                copy.config.erase(key);
            copy.save(nullptr);
            item.src = copy.file;
        }
        items.push_back(std::move(item));
    }
    if (one_printer && printers != 1)
        throw std::runtime_error("An .orca_printer archive holds exactly one printer");

    boost::system::error_code ec;
    fs::create_directories(fs::path(out_path).parent_path(), ec);
    ZipWriter zip(out_path);
    if (! zip.open)
        throw std::runtime_error("Could not create " + out_path);

    json printer_config = json::array(), filament_config = json::array(), process_config = json::array();
    std::string printer_name;
    for (const Item& item : items) {
        const std::string base  = fs::path(item.src).filename().string();
        const std::string entry = structured ? std::string(kind_dir(item.kind)) + "/" + base : base;
        zip.add_file(entry, item.src);
        if (item.kind == PresetKind::Printer) {
            printer_config.push_back(entry);
            if (printer_name.empty())
                printer_name = item.name;
        } else if (item.kind == PresetKind::Filament) {
            filament_config.push_back(entry);
        } else {
            process_config.push_back(entry);
        }
    }
    if (structured) {
        json structure;
        structure["version"]             = core_version();
        structure["bundle_id"]           = "offline_" + (printer_name.empty() ? std::string("export") : printer_name) + "_" + timestamp_now();
        structure["bundle_type"]         = "printer config bundle";
        structure["printer_preset_name"] = printer_name.empty() ? std::string("export") : printer_name;
        structure["printer_config"]      = printer_config;
        structure["filament_config"]     = filament_config;
        structure["process_config"]      = process_config;
        zip.add_text(BUNDLE_STRUCTURE_JSON_NAME, structure.dump());
    }
    zip.finish();
    fs::remove_all(temp_dir, ec);
    return out_path;
}

}} // namespace Slic3r::Mobile
