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
class GCodePreviewData;

namespace GUI {

class GLCanvas3D;

class GLCanvas3DManager
{
    struct GLInfo
    {
        std::string version;
        std::string glsl_version;
        std::string vendor;
        std::string renderer;

        GLInfo();

        void detect();
        bool is_version_greater_or_equal_to(unsigned int major, unsigned int minor) const;

        std::string to_string(bool format_as_html, bool extensions) const;
    };

    enum EMultisampleState : unsigned char
    {
        MS_Unknown,
        MS_Enabled,
        MS_Disabled
    };

    typedef std::map<wxGLCanvas*, GLCanvas3D*> CanvasesMap;

    CanvasesMap m_canvases;
#if ENABLE_USE_UNIQUE_GLCONTEXT
    wxGLContext* m_context;
#endif // ENABLE_USE_UNIQUE_GLCONTEXT
    GLInfo m_gl_info;
    bool m_gl_initialized;
    bool m_use_legacy_opengl;
    bool m_use_VBOs;
    static EMultisampleState s_multisample;

public:
    GLCanvas3DManager();
#if ENABLE_USE_UNIQUE_GLCONTEXT
    ~GLCanvas3DManager();
#endif // ENABLE_USE_UNIQUE_GLCONTEXT

    bool add(wxGLCanvas* canvas);
    bool remove(wxGLCanvas* canvas);
    void remove_all();

    unsigned int count() const;

    void init_gl();
    std::string get_gl_info(bool format_as_html, bool extensions) const;

    bool init(wxGLCanvas* canvas);

    GLCanvas3D* get_canvas(wxGLCanvas* canvas);

    static bool can_multisample() { return s_multisample == MS_Enabled; }
    static wxGLCanvas* create_wxglcanvas(wxWindow *parent);

private:
    CanvasesMap::iterator _get_canvas(wxGLCanvas* canvas);
    CanvasesMap::const_iterator _get_canvas(wxGLCanvas* canvas) const;

    bool _init(GLCanvas3D& canvas);
    static void _detect_multisample(int* attribList);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLCanvas3DManager_hpp_
