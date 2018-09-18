#ifndef slic3r_GUI_PreviewIface_hpp_
#define slic3r_GUI_PreviewIface_hpp_

#include "../../libslic3r/Point.hpp"

class wxGLCanvas;
class wxDropTarget;

namespace Slic3r {

namespace GUI {
class Preview;
} // namespace GUI

class PreviewIface
{
    GUI::Preview* m_preview;

public:
    explicit PreviewIface(GUI::Preview* preview) : m_preview(preview) {}

    void register_on_viewport_changed_callback(void* callback);
    void set_number_extruders(unsigned int number_extruders);
    void reset_gcode_preview_data();
    void reload_print(bool force = false);
    void set_canvas_as_dirty();
    void set_enabled(bool enabled);
    void set_bed_shape(const Pointfs& shape);
    void select_view(const std::string& direction);
    void set_viewport_from_scene(wxGLCanvas* canvas);
    void set_viewport_into_scene(wxGLCanvas* canvas);
    void set_drop_target(wxDropTarget* target);
};

} // namespace Slic3r

#endif // slic3r_GUI_PreviewIface_hpp_
