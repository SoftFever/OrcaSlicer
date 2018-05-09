#ifndef slic3r_GLCanvas3D_hpp_
#define slic3r_GLCanvas3D_hpp_

#include "../../libslic3r/Point.hpp"

class wxGLCanvas;
class wxGLContext;
class wxSizeEvent;

namespace Slic3r {
namespace GUI {

class GLCanvas3D
{
public:
    struct Camera
    {
        enum EType : unsigned char
        {
            CT_Unknown,
            CT_Perspective,
            CT_Ortho,
            CT_Count
        };

        EType type;
        float zoom;
        float phi;
        float theta;
        float distance;
        Pointf3 target;

        Camera();
    };

private:
    wxGLCanvas* m_canvas;
    wxGLContext* m_context;
    Camera m_camera;

    bool m_dirty;

public:
    GLCanvas3D(wxGLCanvas* canvas, wxGLContext* context);

    void set_current();

    bool is_dirty() const;
    void set_dirty(bool dirty);

    bool is_shown_on_screen() const;

    Camera::EType get_camera_type() const;
    void set_camera_type(Camera::EType type);

    float get_camera_zoom() const;
    void set_camera_zoom(float zoom);

    float get_camera_phi() const;
    void set_camera_phi(float phi);

    float get_camera_theta() const;
    void set_camera_theta(float theta);

    float get_camera_distance() const;
    void set_camera_distance(float distance);

    const Pointf3& get_camera_target() const;
    void set_camera_target(const Pointf3& target);

    void on_size(wxSizeEvent& evt);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLCanvas3D_hpp_
