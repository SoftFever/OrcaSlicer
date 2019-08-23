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
class Bed3D;
class GLToolbar;
struct Camera;

class GLCanvas3DManager
{
public:
    class GLInfo
    {
        mutable bool m_detected;

        mutable std::string m_version;
        mutable std::string m_glsl_version;
        mutable std::string m_vendor;
        mutable std::string m_renderer;

        mutable int m_max_tex_size;
        mutable float m_max_anisotropy;

    public:
        GLInfo();

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

private:
    enum EMultisampleState : unsigned char
    {
        MS_Unknown,
        MS_Enabled,
        MS_Disabled
    };

    typedef std::map<wxGLCanvas*, GLCanvas3D*> CanvasesMap;

    CanvasesMap m_canvases;
    wxGLContext* m_context;
    static GLInfo s_gl_info;
    bool m_gl_initialized;
    static EMultisampleState s_multisample;
    static bool s_compressed_textures_supported;

public:
    GLCanvas3DManager();
    ~GLCanvas3DManager();

    bool add(wxGLCanvas* canvas, Bed3D& bed, Camera& camera, GLToolbar& view_toolbar);
    bool remove(wxGLCanvas* canvas);
    void remove_all();

    unsigned int count() const;

    void init_gl();

    bool init(wxGLCanvas* canvas);
    void destroy();

    GLCanvas3D* get_canvas(wxGLCanvas* canvas);

    static bool can_multisample() { return s_multisample == MS_Enabled; }
    static bool are_compressed_textures_supported() { return s_compressed_textures_supported; }

    static wxGLCanvas* create_wxglcanvas(wxWindow *parent);

    static const GLInfo& get_gl_info() { return s_gl_info; }

private:
    CanvasesMap::iterator do_get_canvas(wxGLCanvas* canvas);
    CanvasesMap::const_iterator do_get_canvas(wxGLCanvas* canvas) const;

    bool init(GLCanvas3D& canvas);
    static void detect_multisample(int* attribList);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLCanvas3DManager_hpp_
