#include <slic3r/GUI/GUI_App.hpp>
#include <slic3r/GUI/MainFrame.hpp>
#include <utility>
#include <slic3r/GUI/CreatePresetsDialog.hpp>
#include <boost/regex.hpp>
#include "Spoolman.hpp"
#include "Http.hpp"

namespace Slic3r {

namespace {
template<class Type> Type get_opt(pt::ptree& data, string path) { return data.get_optional<Type>(path).value_or(Type()); }
} // namespace

//---------------------------------
// Spoolman
//---------------------------------

static std::string get_spoolman_api_url()
{
    DynamicPrintConfig& config        = GUI::wxGetApp().preset_bundle->printers.get_edited_preset().config;
    string              host          = config.opt_string("print_host");
    string              spoolman_host = config.opt_string("spoolman_host");
    string              spoolman_port;

    if (auto idx = spoolman_host.find_last_of(':'); idx != string::npos) {
        boost::regex  pattern("(?<host>[a-zA-Z0-9.]+):(?<port>[0-9]+)");
        boost::smatch result;
        boost::regex_search(spoolman_host, result, pattern);
        spoolman_port = result["port"]; // get port value first since it is overwritten when setting the host value in the next line
        spoolman_host = result["host"];
    } else if (regex_match(spoolman_host, regex("^[0-9]+"))) {
        spoolman_port = spoolman_host;
        spoolman_host.clear();
    } else if (auto idx = host.find_last_of(':'); idx != string::npos)
        host = host.erase(idx);

    if (spoolman_host.empty())
        spoolman_host = host;

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

bool Spoolman::use_spoolman_spool(const unsigned int& spool_id, const double& weight_used)
{
    pt::ptree tree;
    tree.put("use_weight", weight_used);

    std::string endpoint = (boost::format("spool/%1%/use") % spool_id).str();
    tree = put_spoolman_json(endpoint, tree);
    if (tree.empty())
        return false;

    get_spoolman_spool_by_id(spool_id)->update_from_json(tree);
    return true;
}

SpoolmanResult Spoolman::create_filament_preset_from_spool(const SpoolmanSpoolShrPtr& spool,
                                                           const Preset*              base_profile,
                                                           bool                       detach,
                                                           bool                       force)
{
    PresetCollection& filaments            = wxGetApp().preset_bundle->filaments;
    string            filament_preset_name = spool->get_preset_name();
    SpoolmanResult    result;

    // Check if the preset already exists
    Preset* preset = filaments.find_preset(filament_preset_name);
    if (preset) {
        if (force) {
            update_filament_preset_from_spool(preset, true, false);
            filaments.save_current_preset(preset->name, detach, false, preset);
            return result;
        }
        std::string msg("Preset already exists with the name");
        BOOST_LOG_TRIVIAL(error) << msg << filament_preset_name;
        result.messages.emplace_back(msg);
    }

    if (!force) {
        // Check for presets with the same spool ID
        int visible(0), invisible(0);
        for (const auto& item : filaments()) { // count num of visible and invisible
            if (item.config.opt_int("spoolman_spool_id") == spool->id) {
                if (item.is_visible)
                    visible++;
                else
                    invisible++;
            }
            if (visible > 1 && invisible > 1)
                break;
        }
        // if there were any, build the message
        if (visible) {
            if (visible > 1)
                result.messages.emplace_back("Multiple visible presets share the same spool ID");
            else
                result.messages.emplace_back("A visible preset shares the same spool ID");
        }
        if (invisible) {
            if (invisible > 1)
                result.messages.emplace_back("Multiple invisible presets share the same spool ID");
            else
                result.messages.emplace_back("An invisible preset shares the same spool ID");
        }
        if (result.has_failed())
            return result;
    }

    std::string inherits = filaments.is_base_preset(*base_profile) ? base_profile->name : base_profile->inherits();

    preset = new Preset(Preset::TYPE_FILAMENT, filament_preset_name);
    preset->config.apply(base_profile->config);
    preset->config.set_key_value("filament_settings_id", new ConfigOptionStrings({filament_preset_name}));
    preset->config.set("inherits", inherits, true);
    spool->apply_to_preset(preset);
    preset->filament_id = get_filament_id(filament_preset_name);
    preset->version     = base_profile->version;
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
    const int&     spool_id = filament_preset->config.opt_int("spoolman_spool_id");
    if (spool_id < 1) {
        result.messages.emplace_back(
            "Preset provided does not have a valid Spoolman spool ID"); // IDs below 1 are not used by spoolman and should be ignored
        return result;
    }
    SpoolmanSpoolShrPtr& spool = get_instance()->m_spools[spool_id];
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
    PresetCollection& printers      = preset_bundle->printers;
    PresetCollection& filaments    = preset_bundle->filaments;

    // Clear the cache so that it can be repopulated with the correct info
    if (clear_cache) get_instance()->clear();
    if (printers.get_edited_preset().spoolman_enabled() && is_server_valid()) {
        for (auto item : filaments.get_visible()) {
            if (item->is_user() && item->spoolman_enabled()) {
                if (auto res = update_filament_preset_from_spool(item, true, true); res.has_failed())
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": Failed to update spoolman statistics with the following error: "
                                             << res.build_single_line_message() << "Spool ID: " << item->config.opt_int("spoolman_spool_id");
            }
        }
    }
}

bool Spoolman::is_server_valid()
{
    return !get_spoolman_json("info").empty();
}

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
    return remove_special_key(getVendor()->name + " " + m_filament_ptr->name + " " + m_filament_ptr->material);
}

void SpoolmanSpool::apply_to_config(Slic3r::DynamicConfig& config) const
{
    config.set_key_value("spoolman_spool_id", new ConfigOptionInt(id));
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