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
#if !ENABLE_NON_STATIC_CANVAS_MANAGER
class Bed3D;
class GLToolbar;
struct Camera;
#endif // !ENABLE_NON_STATIC_CANVAS_MANAGER

class GLCanvas3DManager
{
public:
#if ENABLE_NON_STATIC_CANVAS_MANAGER
    enum class EFramebufferType : unsigned char
    {
        None,
        Arb,
        Ext
    };
#else
    enum EFramebufferType : unsigned char
    {
        FB_None,
        FB_Arb,
        FB_Ext
    };
#endif // ENABLE_NON_STATIC_CANVAS_MANAGER

    class GLInfo
    {
#if ENABLE_NON_STATIC_CANVAS_MANAGER
        mutable bool m_detected{ false };
        mutable int m_max_tex_size{ 0 };
        mutable float m_max_anisotropy{ 0.0f };

        mutable std::string m_version;
        mutable std::string m_glsl_version;
        mutable std::string m_vendor;
        mutable std::string m_renderer;
#else
        mutable bool m_detected;

        mutable std::string m_version;
        mutable std::string m_glsl_version;
        mutable std::string m_vendor;
        mutable std::string m_renderer;

        mutable int m_max_tex_size;
        mutable float m_max_anisotropy;
#endif // ENABLE_NON_STATIC_CANVAS_MANAGER

    public:
#if ENABLE_NON_STATIC_CANVAS_MANAGER
        GLInfo() = default;
#else
        GLInfo();
#endif // ENABLE_NON_STATIC_CANVAS_MANAGER

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
    struct OSInfo
    {
        int major{ 0 };
        int minor{ 0 };
        int micro{ 0 };
    };
#endif //__APPLE__
#endif // ENABLE_HACK_CLOSING_ON_OSX_10_9_5

private:
#if ENABLE_NON_STATIC_CANVAS_MANAGER
    enum class EMultisampleState : unsigned char
    {
        Unknown,
        Enabled,
        Disabled
    };
#else
    enum EMultisampleState : unsigned char
    {
        MS_Unknown,
        MS_Enabled,
        MS_Disabled
    };

    typedef std::map<wxGLCanvas*, GLCanvas3D*> CanvasesMap;
#endif // ENABLE_NON_STATIC_CANVAS_MANAGER

#if ENABLE_NON_STATIC_CANVAS_MANAGER
    bool m_gl_initialized{ false };
    wxGLContext* m_context{ nullptr };
#else
    wxGLContext* m_context;
<<<<<<< HEAD
=======
    static GLInfo s_gl_info;
#if ENABLE_HACK_CLOSING_ON_OSX_10_9_5
#ifdef __APPLE__ 
    static OSInfo s_os_info;
#endif //__APPLE__
#endif // ENABLE_HACK_CLOSING_ON_OSX_10_9_5
>>>>>>> 11bd62a3e6887741701eb908e0fd0cc0b6afb576
    bool m_gl_initialized;
    CanvasesMap m_canvases;
#endif // ENABLE_NON_STATIC_CANVAS_MANAGER
    static GLInfo s_gl_info;
    static bool s_compressed_textures_supported;
    static EMultisampleState s_multisample;
    static EFramebufferType s_framebuffers_type;

public:
#if ENABLE_NON_STATIC_CANVAS_MANAGER
    GLCanvas3DManager() = default;
#else
    GLCanvas3DManager();
#endif // ENABLE_NON_STATIC_CANVAS_MANAGER
    ~GLCanvas3DManager();

#if !ENABLE_NON_STATIC_CANVAS_MANAGER
    bool add(wxGLCanvas* canvas, Bed3D& bed, Camera& camera, GLToolbar& view_toolbar);
    bool remove(wxGLCanvas* canvas);
    void remove_all();

    size_t count() const;
#endif // !ENABLE_NON_STATIC_CANVAS_MANAGER

#if ENABLE_NON_STATIC_CANVAS_MANAGER
    bool init_gl();
#else
    void init_gl();
#endif // ENABLE_NON_STATIC_CANVAS_MANAGER

#if ENABLE_NON_STATIC_CANVAS_MANAGER
    wxGLContext* init_glcontext(wxGLCanvas& canvas);
#else
    bool init(wxGLCanvas* canvas);
    void destroy();
#endif // ENABLE_NON_STATIC_CANVAS_MANAGER

#if !ENABLE_NON_STATIC_CANVAS_MANAGER
    GLCanvas3D* get_canvas(wxGLCanvas* canvas);
#endif // !ENABLE_NON_STATIC_CANVAS_MANAGER

    static bool are_compressed_textures_supported() { return s_compressed_textures_supported; }
#if ENABLE_NON_STATIC_CANVAS_MANAGER
    static bool can_multisample() { return s_multisample == EMultisampleState::Enabled; }
    static bool are_framebuffers_supported() { return (s_framebuffers_type != EFramebufferType::None); }
#else
    static bool can_multisample() { return s_multisample == MS_Enabled; }
    static bool are_framebuffers_supported() { return (s_framebuffers_type != FB_None); }
#endif // ENABLE_NON_STATIC_CANVAS_MANAGER
    static EFramebufferType get_framebuffers_type() { return s_framebuffers_type; }

#if ENABLE_NON_STATIC_CANVAS_MANAGER
    static wxGLCanvas* create_wxglcanvas(wxWindow& parent);
#else
    static wxGLCanvas* create_wxglcanvas(wxWindow *parent);
#endif // ENABLE_NON_STATIC_CANVAS_MANAGER

    static const GLInfo& get_gl_info() { return s_gl_info; }

private:
#if !ENABLE_NON_STATIC_CANVAS_MANAGER
    CanvasesMap::iterator do_get_canvas(wxGLCanvas* canvas);
    CanvasesMap::const_iterator do_get_canvas(wxGLCanvas* canvas) const;

    bool init(GLCanvas3D& canvas);
#endif // ENABLE_NON_STATIC_CANVAS_MANAGER
    static void detect_multisample(int* attribList);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLCanvas3DManager_hpp_
