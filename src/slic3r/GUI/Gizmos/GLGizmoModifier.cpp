// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoModifier.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"

#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/Model.hpp"

#include <numeric>

#include <GL/glew.h>

namespace Slic3r {
namespace GUI {

const int SHAPE_IMAGE_SIZE = 34;

const std::vector<std::pair<std::string, std::string>> GLGizmoModifier::MODIFIER_SHAPES = {
    {L("Cube"),     "toolbar_modifier_cube.svg" },
    {L("Cylinder"), "toolbar_modifier_cylinder.svg" },
    {L("Sphere"),   "toolbar_modifier_sphere.svg" },
    {L("Cone"),     "toolbar_modifier_cone.svg" },
    {L("Timelapse Wipe Tower"), "toolbar_modifier_cube.svg"},
};

GLGizmoModifier::GLGizmoModifier(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{
}

bool GLGizmoModifier::on_init()
{
    bool result = true;
    texture_ids.clear();
    for (auto item: MODIFIER_SHAPES) {
        result = result && init_shape_texture(item.second);
    }

    // BBS
    m_shortcut_key = WXK_NONE;
    return result;
}

bool GLGizmoModifier::init_shape_texture(std::string image_name)
{
    // init shapes image
    bool     compress = false;
    GLint    last_texture;
    unsigned m_image_texture{0};

    std::string path = resources_dir() + "/images/";
    std::string file_name = path + image_name;

    ThumbnailData data;
    if (!get_data_from_svg(file_name, SHAPE_IMAGE_SIZE, data)) return false;

    unsigned char *pixels = (unsigned char *) (&data.pixels[0]);
    glsafe(::glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture));
    glsafe(::glGenTextures(1, &m_image_texture));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_image_texture));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    glsafe(::glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
    if (compress && GLEW_EXT_texture_compression_s3tc)
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, data.width, data.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));
    else
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, data.width, data.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));

    // Store our identifier
    ImTextureID texture_id = (ImTextureID) (intptr_t) m_image_texture;
    texture_ids.push_back(texture_id);
    // Restore state
    glsafe(::glBindTexture(GL_TEXTURE_2D, last_texture));

    return true;
}

void GLGizmoModifier::on_set_state()
{
}

void GLGizmoModifier::on_render_input_window(float x, float y, float bottom_limit)
{
    // BBS: GUI refactor: move gizmo to the right
    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always, 0.f, 0.0f);

    // BBS
    ImGuiWrapper::push_toolbar_style();

    std::string name = "Add Modifier##Modifier";
    m_imgui->begin(_L(name), ImGuiWrapper::TOOLBAR_WINDOW_FLAGS);

    for (int i = 0; i < MODIFIER_SHAPES.size(); i++) {
        if (ImGui::ImageButton(texture_ids[i], ImVec2(34.0f, 34.0f))) {
            wxGetApp().obj_list()->load_generic_subobject(MODIFIER_SHAPES[i].first, ModelVolumeType::PARAMETER_MODIFIER);
        }
        ImGui::SameLine();
    }
    m_imgui->end();
    ImGuiWrapper::pop_toolbar_style();
}

CommonGizmosDataID GLGizmoModifier::on_get_requirements() const
{
    return CommonGizmosDataID::SelectionInfo;
}

std::string GLGizmoModifier::on_get_name() const
{
    return _u8L("Add Modifier");
}

bool GLGizmoModifier::on_is_activable() const
{
    return m_parent.get_selection().is_single_full_instance();
}

void GLGizmoModifier::on_start_dragging()
{
    ;
}

void GLGizmoModifier::on_render()
{
    ;
}

void GLGizmoModifier::on_render_for_picking()
{
    ;
}


} // namespace GUI
} // namespace Slic3r
