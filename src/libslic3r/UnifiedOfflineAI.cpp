#include "UnifiedOfflineAI.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Utils.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Slic3r { namespace AI {

// ------------------------- GeometryAnalyzer -------------------------

GeometryFeatures GeometryAnalyzer::analyze(const ModelObject* model_object) {
    GeometryFeatures features{};

    if (!model_object || model_object->volumes.empty()) {
        return features;
    }

    const ModelVolume* volume = model_object->volumes[0];
    const TriangleMesh& mesh = volume->mesh();

    BoundingBoxf3 bbox = mesh.bounding_box();
    features.width = bbox.size().x();
    features.height = bbox.size().z();
    features.depth = bbox.size().y();

    features.surface_area = calculate_surface_area(mesh);
    features.volume = calculate_volume(mesh);

    features.has_overhangs = detect_overhangs(mesh);
    features.has_bridges = detect_bridges(mesh);
    features.has_thin_walls = detect_thin_walls(mesh);
    features.has_small_details = (features.width < 50.0 || features.depth < 50.0);

    features.overhang_percentage = calculate_overhang_percentage(mesh);
    features.layer_count_estimate = static_cast<int>(std::ceil(features.height / 0.2));

    return features;
}

double GeometryAnalyzer::calculate_surface_area(const TriangleMesh& mesh) {
    const indexed_triangle_set& its = mesh.its;
    double total_area = 0.0;

    for (size_t i = 0; i < its.indices.size(); ++i) {
        const Vec3i32& face = its.indices[i];
        const Vec3f& v0 = its.vertices[face[0]];
        const Vec3f& v1 = its.vertices[face[1]];
        const Vec3f& v2 = its.vertices[face[2]];
        Vec3f edge1 = v1 - v0;
        Vec3f edge2 = v2 - v0;
        Vec3f cross = edge1.cross(edge2);
        total_area += 0.5 * cross.norm();
    }

    return total_area;
}

double GeometryAnalyzer::calculate_volume(const TriangleMesh& mesh) {
    return its_volume(mesh.its);
}

bool GeometryAnalyzer::detect_overhangs(const TriangleMesh& mesh, double threshold_angle) {
    const indexed_triangle_set& its = mesh.its;
    int overhang_count = 0;
    const double threshold_z = std::cos(threshold_angle * M_PI / 180.0);

    for (size_t i = 0; i < its.indices.size(); ++i) {
        const Vec3i32& face = its.indices[i];
        const Vec3f& v0 = its.vertices[face[0]];
        const Vec3f& v1 = its.vertices[face[1]];
        const Vec3f& v2 = its.vertices[face[2]];
        Vec3f normal = (v1 - v0).cross(v2 - v0).normalized();
        if (normal.z() < threshold_z && normal.z() > -0.99) {
            overhang_count++;
        }
    }

    double overhang_ratio = static_cast<double>(overhang_count) / std::max<size_t>(1, its.indices.size());
    return overhang_ratio > 0.05;
}

bool GeometryAnalyzer::detect_bridges(const TriangleMesh& mesh) {
    const indexed_triangle_set& its = mesh.its;
    BoundingBoxf3 bbox = mesh.bounding_box();

    for (size_t i = 0; i < its.indices.size(); ++i) {
        const Vec3i32& face = its.indices[i];
        const Vec3f& v0 = its.vertices[face[0]];
        const Vec3f& v1 = its.vertices[face[1]];
        const Vec3f& v2 = its.vertices[face[2]];
        float centroid_z = (v0.z() + v1.z() + v2.z()) / 3.0f;
        Vec3f normal = (v1 - v0).cross(v2 - v0).normalized();
        if (std::abs(normal.z()) > 0.9 && centroid_z > bbox.min.z() + 5.0f) {
            return true;
        }
    }

    return false;
}

bool GeometryAnalyzer::detect_thin_walls(const TriangleMesh& mesh, double min_thickness) {
    BoundingBoxf3 bbox = mesh.bounding_box();
    Vec3d size = bbox.size();
    double min_dimension = std::min({size.x(), size.y(), size.z()});
    return min_dimension < min_thickness * 2.0;
}

double GeometryAnalyzer::calculate_overhang_percentage(const TriangleMesh& mesh, double threshold_angle) {
    const indexed_triangle_set& its = mesh.its;
    int overhang_count = 0;
    const double threshold_z = std::cos(threshold_angle * M_PI / 180.0);

    for (size_t i = 0; i < its.indices.size(); ++i) {
        const Vec3i32& face = its.indices[i];
        const Vec3f& v0 = its.vertices[face[0]];
        const Vec3f& v1 = its.vertices[face[1]];
        const Vec3f& v2 = its.vertices[face[2]];
        Vec3f normal = (v1 - v0).cross(v2 - v0).normalized();
        if (normal.z() < threshold_z && normal.z() > -0.99) {
            overhang_count++;
        }
    }

    return static_cast<double>(overhang_count) / std::max<size_t>(1, its.indices.size()) * 100.0;
}

