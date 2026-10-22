// opc_json - decode a per-vendor <vendor>.opc preset cache to editable JSON and
// rebuild the cache after the JSON is edited.
//
// The decoded document is a lossless view of VendorCacheData plus the two stamps
// VendorCacheFile keeps outside it:
//
//   {
//     "format": "orcaslicer-opc-json",
//     "format_version": 1,
//     "vendor_name": "...",
//     "vendor_version": "...",          // the version stamped on the cache
//     "vendors": { "<vendor id>": { ...VendorProfile... } },
//     "process_entries": [ { ...CachedPreset..., "config": { ... } } ],
//     "filament_entries": [ ... ],
//     "machine_entries": [ ... ],
//     "parse_errors": 0
//   }
//
// Entry configs use the regular preset JSON serialization. Inheritance and
// includes are kept as names, not resolved: rebuilding does not need a profiles
// directory, and the reassembled cache installs exactly like the original.
//
// This is a CLI workaround because the application has no Python binding that
// exposes cache I/O. Config option names and types come from this build's
// print_config_def: decoding a cache written by a newer build drops options this
// build does not know (as VendorCacheFile::load already does), so decode and
// rebuild with the build whose schema matches the profiles being edited.
//
// CachedPreset metadata lives beside "config" and is copied field by field.
// "inherits" and "includes" are entry metadata, never config options.

#include "libslic3r/Config.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetCacheFormat.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Semver.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace fs = boost::filesystem;
using json     = nlohmann::json;
using namespace Slic3r;

