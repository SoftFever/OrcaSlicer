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

static vector<string> statistics_keys = {"spoolman_remaining_weight", "spoolman_used_weight", "spoolman_remaining_length",
                                         "spoolman_used_length", "spoolman_archived"};

pt::ptree Spoolman::get_spoolman_json(const string& api_endpoint)
{
    DynamicPrintConfig& config        = GUI::wxGetApp().preset_bundle->printers.get_edited_preset().config;
    string              host          = config.opt_string("print_host");
    string              spoolman_host = config.opt_string("spoolman_port");
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

    auto url  = spoolman_host + ":" + spoolman_port + "/api/v1/" + api_endpoint;
    auto http = Http::get(url);

    bool        res;
    std::string res_body;

    http.on_error([&](const std::string& body, std::string error, unsigned status) {
            string msg = "Failed to get data from the Spoolman server. Make sure that the port is correct and the server is running.";
            BOOST_LOG_TRIVIAL(error) << msg << boost::format(" HTTP Error: %1%, HTTP status code: %2%") % error % status;
            show_error(nullptr, msg);
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

bool Spoolman::pull_spoolman_spools()
{
    pt::ptree tree;

    m_vendors.clear();
    m_filaments.clear();
    m_spools.clear();

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

bool Spoolman::create_filament_preset_from_spool(const SpoolmanSpoolShrPtr& spool, const Preset* base_profile)
{
    PresetBundle*     preset_bundle        = wxGetApp().preset_bundle;
    PresetCollection& filaments            = preset_bundle->filaments;
    string            filament_preset_name = remove_special_key(spool->getVendor()->name + " " + spool->m_filament_ptr->name + " " +
                                                                spool->m_filament_ptr->material);
    string            user_filament_id     = get_filament_id(filament_preset_name);

    // Check if the preset already exists
    Preset* preset = filaments.find_preset(filament_preset_name);
    if (preset) {
        BOOST_LOG_TRIVIAL(error) << "Preset already exists with the name " << filament_preset_name;
        return false;
    }

    preset  = new Preset( Preset::TYPE_FILAMENT, filament_preset_name);
    preset->config.apply(base_profile->config);
    preset->config.set_key_value("filament_settings_id", new ConfigOptionStrings({filament_preset_name}));
    preset->config.set("inherits", base_profile->name, true);
    spool->apply_to_config(preset->config);
    preset->filament_id = user_filament_id;
    preset->version     = base_profile->version;
    preset->file        = filaments.path_for_preset(*preset);
    filaments.save_current_preset(filament_preset_name, false, false, preset);

    return true;
}

bool Spoolman::update_filament_preset_from_spool(Preset* filament_preset, bool update_from_server, bool only_update_statistics)
{
    DynamicConfig config;
    const int&    spool_id = filament_preset->config.opt_int("spoolman_spool_id");
    if (spool_id < 1)
        return false; // IDs below 1 are not used by spoolman and should be ignored

    SpoolmanSpoolShrPtr& spool = get_instance()->m_spools[spool_id];
    if (update_from_server)
        spool->update_from_server(!only_update_statistics);
    spool->apply_to_config(config);
    filament_preset->config.apply_only(config, only_update_statistics ? statistics_keys : config.keys());
    return true;
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

void SpoolmanSpool::apply_to_config(Slic3r::DynamicConfig& config) const
{
    config.set_key_value("spoolman_spool_id", new ConfigOptionInt(id));
    config.set_key_value("spoolman_remaining_weight", new ConfigOptionFloat(remaining_weight));
    config.set_key_value("spoolman_used_weight", new ConfigOptionFloat(used_weight));
    config.set_key_value("spoolman_remaining_length", new ConfigOptionFloat(remaining_length));
    config.set_key_value("spoolman_used_length", new ConfigOptionFloat(used_length));
    config.set_key_value("spoolman_archived", new ConfigOptionBool(archived));
    m_filament_ptr->apply_to_config(config);
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