// -------------------- PrinterCapabilityDetector ---------------------

PrinterCapabilityDetector::PrinterCapabilityDetector() {
    m_profiles_directory = resources_dir() + "/profiles";
    load_printer_database();
}

std::optional<PrinterCapabilityDetector::PrinterCapabilities>
PrinterCapabilityDetector::detect_from_preset_bundle(const PresetBundle* bundle) {
    return detect_from_preset_bundle(bundle, nullptr);
}

std::optional<PrinterCapabilityDetector::PrinterCapabilities>
PrinterCapabilityDetector::detect_from_preset_bundle(const PresetBundle* bundle, 
                                                      const AppConfig* app_config) {
    if (!bundle) {
        return std::nullopt;
    }

    std::string current_printer = bundle->printers.get_selected_preset_name();
    
    if (!current_printer.empty()) {
        if (m_cached_capabilities && m_cached_printer_name == current_printer) {
            return m_cached_capabilities;
        }

        if (auto by_name = detect_printer_by_name(current_printer)) {
            m_cached_printer_name = current_printer;
            m_cached_capabilities = by_name;
            
            // Enhance with config from current preset
            // Note: Config serialization not available in this context
            // const Preset* preset = bundle->printers.find_preset(current_printer, false);
            // if (preset) {
            //     enhance_capabilities_from_config(*m_cached_capabilities, config);
            // }
            
            // Update skill level if app_config provided
            if (app_config) {
                m_cached_capabilities->skill_level = detect_user_skill_level(app_config);
            }
            
            return m_cached_capabilities;
        }
    }

    // Try by model_id
    const Preset& current_preset = bundle->printers.get_selected_preset();
    std::string model_id;
    if (current_preset.config.has("model_id")) {
        model_id = current_preset.config.opt_string("model_id");
    }
    
    if (!model_id.empty()) {
        if (auto by_model = detect_printer_by_model_id(model_id)) {
            m_cached_printer_name = current_printer;
            m_cached_capabilities = by_model;
            
            // Note: Config serialization not available in this context
            // enhance_capabilities_from_config(*m_cached_capabilities, config);
            
            if (app_config) {
                m_cached_capabilities->skill_level = detect_user_skill_level(app_config);
            }
            
            return m_cached_capabilities;
        }
    }

    // Infer from config as fallback - always use DIY defaults if we got here
    // Note: Config serialization not available, using default capabilities
    PrinterCapabilities inferred;
    inferred.is_diy_printer = true;
    inferred.profile_name = current_printer.empty() ? "DIY Printer" : current_printer;
    
    if (app_config) {
        inferred.skill_level = detect_user_skill_level(app_config);
    }
    
    m_cached_printer_name = current_printer;
    m_cached_capabilities = inferred;
    return m_cached_capabilities;

    return std::nullopt;
}

std::optional<PrinterCapabilityDetector::PrinterCapabilities>
PrinterCapabilityDetector::detect_printer_by_name(const std::string& printer_name) {
    if (!m_database_loaded) {
        load_printer_database();
    }

    auto it = m_printer_database.find(printer_name);
    if (it != m_printer_database.end()) {
        return convert_profile_to_capabilities(it->second);
    }

    for (const auto& entry : m_printer_database) {
        if (entry.first.find(printer_name) != std::string::npos) {
            return convert_profile_to_capabilities(entry.second);
        }
    }

    return std::nullopt;
}

std::optional<PrinterCapabilityDetector::PrinterCapabilities>
PrinterCapabilityDetector::detect_printer_by_model_id(const std::string& model_id) {
    if (!m_database_loaded) {
        load_printer_database();
    }

    for (const auto& entry : m_printer_database) {
        if (entry.second.model_id == model_id) {
            return convert_profile_to_capabilities(entry.second);
        }
    }

    return std::nullopt;
}

