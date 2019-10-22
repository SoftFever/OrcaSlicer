#include "libslic3r/libslic3r.h"
#include "ThumbnailGenerator.hpp"

#if ENABLE_THUMBNAIL_GENERATOR

#include "GLCanvas3D.hpp"
#include "3DScene.hpp"

#include <GL/glew.h>

namespace Slic3r {
namespace GUI {

void ThumbnailGenerator::reset()
{
    m_data.reset();
}

bool ThumbnailGenerator::render_to_png_file(const GLCanvas3D& canvas, const std::string& filename, unsigned int w, unsigned int h, bool printable_only)
{
    m_data.set(w, h);
    render(canvas, printable_only);

    wxImage image(m_data.width, m_data.height);
    image.InitAlpha();

    for (unsigned int r = 0; r < m_data.height; ++r)
    {
        unsigned int rr = (m_data.height - 1 - r) * m_data.width;
        for (unsigned int c = 0; c < m_data.width; ++c)
        {
            unsigned char* px = m_data.pixels.data() + 4 * (rr + c);
//            unsigned char* px = m_data.pixels.data() + 4 * (r * m_data.width + c);
            image.SetRGB((int)c, (int)r, px[0], px[1], px[2]);
            image.SetAlpha((int)c, (int)r, px[3]);
        }
    }

    image.SaveFile(filename, wxBITMAP_TYPE_PNG);

    return true;
}

void ThumbnailGenerator::render(const GLCanvas3D& canvas, bool printable_only)
{
    const GLVolumeCollection& volumes = canvas.get_volumes();

    std::vector<const GLVolume*> visible_volumes;

    for (const GLVolume* vol : volumes.volumes)
    {
        if (!printable_only || vol->printable)
            visible_volumes.push_back(vol);
    }

    if (visible_volumes.empty())
        return;

    BoundingBoxf3 box;
    for (const GLVolume* vol : visible_volumes)
    {
        box.merge(vol->transformed_bounding_box());
    }

    Camera camera;
    camera.zoom_to_box(box, m_data.width, m_data.height);
    camera.apply_viewport(0, 0, m_data.width, m_data.height);
    camera.apply_view_matrix();
    camera.apply_projection(box);

    glsafe(::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    render_objects(visible_volumes);
    glsafe(::glReadPixels(0, 0, m_data.width, m_data.height, GL_RGBA, GL_UNSIGNED_BYTE, (void*)m_data.pixels.data()));
}

void ThumbnailGenerator::render_objects(const std::vector<const GLVolume*>& volumes) const
{
    static const float orange[] = { 0.99f, 0.49f, 0.26f };
    static const float gray[]   = { 0.64f, 0.64f, 0.64f };

    glsafe(::glEnable(GL_LIGHTING));
    glsafe(::glEnable(GL_DEPTH_TEST));

    for (const GLVolume* vol : volumes)
    {
        glsafe(::glColor3fv(vol->printable ? orange : gray));
        vol->render();
    }

    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_LIGHTING));
}

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_THUMBNAIL_GENERATOR
