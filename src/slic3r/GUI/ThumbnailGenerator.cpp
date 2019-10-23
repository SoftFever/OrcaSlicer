#include "libslic3r/libslic3r.h"
#include "ThumbnailGenerator.hpp"

#if ENABLE_THUMBNAIL_GENERATOR

#include "Camera.hpp"
#include "3DScene.hpp"
#include "GUI.hpp"

#include <boost/filesystem.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <GL/glew.h>

#include <wx/image.h>

namespace Slic3r {
namespace GUI {

void ThumbnailGenerator::reset()
{
    m_data.reset();
}

void ThumbnailGenerator::generate(const GLVolumePtrs& volumes, unsigned int w, unsigned int h, bool printable_only)
{
    std::cout << "Generated thumbnail " << w << "x" << h << std::endl;

    m_data.set(w, h);
    render_and_store(volumes, printable_only);
}

bool ThumbnailGenerator::save_to_png_file(const std::string& filename)
{
    if (!m_data.is_valid())
        return false;

    wxImage image(m_data.width, m_data.height);
    image.InitAlpha();

    for (unsigned int r = 0; r < m_data.height; ++r)
    {
        unsigned int rr = (m_data.height - 1 - r) * m_data.width;
        for (unsigned int c = 0; c < m_data.width; ++c)
        {
            unsigned char* px = m_data.pixels.data() + 4 * (rr + c);
            image.SetRGB((int)c, (int)r, px[0], px[1], px[2]);
            image.SetAlpha((int)c, (int)r, px[3]);
        }
    }

    std::string path = filename;
    if (!boost::iends_with(path, ".png"))
        path = boost::filesystem::path(path).replace_extension(".png").string();

    return image.SaveFile(from_u8(path), wxBITMAP_TYPE_PNG);
}

void ThumbnailGenerator::render_and_store(const GLVolumePtrs& volumes, bool printable_only)
{
    static const float orange[] = { 0.99f, 0.49f, 0.26f };
    static const float gray[] = { 0.64f, 0.64f, 0.64f };

    GLVolumeConstPtrs visible_volumes;

    for (const GLVolume* vol : volumes)
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
    glsafe(::glEnable(GL_LIGHTING));
    glsafe(::glEnable(GL_DEPTH_TEST));

    for (const GLVolume* vol : visible_volumes)
    {
        glsafe(::glColor3fv(vol->printable ? orange : gray));
        vol->render();
    }

    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_LIGHTING));
    glsafe(::glReadPixels(0, 0, m_data.width, m_data.height, GL_RGBA, GL_UNSIGNED_BYTE, (void*)m_data.pixels.data()));
}

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_THUMBNAIL_GENERATOR