PrinterCapabilityDetector::PrinterCapabilities
PrinterCapabilityDetector::infer_capabilities_from_config(const nlohmann::json& machine_config) const {
    PrinterCapabilities caps{};

    caps.has_heated_bed = infer_heated_bed_capability(machine_config);
    caps.has_enclosure = infer_enclosure_capability(machine_config);
    caps.has_auto_leveling = infer_auto_leveling_capability(machine_config);
    caps.has_filament_sensor = infer_filament_sensor_capability(machine_config);
    caps.has_chamber_heating = machine_config.value("support_chamber_temp_control", 0) == 1;

    if (machine_config.contains("nozzle_diameter")) {
        const auto& nozzle = machine_config.at("nozzle_diameter");
        if (nozzle.is_number()) {
            caps.nozzle_diameter = nozzle.get<double>();
        } else if (nozzle.is_array() && !nozzle.empty()) {
            caps.nozzle_diameter = nozzle.front().get<double>();
        }
    }

    caps.max_nozzle_temp = extract_max_temperature(machine_config, "nozzle_temperature_max");
    if (caps.max_nozzle_temp == 0.0) {
        caps.max_nozzle_temp = 260.0;
    }

    caps.max_bed_temp = extract_max_temperature(machine_config, "bed_temperature_max");
    if (caps.max_bed_temp == 0.0 && caps.has_heated_bed) {
        caps.max_bed_temp = 110.0;
    }

    if (machine_config.contains("printable_area") && machine_config.at("printable_area").is_array()) {
        const auto& area = machine_config.at("printable_area");
        if (!area.empty() && area.front().is_object()) {
            caps.bed_size_x = area.front().value("x", caps.bed_size_x);
            caps.bed_size_y = area.front().value("y", caps.bed_size_y);
        }
    }

    caps.bed_size_z = machine_config.value("printable_height", caps.bed_size_z);

    caps.max_print_speed = extract_max_speed(machine_config, "machine_max_speed_x");
    caps.max_travel_speed = std::max(caps.max_print_speed,
                                     extract_max_speed(machine_config, "machine_max_speed_travel"));
    if (caps.max_print_speed == 0.0) {
        caps.max_print_speed = 100.0;
    }
    if (caps.max_travel_speed == 0.0) {
        caps.max_travel_speed = 150.0;
    }

    caps.acceleration = extract_max_speed(machine_config, "machine_max_acceleration_x");
    if (caps.acceleration == 0.0) {
        caps.acceleration = 1000.0;
    }

    // Detect kinematics type from printer name/model
    std::string printer_name_lower;
    if (machine_config.contains("name")) {
        printer_name_lower = machine_config.at("name").get<std::string>();
        std::transform(printer_name_lower.begin(), printer_name_lower.end(), 
                      printer_name_lower.begin(), ::tolower);
    }
    
    // Detect CoreXY printers (bed doesn't move in Y)
    if (printer_name_lower.find("corexy") != std::string::npos ||
        printer_name_lower.find("core xy") != std::string::npos ||
        printer_name_lower.find("voron") != std::string::npos ||
        printer_name_lower.find("vzbot") != std::string::npos ||
        printer_name_lower.find("hypercube") != std::string::npos ||
        printer_name_lower.find("railcore") != std::string::npos) {
        caps.kinematics = PrinterCapabilities::Kinematics::CoreXY;
        caps.is_bed_slinger = false;
    }
    // Detect Delta printers (bed doesn't move at all)
    else if (printer_name_lower.find("delta") != std::string::npos ||
             printer_name_lower.find("kossel") != std::string::npos ||
             printer_name_lower.find("rostock") != std::string::npos) {
        caps.kinematics = PrinterCapabilities::Kinematics::Delta;
        caps.is_bed_slinger = false;
    }
    // Detect CoreXZ (bed moves in Y, but gantry shares X/Z)
    else if (printer_name_lower.find("corexz") != std::string::npos ||
             printer_name_lower.find("core xz") != std::string::npos) {
        caps.kinematics = PrinterCapabilities::Kinematics::CoreXZ;
        caps.is_bed_slinger = true;  // Still a bed slinger in Y axis
    }
    // Default: Cartesian bed slinger (Ender, Prusa MK3, CR-10, etc.)
    else {
        caps.kinematics = PrinterCapabilities::Kinematics::Cartesian;
        caps.is_bed_slinger = true;  // Conservative default
    }

    // Default to intermediate - caller should override if app_config available
    caps.skill_level = PrinterCapabilities::SkillLevel::Intermediate;
    return caps;
}

PrinterCapabilityDetector::PrinterCapabilities
PrinterCapabilityDetector::create_diy_printer_profile(const nlohmann::json& config, const std::string& name) {
    PrinterCapabilities caps = infer_capabilities_from_config(config);
    caps.profile_name = name;
    caps.is_diy_printer = true;
    return caps;
}

void PrinterCapabilityDetector::enhance_capabilities_from_config(PrinterCapabilities& caps,
                                                                 const nlohmann::json& config) const {
    PrinterCapabilities inferred = infer_capabilities_from_config(config);

    caps.has_heated_bed = caps.has_heated_bed || inferred.has_heated_bed;
    caps.has_enclosure = caps.has_enclosure || inferred.has_enclosure;
    caps.has_auto_leveling = caps.has_auto_leveling || inferred.has_auto_leveling;
    caps.has_filament_sensor = caps.has_filament_sensor || inferred.has_filament_sensor;
    caps.has_chamber_heating = caps.has_chamber_heating || inferred.has_chamber_heating;

    caps.nozzle_diameter = inferred.nozzle_diameter;
    caps.max_nozzle_temp = std::max(caps.max_nozzle_temp, inferred.max_nozzle_temp);
    caps.max_bed_temp = std::max(caps.max_bed_temp, inferred.max_bed_temp);
    caps.max_print_speed = std::max(caps.max_print_speed, inferred.max_print_speed);
    caps.max_travel_speed = std::max(caps.max_travel_speed, inferred.max_travel_speed);
    caps.acceleration = std::max(caps.acceleration, inferred.acceleration);

    caps.bed_size_x = inferred.bed_size_x;
    caps.bed_size_y = inferred.bed_size_y;
    caps.bed_size_z = inferred.bed_size_z;
}