namespace {

constexpr const char* FORMAT_NAME    = "orcaslicer-opc-json";
constexpr int         FORMAT_VERSION = 1;

void print_usage(std::ostream& os)
{
    os <<
        "opc_json - decode a per-vendor .opc preset cache to editable JSON and rebuild it\n"
        "\n"
        "Usage:\n"
        "  opc_json decode <vendor>.opc [-o <file.json>] [--vendor <name>]\n"
        "  opc_json encode <file.json> -o <vendor>.opc\n"
        "\n"
        "decode writes <vendor>.opc.json beside the input by default. The vendor name is\n"
        "the input file's stem; --vendor overrides it for a differently named file.\n"
        "\n"
        "encode writes a new cache and requires an explicit output path. Neither mode\n"
        "may write over the file it reads. The JSON carries the vendor name and stamped\n"
        "version, the vendor map, all process/filament/machine entries with their\n"
        "metadata and config, and parse_errors. Inheritance and includes stay unresolved.\n"
        "Config keys and types come from this build's option schema, so use a build\n"
        "matching the profiles you are editing.\n"
        "\n"
        "Examples:\n"
        "  opc_json decode resources/profiles/BambuLab.opc -o /tmp/BambuLab.json\n"
        "  opc_json encode /tmp/BambuLab.json -o /tmp/BambuLab.opc\n";
}

[[noreturn]] void fail(const std::string& message)
{
    throw std::runtime_error(message);
}

const json& require(const json& object, const char* key)
{
    const auto it = object.find(key);
    if (it == object.end())
        fail(std::string("missing \"") + key + "\"");
    return *it;
}

std::string as_string(const json& object, const char* key, const std::string& fallback = {})
{
    const auto it = object.find(key);
    if (it == object.end())
        return fallback;
    if (! it->is_string())
        fail(std::string("\"") + key + "\" must be a string");
    return it->get<std::string>();
}

void read_strings(const json& object, const char* key, std::vector<std::string>& out)
{
    const auto it = object.find(key);
    if (it == object.end())
        return;
    if (! it->is_array())
        fail(std::string("\"") + key + "\" must be an array of strings");
    out.clear();
    for (const auto& value : *it) {
        if (! value.is_string())
            fail(std::string("\"") + key + "\" must be an array of strings");
        out.push_back(value.get<std::string>());
    }
}

void read_strings(const json& object, const char* key, std::set<std::string>& out)
{
    std::vector<std::string> values;
    read_strings(object, key, values);
    out = std::set<std::string>(values.begin(), values.end());
}

json strings_to_json(const std::vector<std::string>& values)
{
    json out = json::array();
    for (const std::string& value : values)
        out.push_back(value);
    return out;
}

json strings_to_json(const std::set<std::string>& values)
{
    json out = json::array();
    for (const std::string& value : values)
        out.push_back(value);
    return out;
}

const char* technology_name(PrinterTechnology technology)
{
    switch (technology) {
    case ptFFF: return "FFF";
    case ptSLA: return "SLA";
    case ptUnknown: return "Unknown";
    case ptAny: return "Any";
    }
    fail("unknown PrinterTechnology value " + std::to_string(int(technology)));
}

PrinterTechnology technology_from_json(const json& object, const char* key)
{
    const auto it = object.find(key);
    if (it == object.end())
        return ptFFF;
    if (it->is_number_integer()) {
        const int value = it->get<int>();
        if (value < ptFFF || value > ptAny)
            fail(std::string("\"") + key + "\" is not a PrinterTechnology value");
        return static_cast<PrinterTechnology>(value);
    }
    if (! it->is_string())
        fail(std::string("\"") + key + "\" must be \"FFF\", \"SLA\", \"Unknown\" or \"Any\"");
    const std::string value = it->get<std::string>();
    if (value == "FFF") return ptFFF;
    if (value == "SLA") return ptSLA;
    if (value == "Unknown") return ptUnknown;
    if (value == "Any") return ptAny;
    fail(std::string("\"") + key + "\" must be \"FFF\", \"SLA\", \"Unknown\" or \"Any\"");
}

json vendor_to_json(const VendorProfile& vendor)
{
    json out;
    out["name"]             = vendor.name;
    out["id"]               = vendor.id;
    // BBS 4-part form, the one the cache itself stamps and the form the vendor
    // JSON carries (`02.04.00.06` -> `2.4.0.6`).
    out["config_version"]   = vendor.config_version.to_string();
    out["config_update_url"] = vendor.config_update_url;
    out["changelog_url"]    = vendor.changelog_url;

    json models = json::array();
    for (const VendorProfile::PrinterModel& model : vendor.models) {
        json item;
        item["id"]           = model.id;
        item["name"]         = model.name;
        item["model_id"]     = model.model_id;
        item["technology"]   = technology_name(model.technology);
        item["family"]       = model.family;
        json variants = json::array();
        for (const VendorProfile::PrinterVariant& variant : model.variants)
            variants.push_back(variant.name);
        item["variants"]                  = std::move(variants);
        item["default_materials"]         = strings_to_json(model.default_materials);
        item["not_support_bed_types"]     = strings_to_json(model.not_support_bed_types);
        item["bed_model"]                 = model.bed_model;
        item["bed_texture"]               = model.bed_texture;
        item["image_bed_type"]            = model.image_bed_type;
        item["bottom_texture_end_name"]   = model.bottom_texture_end_name;
        item["use_double_extruder_default_texture"] = model.use_double_extruder_default_texture;
        item["bottom_texture_rect"]       = model.bottom_texture_rect;
        item["bottom_texture_rect_longer"] = model.bottom_texture_rect_longer;
        item["middle_texture_rect"]       = model.middle_texture_rect;
        item["hotend_model"]              = model.hotend_model;
        models.push_back(std::move(item));
    }
    out["models"]                = std::move(models);
    out["default_filaments"]     = strings_to_json(vendor.default_filaments);
    out["default_sla_materials"] = strings_to_json(vendor.default_sla_materials);
    return out;
}

VendorProfile vendor_from_json(const json& object, const std::string& map_key)
{
    if (! object.is_object())
        fail("vendor profile must be an object");

    VendorProfile vendor;
    vendor.name             = as_string(object, "name");
    vendor.id               = as_string(object, "id", map_key);
    if (vendor.id != map_key)
        fail("vendor profile id \"" + vendor.id + "\" does not match its map key \"" + map_key + "\"");
    vendor.config_update_url = as_string(object, "config_update_url");
    vendor.changelog_url     = as_string(object, "changelog_url");

    const std::string version = as_string(object, "config_version", "0.0.0");
    const auto parsed_version = Semver::parse(version);
    if (! parsed_version)
        fail("vendor config_version \"" + version + "\" is not a version");
    vendor.config_version = *parsed_version;

    const json& models = require(object, "models");
    if (! models.is_array())
        fail("\"models\" must be an array");
    for (const auto& model_json : models) {
        if (! model_json.is_object())
            fail("each model must be an object");
        VendorProfile::PrinterModel model;
        model.id       = as_string(model_json, "id");
        model.name     = as_string(model_json, "name");
        model.model_id = as_string(model_json, "model_id");
        model.technology = technology_from_json(model_json, "technology");
        model.family     = as_string(model_json, "family");

        const auto variants = model_json.find("variants");
        if (variants != model_json.end()) {
            if (! variants->is_array())
                fail("\"variants\" must be an array");
            for (const auto& variant : *variants) {
                if (variant.is_string())
                    model.variants.emplace_back(variant.get<std::string>());
                else if (variant.is_object())
                    model.variants.emplace_back(as_string(variant, "name"));
                else
                    fail("\"variants\" must contain names");
            }
        }

        read_strings(model_json, "default_materials", model.default_materials);
        read_strings(model_json, "not_support_bed_types", model.not_support_bed_types);
        model.bed_model               = as_string(model_json, "bed_model");
        model.bed_texture             = as_string(model_json, "bed_texture");
        model.image_bed_type          = as_string(model_json, "image_bed_type");
        model.bottom_texture_end_name = as_string(model_json, "bottom_texture_end_name");
        model.use_double_extruder_default_texture = as_string(model_json, "use_double_extruder_default_texture");
        model.bottom_texture_rect       = as_string(model_json, "bottom_texture_rect");
        model.bottom_texture_rect_longer = as_string(model_json, "bottom_texture_rect_longer");
        model.middle_texture_rect       = as_string(model_json, "middle_texture_rect");
        model.hotend_model              = as_string(model_json, "hotend_model");
        vendor.models.push_back(std::move(model));
    }

    read_strings(object, "default_filaments", vendor.default_filaments);
    read_strings(object, "default_sla_materials", vendor.default_sla_materials);
    return vendor;
}

json config_to_json(const DynamicPrintConfig& config, const CachedPreset& entry)
{
    std::ostringstream stream;
    // The regular preset JSON shape. No resolver is installed in this process,
    // so the plugin manifest is whatever the config already carries.
    config.save_to_json(stream, entry.name, entry.inherits, std::string());
    json out = json::parse(stream.str());
    // These headers belong to the entry, not to its config object.
    out.erase("version");
    out.erase("name");
    out.erase("from");
    return out;
}

json entry_to_json(const CachedPreset& entry)
{
    json out;
    out["name"]          = entry.name;
    out["sub_path"]      = entry.sub_path;
    out["inherits"]      = entry.inherits;
    out["includes"]      = strings_to_json(entry.includes);
    out["description"]   = entry.description;
    out["instantiation"] = entry.instantiation;
    out["setting_id"]    = entry.setting_id;
    out["filament_id"]   = entry.filament_id;
    out["renamed_from"]  = strings_to_json(entry.renamed_from);
    out["config"]        = config_to_json(entry.config_src, entry);
    return out;
}

void write_text_file(const fs::path& path, const std::string& text)
{
    boost::nowide::ofstream out(path.string(), std::ios::binary | std::ios::trunc);
    if (! out)
        fail("cannot write " + path.string());
    out.write(text.data(), std::streamsize(text.size()));
    if (! out)
        fail("cannot write " + path.string());
}

bool known_config_option(const std::string& key)
{
    if (print_config_def.get(key) != nullptr)
        return true;
    // load_from_json() also accepts a renamed option's old alias.
    for (const auto& option : print_config_def.options)
        for (const t_config_option_key& alias : option.second.aliases)
            if (alias == key)
                return true;
    return false;
}

void validate_config_keys(const json& config)
{
    // The regular preset loader treats these as entry metadata and drops them
    // from the config. An entry carries them beside "config" here, so finding
    // one inside it means the edit landed in the wrong object.
    static constexpr const char* metadata_keys[] = {
        "version", "name", "from", "inherits", "includes", "is_custom", "type",
        "url", "description", "instantiation", "setting_id", "filament_id", "renamed_from",
    };

    for (auto it = config.begin(); it != config.end(); ++ it) {
        for (const char* metadata_key : metadata_keys)
            if (it.key() == metadata_key)
                fail("\"" + it.key() + "\" is entry metadata; put it beside \"config\", not inside it");
        // VendorCacheFile::save would drop an option this build does not define,
        // so reject it here where the user can still fix the spelling.
        if (! known_config_option(it.key()))
            fail("config option \"" + it.key() + "\" is not known to this build");
    }
}

CachedPreset entry_from_json(const json& object, const fs::path& config_file)
{
    if (! object.is_object())
        fail("entry must be an object");

    CachedPreset entry;
    entry.name          = as_string(object, "name");
    if (entry.name.empty())
        fail("entry \"name\" must not be empty");
    entry.sub_path      = as_string(object, "sub_path");
    entry.inherits      = as_string(object, "inherits");
    entry.description   = as_string(object, "description");
    entry.instantiation = as_string(object, "instantiation");
    entry.setting_id    = as_string(object, "setting_id");
    entry.filament_id   = as_string(object, "filament_id");
    read_strings(object, "includes", entry.includes);
    read_strings(object, "renamed_from", entry.renamed_from);

    const json& config = require(object, "config");
    if (! config.is_object())
        fail("\"config\" must be an object");
    validate_config_keys(config);
    // The ConfigBase JSON loader takes a file, so the embedded config is handed
    // to it through one temporary that is reused for every entry.
    write_text_file(config_file, config.dump());

    ConfigSubstitutionContext substitutions(ForwardCompatibilitySubstitutionRule::Disable);
    std::map<std::string, std::string> key_values;
    std::string reason;
    // load_inherits_to_config=false: "inherits" and "includes" stay entry
    // metadata, exactly as PresetBundle::parse_vendor_json reads a subfile.
    const int loaded = entry.config_src.load_from_json(config_file.string(), substitutions, false, key_values, reason);
    if (loaded != 0)
        fail("config could not be loaded" + (reason.empty() ? std::string() : ": " + reason));
    return entry;
}

std::vector<CachedPreset> entries_from_json(const json& root, const char* key, const fs::path& config_file)
{
    const auto it = root.find(key);
    if (it == root.end())
        return {};
    if (! it->is_array())
        fail(std::string("\"") + key + "\" must be an array");

    std::vector<CachedPreset> entries;
    entries.reserve(std::min<size_t>(it->size(), 4096));
    size_t index = 0;
    for (const auto& entry_json : *it) {
        try {
            entries.push_back(entry_from_json(entry_json, config_file));
        } catch (const std::exception& ex) {
            fail(std::string(key) + "[" + std::to_string(index) + "]: " + ex.what());
        }
        ++ index;
    }
    return entries;
}

json vendors_to_json(const VendorMap& vendors)
{
    json out = json::object();
    for (const auto& [id, vendor] : vendors)
        out[id] = vendor_to_json(vendor);
    return out;
}

uint64_t parse_errors_from_json(const json& root)
{
    const auto it = root.find("parse_errors");
    if (it == root.end())
        return 0;
    if (it->is_number_unsigned())
        return it->get<uint64_t>();
    if (it->is_number_integer()) {
        const int64_t value = it->get<int64_t>();
        if (value < 0)
            fail("\"parse_errors\" must not be negative");
        return uint64_t(value);
    }
    fail("\"parse_errors\" must be a non-negative integer");
}

void ensure_output_parent(const fs::path& output)
{
    const fs::path parent = output.parent_path();
    if (! parent.empty() && ! fs::exists(parent))
        fs::create_directories(parent);
    if (fs::exists(output) && fs::is_directory(output))
        fail("output path is a directory: " + output.string());
}

bool same_file(const fs::path& a, const fs::path& b)
{
    boost::system::error_code ec;
    if (fs::exists(a) && fs::exists(b) && fs::equivalent(a, b, ec) && ! ec)
        return true;
    const fs::path abs_a = fs::absolute(a, ec).lexically_normal();
    if (ec)
        return false;
    const fs::path abs_b = fs::absolute(b, ec).lexically_normal();
    return ! ec && abs_a == abs_b;
}

struct TemporaryFile
{
    fs::path path;

