#pragma once

#include "AIAdapter.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"
#include <nlohmann/json.hpp>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r { namespace AI {

class GeometryAnalyzer {
public:
    static GeometryFeatures analyze(const ModelObject* model_object);
    static double calculate_surface_area(const TriangleMesh& mesh);
    static double calculate_volume(const TriangleMesh& mesh);
    static bool detect_overhangs(const TriangleMesh& mesh, double threshold_angle = 45.0);
    static bool detect_bridges(const TriangleMesh& mesh);
    static bool detect_thin_walls(const TriangleMesh& mesh, double min_thickness = 0.8);
    static double calculate_overhang_percentage(const TriangleMesh& mesh, double threshold_angle = 45.0);
};

class PrinterCapabilityDetector {
public:
    struct PrinterCapabilities {
        bool has_heated_bed = false;
        bool has_enclosure = false;
        bool has_auto_leveling = false;
        bool has_filament_sensor = false;
        bool has_chamber_heating = false;
        double nozzle_diameter = 0.4;
        double min_nozzle_temp = 180.0;
        double max_nozzle_temp = 260.0;
        double bed_size_x = 200.0;
        double bed_size_y = 200.0;
        double bed_size_z = 200.0;
        double max_bed_temp = 110.0;
        double max_print_speed = 100.0;
        double max_travel_speed = 150.0;
        double acceleration = 1000.0;
        enum class SkillLevel { Beginner, Intermediate, Expert } skill_level = SkillLevel::Intermediate;
        enum class Kinematics { Cartesian, CoreXY, CoreXZ, Delta, Unknown } kinematics = Kinematics::Cartesian;
        bool is_bed_slinger = true;  // Default to bed slinger (most common, most conservative)
        bool is_diy_printer = false;
        std::string profile_name;
    };

    PrinterCapabilityDetector();
    
    // Main detection methods - these can work with or without PresetBundle
    std::optional<PrinterCapabilities> detect_printer_by_name(const std::string& printer_name);
    std::optional<PrinterCapabilities> detect_printer_by_model_id(const std::string& model_id);
    PrinterCapabilities infer_capabilities_from_config(const nlohmann::json& machine_config) const;
    PrinterCapabilities create_diy_printer_profile(const nlohmann::json& config, const std::string& name = "DIY Printer");
    void enhance_capabilities_from_config(PrinterCapabilities& caps, const nlohmann::json& config) const;
    
    // Utility methods
    std::vector<std::string> get_supported_printers() const;
    bool is_printer_supported(const std::string& printer_name) const;
    
    // Methods that require PresetBundle - call these from GUI code
    std::optional<PrinterCapabilities> detect_from_preset_bundle(const PresetBundle* bundle);
    std::optional<PrinterCapabilities> detect_from_preset_bundle(const PresetBundle* bundle, 
                                                                  const AppConfig* app_config);

private:
    struct PrinterProfile {
        std::string name;
        std::string manufacturer;
        std::string model_id;
        std::vector<double> nozzle_diameters;
        double printable_height = 200.0;
        double bed_size_x = 200.0;
        double bed_size_y = 200.0;
        bool has_heated_bed = false;
        bool has_enclosure = false;
        bool has_auto_leveling = false;
        bool has_filament_sensor = false;
        bool has_chamber_heating = false;
        double max_nozzle_temp = 260.0;
        double max_bed_temp = 110.0;
        double max_print_speed = 120.0;
        double max_travel_speed = 150.0;
        double max_acceleration = 1000.0;
        bool supports_linear_advance = false;
        std::string gcode_flavor;
        std::string config_file_path;
        std::string machine_file_path;
    };