std::vector<std::string> PrinterCapabilityDetector::get_supported_printers() const {
    std::vector<std::string> names;
    names.reserve(m_printer_database.size());
    for (const auto& entry : m_printer_database) {
        names.push_back(entry.first);
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool PrinterCapabilityDetector::is_printer_supported(const std::string& printer_name) const {
    return m_printer_database.find(printer_name) != m_printer_database.end();
}

void PrinterCapabilityDetector::load_printer_database() {
    if (m_database_loaded) {
        return;
    }

    m_printer_database.clear();
    m_manufacturer_index.clear();

    if (!std::filesystem::exists(m_profiles_directory)) {
        m_database_loaded = true;
        return;
    }

    for (const auto& manufacturer_entry : std::filesystem::directory_iterator(m_profiles_directory)) {
        if (manufacturer_entry.is_directory()) {
            load_manufacturer_profiles(manufacturer_entry.path().string());
        }
    }

    m_database_loaded = true;
}

void PrinterCapabilityDetector::load_manufacturer_profiles(const std::string& manufacturer_dir) {
    std::string machine_dir = manufacturer_dir + "/machine";
    if (!std::filesystem::exists(machine_dir)) {
        return;
    }

    const std::string config_dir = manufacturer_dir + "/config";

    for (const auto& machine_entry : std::filesystem::directory_iterator(machine_dir)) {
        if (!machine_entry.is_regular_file()) {
            continue;
        }

        const std::string machine_path = machine_entry.path().string();
        const std::string filename = machine_entry.path().filename().string();
        const std::string config_path = config_dir + "/" + filename;

        if (!std::filesystem::exists(config_path)) {
            continue;
        }

        try {
            PrinterProfile profile = parse_printer_profile(config_path, machine_path);
            if (!profile.name.empty()) {
                m_manufacturer_index[profile.manufacturer].push_back(profile.name);
                m_printer_database[profile.name] = std::move(profile);
            }
        } catch (...) {
            // Ignore malformed profiles; upstream data occasionally incomplete.
        }
    }
}

PrinterCapabilityDetector::PrinterProfile
PrinterCapabilityDetector::parse_printer_profile(const std::string& config_path,
                                                 const std::string& machine_path) {
    PrinterProfile profile;

    std::ifstream machine_stream(machine_path);
    nlohmann::json machine_json;
    machine_stream >> machine_json;

    profile.name = machine_json.value("name", "");
    profile.manufacturer = machine_json.value("manufacturer", "");
    profile.model_id = machine_json.value("model_id", "");
    profile.printable_height = machine_json.value("printable_height", 200.0);

    if (machine_json.contains("nozzle_diameter")) {
        const auto& nozzle = machine_json.at("nozzle_diameter");
        if (nozzle.is_array()) {
            for (const auto& item : nozzle) {
                profile.nozzle_diameters.push_back(item.get<double>());
            }
        } else if (nozzle.is_number()) {
            profile.nozzle_diameters.push_back(nozzle.get<double>());
        }
    }

    std::ifstream config_stream(config_path);
    if (config_stream.good()) {
        nlohmann::json config_json;
        config_stream >> config_json;
        auto caps = infer_capabilities_from_config(config_json);
        profile.has_heated_bed = caps.has_heated_bed;
        profile.has_enclosure = caps.has_enclosure;
        profile.has_auto_leveling = caps.has_auto_leveling;
        profile.has_filament_sensor = caps.has_filament_sensor;
        profile.has_chamber_heating = caps.has_chamber_heating;
        profile.max_nozzle_temp = caps.max_nozzle_temp;
        profile.max_bed_temp = caps.max_bed_temp;
        profile.max_print_speed = caps.max_print_speed;
        profile.max_travel_speed = caps.max_travel_speed;
        profile.max_acceleration = caps.acceleration;
        profile.bed_size_x = caps.bed_size_x;
        profile.bed_size_y = caps.bed_size_y;
        profile.printable_height = caps.bed_size_z;
    }

    profile.config_file_path = config_path;
    profile.machine_file_path = machine_path;
    return profile;
}

bool PrinterCapabilityDetector::infer_heated_bed_capability(const nlohmann::json& config) const {
    if (config.contains("bed_temperature")) {
        const auto& value = config.at("bed_temperature");
        if (value.is_array()) {
            for (const auto& entry : value) {
                if (entry.is_number() && entry.get<double>() > 0.0) {
                    return true;
                }
            }
        } else if (value.is_number()) {
            return value.get<double>() > 0.0;
        }
    }

    return config.value("has_heated_bed", 0) == 1;
}

bool PrinterCapabilityDetector::infer_enclosure_capability(const nlohmann::json& config) const {
    return config.value("has_enclosure", 0) == 1 || config.contains("enclosure_temperature");
}

bool PrinterCapabilityDetector::infer_auto_leveling_capability(const nlohmann::json& config) const {
    return config.value("has_auto_leveling", 0) == 1 || config.contains("auto_leveling") ;
}

bool PrinterCapabilityDetector::infer_filament_sensor_capability(const nlohmann::json& config) const {
    return config.value("filament_runout_sensor", 0) == 1 || config.contains("filament_sensor");
}

double PrinterCapabilityDetector::extract_max_temperature(const nlohmann::json& config,
                                                           const std::string& key) const {
    if (config.contains(key)) {
        const auto& value = config.at(key);
        if (value.is_number()) {
            return value.get<double>();
        }
        if (value.is_array() && !value.empty() && value.front().is_number()) {
            return value.front().get<double>();
        }
    }
    return 0.0;
}

double PrinterCapabilityDetector::extract_max_speed(const nlohmann::json& config,
                                                     const std::string& key) const {
    if (config.contains(key)) {
        const auto& value = config.at(key);
        if (value.is_number()) {
            return value.get<double>();
        }
        if (value.is_array() && !value.empty() && value.front().is_number()) {
            return value.front().get<double>();
        }
    }
    return 0.0;
}

PrinterCapabilityDetector::PrinterCapabilities::SkillLevel
PrinterCapabilityDetector::detect_user_skill_level(const AppConfig* app_config) const {
    if (!app_config) {
        return PrinterCapabilities::SkillLevel::Intermediate;
    }
    
    std::string usage;
    if (app_config->get("ai", "ai_user_skill_level", usage)) {
        if (usage == "beginner") {
            return PrinterCapabilities::SkillLevel::Beginner;
        }
        if (usage == "expert") {
            return PrinterCapabilities::SkillLevel::Expert;
        }
    }
    return PrinterCapabilities::SkillLevel::Intermediate;
}

PrinterCapabilityDetector::PrinterCapabilities
PrinterCapabilityDetector::convert_profile_to_capabilities(const PrinterProfile& profile) const {
    PrinterCapabilities caps{};
    caps.has_heated_bed = profile.has_heated_bed;
    caps.has_enclosure = profile.has_enclosure;
    caps.has_auto_leveling = profile.has_auto_leveling;
    caps.has_filament_sensor = profile.has_filament_sensor;
    caps.has_chamber_heating = profile.has_chamber_heating;
    caps.nozzle_diameter = profile.nozzle_diameters.empty() ? 0.4 : profile.nozzle_diameters.front();
    caps.max_nozzle_temp = profile.max_nozzle_temp;
    caps.max_bed_temp = profile.max_bed_temp;
    caps.bed_size_x = profile.bed_size_x;
    caps.bed_size_y = profile.bed_size_y;
    caps.bed_size_z = profile.printable_height;
    caps.max_print_speed = profile.max_print_speed;
    caps.max_travel_speed = profile.max_travel_speed;
    caps.acceleration = profile.max_acceleration;
    caps.profile_name = profile.name;
    return caps;
}

// ------------------------- OfflineAIAdapter -------------------------

namespace {
const std::map<std::string, OfflineAIAdapter::MaterialPreset> kMaterialDefaults = {
    // temp, bed_temp, speed, layer_height, infill, retract_len, retract_speed, fan_min, fan_max, needs_cool, bridge_flow, overhang_mult, ext_perim_mult
    {"PLA", {210.0, 60.0, 60.0, 0.2, 20.0, 0.8, 40.0, 0, 100, true, 0.95, 0.5, 0.5}},
    {"ABS", {235.0, 100.0, 50.0, 0.2, 20.0, 1.0, 40.0, 0, 30, false, 0.95, 0.6, 0.6}},
    {"PETG", {235.0, 75.0, 50.0, 0.2, 20.0, 1.5, 30.0, 30, 50, true, 0.95, 0.55, 0.55}},
    {"TPU", {220.0, 50.0, 30.0, 0.2, 15.0, 0.5, 25.0, 50, 100, true, 1.0, 0.4, 0.4}}
};

const std::map<std::string, OfflineAIAdapter::QualityModifier> kQualityModifiers = {
    {"draft", {1.5, 1.3, -5.0}},
    {"normal", {1.0, 1.0, 0.0}},
    {"fine", {0.5, 0.7, 5.0}}
};
}

OfflineAIAdapter::OfflineAIAdapter() = default;

bool OfflineAIAdapter::is_available() const {
    return true;
}

std::string OfflineAIAdapter::name() const {
    return "offline_native";
}

AIOptimizationResult OfflineAIAdapter::optimize(const GeometryFeatures& features,
                                                 const nlohmann::json& current_parameters,
                                                 const std::string& context) {
    (void)context;

    AIOptimizationResult result;
    result.source = "offline_native";
    result.confidence = 0.75;

    std::string material = current_parameters.value("material", "PLA");
    std::string quality = current_parameters.value("print_quality", "normal");

    MaterialPreset preset = get_material_preset(material);
    QualityModifier quality_mod = get_quality_modifier(quality);

    preset.layer_height *= quality_mod.layer_height_multiplier;
    preset.print_speed *= quality_mod.speed_multiplier;
    preset.infill += quality_mod.infill_adjustment;

    std::vector<std::string> reasoning_parts;
    apply_geometry_adjustments(preset, features, reasoning_parts);
    apply_printer_capability_adjustments(preset, reasoning_parts);

    // Basic parameters
    result.parameters["temperature"] = preset.temperature;
    result.parameters["bed_temperature"] = preset.bed_temperature;
    result.parameters["layer_height"] = preset.layer_height;
    result.parameters["infill"] = std::clamp(preset.infill, 5.0, 100.0);
    result.parameters["enable_support"] = features.has_overhangs || features.has_bridges;
    result.parameters["material"] = material;

    // Generate comprehensive parameters for all aspects of printing
    generate_speed_parameters(result.parameters, preset, features);
    generate_wall_parameters(result.parameters, preset);
    generate_infill_parameters(result.parameters, preset, features);
    generate_support_parameters(result.parameters, features);
    generate_cooling_parameters(result.parameters, preset, features);
    generate_retraction_parameters(result.parameters, preset);
    generate_quality_parameters(result.parameters, preset, features);

    if (current_parameters.contains("custom_params") && current_parameters["custom_params"].is_object()) {
        for (const auto& [key, value] : current_parameters["custom_params"].items()) {
            result.parameters[key] = value;
        }
    }

    std::ostringstream oss;
    oss << "Native offline optimization for " << material << " at " << quality << " quality";
    if (!reasoning_parts.empty()) {
        oss << ". ";
        for (size_t i = 0; i < reasoning_parts.size(); ++i) {
            if (i > 0) {
                oss << ". ";
            }
            oss << reasoning_parts[i];
        }
    }
    result.reasoning = oss.str();

    // Filter parameters if AI mode is enabled
    if (m_ai_mode_enabled) {
        result.parameters = filter_parameters_for_ai_mode(result.parameters);
    }

    return result;
}

void OfflineAIAdapter::set_printer_capabilities(const PrinterCapabilityDetector::PrinterCapabilities& caps) {
    m_printer_caps = caps;
    m_caps_initialized = true;
}

OfflineAIAdapter::MaterialPreset
OfflineAIAdapter::get_material_preset(const std::string& material) const {
    auto it = kMaterialDefaults.find(material);
    if (it != kMaterialDefaults.end()) {
        return it->second;
    }
    return kMaterialDefaults.at("PLA");
}

OfflineAIAdapter::QualityModifier
OfflineAIAdapter::get_quality_modifier(const std::string& quality) const {
    auto it = kQualityModifiers.find(quality);
    if (it != kQualityModifiers.end()) {
        return it->second;
    }
    return kQualityModifiers.at("normal");
}

void OfflineAIAdapter::apply_geometry_adjustments(MaterialPreset& preset,
                                                  const GeometryFeatures& features,
                                                  std::vector<std::string>& reasoning_parts) const {
    // Tall print adjustments - MORE aggressive for bed slingers
    if (features.height > 100.0) {
        if (m_caps_initialized && m_printer_caps.is_bed_slinger) {
            // Bed slinger: moving bed creates inertial forces proportional to height
            // Formula: speed_multiplier = 1.0 - (height_mm / 1000)
            // Examples: 100mm = 0.9x, 150mm = 0.85x, 200mm = 0.8x, 300mm = 0.7x
            double height_penalty = std::min(0.3, features.height / 1000.0);
            preset.print_speed *= (1.0 - height_penalty);
            preset.infill += 5.0 + (features.height / 50.0);  // More infill for taller prints
            reasoning_parts.push_back("Bed slinger with tall print: reduced speed significantly to prevent knockover from bed acceleration");
        } else if (m_caps_initialized && !m_printer_caps.is_bed_slinger) {
            // CoreXY/Delta: bed doesn't move, less concern for inertial forces
            preset.print_speed *= 0.95;  // Minor reduction for vibration only
            preset.infill += 5.0;
            reasoning_parts.push_back("CoreXY/Delta printer: minimal speed reduction for tall print (bed stationary)");
        } else {
            // Unknown printer type: assume bed slinger (conservative)
            preset.print_speed *= 0.9;
            preset.infill += 5.0;
            reasoning_parts.push_back("Reduced speed and increased infill for tall print");
        }
    }

    if (features.has_bridges) {
        preset.print_speed *= 0.95;
        reasoning_parts.push_back("Slightly reduced speed for bridge features");
    }

    if (features.has_small_details) {
        preset.print_speed *= 0.8;
        reasoning_parts.push_back("Reduced speed for small details");
    }

    if (features.has_thin_walls) {
        preset.print_speed *= 0.85;
        reasoning_parts.push_back("Reduced speed for thin walls");
    }

    if (features.overhang_percentage > 15.0) {
        reasoning_parts.push_back("Significant overhangs detected");
    }

    if (features.has_overhangs) {
        reasoning_parts.push_back("Enabled support for overhangs");
    }
}

void OfflineAIAdapter::apply_printer_capability_adjustments(MaterialPreset& preset,
                                                            std::vector<std::string>& reasoning_parts) const {
    if (!m_caps_initialized) {
        return;
    }

    if (!m_printer_caps.has_heated_bed) {
        preset.bed_temperature = 0.0;
        reasoning_parts.push_back("Disabled bed heating for non-heated bed printer");
    } else {
        preset.bed_temperature = std::min(preset.bed_temperature, m_printer_caps.max_bed_temp);
    }

    preset.temperature = std::min(preset.temperature, m_printer_caps.max_nozzle_temp);

    if (m_printer_caps.skill_level == PrinterCapabilityDetector::PrinterCapabilities::SkillLevel::Beginner) {
        preset.print_speed *= 0.85;
        reasoning_parts.push_back("Conservative speeds for beginner profile");
    }

    preset.print_speed = std::min(preset.print_speed, m_printer_caps.max_print_speed);
}

// Filter to only keep printer and filament parameters for AI mode UI
nlohmann::json OfflineAIAdapter::filter_parameters_for_ai_mode(const nlohmann::json& params) {
    nlohmann::json filtered;
    
    // Only keep printer-related parameters
    if (params.contains("printer")) filtered["printer"] = params["printer"];
    if (params.contains("nozzle_diameter")) filtered["nozzle_diameter"] = params["nozzle_diameter"];
    
    // Only keep filament-related parameters
    if (params.contains("material")) filtered["material"] = params["material"];
    if (params.contains("temperature")) filtered["temperature"] = params["temperature"];
    if (params.contains("bed_temperature")) filtered["bed_temperature"] = params["bed_temperature"];
    if (params.contains("filament_color")) filtered["filament_color"] = params["filament_color"];
    
    return filtered;
}

// Generate all speed-related parameters based on material and geometry
void OfflineAIAdapter::generate_speed_parameters(nlohmann::json& params, 
                                                  const MaterialPreset& preset, 
                                                  const GeometryFeatures& features) const {
    double base_speed = preset.print_speed;
    
    // Adjust for geometry
    if (features.has_small_details) {
        base_speed *= 0.8;
    }
    if (features.height > 100.0) {
        base_speed *= 0.9;
    }
    
    params["outer_wall_speed"] = base_speed * preset.external_perimeter_speed_multiplier;
    params["inner_wall_speed"] = base_speed;
    params["sparse_infill_speed"] = base_speed * 1.2;
    params["internal_solid_infill_speed"] = base_speed;
    params["top_surface_speed"] = base_speed * 0.6;
    params["support_speed"] = base_speed * 0.8;
    params["support_interface_speed"] = base_speed * 0.7;
    params["bridge_speed"] = base_speed * 0.6;
    params["gap_fill_speed"] = base_speed * 0.5;
    params["travel_speed"] = m_caps_initialized ? m_printer_caps.max_travel_speed : base_speed * 2.5;
    params["first_layer_speed"] = base_speed * 0.5;
    
    // Overhangs need slower speeds
    if (features.has_overhangs) {
        params["overhang_speed"] = base_speed * preset.overhang_speed_multiplier;
    }
}

// Generate wall/perimeter parameters based on nozzle size
void OfflineAIAdapter::generate_wall_parameters(nlohmann::json& params, const MaterialPreset& preset) const {
    double nozzle = m_caps_initialized ? m_printer_caps.nozzle_diameter : 0.4;
    
    // Calculate optimal wall count based on nozzle
    int wall_loops = std::max(2, static_cast<int>(std::ceil(1.2 / nozzle)));
    params["wall_loops"] = wall_loops;
    params["line_width"] = nozzle;
    params["outer_wall_line_width"] = nozzle;
    params["inner_wall_line_width"] = nozzle * 1.05;
    params["top_surface_line_width"] = nozzle * 0.9;
    params["internal_solid_infill_line_width"] = nozzle;
    params["sparse_infill_line_width"] = nozzle * 1.2;
    params["support_line_width"] = nozzle;
}

// Generate infill parameters based on geometry strength needs
void OfflineAIAdapter::generate_infill_parameters(nlohmann::json& params, 
                                                   const MaterialPreset& preset,
                                                   const GeometryFeatures& features) const {
    double infill = std::clamp(preset.infill, 5.0, 100.0);
    
    // Adjust infill based on geometry
    if (features.height > 100.0) {
        infill += 5.0; // Taller prints need more strength
    }
    if (features.volume > 100000.0) {
        infill = std::max(infill, 15.0); // Large prints need minimum infill
    }
    
    params["sparse_infill_density"] = std::clamp(infill, 5.0, 100.0);
    params["sparse_infill_pattern"] = infill < 25.0 ? "grid" : "gyroid";
    params["top_shell_layers"] = std::max(3, static_cast<int>(std::ceil(0.8 / preset.layer_height)));
    params["bottom_shell_layers"] = std::max(3, static_cast<int>(std::ceil(0.6 / preset.layer_height)));
}

// Generate support parameters based on detected geometry features
void OfflineAIAdapter::generate_support_parameters(nlohmann::json& params, const GeometryFeatures& features) const {
    if (features.has_overhangs || features.has_bridges) {
        params["enable_support"] = true;
        params["support_type"] = "normal";
        params["support_on_build_plate_only"] = false;
        params["support_angle"] = 45.0;
        params["support_interface_pattern"] = "rectilinear";
        params["support_interface_top_layers"] = 2;
        params["support_interface_bottom_layers"] = 2;
        params["support_base_pattern"] = "rectilinear";
        params["support_base_pattern_spacing"] = 2.5;
        
        if (features.overhang_percentage > 20.0) {
            params["support_interface_top_layers"] = 3;
            params["support_interface_bottom_layers"] = 3;
        }
    } else {
        params["enable_support"] = false;
    }
}

// Generate cooling/fan parameters based on material and geometry
void OfflineAIAdapter::generate_cooling_parameters(nlohmann::json& params, 
                                                    const MaterialPreset& preset,
                                                    const GeometryFeatures& features) const {
    params["fan_min_speed"] = preset.fan_speed_min;
    params["fan_max_speed"] = preset.fan_speed_max;
    
    if (features.has_bridges) {
        params["bridge_fan_speed"] = 100;
        params["bridge_flow_ratio"] = preset.bridge_flow_ratio;
    }
    
    if (features.has_overhangs) {
        params["overhang_fan_speed"] = std::min(100, preset.fan_speed_max + 20);
        params["overhang_fan_threshold"] = "50%";
    }
    
    // First layer cooling
    params["slow_down_for_layer_cooling"] = true;
    params["fan_cooling_layer_time"] = 60.0;
    
    if (features.has_small_details) {
        params["slow_down_min_speed"] = 10.0;
    }
}

// Generate retraction parameters based on material properties
void OfflineAIAdapter::generate_retraction_parameters(nlohmann::json& params, const MaterialPreset& preset) const {
    params["retraction_length"] = preset.retraction_length;
    params["retraction_speed"] = preset.retraction_speed;
    params["deretraction_speed"] = preset.retraction_speed * 0.8;
    params["retract_before_wipe"] = 70;
    params["retract_when_changing_layer"] = true;
    params["wipe"] = true;
    params["wipe_distance"] = 2.0;
    
    // Z-hop for overhangs
    params["retract_lift_above"] = "0";
    params["retract_lift_below"] = "0";
}

// Generate quality and finishing parameters
void OfflineAIAdapter::generate_quality_parameters(nlohmann::json& params, 
                                                    const MaterialPreset& preset,
                                                    const GeometryFeatures& features) const {
    // Seam settings
    params["seam_position"] = "aligned";
    params["seam_gap"] = 0;
    
    // First layer settings
    params["initial_layer_line_width"] = 0.42; // Slightly wider for adhesion
    params["initial_layer_print_height"] = preset.layer_height * 1.25;
    
    // Ironing for top surfaces if high quality
    if (preset.layer_height <= 0.15) {
        params["ironing_type"] = "top";
        params["ironing_flow"] = 15.0;
        params["ironing_spacing"] = 0.1;
    }
    
    // Adaptive layer height for complex geometry
    if (features.has_small_details || features.has_bridges) {
        params["adaptive_layer_height"] = true;
        params["max_layer_height"] = preset.layer_height;
        params["min_layer_height"] = preset.layer_height * 0.75;
    }
    
    // Arc fitting for smoother curves
    params["arc_fitting"] = true;
    params["arc_fitting_tolerance"] = 0.05;
}

}} // namespace Slic3r::AI
