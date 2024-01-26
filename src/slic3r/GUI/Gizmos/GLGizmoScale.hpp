///|/ Copyright (c) Prusa Research 2019 - 2023 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Filip Sykala @Jony01
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GLGizmoScale_hpp_
#define slic3r_GLGizmoScale_hpp_

#include "GLGizmoBase.hpp"
//BBS: add size adjust related
#include "GizmoObjectManipulation.hpp"

#include "libslic3r/BoundingBox.hpp"


namespace Slic3r {
namespace GUI {

class GLGizmoScale3D : public GLGizmoBase
{
    static const float Offset;

    struct StartingData
    {
        Vec3d scale;
        Vec3d drag_position;
        Vec3d plane_center;  // keep the relative center position for scale in the bottom plane
        Vec3d plane_nromal;  // keep the bottom plane 
        BoundingBoxf3 box;
        Vec3d pivots[6];
        bool ctrl_down;

        StartingData() : scale(Vec3d::Ones()), drag_position(Vec3d::Zero()), ctrl_down(false) { for (int i = 0; i < 5; ++i) { pivots[i] = Vec3d::Zero(); } }
    };

    BoundingBoxf3 m_box;
    Transform3d m_transform;
    Vec3d m_scale{ Vec3d::Ones() };
    double m_snap_step{ 0.05 };
    StartingData m_starting;

    ColorRGBA m_base_color;
    ColorRGBA m_drag_color;
    ColorRGBA m_highlight_color;

    struct GrabberConnection
    {
        GLModel model;
        std::pair<unsigned int, unsigned int> grabber_indices;
        Vec3d old_v1{ Vec3d::Zero() };
        Vec3d old_v2{ Vec3d::Zero() };
    };
    std::array<GrabberConnection, 7> m_grabber_connections;

    //BBS: add size adjust related
    GizmoObjectManipulation* m_object_manipulation;

public:
    //BBS: add obj manipulation logic
    //GLGizmoScale3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    GLGizmoScale3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, GizmoObjectManipulation* obj_manipulation);

    double get_snap_step(double step) const { return m_snap_step; }
    void set_snap_step(double step) { m_snap_step = step; }

    const Vec3d& get_scale() const { return m_scale; }
    void set_scale(const Vec3d& scale) { m_starting.scale = scale; m_scale = scale; }

    std::string get_tooltip() const override;

    /// <summary>
    /// Postpone to Grabber for scale
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>Return True when use the information otherwise False.</returns>
    bool on_mouse(const wxMouseEvent &mouse_event) override;

    void data_changed(bool is_serializing) override;
    void enable_ununiversal_scale(bool enable);
protected:
    virtual bool on_init() override;
    virtual std::string on_get_name() const override;
    virtual bool on_is_activable() const override;
    virtual void on_start_dragging() override;
    virtual void on_stop_dragging() override;
    virtual void on_dragging(const UpdateData& data) override;
    virtual void on_render() override;
    virtual void on_register_raycasters_for_picking() override;
    virtual void on_unregister_raycasters_for_picking() override;
    //BBS: GUI refactor: add object manipulation
    virtual void on_render_input_window(float x, float y, float bottom_limit);

private:
    void render_grabbers_connection(unsigned int id_1, unsigned int id_2, const ColorRGBA& color);

    void do_scale_along_axis(Axis axis, const UpdateData& data);
    void do_scale_uniform(const UpdateData& data);

    double calc_ratio(const UpdateData& data) const;
    void update_render_data();
};


} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoScale_hpp_
