#include <slic3r/GUI/GUI_App.hpp>
#include <slic3r/GUI/MainFrame.hpp>
#include <utility>
#include <slic3r/GUI/CreatePresetsDialog.hpp>
#include <boost/regex.hpp>
#include <boost/property_tree/json_parser.hpp>
#include "Spoolman.hpp"
#include "Http.hpp"

namespace Slic3r {

namespace {
template<class Type> Type get_opt(pt::ptree& data, string path) { return data.get_optional<Type>(path).value_or(Type()); }
} // namespace

// Max timout in seconds for Spoolman HTTP requests
static constexpr long MAX_TIMEOUT = 5;

//---------------------------------
// Spoolman
//---------------------------------

static std::string get_spoolman_api_url()
{
    std::string spoolman_host = wxGetApp().app_config->get("spoolman", "host");
    std::string spoolman_port = Spoolman::DEFAULT_PORT;

    // Remove http(s) designator from the string as it interferes with the next step
    spoolman_host = boost::regex_replace(spoolman_host, boost::regex("https?://"), "");

    // If the host contains a port, use that rather than the default
    if (spoolman_host.find_last_of(':') != string::npos) {
        static const boost::regex pattern(R"((?<host>[a-z0-9.\-_]+):(?<port>[0-9]+))", boost::regex_constants::icase);
        boost::smatch result;
        if (boost::regex_match(spoolman_host, result, pattern)) {
            spoolman_port = result["port"]; // get port value first since it is overwritten when setting the host value in the next line
            spoolman_host = result["host"];
        } else {
            BOOST_LOG_TRIVIAL(error) << "Failed to parse host string. Host: " << spoolman_host << ", Port: " << spoolman_port;
        }
    }

    return spoolman_host + ":" + spoolman_port + "/api/v1/";
}

pt::ptree Spoolman::get_spoolman_json(const string& api_endpoint)
{
    auto url  = get_spoolman_api_url() + api_endpoint;
    auto http = Http::get(url);

    bool        res;
    std::string res_body;

    http.on_error([&](const std::string& body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "Failed to get data from the Spoolman server. Make sure that the port is correct and the server is running." << boost::format(" HTTP Error: %1%, HTTP status code: %2%") % error % status;
            res = false;
        })
        .on_complete([&](std::string body, unsigned) {
            res_body = std::move(body);
            res      = true;
        })
        .timeout_max(MAX_TIMEOUT)
        .perform_sync();

    if (!res)
        return {};

    if (res_body.empty()) {
        BOOST_LOG_TRIVIAL(info) << "Spoolman request returned an empty string";
        return {};
    }

    pt::ptree tree;
    try {
        stringstream ss(res_body);
        pt::read_json(ss, tree);
    } catch (std::exception& exception) {
        BOOST_LOG_TRIVIAL(error) << "Failed to read json into property tree. Exception: " << exception.what();
        return {};
    }

    return tree;
}

pt::ptree Spoolman::put_spoolman_json(const string& api_endpoint, const pt::ptree& data)
{
    auto url  = get_spoolman_api_url() + api_endpoint;
    auto http = Http::put2(url);

    bool        res;
    std::string res_body;

    stringstream ss;
    pt::write_json(ss, data);

    http.header("Content-Type", "application/json")
        .set_post_body(ss.str())
        .on_error([&](const std::string& body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "Failed to put data to the Spoolman server. Make sure that the port is correct and the server is running." << boost::format(" HTTP Error: %1%, HTTP status code: %2%, Response body: %3%") % error % status % body;
            res = false;
        })
        .on_complete([&](std::string body, unsigned) {
            res_body = std::move(body);
            res      = true;
        })
        .timeout_max(MAX_TIMEOUT)
        .perform_sync();

    if (!res)
        return {};

    if (res_body.empty()) {
        BOOST_LOG_TRIVIAL(info) << "Spoolman request returned an empty string";
        return {};
    }

    pt::ptree tree;
    try {
        ss = stringstream(res_body);
        pt::read_json(ss, tree);
    } catch (std::exception& exception) {
        BOOST_LOG_TRIVIAL(error) << "Failed to read json into property tree. Exception: " << exception.what();
        return {};
    }

    return tree;
}

bool Spoolman::pull_spoolman_spools()
{
    pt::ptree tree;

    this->clear();

    // Vendor
    tree = get_spoolman_json("vendor");
    if (tree.empty())
        return false;
    for (const auto& item : tree)
        m_vendors.emplace(item.second.get<int>("id"), make_shared<SpoolmanVendor>(SpoolmanVendor(item.second)));

    // Filament
    tree = get_spoolman_json("filament");
    if (tree.empty())
        return false;
    for (const auto& item : tree)
        m_filaments.emplace(item.second.get<int>("id"), make_shared<SpoolmanFilament>(SpoolmanFilament(item.second)));

    // Spool
    tree = get_spoolman_json("spool");
    if (tree.empty())
        return false;
    for (const auto& item : tree)
        m_spools.emplace(item.second.get<int>("id"), make_shared<SpoolmanSpool>(SpoolmanSpool(item.second)));

    return true;
}

bool Spoolman::use_spoolman_spool(const unsigned int& spool_id, const double& usage, const std::string& usage_type)
{
    pt::ptree tree;
    tree.put("use_" + usage_type, usage);

    std::string endpoint = (boost::format("spool/%1%/use") % spool_id).str();
    tree = put_spoolman_json(endpoint, tree);
    if (tree.empty())
        return false;

    get_spoolman_spool_by_id(spool_id)->update_from_json(tree);
    return true;
}

bool Spoolman::use_spoolman_spools(const std::map<unsigned int, double>& data, const std::string& usage_type)
{
    if (!(usage_type == "length" || usage_type == "weight"))
        return false;

    std::vector<unsigned int> spool_ids;

    for (auto& [spool_id, usage] : data) {
        if (!use_spoolman_spool(spool_id, usage, usage_type))
            return false;
        spool_ids.emplace_back(spool_id);
    }

    update_specific_spool_statistics(spool_ids);

    m_use_undo_buffer = data;
    m_last_usage_type = usage_type;
    return true;
}

bool Spoolman::undo_use_spoolman_spools()
{
    if (m_use_undo_buffer.empty() || m_last_usage_type.empty())
        return false;

    std::vector<unsigned int> spool_ids;

    for (auto& [spool_id, usage] : m_use_undo_buffer) {
        if (!use_spoolman_spool(spool_id, usage * -1, m_last_usage_type))
            return false;
        spool_ids.emplace_back(spool_id);
    }

    update_specific_spool_statistics(spool_ids);

    m_use_undo_buffer.clear();
    m_last_usage_type.clear();
    return true;
}

SpoolmanResult Spoolman::create_filament_preset_from_spool(const SpoolmanSpoolShrPtr& spool,
                                                           const Preset*              base_preset,
                                                           bool                       detach,
                                                           bool                       force)
{
    PresetCollection& filaments = wxGetApp().preset_bundle->filaments;
    SpoolmanResult    result;

    if (!base_preset)
        base_preset = &filaments.get_edited_preset();

    std::string filament_preset_name = spool->get_preset_name();

    // Bring over the printer name from the base preset or add one for the current printer
    if (const auto idx = base_preset->name.rfind(" @"); idx != std::string::npos)
        filament_preset_name += base_preset->name.substr(idx);
    else
        filament_preset_name += " @" + wxGetApp().preset_bundle->printers.get_selected_preset_name();

    if (const auto idx = filament_preset_name.rfind(" - Copy"); idx != std::string::npos)
        filament_preset_name.erase(idx);

    Preset* preset = filaments.find_preset(filament_preset_name);

    if (force) {
        if (preset && !preset->is_user())
            result.messages.emplace_back(_u8L("A system preset exists with the same name and cannot be overwritten"));
    } else {
        // Check if a preset with the same name already exists
        if (preset) {
            if (preset->is_user())
                result.messages.emplace_back(_u8L("Preset already exists with the same name"));
            else
                result.messages.emplace_back(_u8L("A system preset exists with the same name and cannot be overwritten"));
        }

        // Check for presets with the same spool ID
        int compatible(0);
        for (const auto item : filaments.get_compatible()) { // count num of visible and invisible
            if (item->is_user() && item->config.opt_int("spoolman_spool_id", 0) == spool->id) {
                compatible++;
                if (compatible > 1)
                    break;
            }
        }
        // if there were any, build the message
        if (compatible) {
            if (compatible > 1)
                result.messages.emplace_back(_u8L("Multiple compatible presets share the same spool ID"));
            else
                result.messages.emplace_back(_u8L("A compatible preset shares the same spool ID"));
        }

        // Check if the material types match between the base preset and the spool
        if (base_preset->config.opt_string("filament_type", 0) != spool->m_filament_ptr->material) {
            result.messages.emplace_back(_u8L("The materials of the base preset and the Spoolman spool do not match"));
        }
    }

    if (result.has_failed())
        return result;

    // get the first preset that is a system preset or base user preset in the inheritance hierarchy
    std::string inherits;
    if (!detach) {
        if (const auto base = filaments.get_preset_base(*base_preset))
            inherits = base->name;
        else // fallback if the above operation fails
            inherits = base_preset->name;
    }

    preset = new Preset(Preset::TYPE_FILAMENT, filament_preset_name);
    preset->config.apply(base_preset->config);
    preset->config.set_key_value("filament_settings_id", new ConfigOptionStrings({filament_preset_name}));
    preset->config.set("inherits", inherits, true);
    spool->apply_to_preset(preset);
    preset->filament_id = get_filament_id(filament_preset_name);
    preset->version     = base_preset->version;
    preset->loaded      = true;
    filaments.save_current_preset(filament_preset_name, detach, false, preset);

    return result;
}

SpoolmanResult Spoolman::update_filament_preset_from_spool(Preset* filament_preset, bool update_from_server, bool only_update_statistics)
{
    DynamicConfig  config;
    SpoolmanResult result;
    if (filament_preset->type != Preset::TYPE_FILAMENT) {
        result.messages.emplace_back("Preset is not a filament preset");
        return result;
    }
    const int&     spool_id = filament_preset->config.opt_int("spoolman_spool_id", 0);
    if (spool_id < 1) {
        result.messages.emplace_back(
            "Preset provided does not have a valid Spoolman spool ID"); // IDs below 1 are not used by spoolman and should be ignored
        return result;
    }
    SpoolmanSpoolShrPtr spool = get_instance()->get_spoolman_spool_by_id(spool_id);
    if (!spool) {
        result.messages.emplace_back("The spool ID does not exist in the local spool cache");
        return result;
    }
    if (update_from_server)
        spool->update_from_server(!only_update_statistics);
    spool->apply_to_preset(filament_preset, only_update_statistics);
    return result;
}

void Spoolman::update_visible_spool_statistics(bool clear_cache)
{
    PresetBundle* preset_bundle = GUI::wxGetApp().preset_bundle;
    PresetCollection& filaments    = preset_bundle->filaments;

    // Clear the cache so that it can be repopulated with the correct info
    if (clear_cache) get_instance()->clear();
    if (is_server_valid()) {
        for (const auto item : filaments.get_compatible()) {
            if (item->is_user() && item->spoolman_enabled()) {
                if (auto res = update_filament_preset_from_spool(item, true, true); res.has_failed())
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": Failed to update spoolman statistics with the following error: "
                                             << res.build_single_line_message() << "Spool ID: " << item->config.opt_int("spoolman_spool_id", 0);
            }
        }
    }
}

void Spoolman::update_specific_spool_statistics(const std::vector<unsigned int>& spool_ids)
{
    PresetBundle* preset_bundle = GUI::wxGetApp().preset_bundle;
    PresetCollection& filaments    = preset_bundle->filaments;

    std::set spool_ids_set(spool_ids.begin(), spool_ids.end());
    // make sure '0' is not a value
    spool_ids_set.erase(0);

    if (is_server_valid()) {
        for (const auto item : filaments.get_compatible()) {
            if (item->is_user() && spool_ids_set.count(item->config.opt_int("spoolman_spool_id", 0)) > 0) {
                if (auto res = update_filament_preset_from_spool(item, true, true); res.has_failed())
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": Failed to update spoolman statistics with the following error: "
                                             << res.build_single_line_message() << "Spool ID: " << item->config.opt_int("spoolman_spool_id", 0);
            }
        }
    }
}


bool Spoolman::is_server_valid()
{
    bool res = false;
    if (!is_enabled())
        return res;

    Http::get(get_spoolman_api_url() + "info").on_complete([&res](std::string, unsigned http_status) {
        if (http_status == 200)
            res = true;
    })
    .timeout_max(MAX_TIMEOUT)
    .perform_sync();
    return res;
}

bool Spoolman::is_enabled() { return GUI::wxGetApp().app_config->get_bool("spoolman", "enabled"); }

//---------------------------------
// SpoolmanVendor
//---------------------------------

void SpoolmanVendor::update_from_server() { update_from_json(Spoolman::get_spoolman_json("vendor/" + std::to_string(id))); }

void SpoolmanVendor::update_from_json(pt::ptree json_data)
{
    id   = json_data.get<int>("id");
    name = get_opt<string>(json_data, "name");
}

void SpoolmanVendor::apply_to_config(Slic3r::DynamicConfig& config) const
{
    config.set_key_value("filament_vendor", new ConfigOptionStrings({name}));
}

//---------------------------------
// SpoolmanFilament
//---------------------------------

void SpoolmanFilament::update_from_server(bool recursive)
{
    const boost::property_tree::ptree& json_data = Spoolman::get_spoolman_json("filament/" + std::to_string(id));
    update_from_json(json_data);
    if (recursive)
        m_vendor_ptr->update_from_json(json_data.get_child("vendor"));
}

void SpoolmanFilament::update_from_json(pt::ptree json_data)
{
    if (int vendor_id = json_data.get<int>("vendor.id"); m_vendor_ptr && m_vendor_ptr->id != vendor_id) {
        if (!m_spoolman->m_vendors.count(vendor_id))
            m_spoolman->m_vendors.emplace(vendor_id, make_shared<SpoolmanVendor>(SpoolmanVendor(json_data.get_child("vendor"))));
        m_vendor_ptr = m_spoolman->m_vendors[vendor_id];
    }
    id             = json_data.get<int>("id");
    name           = get_opt<string>(json_data, "name");
    material       = get_opt<string>(json_data, "material");
    price          = get_opt<float>(json_data, "price");
    density        = get_opt<float>(json_data, "density");
    diameter       = get_opt<float>(json_data, "diameter");
    article_number = get_opt<string>(json_data, "article_number");
    extruder_temp  = get_opt<int>(json_data, "settings_extruder_temp");
    bed_temp       = get_opt<int>(json_data, "settings_bed_temp");
    color          = "#" + get_opt<string>(json_data, "color_hex");
}

void SpoolmanFilament::apply_to_config(Slic3r::DynamicConfig& config) const
{
    config.set_key_value("filament_type", new ConfigOptionStrings({material}));
    config.set_key_value("filament_cost", new ConfigOptionFloats({price}));
    config.set_key_value("filament_density", new ConfigOptionFloats({density}));
    config.set_key_value("filament_diameter", new ConfigOptionFloats({diameter}));
    config.set_key_value("nozzle_temperature_initial_layer", new ConfigOptionInts({extruder_temp + 5}));
    config.set_key_value("nozzle_temperature", new ConfigOptionInts({extruder_temp}));
    config.set_key_value("hot_plate_temp_initial_layer", new ConfigOptionInts({bed_temp + 5}));
    config.set_key_value("hot_plate_temp", new ConfigOptionInts({bed_temp}));
    config.set_key_value("default_filament_colour", new ConfigOptionStrings{color});
    m_vendor_ptr->apply_to_config(config);
}

//---------------------------------
// SpoolmanSpool
//---------------------------------

void SpoolmanSpool::update_from_server(bool recursive)
{
    const boost::property_tree::ptree& json_data = Spoolman::get_spoolman_json("spool/" + std::to_string(id));
    update_from_json(json_data);
    if (recursive) {
        m_filament_ptr->update_from_json(json_data.get_child("filament"));
        getVendor()->update_from_json(json_data.get_child("filament.vendor"));
    }
}

std::string SpoolmanSpool::get_preset_name()
{
    auto name = getVendor()->name;

    if (!m_filament_ptr->name.empty())
        name += " " + m_filament_ptr->name;
    if (!m_filament_ptr->material.empty())
        name += " " + m_filament_ptr->material;

    return remove_special_key(name);
}

void SpoolmanSpool::apply_to_config(Slic3r::DynamicConfig& config) const
{
    config.set_key_value("spoolman_spool_id", new ConfigOptionInts({id}));
    m_filament_ptr->apply_to_config(config);
}

void SpoolmanSpool::apply_to_preset(Preset* preset, bool only_update_statistics) const
{
    auto spoolman_stats = preset->spoolman_statistics;
    spoolman_stats->remaining_weight = remaining_weight;
    spoolman_stats->used_weight = used_weight;
    spoolman_stats->remaining_length = remaining_length;
    spoolman_stats->used_length = used_length;
    spoolman_stats->archived = archived;
    if (only_update_statistics)
        return;
    this->apply_to_config(preset->config);
}

void SpoolmanSpool::update_from_json(pt::ptree json_data)
{
    if (int filament_id = json_data.get<int>("filament.id"); m_filament_ptr && m_filament_ptr->id != filament_id) {
        if (!m_spoolman->m_filaments.count(filament_id))
            m_spoolman->m_filaments.emplace(filament_id, make_shared<SpoolmanFilament>(SpoolmanFilament(json_data.get_child("filament"))));
        m_filament_ptr = m_spoolman->m_filaments.at(filament_id);
    }
    id               = json_data.get<int>("id");
    remaining_weight = get_opt<float>(json_data, "remaining_weight");
    used_weight      = get_opt<float>(json_data, "used_weight");
    remaining_length = get_opt<float>(json_data, "remaining_length");
    used_length      = get_opt<float>(json_data, "used_length");
    archived         = get_opt<bool>(json_data, "archived");
}

} // namespace Slic3r