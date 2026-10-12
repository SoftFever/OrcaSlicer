#ifndef slic3r_GLGizmoImage_hpp_
#define slic3r_GLGizmoImage_hpp_

#include "slic3r/GUI/Gizmos/GLGizmoTextureDisplacement.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <string>
#include <wx/string.h>

namespace Slic3r::GUI {

class GLCanvas3D;

// "Put this picture on the model": the texture-displacement gizmo with the relief taken out of it.
//
// There is no second engine here and no copy of one. It is GLGizmoTextureDisplacement with
// image_mode() on, which pins every layer to zero depth and colour on (see
// apply_image_mode_presets()) and leaves out the controls that only mean something once the surface
// moves. Painting, the layer stack, the texture library, the previews and the bake are the very same
// code, reached the very same way - a zero-depth bake refines the painted area and writes the
// colours exactly as a relief bake does, it just displaces by nothing.
//
// Its own toolbar button because the two are different jobs, not two settings of one: a user who
// wants a photo on a part should not have to know what a height map is, or find the one slider that
// has to be zero.
class GLGizmoImage : public GLGizmoTextureDisplacement
{
public:
    GLGizmoImage(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id);

protected:
    bool image_mode() const override { return true; }

    bool        on_init() override;
    std::string on_get_name() const override;

    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

    std::string get_gizmo_entering_text() const override { return _u8L("Entering Image painting"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leaving Image painting"); }
    std::string get_action_snapshot_name() const override { return _u8L("Image editing"); }
};

} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoImage_hpp_
