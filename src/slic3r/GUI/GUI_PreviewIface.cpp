#include "../../libslic3r/libslic3r.h"
#include "GUI_PreviewIface.hpp"
#include "GUI_Preview.hpp"

namespace Slic3r {

void PreviewIface::set_number_extruders(unsigned int number_extruders)
{
    m_preview->set_number_extruders(number_extruders);
}

void PreviewIface::reset_gcode_preview_data()
{
    m_preview->reset_gcode_preview_data();
}

void PreviewIface::reload_print(bool force)
{
    m_preview->reload_print(force);
}

void PreviewIface::set_canvas_as_dirty()
{
    m_preview->set_canvas_as_dirty();
}

void PreviewIface::set_enabled(bool enabled)
{
    m_preview->set_enabled(enabled);
}

void PreviewIface::set_bed_shape(const Pointfs& shape)
{
    m_preview->set_bed_shape(shape);
}

void PreviewIface::select_view(const std::string& direction)
{
    m_preview->select_view(direction);
}

void PreviewIface::set_viewport_from_scene(wxGLCanvas* canvas)
{
    m_preview->set_viewport_from_scene(canvas);
}

void PreviewIface::set_viewport_into_scene(wxGLCanvas* canvas)
{
    m_preview->set_viewport_into_scene(canvas);
}

void PreviewIface::set_drop_target(wxDropTarget* target)
{
    m_preview->set_drop_target(target);
}

} // namespace Slic3r
