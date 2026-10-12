#include "GLGizmoImage.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Shortcuts.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoPainterBase.hpp"

#include <string>
#include <wx/string.h>

namespace Slic3r::GUI {

GLGizmoImage::GLGizmoImage(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id)
    : GLGizmoTextureDisplacement(parent, icon_filename, sprite_id)
{
}

bool GLGizmoImage::on_init()
{
    if (!GLGizmoTextureDisplacement::on_init())
        return false;
    // Everything the base set up stays - the brush descriptions and the shortcut lists are the same
    // tools - except the key that opens it, which is this tool's own.
    m_shortcut = Shortcut::GizmoImage;
    return true;
}

std::string GLGizmoImage::on_get_name() const
{
    return _u8L("Image");
}

wxString GLGizmoImage::handle_snapshot_action_name(bool shift_down, GLGizmoPainterBase::Button button_down) const
{
    // Same paint, same masks; only the undo step's wording follows the tool the user is in.
    const bool erasing = shift_down || (m_erase_mode != (button_down == GLGizmoPainterBase::Button::Right));
    return erasing ? _L("Erase image paint") : _L("Paint image");
}

} // namespace Slic3r::GUI
