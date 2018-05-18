#ifndef slic3r_GLCanvas3DManager_hpp_
#define slic3r_GLCanvas3DManager_hpp_

#include "../../slic3r/GUI/GLCanvas3D.hpp"

#include <map>

namespace Slic3r {
namespace GUI {

class GLCanvas3DManager
{
    struct GLVersion
    {
        unsigned int vn_major;
        unsigned int vn_minor;

        GLVersion();
        bool detect();

        bool is_greater_or_equal_to(unsigned int major, unsigned int minor) const;
    };

    struct LayerEditing
    {
        bool allowed;

        LayerEditing();
    };

    typedef std::map<wxGLCanvas*, GLCanvas3D*> CanvasesMap;

    CanvasesMap m_canvases;
    GLVersion m_gl_version;
    LayerEditing m_layer_editing;
    bool m_gl_initialized;
    bool m_use_legacy_opengl;
    bool m_use_VBOs;

public:
    GLCanvas3DManager();

    bool add(wxGLCanvas* canvas, wxGLContext* context);
    bool remove(wxGLCanvas* canvas);

    void remove_all();

    unsigned int count() const;

    void init_gl();

    bool use_VBOs() const;
    bool layer_editing_allowed() const;

    bool is_dirty(wxGLCanvas* canvas) const;
    void set_dirty(wxGLCanvas* canvas, bool dirty);

    bool is_shown_on_screen(wxGLCanvas* canvas) const;

    void resize(wxGLCanvas* canvas, unsigned int w, unsigned int h);

    GLVolumeCollection* get_volumes(wxGLCanvas* canvas);
    void set_volumes(wxGLCanvas* canvas, GLVolumeCollection* volumes);

    void set_bed_shape(wxGLCanvas* canvas, const Pointfs& shape);
    void set_auto_bed_shape(wxGLCanvas* canvas);

    Pointf get_bed_origin(wxGLCanvas* canvas) const;
    void set_bed_origin(wxGLCanvas* canvas, const Pointf& origin);

    BoundingBoxf3 get_bed_bounding_box(wxGLCanvas* canvas);
    BoundingBoxf3 get_volumes_bounding_box(wxGLCanvas* canvas);
    BoundingBoxf3 get_max_bounding_box(wxGLCanvas* canvas);

    void set_cutting_plane(wxGLCanvas* canvas, float z, const ExPolygons& polygons);

    unsigned int get_camera_type(wxGLCanvas* canvas) const;
    void set_camera_type(wxGLCanvas* canvas, unsigned int type);
    std::string get_camera_type_as_string(wxGLCanvas* canvas) const;
    
    float get_camera_zoom(wxGLCanvas* canvas) const;
    void set_camera_zoom(wxGLCanvas* canvas, float zoom);

    float get_camera_phi(wxGLCanvas* canvas) const;
    void set_camera_phi(wxGLCanvas* canvas, float phi);

    float get_camera_theta(wxGLCanvas* canvas) const;
    void set_camera_theta(wxGLCanvas* canvas, float theta);

    float get_camera_distance(wxGLCanvas* canvas) const;
    void set_camera_distance(wxGLCanvas* canvas, float distance);

    Pointf3 get_camera_target(wxGLCanvas* canvas) const;
    void set_camera_target(wxGLCanvas* canvas, const Pointf3* target);

    void zoom_to_bed(wxGLCanvas* canvas);
    void zoom_to_volumes(wxGLCanvas* canvas);
    void select_view(wxGLCanvas* canvas, const std::string& direction);

    void render_bed(wxGLCanvas* canvas);
    void render_cutting_plane(wxGLCanvas* canvas);

    void register_on_viewport_changed_callback(wxGLCanvas* canvas, void* callback);

private:
    CanvasesMap::iterator _get_canvas(wxGLCanvas* canvas);
    CanvasesMap::const_iterator _get_canvas(wxGLCanvas* canvas) const;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLCanvas3DManager_hpp_
