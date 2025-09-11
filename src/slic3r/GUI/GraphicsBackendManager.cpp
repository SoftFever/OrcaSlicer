#include "GraphicsBackendManager.hpp"
#include <boost/log/trivial.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef __linux__
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <EGL/egl.h>
#endif

namespace Slic3r {
namespace GUI {

// Thread safety for OpenGL detection operations
std::mutex GraphicsBackendManager::opengl_detection_mutex_;

GraphicsBackendManager& GraphicsBackendManager::get_instance()
{
    static GraphicsBackendManager instance;
    return instance;
}

GraphicsBackendManager::GraphicsConfig GraphicsBackendManager::detect_graphics_environment()
{
    GraphicsConfig config;
    
    config.session_type = detect_session_type();
    config.driver = detect_graphics_driver();
    
    BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected session type: " 
                            << (config.session_type == SessionType::Wayland ? "Wayland" : 
                                config.session_type == SessionType::X11 ? "X11" : "Unknown");
    BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected driver: " 
                            << (config.driver == GraphicsDriver::NVIDIA ? "NVIDIA" :
                                config.driver == GraphicsDriver::AMD ? "AMD" :
                                config.driver == GraphicsDriver::Intel ? "Intel" :
                                config.driver == GraphicsDriver::Mesa ? "Mesa" : "Unknown");
    
    return config;
}

GraphicsBackendManager::SessionType GraphicsBackendManager::detect_session_type()
{
#ifdef __linux__
    // In Flatpak, we should check both the host session and the sandbox session
    bool in_flatpak = (std::getenv("FLATPAK_ID") != nullptr);
    
    const char* xdg_session_type = std::getenv("XDG_SESSION_TYPE");
    if (xdg_session_type) {
        std::string session_type(xdg_session_type);
        if (boost::iequals(session_type, "wayland")) {
            if (in_flatpak) {
                BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Flatpak with Wayland session detected";
            }
            return SessionType::Wayland;
        } else if (boost::iequals(session_type, "x11")) {
            if (in_flatpak) {
                BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Flatpak with X11 session detected";
            }
            return SessionType::X11;
        }
    }
    
    // Fallback detection methods
    const char* wayland_display = std::getenv("WAYLAND_DISPLAY");
    if (wayland_display) {
        if (in_flatpak) {
            BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Flatpak with WAYLAND_DISPLAY=" << wayland_display;
        }
        return SessionType::Wayland;
    }
    
    const char* display = std::getenv("DISPLAY");
    if (display) {
        if (in_flatpak) {
            BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Flatpak with DISPLAY=" << display;
        }
        return SessionType::X11;
    }
#endif
    
    return SessionType::Unknown;
}

GraphicsBackendManager::GraphicsDriver GraphicsBackendManager::detect_graphics_driver()
{
#ifdef __linux__
    // First try container-aware detection methods
    GraphicsDriver container_driver = detect_graphics_driver_container_aware();
    if (container_driver != GraphicsDriver::Unknown) {
        BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected driver via container-aware method";
        return container_driver;
    }
    
    // Try to get OpenGL vendor and renderer info using direct OpenGL API calls
    // This creates minimal OpenGL contexts (GLX/EGL) and uses glGetString() directly
    std::string glx_info = get_glx_info();
    std::string egl_info = get_egl_info();
    
    BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: GLX info: " << (glx_info.empty() ? "empty" : glx_info.substr(0, 100));
    BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: EGL info: " << (egl_info.empty() ? "empty" : egl_info.substr(0, 100));
    
    // Check for NVIDIA
    if (boost::icontains(glx_info, "nvidia") || 
        boost::icontains(egl_info, "nvidia") ||
        boost::icontains(glx_info, "NVIDIA") || 
        boost::icontains(egl_info, "NVIDIA")) {
        BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected NVIDIA driver via GLX/EGL";
        return GraphicsDriver::NVIDIA;
    }
    
    // Check for AMD
    if (boost::icontains(glx_info, "amd") || 
        boost::icontains(egl_info, "amd") ||
        boost::icontains(glx_info, "AMD") || 
        boost::icontains(egl_info, "AMD") ||
        boost::icontains(glx_info, "radeon") || 
        boost::icontains(egl_info, "radeon")) {
        BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected AMD driver via GLX/EGL";
        return GraphicsDriver::AMD;
    }
    
    // Check for Intel
    if (boost::icontains(glx_info, "intel") || 
        boost::icontains(egl_info, "intel") ||
        boost::icontains(glx_info, "Intel") || 
        boost::icontains(egl_info, "Intel")) {
        BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected Intel driver via GLX/EGL";
        return GraphicsDriver::Intel;
    }
    
    // Check for Mesa
    if (boost::icontains(glx_info, "mesa") || 
        boost::icontains(egl_info, "mesa") ||
        boost::icontains(glx_info, "Mesa") || 
        boost::icontains(egl_info, "Mesa")) {
        BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected Mesa driver via GLX/EGL";
        return GraphicsDriver::Mesa;
    }
    
    // Try to run glxinfo as final fallback (may not work in containers)
    BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: Direct OpenGL and GLX/EGL detection failed, trying glxinfo fallback";
    std::string glx_output = execute_command("glxinfo 2>/dev/null | grep 'OpenGL vendor\\|OpenGL renderer'");
    if (!glx_output.empty()) {
        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: glxinfo fallback output: " << glx_output.substr(0, 200);
        
        if (boost::icontains(glx_output, "nvidia")) {
            BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected NVIDIA driver via glxinfo final fallback";
            return GraphicsDriver::NVIDIA;
        } else if (boost::icontains(glx_output, "amd") || boost::icontains(glx_output, "radeon")) {
            BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected AMD driver via glxinfo final fallback";
            return GraphicsDriver::AMD;
        } else if (boost::icontains(glx_output, "intel")) {
            BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected Intel driver via glxinfo final fallback";
            return GraphicsDriver::Intel;
        } else if (boost::icontains(glx_output, "mesa")) {
            BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected Mesa driver via glxinfo final fallback";
            return GraphicsDriver::Mesa;
        }
    }
    
    // Try additional fallback methods
    BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: All OpenGL detection methods failed, trying hardware-based fallbacks";
    
    // Check for NVIDIA using nvidia-smi
    std::string nvidia_check = execute_command("nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | head -n1");
    if (!nvidia_check.empty() && nvidia_check != "N/A") {
        BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected NVIDIA driver via nvidia-smi fallback";
        return GraphicsDriver::NVIDIA;
    }
    
    // Check for AMD using lspci
    std::string amd_check = execute_command("lspci | grep -i 'vga\\|3d' | grep -i amd 2>/dev/null");
    if (!amd_check.empty()) {
        BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected AMD driver via lspci fallback";
        return GraphicsDriver::AMD;
    }
    
    // Check for Intel using lspci
    std::string intel_check = execute_command("lspci | grep -i 'vga\\|3d' | grep -i intel 2>/dev/null");
    if (!intel_check.empty()) {
        BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected Intel driver via lspci fallback";
        return GraphicsDriver::Intel;
    }
    
    BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Failed to detect graphics driver, using Mesa as fallback";
    return GraphicsDriver::Mesa; // Default to Mesa as safest fallback
#else
    return GraphicsDriver::Unknown;
#endif
}

GraphicsBackendManager::GraphicsDriver GraphicsBackendManager::detect_graphics_driver_container_aware()
{
#ifdef __linux__
    BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: Using container-aware graphics detection";
    
    // Check if running in container
    bool in_container = is_running_in_container();
    if (in_container) {
        BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Running in container, using filesystem-based detection";
    }
    
    // Try environment variable based detection first
    const char* gl_vendor = std::getenv("__GLX_VENDOR_LIBRARY_NAME");
    if (gl_vendor) {
        std::string vendor(gl_vendor);
        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: __GLX_VENDOR_LIBRARY_NAME=" << vendor;
        if (boost::icontains(vendor, "nvidia")) {
            BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected NVIDIA via environment variable";
            return GraphicsDriver::NVIDIA;
        } else if (boost::icontains(vendor, "mesa")) {
            BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected Mesa via environment variable";
            return GraphicsDriver::Mesa;
        }
    }
    
    // Try to detect NVIDIA via filesystem access
    // Check for NVIDIA driver files (also check Flatpak-specific paths)
    std::string nvidia_version = read_file_content("/proc/driver/nvidia/version");
    if (!nvidia_version.empty()) {
        BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected NVIDIA driver via /proc/driver/nvidia/version";
        return GraphicsDriver::NVIDIA;
    }
    
    // In Flatpak, NVIDIA libs might be exposed via runtime extensions
    if (in_container) {
        // Check for NVIDIA libraries in Flatpak runtime paths
        std::string nvidia_lib_check = execute_command("find /usr/lib* /run/host/usr/lib* -name 'libGLX_nvidia.so*' 2>/dev/null | head -1");
        if (!nvidia_lib_check.empty()) {
            BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected NVIDIA driver via Flatpak runtime libraries";
            return GraphicsDriver::NVIDIA;
        }
        
        // Check if nvidia-smi is available in Flatpak
        std::string nvidia_smi_check = execute_command("which nvidia-smi 2>/dev/null");
        if (!nvidia_smi_check.empty()) {
            std::string gpu_info = execute_command("nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | head -n1");
            if (!gpu_info.empty() && gpu_info != "N/A") {
                BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected NVIDIA driver via nvidia-smi in Flatpak";
                return GraphicsDriver::NVIDIA;
            }
        }
    }
    
    // Check for DRM devices to detect graphics hardware
    std::string drm_devices = execute_command("ls -1 /sys/class/drm/card*/device/vendor 2>/dev/null | head -5");
    if (!drm_devices.empty()) {
        // Read vendor IDs from DRM
        std::istringstream iss(drm_devices);
        std::string vendor_file;
        while (std::getline(iss, vendor_file)) {
            std::string vendor_id = read_file_content(vendor_file);
            if (!vendor_id.empty()) {
                // Remove newline and whitespace
                boost::trim(vendor_id);
                BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: Found DRM vendor ID: " << vendor_id;
                
                if (vendor_id == "0x10de") {  // NVIDIA
                    BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected NVIDIA via DRM vendor ID";
                    return GraphicsDriver::NVIDIA;
                } else if (vendor_id == "0x1002") {  // AMD
                    BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected AMD via DRM vendor ID";
                    return GraphicsDriver::AMD;
                } else if (vendor_id == "0x8086") {  // Intel
                    BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected Intel via DRM vendor ID";
                    return GraphicsDriver::Intel;
                }
            }
        }
    }
    
    // Check for Mesa drivers in common locations
    std::string mesa_check = execute_command("find /usr/lib* -name 'libGL.so*' -exec readlink -f {} \\; 2>/dev/null | grep -i mesa | head -1");
    if (!mesa_check.empty()) {
        BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected Mesa via library search";
        return GraphicsDriver::Mesa;
    }
    
    // If we're in a container and couldn't detect anything specific, check environment
    if (in_container) {
        // Check for common Flatpak environment variables that might hint at graphics setup
        const char* flatpak_gl_drivers = std::getenv("FLATPAK_GL_DRIVERS");
        if (flatpak_gl_drivers) {
            std::string drivers(flatpak_gl_drivers);
            BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: FLATPAK_GL_DRIVERS=" << drivers;
            if (boost::icontains(drivers, "nvidia")) {
                BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Detected NVIDIA via FLATPAK_GL_DRIVERS";
                return GraphicsDriver::NVIDIA;
            }
        }
        
        // In containers, we often rely on the host system's graphics
        const char* display = std::getenv("DISPLAY");
        const char* wayland_display = std::getenv("WAYLAND_DISPLAY");
        
        if (display || wayland_display) {
            BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Container with display forwarding, will try additional detection";
            // Don't immediately default to Mesa - let other detection methods run
        }
    }
    
    return GraphicsDriver::Unknown;
#else
    return GraphicsDriver::Unknown;
#endif
}

bool GraphicsBackendManager::is_running_in_container()
{
#ifdef __linux__
    // Check for Flatpak
    const char* flatpak_id = std::getenv("FLATPAK_ID");
    if (flatpak_id) {
        BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Running in Flatpak: " << flatpak_id;
        
        // Log additional Flatpak environment information for debugging
        const char* flatpak_sandbox = std::getenv("FLATPAK_SANDBOX_DIR");
        if (flatpak_sandbox) {
            BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: Flatpak sandbox dir: " << flatpak_sandbox;
        }
        
        const char* xdg_runtime_dir = std::getenv("XDG_RUNTIME_DIR");
        if (xdg_runtime_dir) {
            BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: XDG_RUNTIME_DIR: " << xdg_runtime_dir;
        }
        
        return true;
    }
    
    // Check for AppImage
    const char* appimage = std::getenv("APPIMAGE");
    const char* orcaslicer_appimage = std::getenv("ORCASLICER_APPIMAGE");
    if (appimage || orcaslicer_appimage) {
        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: Running in AppImage: " 
                                << (appimage ? appimage : "detected via ORCASLICER_APPIMAGE");
        return true;
    }
    
    // Check for Docker/other containers via cgroup
    std::string cgroup = read_file_content("/proc/1/cgroup");
    if (!cgroup.empty() && (boost::icontains(cgroup, "docker") || 
                           boost::icontains(cgroup, "lxc") || 
                           boost::icontains(cgroup, "containerd"))) {
        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: Running in container (detected via cgroup)";
        return true;
    }
    
    // Check for container via /.dockerenv or other indicators
    std::ifstream dockerenv("/.dockerenv");
    if (dockerenv.good()) {
        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: Running in Docker container";
        return true;
    }
    
    return false;
#else
    return false;
#endif
}

std::string GraphicsBackendManager::read_file_content(const std::string& filepath)
{
#ifdef __linux__
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return "";
    }
    
