#include "GLSelectionRectangle.hpp"
#include "Camera.hpp"
#include "3DScene.hpp"
#include "GLCanvas3D.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include <igl/project.h>

#include <GL/glew.h>

namespace Slic3r {
namespace GUI {

    void GLSelectionRectangle::start_dragging(const Vec2d& mouse_position, EState state)
    {
        if (is_dragging() || (state == Off))
            return;

        m_state = state;
        m_start_corner = mouse_position;
        m_end_corner = mouse_position;
    }

    void GLSelectionRectangle::dragging(const Vec2d& mouse_position)
    {
        if (!is_dragging())
            return;

        m_end_corner = mouse_position;
    }

    std::vector<unsigned int> GLSelectionRectangle::stop_dragging(const GLCanvas3D& canvas, const std::vector<Vec3d>& points)
    {
        std::vector<unsigned int> out;

        if (!is_dragging())
            return out;

        m_state = Off;

        const Camera& camera = wxGetApp().plater()->get_camera();
        Matrix4d modelview = camera.get_view_matrix().matrix();
        Matrix4d projection= camera.get_projection_matrix().matrix();
        Vec4i viewport(camera.get_viewport().data());

        // Convert our std::vector to Eigen dynamic matrix.
        Eigen::Matrix<double, Eigen::Dynamic, 3, Eigen::DontAlign> pts(points.size(), 3);
        for (size_t i=0; i<points.size(); ++i)
            pts.block<1, 3>(i, 0) = points[i];

        // Get the projections.
        Eigen::Matrix<double, Eigen::Dynamic, 3, Eigen::DontAlign> projections;
        igl::project(pts, modelview, projection, viewport, projections);

        // bounding box created from the rectangle corners - will take care of order of the corners
        BoundingBox rectangle(Points{ Point(m_start_corner.cast<coord_t>()), Point(m_end_corner.cast<coord_t>()) });

        // Iterate over all points and determine whether they're in the rectangle.
        for (int i = 0; i<projections.rows(); ++i)
            if (rectangle.contains(Point(projections(i, 0), canvas.get_canvas_size().get_height() - projections(i, 1))))
                out.push_back(i);

        return out;
    }

    void GLSelectionRectangle::stop_dragging()
    {
        if (is_dragging())
            m_state = Off;
    }

    void GLSelectionRectangle::render(const GLCanvas3D& canvas) const
    {
        if (!is_dragging())
            return;

        const Camera& camera = wxGetApp().plater()->get_camera();
        float inv_zoom = (float)camera.get_inv_zoom();

        Size cnv_size = canvas.get_canvas_size();
        float cnv_half_width = 0.5f * (float)cnv_size.get_width();
        float cnv_half_height = 0.5f * (float)cnv_size.get_height();
        if ((cnv_half_width == 0.0f) || (cnv_half_height == 0.0f))
            return;

        Vec2d start(m_start_corner(0) - cnv_half_width, cnv_half_height - m_start_corner(1));
        Vec2d end(m_end_corner(0) - cnv_half_width, cnv_half_height - m_end_corner(1));

        float left = (float)std::min(start(0), end(0)) * inv_zoom;
        float top = (float)std::max(start(1), end(1)) * inv_zoom;
        float right = (float)std::max(start(0), end(0)) * inv_zoom;
        float bottom = (float)std::min(start(1), end(1)) * inv_zoom;

        glsafe(::glLineWidth(1.5f));
        float color[3];
        color[0] = (m_state == Select) ? 0.3f : 1.0f;
        color[1] = (m_state == Select) ? 1.0f : 0.3f;
        color[2] = 0.3f;
        glsafe(::glColor3fv(color));

        glsafe(::glDisable(GL_DEPTH_TEST));

        glsafe(::glPushMatrix());
        glsafe(::glLoadIdentity());
        // ensure that the rectangle is renderered inside the frustrum
        glsafe(::glTranslated(0.0, 0.0, -(camera.get_near_z() + 0.5)));
        // ensure that the overlay fits the frustrum near z plane
        double gui_scale = camera.get_gui_scale();
        glsafe(::glScaled(gui_scale, gui_scale, 1.0));

        glsafe(::glPushAttrib(GL_ENABLE_BIT));
        glsafe(::glLineStipple(4, 0xAAAA));
        glsafe(::glEnable(GL_LINE_STIPPLE));

        ::glBegin(GL_LINE_LOOP);
        ::glVertex2f((GLfloat)left, (GLfloat)bottom);
        ::glVertex2f((GLfloat)right, (GLfloat)bottom);
        ::glVertex2f((GLfloat)right, (GLfloat)top);
        ::glVertex2f((GLfloat)left, (GLfloat)top);
        glsafe(::glEnd());

        glsafe(::glPopAttrib());

        glsafe(::glPopMatrix());
    }

} // namespace GUI
} // namespace Slic3r
