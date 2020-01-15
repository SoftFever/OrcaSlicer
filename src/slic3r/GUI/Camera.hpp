#ifndef slic3r_Camera_hpp_
#define slic3r_Camera_hpp_

#include "libslic3r/BoundingBox.hpp"
#if ENABLE_THUMBNAIL_GENERATOR
#include "3DScene.hpp"
#endif // ENABLE_THUMBNAIL_GENERATOR
#include <array>

namespace Slic3r {
namespace GUI {

struct Camera
{
    static const double DefaultDistance;
#if ENABLE_THUMBNAIL_GENERATOR
    static const double DefaultZoomToBoxMarginFactor;
    static const double DefaultZoomToVolumesMarginFactor;
#endif // ENABLE_THUMBNAIL_GENERATOR
    static double FrustrumMinZRange;
    static double FrustrumMinNearZ;
    static double FrustrumZMargin;
    static double MaxFovDeg;

    enum EType : unsigned char
    {
        Unknown,
        Ortho,
        Perspective,
        Num_types
    };

#if !ENABLE_6DOF_CAMERA
    float phi;
    bool inverted_phi;
#endif // !ENABLE_6DOF_CAMERA
    bool requires_zoom_to_bed;

private:
    EType m_type;
    Vec3d m_target;
#if !ENABLE_6DOF_CAMERA
    float m_theta;
#endif // !ENABLE_6DOF_CAMERA
    double m_zoom;
    // Distance between camera position and camera target measured along the camera Z axis
    mutable double m_distance;
    mutable double m_gui_scale;

    mutable std::array<int, 4> m_viewport;
#if ENABLE_6DOF_CAMERA
    Transform3d m_view_matrix;
#else
    mutable Transform3d m_view_matrix;
#endif // ENABLE_6DOF_CAMERA
    mutable Transform3d m_projection_matrix;
    mutable std::pair<double, double> m_frustrum_zs;

    BoundingBoxf3 m_scene_box;

public:
    Camera();

    EType get_type() const { return m_type; }
    std::string get_type_as_string() const;
    void set_type(EType type);
    // valid values for type: "0" -> ortho, "1" -> perspective
    void set_type(const std::string& type);
    void select_next_type();

    const Vec3d& get_target() const { return m_target; }
    void set_target(const Vec3d& target);

    double get_distance() const { return m_distance; }
    double get_gui_scale() const { return m_gui_scale; }

#if !ENABLE_6DOF_CAMERA
    float get_theta() const { return m_theta; }
    void set_theta(float theta, bool apply_limit);
#endif // !ENABLE_6DOF_CAMERA

    double get_zoom() const { return m_zoom; }
    void update_zoom(double delta_zoom);
    void set_zoom(double zoom);

    const BoundingBoxf3& get_scene_box() const { return m_scene_box; }
    void set_scene_box(const BoundingBoxf3& box) { m_scene_box = box; }

#if ENABLE_6DOF_CAMERA
    void select_view(const std::string& direction);
#else
    bool select_view(const std::string& direction);
#endif // ENABLE_6DOF_CAMERA

    const std::array<int, 4>& get_viewport() const { return m_viewport; }
    const Transform3d& get_view_matrix() const { return m_view_matrix; }
    const Transform3d& get_projection_matrix() const { return m_projection_matrix; }

    Vec3d get_dir_right() const { return m_view_matrix.matrix().block(0, 0, 3, 3).row(0); }
    Vec3d get_dir_up() const { return m_view_matrix.matrix().block(0, 0, 3, 3).row(1); }
    Vec3d get_dir_forward() const { return -m_view_matrix.matrix().block(0, 0, 3, 3).row(2); }

    Vec3d get_position() const { return m_view_matrix.matrix().inverse().block(0, 3, 3, 1); }

    double get_near_z() const { return m_frustrum_zs.first; }
    double get_far_z() const { return m_frustrum_zs.second; }

    double get_fov() const;

    void apply_viewport(int x, int y, unsigned int w, unsigned int h) const;
    void apply_view_matrix() const;
    // Calculates and applies the projection matrix tighting the frustrum z range around the given box.
    // If larger z span is needed, pass the desired values of near and far z (negative values are ignored)
    void apply_projection(const BoundingBoxf3& box, double near_z = -1.0, double far_z = -1.0) const;

#if ENABLE_THUMBNAIL_GENERATOR
    void zoom_to_box(const BoundingBoxf3& box, int canvas_w, int canvas_h, double margin_factor = DefaultZoomToBoxMarginFactor);
    void zoom_to_volumes(const GLVolumePtrs& volumes, int canvas_w, int canvas_h, double margin_factor = DefaultZoomToVolumesMarginFactor);
#else
    void zoom_to_box(const BoundingBoxf3& box, int canvas_w, int canvas_h);
#endif // ENABLE_THUMBNAIL_GENERATOR

#if ENABLE_CAMERA_STATISTICS
    void debug_render() const;
#endif // ENABLE_CAMERA_STATISTICS

#if ENABLE_6DOF_CAMERA
    // translate the camera in world space
    void translate_world(const Vec3d& displacement);

    // rotate the camera on a sphere having center == m_target and radius == m_distance
    // using the given variations of spherical coordinates
    void rotate_on_sphere(double delta_azimut_rad, double delta_zenit_rad, bool apply_limit);

    // rotate the camera around three axes parallel to the camera local axes and passing through m_target
    void rotate_local_around_target(const Vec3d& rotation_rad);

    // returns true if the camera z axis (forward) is pointing in the negative direction of the world z axis
    bool is_looking_downward() const;
#endif // ENABLE_6DOF_CAMERA

private:
    // returns tight values for nearZ and farZ plane around the given bounding box
    // the camera MUST be outside of the bounding box in eye coordinate of the given box
    std::pair<double, double> calc_tight_frustrum_zs_around(const BoundingBoxf3& box) const;
#if ENABLE_THUMBNAIL_GENERATOR
    double calc_zoom_to_bounding_box_factor(const BoundingBoxf3& box, int canvas_w, int canvas_h, double margin_factor = DefaultZoomToBoxMarginFactor) const;
    double calc_zoom_to_volumes_factor(const GLVolumePtrs& volumes, int canvas_w, int canvas_h, Vec3d& center, double margin_factor = DefaultZoomToVolumesMarginFactor) const;
#else
    double calc_zoom_to_bounding_box_factor(const BoundingBoxf3& box, int canvas_w, int canvas_h) const;
#endif // ENABLE_THUMBNAIL_GENERATOR
    void set_distance(double distance) const;

#if ENABLE_6DOF_CAMERA
    Transform3d look_at(const Vec3d& position, const Vec3d& target, const Vec3d& up) const;
    void set_default_orientation();
    Vec3d validate_target(const Vec3d& target) const;
#endif // ENABLE_6DOF_CAMERA
};

} // GUI
} // Slic3r

#endif // slic3r_Camera_hpp_

