#ifndef slic3r_GLCanvas3D_hpp_
#define slic3r_GLCanvas3D_hpp_

#include "../../libslic3r/BoundingBox.hpp"
#include "../../libslic3r/Utils.hpp"

class wxGLCanvas;
class wxGLContext;
class wxSizeEvent;
class wxIdleEvent;

namespace Slic3r {

class GLVolumeCollection;

namespace GUI {

class GLCanvas3D
{
public:
    class Camera
    {
    public:
        enum EType : unsigned char
        {
            CT_Unknown,
            CT_Perspective,
            CT_Ortho,
            CT_Count
        };

    private:
        EType m_type;
        float m_zoom;
        float m_phi;
        float m_theta;
        float m_distance;
        Pointf3 m_target;

    public:
        Camera();

        Camera::EType get_type() const;
        void set_type(Camera::EType type);
        std::string get_type_as_string() const;

        float get_zoom() const;
        void set_zoom(float zoom);

        float get_phi() const;
        void set_phi(float phi);

        float get_theta() const;
        void set_theta(float theta);

        float get_distance() const;
        void set_distance(float distance);

        const Pointf3& get_target() const;
        void set_target(const Pointf3& target);
    };

    class Bed
    {
        Pointfs m_shape;
        BoundingBoxf3 m_bounding_box;
        Pointf m_origin;

    public:
        const Pointfs& get_shape() const;
        void set_shape(const Pointfs& shape);

        const BoundingBoxf3& get_bounding_box() const;

        const Pointf& get_origin() const;
        void set_origin(const Pointf& origin);

    private:
        void _calc_bounding_box();
    };

private:
    wxGLCanvas* m_canvas;
    wxGLContext* m_context;
    Camera m_camera;
    Bed m_bed;

    GLVolumeCollection* m_volumes;

    bool m_dirty;
    bool m_apply_zoom_to_volumes_filter;

    PerlCallback m_on_viewport_changed_callback;

public:
    GLCanvas3D(wxGLCanvas* canvas, wxGLContext* context);
    ~GLCanvas3D();

    void set_current();

    bool is_shown_on_screen() const;

    void resize(unsigned int w, unsigned int h);

    GLVolumeCollection* get_volumes();
    void set_volumes(GLVolumeCollection* volumes);

    void set_bed_shape(const Pointfs& shape);

    const Pointf& get_bed_origin() const;
    void set_bed_origin(const Pointf& origin);

    bool is_dirty() const;
    void set_dirty(bool dirty);

    Camera::EType get_camera_type() const;
    void set_camera_type(Camera::EType type);
    std::string get_camera_type_as_string() const;

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

    BoundingBoxf3 bed_bounding_box() const;
    BoundingBoxf3 volumes_bounding_box() const;
    BoundingBoxf3 max_bounding_box() const;

    void zoom_to_bed();
    void zoom_to_volumes();
    void select_view(const std::string& direction);

    void register_on_viewport_changed_callback(void* callback);

    void on_size(wxSizeEvent& evt);
    void on_idle(wxIdleEvent& evt);

private:
    void _zoom_to_bounding_box(const BoundingBoxf3& bbox);
    std::pair<int, int> _get_canvas_size() const;
    float _get_zoom_to_bounding_box_factor(const BoundingBoxf3& bbox) const;

    void _deregister_callbacks();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLCanvas3D_hpp_