    std::stringstream buffer;
    try {
        buffer << file.rdbuf();
        return buffer.str();
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: Error reading " << filepath << ": " << e.what();
        return "";
    }
#else
    return "";
#endif
}

std::string GraphicsBackendManager::get_nvidia_driver_version()
{
#ifdef __linux__
    return execute_command("nvidia-smi --query-gpu=driver_version --format=csv,noheader 2>/dev/null | head -n1");
#else
    return "";
#endif
}

bool GraphicsBackendManager::is_nvidia_driver_newer_than(int major_version)
{
    std::string version = get_nvidia_driver_version();
    if (version.empty()) {
        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: No NVIDIA driver version detected";
        return false;
    }
    
    BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: NVIDIA driver version: " << version;
    
    try {
        size_t dot_pos = version.find('.');
        if (dot_pos != std::string::npos) {
            std::string major_str = version.substr(0, dot_pos);
            int driver_major = std::stoi(major_str);
            bool is_newer = driver_major > major_version;
            BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: Driver major version: " << driver_major 
                                    << ", threshold: " << major_version << ", is newer: " << is_newer;
            return is_newer;
        } else {
            BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Invalid NVIDIA driver version format: " << version;
            return false;
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Failed to parse NVIDIA driver version '" 
                                   << version << "': " << e.what();
        return false;
    } catch (...) {
        BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Unknown error parsing NVIDIA driver version: " << version;
        return false;
    }
}

/**
 * Helper function to query OpenGL strings and check for errors.
 * 
 * @param context_type String describing the context type for logging (e.g., "GLX", "EGL")
 * @return String containing OpenGL vendor, renderer, and version information,
 *         or empty string if detection fails or errors occur
 */
std::string GraphicsBackendManager::query_opengl_strings(const std::string& context_type)
{
#ifdef __linux__
    // Query OpenGL information
    const GLubyte* vendor = glGetString(GL_VENDOR);
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: OpenGL error after glGetString(GL_VENDOR): " << error;
        return "";
    }
    
    const GLubyte* renderer = glGetString(GL_RENDERER);
    error = glGetError();
    if (error != GL_NO_ERROR) {
        BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: OpenGL error after glGetString(GL_RENDERER): " << error;
        return "";
    }
    
    const GLubyte* version = glGetString(GL_VERSION);
    error = glGetError();
    if (error != GL_NO_ERROR) {
        BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: OpenGL error after glGetString(GL_VERSION): " << error;
        return "";
    }
    
    if (vendor && renderer && version) {
        std::string result = "OpenGL vendor string: " + std::string(reinterpret_cast<const char*>(vendor)) + "\n";
        result += "OpenGL renderer string: " + std::string(reinterpret_cast<const char*>(renderer)) + "\n";
        result += "OpenGL version string: " + std::string(reinterpret_cast<const char*>(version));
        
        BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Successfully retrieved OpenGL info via " << context_type;
        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: Vendor: " << reinterpret_cast<const char*>(vendor);
        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: Renderer: " << reinterpret_cast<const char*>(renderer);
        return result;
    }
    
    BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: glGetString returned null pointers via " << context_type;
    return "";
#else
    return "";
#endif
}

/**
 * Direct OpenGL graphics detection using native API calls.
 * 
 * This function creates minimal OpenGL contexts to query graphics information directly
 * via OpenGL API calls, eliminating dependency on external tools like glxinfo/eglinfo.
 * 
 * Detection process:
 * 1. Try GLX context creation (for X11 environments)
 * 2. Try EGL context creation (for Wayland/headless environments)
 * 3. Query glGetString(GL_VENDOR/GL_RENDERER/GL_VERSION)
 * 4. Clean up all created resources
 * 
 * Benefits:
 * - Works reliably in containers (Docker, Flatpak, AppImage)
 * - No external command dependencies
 * - Better error handling and logging
 * - Supports both X11 and Wayland
 * 
 * @return String containing OpenGL vendor, renderer, and version information,
 *         or empty string if detection fails
 */
std::string GraphicsBackendManager::get_opengl_info_direct()
{
#ifdef __linux__
    // Thread safety: X11 and EGL operations are not thread-safe
    std::lock_guard<std::mutex> lock(opengl_detection_mutex_);
    
    BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: Attempting direct OpenGL detection";
    
    std::string result;
    Display* display = nullptr;
    GLXContext context = nullptr;
    Window window = 0;
    Colormap colormap = 0;
    XVisualInfo* visual = nullptr;
    
    try {
        // Try X11 first
        display = XOpenDisplay(nullptr);
        if (display) {
            BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: X11 display opened successfully";
            
            // Get a visual
            int screen = DefaultScreen(display);
            int attribs[] = {
                GLX_RGBA,
                GLX_RED_SIZE, 1,
                GLX_GREEN_SIZE, 1,
                GLX_BLUE_SIZE, 1,
                GLX_DEPTH_SIZE, 12,
                GLX_DOUBLEBUFFER,
                None
            };
            
            visual = glXChooseVisual(display, screen, attribs);
            if (visual) {
                BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: GLX visual chosen successfully";
                
                // Create a minimal window
                Window root = RootWindow(display, screen);
                colormap = XCreateColormap(display, root, visual->visual, AllocNone);
                
                XSetWindowAttributes attrs;
                attrs.colormap = colormap;
                attrs.event_mask = 0;
                
                window = XCreateWindow(display, root, 0, 0, 1, 1, 0,
                                     visual->depth, InputOutput, visual->visual,
                                     CWColormap | CWEventMask, &attrs);
                
                if (window) {
                    BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: X11 window created successfully";
                    
                    // Create OpenGL context
                    context = glXCreateContext(display, visual, nullptr, GL_TRUE);
                    if (context && glXMakeCurrent(display, window, context)) {
                        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: OpenGL context created and made current";
                        
                        // Query OpenGL information using helper function
                        result = query_opengl_strings("GLX");
                        
                        glXMakeCurrent(display, None, nullptr);
                    } else {
                        BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Failed to create or make current OpenGL context";
                    }
                } else {
                    BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Failed to create X11 window";
                }
            } else {
                BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Failed to choose GLX visual";
            }
        } else {
            BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: X11 display not available, trying EGL";
            
            // Try EGL as fallback (for Wayland or headless)
            // In Flatpak on Wayland, we should try to get the Wayland display
            EGLDisplay egl_display = EGL_NO_DISPLAY;
            
            // First try to get Wayland display if we're on Wayland
            const char* wayland_display_env = std::getenv("WAYLAND_DISPLAY");
            if (wayland_display_env) {
                BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: Attempting EGL with Wayland display: " << wayland_display_env;
                // Try to get platform display for Wayland (requires EGL 1.5 or EGL_EXT_platform_wayland)
                typedef EGLDisplay (*PFNEGLGETPLATFORMDISPLAYPROC)(EGLenum platform, void *native_display, const EGLint *attrib_list);
                PFNEGLGETPLATFORMDISPLAYPROC eglGetPlatformDisplay = 
                    (PFNEGLGETPLATFORMDISPLAYPROC) eglGetProcAddress("eglGetPlatformDisplay");
                
                if (eglGetPlatformDisplay) {
                    // EGL_PLATFORM_WAYLAND_KHR = 0x31D8
                    egl_display = eglGetPlatformDisplay(0x31D8, EGL_DEFAULT_DISPLAY, nullptr);
                    if (egl_display != EGL_NO_DISPLAY) {
                        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: Got EGL display via Wayland platform";
                    }
                }
            }
            
            // Fallback to default display if Wayland-specific didn't work
            if (egl_display == EGL_NO_DISPLAY) {
                egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
            }
            
            if (egl_display != EGL_NO_DISPLAY) {
                EGLint major, minor;
                if (eglInitialize(egl_display, &major, &minor)) {
                    BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: EGL initialized successfully";
                    
                    EGLint config_attribs[] = {
                        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                        EGL_BLUE_SIZE, 8,
                        EGL_GREEN_SIZE, 8,
                        EGL_RED_SIZE, 8,
                        EGL_DEPTH_SIZE, 8,
                        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                        EGL_NONE
                    };
                    
                    EGLConfig config;
                    EGLint num_configs;
                    if (eglChooseConfig(egl_display, config_attribs, &config, 1, &num_configs) && num_configs > 0) {
                        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: EGL config chosen successfully";
                        
                        // Create a pbuffer surface
                        EGLint pbuffer_attribs[] = {
                            EGL_WIDTH, 1,
                            EGL_HEIGHT, 1,
                            EGL_NONE
                        };
                        
                        EGLSurface surface = eglCreatePbufferSurface(egl_display, config, pbuffer_attribs);
                        if (surface != EGL_NO_SURFACE) {
                            BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: EGL pbuffer surface created successfully";
                            
                            eglBindAPI(EGL_OPENGL_API);
                            EGLContext egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, nullptr);
                            
                            if (egl_context != EGL_NO_CONTEXT && eglMakeCurrent(egl_display, surface, surface, egl_context)) {
                                BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: EGL context created and made current";
                                
                                // Query OpenGL information using helper function
                                result = query_opengl_strings("EGL");
                                
                                eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                                eglDestroyContext(egl_display, egl_context);
                            } else {
                                BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Failed to create or make current EGL context";
                            }
                            
                            eglDestroySurface(egl_display, surface);
                        } else {
                            BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Failed to create EGL pbuffer surface";
                        }
                    } else {
                        BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Failed to choose EGL config";
                    }
                    
                    eglTerminate(egl_display);
                } else {
                    BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Failed to initialize EGL";
                }
            } else {
                BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: EGL display not available";
            }
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "GraphicsBackendManager: Exception during direct OpenGL detection: " << e.what();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "GraphicsBackendManager: Unknown exception during direct OpenGL detection";
    }
    
    // Cleanup X11 resources (fixing resource leaks)
    if (context) {
        glXDestroyContext(display, context);
    }
    if (window && display) {
        XDestroyWindow(display, window);
    }
    if (colormap && display) {
        XFreeColormap(display, colormap);
    }
    if (visual) {
        XFree(visual);
    }
    if (display) {
        XCloseDisplay(display);
    }
    
    if (result.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Direct OpenGL detection failed, falling back to command-line tools";
    }
    
    return result;
#else
    BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: Direct OpenGL detection not available on this platform";
    return "";
#endif
}

std::string GraphicsBackendManager::get_glx_info()
{
    // Primary method: Use direct OpenGL API calls via GLX/EGL contexts
    // This avoids dependency on external glxinfo command and works better in containers
    std::string direct_info = get_opengl_info_direct();
    if (!direct_info.empty()) {
        return direct_info;
    }
    
    // Final fallback: Use glxinfo command if direct detection failed
    // In Flatpak, this might actually work if the runtime has the tools
    std::string glxinfo_result = execute_command("glxinfo 2>/dev/null | grep -E 'OpenGL vendor|OpenGL renderer' | head -10");
    if (!glxinfo_result.empty()) {
        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: glxinfo fallback succeeded in container/host environment";
    }
    return glxinfo_result;
}

std::string GraphicsBackendManager::get_egl_info()
{
    // Primary method: Use direct OpenGL API calls via EGL contexts
    // This avoids dependency on external eglinfo command and works better in containers
    std::string direct_info = get_opengl_info_direct();
    if (!direct_info.empty()) {
        return direct_info;
    }
    
    // Final fallback: Use eglinfo command if direct detection failed
    // In Flatpak, this might actually work if the runtime has the tools
    std::string eglinfo_result = execute_command("eglinfo 2>/dev/null | grep -E 'EGL vendor|EGL renderer' | head -10");
    if (!eglinfo_result.empty()) {
        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: eglinfo fallback succeeded in container/host environment";
    }
    return eglinfo_result;
}

void GraphicsBackendManager::set_environment_variable(const std::string& name, const std::string& value)
{
#ifdef __linux__
    if (!value.empty()) {
        setenv(name.c_str(), value.c_str(), 1);
        BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Set " << name << "=" << value;
    }
#endif
}

void GraphicsBackendManager::unset_environment_variable(const std::string& name)
{
#ifdef __linux__
    unsetenv(name.c_str());
    BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Unset " << name;
#endif
}

GraphicsBackendManager::GraphicsConfig GraphicsBackendManager::get_nvidia_wayland_config()
{
    GraphicsConfig config;
    config.session_type = SessionType::Wayland;
    config.driver = GraphicsDriver::NVIDIA;
    
    // Check if we're running in Flatpak
    bool in_flatpak = (std::getenv("FLATPAK_ID") != nullptr);
    
    // For NVIDIA on Wayland, we need specific configuration
    if (is_nvidia_driver_newer_than(555)) {
        // Newer NVIDIA drivers (555+) work better with Zink
        config.use_zink = true;
        config.gbm_backend = "dri";
        config.mesa_loader_driver = "zink";
        config.gallium_driver = "zink";
        config.glx_vendor_library = "mesa";
        
        // In Flatpak, the EGL vendor library path might be different
        if (in_flatpak) {
            // Try multiple possible paths for Flatpak
            std::string egl_paths[] = {
                "/usr/share/glvnd/egl_vendor.d/50_mesa.json",
                "/run/host/usr/share/glvnd/egl_vendor.d/50_mesa.json",
                "/usr/lib/x86_64-linux-gnu/GL/egl_vendor.d/50_mesa.json"
            };
            
            for (const auto& path : egl_paths) {
                std::ifstream test_file(path);
                if (test_file.good()) {
                    config.egl_vendor_library = path;
                    BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: Found EGL vendor file at " << path;
                    break;
                }
            }
            
            if (config.egl_vendor_library.empty()) {
                // Use default if not found
                config.egl_vendor_library = "/usr/share/glvnd/egl_vendor.d/50_mesa.json";
            }
        } else {
            config.egl_vendor_library = "/usr/share/glvnd/egl_vendor.d/50_mesa.json";
        }
        
        config.disable_dmabuf = true;
    } else {
        // Older NVIDIA drivers need different approach
        config.gbm_backend = "dri";
        config.force_dri_backend = true;
        
        // In Flatpak with older drivers, we might need additional settings
        if (in_flatpak) {
            // Force software rendering fallback if needed
            config.glx_vendor_library = "mesa";
            BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Using mesa GLX vendor for Flatpak with older NVIDIA drivers";
        }
    }
    
    return config;
}

GraphicsBackendManager::GraphicsConfig GraphicsBackendManager::get_nvidia_x11_config()
{
    GraphicsConfig config;
    config.session_type = SessionType::X11;
    config.driver = GraphicsDriver::NVIDIA;
    
    // For NVIDIA on X11, we can use native drivers
    config.gbm_backend = "dri";
    config.force_dri_backend = true;
    
    return config;
}

GraphicsBackendManager::GraphicsConfig GraphicsBackendManager::get_amd_config()
{
    GraphicsConfig config;
    config.driver = GraphicsDriver::AMD;
    
    // AMD drivers generally work well with Mesa
    config.gbm_backend = "dri";
    config.mesa_loader_driver = "radeonsi";
    config.gallium_driver = "radeonsi";
    
    return config;
}

GraphicsBackendManager::GraphicsConfig GraphicsBackendManager::get_intel_config()
{
    GraphicsConfig config;
    config.driver = GraphicsDriver::Intel;
    
    // Intel drivers work well with Mesa
    config.gbm_backend = "dri";
    config.mesa_loader_driver = "i965";
    config.gallium_driver = "i965";
    
    return config;
}

GraphicsBackendManager::GraphicsConfig GraphicsBackendManager::get_mesa_config()
{
    GraphicsConfig config;
    config.driver = GraphicsDriver::Mesa;
    
    // Mesa software rendering
    config.gbm_backend = "dri";
    config.mesa_loader_driver = "swrast";
    config.gallium_driver = "swrast";
    
    return config;
}

GraphicsBackendManager::GraphicsConfig GraphicsBackendManager::get_recommended_config()
{
    GraphicsConfig detected = detect_graphics_environment();
    GraphicsConfig config;
    
    switch (detected.driver) {
        case GraphicsDriver::NVIDIA:
            if (detected.session_type == SessionType::Wayland) {
                config = get_nvidia_wayland_config();
            } else {
                config = get_nvidia_x11_config();
            }
            break;
        case GraphicsDriver::AMD:
            config = get_amd_config();
            break;
        case GraphicsDriver::Intel:
            config = get_intel_config();
            break;
        case GraphicsDriver::Mesa:
            config = get_mesa_config();
            break;
        default:
            // Fallback to basic configuration
            config.gbm_backend = "dri";
            config.force_dri_backend = true;
            break;
    }
    
    // Special handling for Flatpak environments
    if (std::getenv("FLATPAK_ID")) {
        // Ensure GBM_BACKEND is always set to "dri" in Flatpak
        if (config.gbm_backend.empty() || config.gbm_backend != "dri") {
            BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Forcing GBM_BACKEND=dri for Flatpak environment";
            config.gbm_backend = "dri";
        }
        
        // In Flatpak with Wayland, we might need to disable dmabuf
        if (detected.session_type == SessionType::Wayland) {
            config.disable_dmabuf = true;
            BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Disabling DMABUF for Flatpak Wayland session";
        }
        
        // For unknown drivers in Flatpak, use safe defaults
        if (detected.driver == GraphicsDriver::Unknown) {
            BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Unknown driver in Flatpak, using safe Mesa defaults";
            config.driver = GraphicsDriver::Mesa;
            config.gbm_backend = "dri";
            config.force_dri_backend = true;
            config.disable_dmabuf = true;
        }
    }
    
    // Copy detected session type to config
    config.session_type = detected.session_type;
    
    return config;
}

void GraphicsBackendManager::apply_graphics_config(const GraphicsConfig& config)
{
    BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Applying graphics configuration...";
    
    // Log if we're in Flatpak for debugging
    if (std::getenv("FLATPAK_ID")) {
        BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Applying configuration in Flatpak environment";
    }
    
    // Validate configuration before applying
    if (!validate_configuration(config)) {
        BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Configuration validation failed, but continuing with application";
    }
    
    if (config.session_type == SessionType::Unknown) {
        BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Unknown session type, using fallback configuration";
    }
    
    if (config.driver == GraphicsDriver::Unknown) {
        BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Unknown graphics driver, using fallback configuration";
    }
    
    // Apply environment variables based on configuration
    if (!config.gbm_backend.empty()) {
        set_environment_variable("GBM_BACKEND", config.gbm_backend);
    } else {
        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: No GBM_BACKEND specified in configuration";
    }
    
    if (!config.mesa_loader_driver.empty()) {
        set_environment_variable("MESA_LOADER_DRIVER_OVERRIDE", config.mesa_loader_driver);
    } else {
        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: No MESA_LOADER_DRIVER_OVERRIDE specified in configuration";
    }
    
    if (!config.gallium_driver.empty()) {
        set_environment_variable("GALLIUM_DRIVER", config.gallium_driver);
    } else {
        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: No GALLIUM_DRIVER specified in configuration";
    }
    
    if (!config.glx_vendor_library.empty()) {
        set_environment_variable("__GLX_VENDOR_LIBRARY_NAME", config.glx_vendor_library);
    } else {
        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: No __GLX_VENDOR_LIBRARY_NAME specified in configuration";
    }
    
    if (!config.egl_vendor_library.empty()) {
        set_environment_variable("__EGL_VENDOR_LIBRARY_FILENAMES", config.egl_vendor_library);
    } else {
        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: No __EGL_VENDOR_LIBRARY_FILENAMES specified in configuration";
    }
    
    if (config.disable_dmabuf) {
        set_environment_variable("WEBKIT_DISABLE_DMABUF_RENDERER", "1");
        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: Disabled DMABUF renderer";
    }
    
    // Force DRI backend if needed
    if (config.force_dri_backend) {
        set_environment_variable("LIBGL_ALWAYS_SOFTWARE", "0");
        set_environment_variable("MESA_GL_VERSION_OVERRIDE", "3.3");
        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: Forced DRI backend configuration";
    }
    
    // Log the final configuration summary
    BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Configuration applied successfully";
    BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Session: " 
                            << (config.session_type == SessionType::Wayland ? "Wayland" : 
                                config.session_type == SessionType::X11 ? "X11" : "Unknown");
    BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Driver: " 
                            << (config.driver == GraphicsDriver::NVIDIA ? "NVIDIA" :
                                config.driver == GraphicsDriver::AMD ? "AMD" :
                                config.driver == GraphicsDriver::Intel ? "Intel" :
                                config.driver == GraphicsDriver::Mesa ? "Mesa" : "Unknown");
    BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Use Zink: " << (config.use_zink ? "Yes" : "No");
    BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Force DRI: " << (config.force_dri_backend ? "Yes" : "No");
    BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Disable DMABUF: " << (config.disable_dmabuf ? "Yes" : "No");
}

bool GraphicsBackendManager::is_configuration_optimal()
{
    // Check if current environment variables are set optimally
    const char* gbm_backend = std::getenv("GBM_BACKEND");
    if (!gbm_backend || std::string(gbm_backend) != "dri") {
        return false;
    }
    
    return true;
}

bool GraphicsBackendManager::validate_configuration(const GraphicsConfig& config)
{
    bool valid = true;
    
    // Check for required fields based on driver type
    switch (config.driver) {
        case GraphicsDriver::NVIDIA:
            if (config.session_type == SessionType::Wayland) {
                if (config.gbm_backend.empty()) {
                    BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: NVIDIA Wayland config missing GBM_BACKEND";
                    valid = false;
                }
            }
            break;
            
        case GraphicsDriver::AMD:
            if (config.mesa_loader_driver.empty()) {
                BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: AMD config missing MESA_LOADER_DRIVER_OVERRIDE";
                valid = false;
            }
            break;
            
        case GraphicsDriver::Intel:
            if (config.mesa_loader_driver.empty()) {
                BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Intel config missing MESA_LOADER_DRIVER_OVERRIDE";
                valid = false;
            }
            break;
            
        case GraphicsDriver::Mesa:
            // Mesa config is usually minimal, no specific requirements
            break;
            
        case GraphicsDriver::Unknown:
            BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Unknown driver type in configuration";
            valid = false;
            break;
    }
    
    // Check for conflicting settings
    if (config.use_zink && config.force_dri_backend) {
        BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Conflicting settings: use_zink and force_dri_backend both true";
        valid = false;
    }
    
    if (valid) {
        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: Configuration validation passed";
    } else {
        BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Configuration validation failed";
    }
    
    return valid;
}

void GraphicsBackendManager::log_graphics_info()
{
    BOOST_LOG_TRIVIAL(info) << "GraphicsBackendManager: Current graphics information:";
    
    try {
        GraphicsConfig detected = detect_graphics_environment();
        BOOST_LOG_TRIVIAL(info) << "  Session Type: " 
                                << (detected.session_type == SessionType::Wayland ? "Wayland" : 
                                    detected.session_type == SessionType::X11 ? "X11" : "Unknown");
        BOOST_LOG_TRIVIAL(info) << "  Graphics Driver: " 
                                << (detected.driver == GraphicsDriver::NVIDIA ? "NVIDIA" :
                                    detected.driver == GraphicsDriver::AMD ? "AMD" :
                                    detected.driver == GraphicsDriver::Intel ? "Intel" :
                                    detected.driver == GraphicsDriver::Mesa ? "Mesa" : "Unknown");
        
        if (detected.driver == GraphicsDriver::NVIDIA) {
            std::string nvidia_version = get_nvidia_driver_version();
            if (!nvidia_version.empty()) {
                BOOST_LOG_TRIVIAL(info) << "  NVIDIA Driver Version: " << nvidia_version;
            }
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Error during graphics info logging: " << e.what();
    }
    
    // Log current environment variables (these are always safe)
    const char* gbm_backend = std::getenv("GBM_BACKEND");
    if (gbm_backend) {
        BOOST_LOG_TRIVIAL(info) << "  GBM_BACKEND: " << gbm_backend;
    }
    
    const char* mesa_loader = std::getenv("MESA_LOADER_DRIVER_OVERRIDE");
    if (mesa_loader) {
        BOOST_LOG_TRIVIAL(info) << "  MESA_LOADER_DRIVER_OVERRIDE: " << mesa_loader;
    }
    
    const char* gallium_driver = std::getenv("GALLIUM_DRIVER");
    if (gallium_driver) {
        BOOST_LOG_TRIVIAL(info) << "  GALLIUM_DRIVER: " << gallium_driver;
    }
    
    // Log container detection
    if (is_running_in_container()) {
        BOOST_LOG_TRIVIAL(info) << "  Container Environment: Detected";
    }
}

// Helper function to execute shell commands
std::string GraphicsBackendManager::execute_command(const std::string& command)
{
#ifdef __linux__
    std::string result;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Failed to execute command: " << command;
        return "";
    }
    
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    
    int status = pclose(pipe);
    if (status != 0) {
        BOOST_LOG_TRIVIAL(warning) << "GraphicsBackendManager: Command failed with status " << status << ": " << command;
        // Don't return empty string for failed commands, as some commands may fail but still provide useful output
    }
    
    // Remove trailing newline
    if (!result.empty() && result[result.length()-1] == '\n') {
        result.erase(result.length()-1);
    }
    
    if (result.empty()) {
        BOOST_LOG_TRIVIAL(debug) << "GraphicsBackendManager: Command returned no output: " << command;
    }
    
    return result;
#else
    return "";
#endif
}

} // namespace GUI
} // namespace Slic3r 