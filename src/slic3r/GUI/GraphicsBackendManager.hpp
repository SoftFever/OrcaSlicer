#ifndef slic3r_GraphicsBackendManager_hpp_
#define slic3r_GraphicsBackendManager_hpp_

#include <string>

namespace Slic3r {
namespace GUI {

class GraphicsBackendManager
{
public:
    enum class SessionType
    {
        Unknown,
        X11,
        Wayland
    };

    enum class GraphicsDriver
    {
        Unknown,
        NVIDIA,
        AMD,
        Intel,
        Mesa
    };

    struct GraphicsConfig
    {
        SessionType session_type = SessionType::Unknown;
        GraphicsDriver driver = GraphicsDriver::Unknown;
        std::string gbm_backend;
        std::string mesa_loader_driver;
        std::string gallium_driver;
        std::string glx_vendor_library;
        std::string egl_vendor_library;
        bool force_dri_backend = false;
        bool use_zink = false;
        bool disable_dmabuf = false;
    };

    static GraphicsBackendManager& get_instance();
    
    // Detect the current graphics environment
    GraphicsConfig detect_graphics_environment();
    
    // Apply graphics configuration
    void apply_graphics_config(const GraphicsConfig& config);
    
    // Get recommended configuration for current system
    GraphicsConfig get_recommended_config();
    
    // Check if current configuration is optimal
    bool is_configuration_optimal();
    
    // Validate configuration before applying
    bool validate_configuration(const GraphicsConfig& config);
    
    // Log current graphics information
    void log_graphics_info();

private:
    GraphicsBackendManager() = default;
    ~GraphicsBackendManager() = default;
    GraphicsBackendManager(const GraphicsBackendManager&) = delete;
    GraphicsBackendManager& operator=(const GraphicsBackendManager&) = delete;

    // Helper methods
    SessionType detect_session_type();
    GraphicsDriver detect_graphics_driver();
    GraphicsDriver detect_graphics_driver_container_aware();
    bool is_running_in_container();
    std::string read_file_content(const std::string& filepath);
    std::string get_nvidia_driver_version();
    bool is_nvidia_driver_newer_than(int major_version);
    std::string get_glx_info();
    std::string get_egl_info();
    void set_environment_variable(const std::string& name, const std::string& value);
    void unset_environment_variable(const std::string& name);
    std::string execute_command(const std::string& command);
    
    // Configuration templates
    GraphicsConfig get_nvidia_wayland_config();
    GraphicsConfig get_nvidia_x11_config();
    GraphicsConfig get_amd_config();
    GraphicsConfig get_intel_config();
    GraphicsConfig get_mesa_config();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GraphicsBackendManager_hpp_ 