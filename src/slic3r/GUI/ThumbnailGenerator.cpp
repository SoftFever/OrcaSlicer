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

void ThumbnailGenerator::render_and_store(const GLVolumePtrs& volumes, bool printable_only)
{
    auto is_visible = [] (const GLVolume& v) -> bool {
        bool ret = v.printable;
        ret &= (!v.shader_outside_printer_detection_enabled || !v.is_outside);
        return ret;
    };

    static const float orange[] = { 0.99f, 0.49f, 0.26f };
    static const float gray[] = { 0.64f, 0.64f, 0.64f };

    GLVolumeConstPtrs visible_volumes;

    for (const GLVolume* vol : volumes)
    {
        if (!printable_only || is_visible(*vol))
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
        glsafe(::glColor3fv((vol->printable && !vol->is_outside) ? orange : gray));
        vol->render();
    }

    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_LIGHTING));
    glsafe(::glReadPixels(0, 0, m_data.width, m_data.height, GL_RGBA, GL_UNSIGNED_BYTE, (void*)m_data.pixels.data()));
}

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_THUMBNAIL_GENERATOR