    TemporaryFile() : path(fs::temp_directory_path() / fs::unique_path("orcaslicer-opc-json-%%%%%%%%-%%%%.json")) {}

    ~TemporaryFile()
    {
        boost::system::error_code ec;
        fs::remove(path, ec);
    }
};

struct Decoded
{
    std::string vendor_name;
    std::string vendor_version;
    VendorCacheData data;
};

Decoded decode_cache(const fs::path& input, const std::string& vendor_override)
{
    if (! fs::is_regular_file(input))
        fail("input is not a file: " + input.string());

    Decoded decoded;
    decoded.vendor_name = vendor_override.empty() ? input.stem().string() : vendor_override;
    if (decoded.vendor_name.empty())
        fail("cannot infer the vendor name from \"" + input.string() + "\"; pass --vendor <name>");

    decoded.vendor_version = VendorCacheFile::peek_version(input.string(), decoded.vendor_name);
    if (decoded.vendor_version.empty())
        fail("not a readable cache for vendor \"" + decoded.vendor_name + "\": " + input.string() +
             " (pass --vendor <name> if the file is named differently)");

    if (! VendorCacheFile::load(input.string(), decoded.vendor_name, Semver::inf(), decoded.data))
        fail("could not load cache: " + input.string());
    return decoded;
}

json decoded_to_json(const Decoded& decoded)
{
    json out;
    out["format"]         = FORMAT_NAME;
    out["format_version"] = FORMAT_VERSION;
    out["vendor_name"]    = decoded.vendor_name;
    out["vendor_version"] = decoded.vendor_version;
    out["vendors"]        = vendors_to_json(decoded.data.vendors);

    json process = json::array();
    for (const CachedPreset& entry : decoded.data.process_entries)
        process.push_back(entry_to_json(entry));
    out["process_entries"] = std::move(process);

    json filament = json::array();
    for (const CachedPreset& entry : decoded.data.filament_entries)
        filament.push_back(entry_to_json(entry));
    out["filament_entries"] = std::move(filament);

    json machine = json::array();
    for (const CachedPreset& entry : decoded.data.machine_entries)
        machine.push_back(entry_to_json(entry));
    out["machine_entries"] = std::move(machine);

    out["parse_errors"] = decoded.data.parse_errors;
    return out;
}

json read_json_file(const fs::path& input)
{
    if (! fs::is_regular_file(input))
        fail("input is not a file: " + input.string());
    boost::nowide::ifstream in(input.string(), std::ios::binary);
    if (! in)
        fail("cannot read " + input.string());
    json document;
    try {
        in >> document;
    } catch (const nlohmann::json::exception& ex) {
        fail("cannot parse " + input.string() + ": " + ex.what());
    }
    return document;
}

void validate_vendor_stamp(const std::string& vendor_version)
{
    if (! Semver::parse(vendor_version))
        fail("\"vendor_version\" is not a version: \"" + vendor_version + "\"");
}

void encode_cache(const fs::path& input, const fs::path& output)
{
    if (same_file(input, output))
        fail("output must not be the input file");

    const json document = read_json_file(input);
    if (! document.is_object())
        fail("top-level JSON must be an object");
    if (as_string(document, "format") != FORMAT_NAME)
        fail(std::string("unsupported JSON format; expected \"") + FORMAT_NAME + "\"");
    const json& version = require(document, "format_version");
    if (! version.is_number_integer() || version.get<int>() != FORMAT_VERSION)
        fail("unsupported format_version; expected " + std::to_string(FORMAT_VERSION));

    const std::string vendor_name = as_string(document, "vendor_name");
    if (vendor_name.empty())
        fail("\"vendor_name\" must not be empty");
    const std::string vendor_version = as_string(document, "vendor_version");
    if (vendor_version.empty())
        fail("\"vendor_version\" must not be empty");
    validate_vendor_stamp(vendor_version);

    VendorCacheData data;
    const json& vendors = require(document, "vendors");
    if (! vendors.is_object() || vendors.empty())
        fail("\"vendors\" must be a non-empty object");
    for (auto it = vendors.begin(); it != vendors.end(); ++ it) {
        if (it.key().empty())
            fail("\"vendors\" must not have an empty id");
        data.vendors.emplace(it.key(), vendor_from_json(it.value(), it.key()));
    }
    if (data.vendors.find(vendor_name) == data.vendors.end())
        fail("the vendor map does not carry the cache's own vendor \"" + vendor_name + "\"");

    ensure_output_parent(output);
    TemporaryFile config_file;
    data.process_entries  = entries_from_json(document, "process_entries", config_file.path);
    data.filament_entries = entries_from_json(document, "filament_entries", config_file.path);
    data.machine_entries  = entries_from_json(document, "machine_entries", config_file.path);
    data.parse_errors     = parse_errors_from_json(document);

    if (! VendorCacheFile::save(output.string(), vendor_name, vendor_version, data))
        fail("could not write cache: " + output.string());

    std::cout << "Wrote " << vendor_name << " (" << vendor_version << "): "
              << data.process_entries.size() << " process, "
              << data.filament_entries.size() << " filament, "
              << data.machine_entries.size() << " machine presets -> "
              << output.string() << "\n";
}

void run_decode(const fs::path& input, const fs::path& output, const std::string& vendor)
{
    if (same_file(input, output))
        fail("output must not be the input file");
    ensure_output_parent(output);

    const Decoded decoded = decode_cache(input, vendor);
    const json document   = decoded_to_json(decoded);
    if (const std::error_code ec = write_file_atomically(output.string(), document.dump(2) + "\n"))
        fail("cannot write " + output.string() + ": " + ec.message());

    std::cout << "Decoded " << decoded.vendor_name << " (" << decoded.vendor_version << "): "
              << decoded.data.process_entries.size() << " process, "
              << decoded.data.filament_entries.size() << " filament, "
              << decoded.data.machine_entries.size() << " machine presets -> "
              << output.string() << "\n";
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc < 2) {
        print_usage(std::cerr);
        return 2;
    }

