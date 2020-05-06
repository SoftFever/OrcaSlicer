#ifndef slic3r_GLCanvas3DManager_hpp_
#define slic3r_GLCanvas3DManager_hpp_

#include "libslic3r/BoundingBox.hpp"

#include <map>
#include <vector>

class wxWindow;
class wxGLCanvas;
class wxGLContext;

namespace Slic3r {

class BackgroundSlicingProcess;
class DynamicPrintConfig;
class Model;
class ExPolygon;
typedef std::vector<ExPolygon> ExPolygons;
class ModelObject;
class PrintObject;

namespace GUI {

class GLCanvas3D;

class GLCanvas3DManager
{
public:
    enum class EFramebufferType : unsigned char
    {
        Unknown,
        Arb,
        Ext
    };

    class GLInfo
    {
        mutable bool m_detected{ false };
        mutable int m_max_tex_size{ 0 };
        mutable float m_max_anisotropy{ 0.0f };

        mutable std::string m_version;
        mutable std::string m_glsl_version;
        mutable std::string m_vendor;
        mutable std::string m_renderer;

    public:
        GLInfo() = default;

        const std::string& get_version() const;
        const std::string& get_glsl_version() const;
        const std::string& get_vendor() const;
        const std::string& get_renderer() const;

        int get_max_tex_size() const;
        float get_max_anisotropy() const;

        bool is_version_greater_or_equal_to(unsigned int major, unsigned int minor) const;

        std::string to_string(bool format_as_html, bool extensions) const;

    private:
        void detect() const;
    };

#if ENABLE_HACK_CLOSING_ON_OSX_10_9_5
#ifdef __APPLE__ 
    // Part of hack to remove crash when closing the application on OSX 10.9.5 when building against newer wxWidgets
    struct OSInfo
    {
        int major{ 0 };
        int minor{ 0 };
        int micro{ 0 };
    };
#endif //__APPLE__
#endif // ENABLE_HACK_CLOSING_ON_OSX_10_9_5

private:
    enum class EMultisampleState : unsigned char
    {
        Unknown,
        Enabled,
        Disabled
    };

    bool m_gl_initialized{ false };
    wxGLContext* m_context{ nullptr };
    static GLInfo s_gl_info;
#if ENABLE_HACK_CLOSING_ON_OSX_10_9_5
#ifdef __APPLE__ 
    // Part of hack to remove crash when closing the application on OSX 10.9.5 when building against newer wxWidgets
    static OSInfo s_os_info;
#endif //__APPLE__
#endif // ENABLE_HACK_CLOSING_ON_OSX_10_9_5
    static bool s_compressed_textures_supported;
    static EMultisampleState s_multisample;
    static EFramebufferType s_framebuffers_type;

public:
    GLCanvas3DManager() = default;
    ~GLCanvas3DManager();

    bool init_gl();

    wxGLContext* init_glcontext(wxGLCanvas& canvas);

    static bool are_compressed_textures_supported() { return s_compressed_textures_supported; }
    static bool can_multisample() { return s_multisample == EMultisampleState::Enabled; }
    static bool are_framebuffers_supported() { return (s_framebuffers_type != EFramebufferType::Unknown); }
    static EFramebufferType get_framebuffers_type() { return s_framebuffers_type; }
    static wxGLCanvas* create_wxglcanvas(wxWindow& parent);
    static const GLInfo& get_gl_info() { return s_gl_info; }

private:
    static void detect_multisample(int* attribList);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLCanvas3DManager_hpp_