    void load_printer_database();
    void load_manufacturer_profiles(const std::string& manufacturer_dir);
    PrinterProfile parse_printer_profile(const std::string& config_path, const std::string& machine_path);
    bool infer_heated_bed_capability(const nlohmann::json& config) const;
    bool infer_enclosure_capability(const nlohmann::json& config) const;
    bool infer_auto_leveling_capability(const nlohmann::json& config) const;
    bool infer_filament_sensor_capability(const nlohmann::json& config) const;
    double extract_max_temperature(const nlohmann::json& config, const std::string& key) const;
    double extract_max_speed(const nlohmann::json& config, const std::string& key) const;
    PrinterCapabilities::SkillLevel detect_user_skill_level(const AppConfig* app_config) const;
    PrinterCapabilities convert_profile_to_capabilities(const PrinterProfile& profile) const;

    std::map<std::string, PrinterProfile> m_printer_database;
    std::map<std::string, std::vector<std::string>> m_manufacturer_index;
    std::string m_profiles_directory;
    bool m_database_loaded = false;
    mutable std::optional<PrinterCapabilities> m_cached_capabilities;
    mutable std::string m_cached_printer_name;
};

class OfflineAIAdapter : public AIAdapter {
public:
    OfflineAIAdapter();
    ~OfflineAIAdapter() override = default;

    bool is_available() const override;
    std::string name() const override;
    AIOptimizationResult optimize(const GeometryFeatures& features,
                                  const nlohmann::json& current_parameters,
                                  const std::string& context = "") override;

    // AI mode control - when enabled, optimizes ALL parameters automatically
    void set_ai_mode(bool enabled) { m_ai_mode_enabled = enabled; }
    bool ai_mode_enabled() const { return m_ai_mode_enabled; }
    
    // Filter parameters to only show printer/filament settings in AI mode
    static nlohmann::json filter_parameters_for_ai_mode(const nlohmann::json& params);

    void set_printer_capabilities(const PrinterCapabilityDetector::PrinterCapabilities& caps);

    struct MaterialPreset {
        double temperature;
        double bed_temperature;
        double print_speed;
        double layer_height;
        double infill;
        
        // Advanced material properties for comprehensive optimization
        double retraction_length = 0.8;
        double retraction_speed = 40.0;
        int fan_speed_min = 0;
        int fan_speed_max = 100;
        bool needs_cooling = true;
        double bridge_flow_ratio = 0.95;
        double overhang_speed_multiplier = 0.5;
        double external_perimeter_speed_multiplier = 0.5;
    };

    struct QualityModifier {
        double layer_height_multiplier;
        double speed_multiplier;
        double infill_adjustment;
    };

private:
    MaterialPreset get_material_preset(const std::string& material) const;
    QualityModifier get_quality_modifier(const std::string& quality) const;
    void apply_geometry_adjustments(MaterialPreset& preset,
                                    const GeometryFeatures& features,
                                    std::vector<std::string>& reasoning_parts) const;
    void apply_printer_capability_adjustments(MaterialPreset& preset,
                                              std::vector<std::string>& reasoning_parts) const;
    
    // Comprehensive parameter generation for Auto Slice mode
    void generate_speed_parameters(nlohmann::json& params, const MaterialPreset& preset, const GeometryFeatures& features) const;
    void generate_wall_parameters(nlohmann::json& params, const MaterialPreset& preset) const;
    void generate_infill_parameters(nlohmann::json& params, const MaterialPreset& preset, const GeometryFeatures& features) const;
    void generate_support_parameters(nlohmann::json& params, const GeometryFeatures& features) const;
    void generate_cooling_parameters(nlohmann::json& params, const MaterialPreset& preset, const GeometryFeatures& features) const;
    void generate_retraction_parameters(nlohmann::json& params, const MaterialPreset& preset) const;
    void generate_quality_parameters(nlohmann::json& params, const MaterialPreset& preset, const GeometryFeatures& features) const;

    PrinterCapabilityDetector::PrinterCapabilities m_printer_caps;
    bool m_caps_initialized = false;
    bool m_ai_mode_enabled = false;
};

}} // namespace Slic3r::AI