    const std::string mode = argv[1];
    if (mode == "-h" || mode == "--help") {
        print_usage(std::cout);
        return 0;
    }
    if (mode != "decode" && mode != "encode") {
        std::cerr << "opc_json: unknown mode \"" << mode << "\"\n\n";
        print_usage(std::cerr);
        return 2;
    }

    std::string input;
    std::string output;
    std::string vendor;

    try {
        for (int i = 2; i < argc; ++ i) {
            const std::string arg = argv[i];
            auto value = [&](const char* option) -> std::string {
                if (i + 1 >= argc)
                    fail(std::string(option) + " needs a value");
                return argv[++ i];
            };

            if (arg == "-o" || arg == "--output")
                output = value(arg.c_str());
            else if (arg == "--vendor")
                vendor = value("--vendor");
            else if (arg == "-h" || arg == "--help") {
                print_usage(std::cout);
                return 0;
            } else if (! arg.empty() && arg[0] == '-') {
                fail("unknown option \"" + arg + "\"");
            } else if (! input.empty()) {
                fail("only one input file may be given");
            } else {
                input = arg;
            }
        }

        if (input.empty())
            fail("an input file is required");
        if (mode == "decode") {
            if (output.empty())
                output = input + ".json";
        } else {
            if (! vendor.empty())
                fail("encode takes the vendor name from the JSON, not --vendor");
            if (output.empty())
                fail("encode needs an explicit output path (-o <vendor>.opc)");
        }

        if (mode == "decode")
            run_decode(fs::path(input), fs::path(output), vendor);
        else
            encode_cache(fs::path(input), fs::path(output));
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "opc_json: " << ex.what() << "\n";
        return 1;
    }
}
