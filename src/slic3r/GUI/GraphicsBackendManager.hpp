#ifndef slic3r_GraphicsBackendManager_hpp_
#define slic3r_GraphicsBackendManager_hpp_

#include <string>
#include <mutex>

namespace Slic3r {
namespace GUI {

/**
 * GraphicsBackendManager - Automatic Graphics Backend Detection and Configuration
 * 
 * This class provides automatic detection of graphics hardware and session types,
 * and applies optimal OpenGL/graphics configurations for Linux systems.
 * 
 * Key Features:
 * - Direct OpenGL API-based detection (no external tool dependencies)
 * - Support for X11 and Wayland sessions
 * - NVIDIA, AMD, Intel, and Mesa driver detection
 * - Container-aware detection (Docker, Flatpak, AppImage)
 * - Automatic environment variable configuration
 * 
 * Detection Methods (in order of preference):
 * 1. Direct OpenGL context creation and glGetString() API calls
 * 2. Container-aware filesystem detection
 * 3. Command-line tools (glxinfo/eglinfo) as fallback
 * 4. Hardware-based detection (PCI vendor IDs, nvidia-smi)
 */
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
    
    /**
     * Detect the current graphics environment using direct OpenGL API calls.
     * Primary detection method uses glGetString() via minimal GLX/EGL contexts.
     * Falls back to filesystem and command-line detection if needed.
     */
    GraphicsConfig detect_graphics_environment();
    
    /**
     * Apply graphics configuration by setting appropriate environment variables.
     * Validates configuration before applying and logs the applied settings.
     */
    void apply_graphics_config(const GraphicsConfig& config);
    
    /**
     * Get recommended configuration for current system based on detected hardware
     * and session type. Uses direct OpenGL detection for optimal compatibility.
     */
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

    // Detection helper methods
    SessionType detect_session_type();
    GraphicsDriver detect_graphics_driver();
    GraphicsDriver detect_graphics_driver_container_aware();
    bool is_running_in_container();
    std::string read_file_content(const std::string& filepath);
    std::string get_nvidia_driver_version();
    bool is_nvidia_driver_newer_than(int major_version);
    
    // Graphics information retrieval (now uses direct OpenGL API calls as primary method)
    std::string get_glx_info();           // GLX/X11 graphics info (direct OpenGL + fallback)
    std::string get_egl_info();           // EGL graphics info (direct OpenGL + fallback)
    std::string get_opengl_info_direct(); // Direct OpenGL detection via GLX/EGL contexts
    std::string query_opengl_strings(const std::string& context_type); // Helper to query OpenGL info strings
    void set_environment_variable(const std::string& name, const std::string& value);
    void unset_environment_variable(const std::string& name);
    std::string execute_command(const std::string& command);
    
    // Thread safety for OpenGL operations
    static std::mutex opengl_detection_mutex_;
    
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