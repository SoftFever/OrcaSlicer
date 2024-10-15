#include "libslic3r/libslic3r.h"
#include "GLGizmosManager.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/Utils/UndoRedo.hpp"
#include "slic3r/GUI/NotificationManager.hpp"

#include "slic3r/GUI/Gizmos/GLGizmoMove.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoScale.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoRotate.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoFlatten.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoFdmSupports.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoBrimEars.hpp"
// BBS
#include "slic3r/GUI/Gizmos/GLGizmoAdvancedCut.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoFaceDetector.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoHollow.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoSeam.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoMmuSegmentation.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoSimplify.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoText.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoSVG.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoMeshBoolean.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoAssembly.hpp"

#include "libslic3r/format.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <wx/glcanvas.h>

namespace Slic3r {
namespace GUI {
//BBS: GUI refactor: to support top layout
#if BBS_TOOLBAR_ON_TOP
const float GLGizmosManager::Default_Icons_Size = 40;
#else
const float GLGizmosManager::Default_Icons_Size = 64;
#endif

GLGizmosManager::GLGizmosManager(GLCanvas3D& parent)
    : m_parent(parent)
    , m_enabled(false)
    , m_icons_texture_dirty(true)
    , m_current(Undefined)
    , m_tooltip("")
    , m_serializing(false)
    //BBS: GUI refactor: add object manipulation in gizmo
    , m_object_manipulation(parent)
{
    m_timer_set_color.Bind(wxEVT_TIMER, &GLGizmosManager::on_set_color_timer, this);
}

std::vector<size_t> GLGizmosManager::get_selectable_idxs() const
{
    std::vector<size_t> out;
    if (m_parent.get_canvas_type() == GLCanvas3D::CanvasAssembleView) {
        for (size_t i = 0; i < m_gizmos.size(); ++i)
            if (m_gizmos[i]->get_sprite_id() == (unsigned int) Move ||
                m_gizmos[i]->get_sprite_id() == (unsigned int) Rotate ||
                m_gizmos[i]->get_sprite_id() == (unsigned int) Measure ||
                m_gizmos[i]->get_sprite_id() == (unsigned int) Assembly ||
                m_gizmos[i]->get_sprite_id() == (unsigned int) MmuSegmentation)
                out.push_back(i);
    }
    else {
        for (size_t i = 0; i < m_gizmos.size(); ++i)
            if (m_gizmos[i]->is_selectable())
                out.push_back(i);
    }
    return out;
}

//BBS: GUI refactor: GLToolbar&&Gizmo adjust
//when judge the mouse position, {0, 0} is at the left-up corner, and no need to multiply camera_zoom
size_t GLGizmosManager::get_gizmo_idx_from_mouse(const Vec2d& mouse_pos) const
{
    if (! m_enabled)
        return Undefined;

    float cnv_h = (float)m_parent.get_canvas_size().get_height();
    float height = get_scaled_total_height();
    float icons_size = m_layout.scaled_icons_size();
    float border = m_layout.scaled_border();

    //BBS: GUI refactor: GLToolbar&&Gizmo adjust
    float cnv_w = (float)m_parent.get_canvas_size().get_width();
    float width = get_scaled_total_width();
#if BBS_TOOLBAR_ON_TOP
    //float space_width = GLGizmosManager::Default_Icons_Size * wxGetApp().toolbar_icon_scale();;
    float top_x = std::max(m_parent.get_main_toolbar_width() + border, 0.5f * (cnv_w - width + m_parent.get_main_toolbar_width() + m_parent.get_collapse_toolbar_width() - m_parent.get_assemble_view_toolbar_width()) + border);
    if (m_parent.get_canvas_type() == GLCanvas3D::CanvasAssembleView)
        top_x = 0.5f * cnv_w + 0.5f * (m_parent.get_assembly_paint_toolbar_width());
    float top_y = 0;
    float stride_x = m_layout.scaled_stride_x();

    // is mouse vertically in the area?
    //if ((border <= (float)mouse_pos(0) && ((float)mouse_pos(0) <= border + icons_size))) {
    if (((top_y + border) <= (float)mouse_pos(1)) && ((float)mouse_pos(1) <= (top_y + border + icons_size))) {
        // which icon is it on?
        int from_left = (float) mouse_pos(0) - top_x < 0 ? -1 : (int) ((float) mouse_pos(0) - top_x) / stride_x;
        if (from_left < 0)
            return Undefined;
        // is it really on the icon or already past the border?
        if ((float)mouse_pos(0) <= top_x + from_left * stride_x + icons_size) {
            std::vector<size_t> selectable = get_selectable_idxs();
            if (from_left < selectable.size())
                return selectable[from_left];
        }
    }
#else
    //float top_y = 0.5f * (cnv_h - height) + border;
    float top_x = cnv_w - width;
    float top_y = 0.5f * (cnv_h - height + m_parent.get_main_toolbar_height() - m_parent.get_assemble_view_toolbar_width()) + border;
    float stride_y = m_layout.scaled_stride_y();

    // is mouse horizontally in the area?
    //if ((border <= (float)mouse_pos(0) && ((float)mouse_pos(0) <= border + icons_size))) {
    if (((top_x + border) <= (float)mouse_pos(0)) && ((float)mouse_pos(0) <= (top_x + border + icons_size))) {
        // which icon is it on?
        size_t from_top = (size_t)((float)mouse_pos(1) - top_y) / stride_y;
        if (from_top < 0)
            return Undefined;
        // is it really on the icon or already past the border?
        if ((float)mouse_pos(1) <= top_y + from_top * stride_y + icons_size) {
            std::vector<size_t> selectable = get_selectable_idxs();
            if (from_top < selectable.size())
                return selectable[from_top];
        }
    }
#endif

    return Undefined;
}

void GLGizmosManager::switch_gizmos_icon_filename()
{
    m_background_texture.metadata.filename = m_is_dark ? "toolbar_background_dark.png" : "toolbar_background.png";
    m_background_texture.metadata.left = 16;
    m_background_texture.metadata.top = 16;
    m_background_texture.metadata.right = 16;
    m_background_texture.metadata.bottom = 16;
    if (!m_background_texture.metadata.filename.empty())
        m_background_texture.texture.load_from_file(resources_dir() + "/images/" + m_background_texture.metadata.filename, false, GLTexture::SingleThreaded, false);

    for (auto& gizmo : m_gizmos) {
        gizmo->on_change_color_mode(m_is_dark);
        switch (gizmo->get_sprite_id())
        {
        case(EType::Move):
            gizmo->set_icon_filename(m_is_dark ? "toolbar_move_dark.svg" : "toolbar_move.svg");
            break;
        case(EType::Rotate):
            gizmo->set_icon_filename(m_is_dark ? "toolbar_rotate_dark.svg" : "toolbar_rotate.svg");
            break;
        case(EType::Scale):
            gizmo->set_icon_filename(m_is_dark ? "toolbar_scale_dark.svg" : "toolbar_scale.svg");
            break;
        case(EType::Flatten):
            gizmo->set_icon_filename(m_is_dark ? "toolbar_flatten_dark.svg" : "toolbar_flatten.svg");
            break;
        case(EType::Cut):
            gizmo->set_icon_filename(m_is_dark ? "toolbar_cut_dark.svg" : "toolbar_cut.svg");
            break;
        case(EType::FdmSupports):
            gizmo->set_icon_filename(m_is_dark ? "toolbar_support_dark.svg" : "toolbar_support.svg");
            break;
        case(EType::Seam):
            gizmo->set_icon_filename(m_is_dark ? "toolbar_seam_dark.svg" : "toolbar_seam.svg");
            break;
        case(EType::Text):
            gizmo->set_icon_filename(m_is_dark ? "toolbar_text_dark.svg" : "toolbar_text.svg");
            break;
        case(EType::MmuSegmentation):
            gizmo->set_icon_filename(m_is_dark ? "mmu_segmentation_dark.svg" : "mmu_segmentation.svg");
            break;
        case(EType::MeshBoolean):
            gizmo->set_icon_filename(m_is_dark ? "toolbar_meshboolean_dark.svg" : "toolbar_meshboolean.svg");
            break;
        case (EType::Measure):
            gizmo->set_icon_filename(m_is_dark ? "toolbar_measure_dark.svg" : "toolbar_measure.svg");
            break;
        case (EType::Assembly):
            gizmo->set_icon_filename(m_is_dark ? "toolbar_assembly_dark.svg" : "toolbar_assembly.svg");
            break;
        }

    }
}

bool GLGizmosManager::init()
{
    bool result = init_icon_textures();
    if (!result) return result;

    m_background_texture.metadata.filename = m_is_dark ? "toolbar_background_dark.png" : "toolbar_background.png";
    m_background_texture.metadata.left = 16;
    m_background_texture.metadata.top = 16;
    m_background_texture.metadata.right = 16;
    m_background_texture.metadata.bottom = 16;

    if (!m_background_texture.metadata.filename.empty())
    {
        if (!m_background_texture.texture.load_from_file(resources_dir() + "/images/" + m_background_texture.metadata.filename, false, GLTexture::SingleThreaded, false))
            return false;
    }

    // Order of gizmos in the vector must match order in EType!
    //BBS: GUI refactor: add obj manipulation
    m_gizmos.clear();
    unsigned int sprite_id = 0;
    m_gizmos.emplace_back(new GLGizmoMove3D(m_parent, m_is_dark ? "toolbar_move_dark.svg" : "toolbar_move.svg", EType::Move, &m_object_manipulation));
    m_gizmos.emplace_back(new GLGizmoRotate3D(m_parent, m_is_dark ? "toolbar_rotate_dark.svg" : "toolbar_rotate.svg", EType::Rotate, &m_object_manipulation));
    m_gizmos.emplace_back(new GLGizmoScale3D(m_parent, m_is_dark ? "toolbar_scale_dark.svg" : "toolbar_scale.svg", EType::Scale, &m_object_manipulation));
    m_gizmos.emplace_back(new GLGizmoFlatten(m_parent, m_is_dark ? "toolbar_flatten_dark.svg" : "toolbar_flatten.svg", EType::Flatten));
    m_gizmos.emplace_back(new GLGizmoAdvancedCut(m_parent, m_is_dark ? "toolbar_cut_dark.svg" : "toolbar_cut.svg", EType::Cut));
    m_gizmos.emplace_back(new GLGizmoMeshBoolean(m_parent, m_is_dark ? "toolbar_meshboolean_dark.svg" : "toolbar_meshboolean.svg", EType::MeshBoolean));
    m_gizmos.emplace_back(new GLGizmoFdmSupports(m_parent, m_is_dark ? "toolbar_support_dark.svg" : "toolbar_support.svg", EType::FdmSupports));
    m_gizmos.emplace_back(new GLGizmoSeam(m_parent, m_is_dark ? "toolbar_seam_dark.svg" : "toolbar_seam.svg", EType::Seam));
    m_gizmos.emplace_back(new GLGizmoText(m_parent, m_is_dark ? "toolbar_text_dark.svg" : "toolbar_text.svg", EType::Text));
    m_gizmos.emplace_back(new GLGizmoSVG(m_parent, EType::Svg));
    m_gizmos.emplace_back(new GLGizmoMmuSegmentation(m_parent, m_is_dark ? "mmu_segmentation_dark.svg" : "mmu_segmentation.svg", EType::MmuSegmentation));
    m_gizmos.emplace_back(new GLGizmoMeasure(m_parent, m_is_dark ? "toolbar_measure_dark.svg" : "toolbar_measure.svg", EType::Measure));
    m_gizmos.emplace_back(new GLGizmoAssembly(m_parent, m_is_dark ? "toolbar_assembly_dark.svg" : "toolbar_assembly.svg", EType::Assembly));
    m_gizmos.emplace_back(new GLGizmoSimplify(m_parent, "reduce_triangles.svg", EType::Simplify));
    m_gizmos.emplace_back(new GLGizmoBrimEars(m_parent, m_is_dark ? "toolbar_brimears_dark.svg" : "toolbar_brimears.svg", EType::BrimEars));
    //m_gizmos.emplace_back(new GLGizmoSlaSupports(m_parent, "sla_supports.svg", sprite_id++));
    //m_gizmos.emplace_back(new GLGizmoFaceDetector(m_parent, "face recognition.svg", sprite_id++));
    //m_gizmos.emplace_back(new GLGizmoHollow(m_parent, "hollow.svg", sprite_id++));

    m_common_gizmos_data.reset(new CommonGizmosDataPool(&m_parent));
    if(!m_assemble_view_data)
        m_assemble_view_data.reset(new AssembleViewDataPool(&m_parent));

    for (auto& gizmo : m_gizmos) {
        if (! gizmo->init()) {
            m_gizmos.clear();
            return false;
        }
        gizmo->set_common_data_pool(m_common_gizmos_data.get());
        gizmo->on_change_color_mode(m_is_dark);
    }

    m_current = Undefined;
    m_hover = Undefined;
    m_highlight = std::pair<EType, bool>(Undefined, false);

    return true;
}

bool GLGizmosManager::init_icon_textures()
{
    ImTextureID texture_id;

    icon_list.clear();
    if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/toolbar_reset.svg", 14, 14, texture_id))
        icon_list.insert(std::make_pair((int)IC_TOOLBAR_RESET, texture_id));
    else
        return false;

    if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/toolbar_reset_hover.svg", 14, 14, texture_id))
        icon_list.insert(std::make_pair((int)IC_TOOLBAR_RESET_HOVER, texture_id));
    else
        return false;

    if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/toolbar_reset_zero.svg", 14, 14, texture_id))
        icon_list.insert(std::make_pair((int) IC_TOOLBAR_RESET_ZERO, texture_id));
    else
        return false;

    if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/toolbar_reset_zero_hover.svg", 14, 14, texture_id))
        icon_list.insert(std::make_pair((int) IC_TOOLBAR_RESET_ZERO_HOVER, texture_id));
    else
        return false;

    if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/toolbar_tooltip.svg", 30, 22, texture_id))
        icon_list.insert(std::make_pair((int)IC_TOOLBAR_TOOLTIP, texture_id));
    else
        return false;

    if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/toolbar_tooltip_hover.svg", 30, 22, texture_id))
        icon_list.insert(std::make_pair((int)IC_TOOLBAR_TOOLTIP_HOVER, texture_id));
    else
        return false;


     if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/text_B.svg", 20, 20, texture_id))
        icon_list.insert(std::make_pair((int)IC_TEXT_B, texture_id));
    else
        return false;

     if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/text_B_dark.svg", 20, 20, texture_id))
         icon_list.insert(std::make_pair((int)IC_TEXT_B_DARK, texture_id));
     else
         return false;

     if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/text_T.svg", 20, 20, texture_id))
        icon_list.insert(std::make_pair((int)IC_TEXT_T, texture_id));
    else
        return false;

     if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/text_T_dark.svg", 20, 20, texture_id))
         icon_list.insert(std::make_pair((int)IC_TEXT_T_DARK, texture_id));
     else
         return false;

    return true;
}

float GLGizmosManager::get_layout_scale()
{
    return m_layout.scale;
}

bool GLGizmosManager::init_arrow(const BackgroundTexture::Metadata& arrow_texture)
{
    if (m_arrow_texture.texture.get_id() != 0)
        return true;

    std::string path = resources_dir() + "/images/";
    bool res = false;

    if (!arrow_texture.filename.empty())
        res = m_arrow_texture.texture.load_from_svg_file(path + arrow_texture.filename, false, false, false, 1000);
    if (res)
        m_arrow_texture.metadata = arrow_texture;

    return res;
}

void GLGizmosManager::set_overlay_icon_size(float size)
{
    if (m_layout.icons_size != size)
    {
        m_layout.icons_size = size;
        m_icons_texture_dirty = true;
    }
}

void GLGizmosManager::set_overlay_scale(float scale)
{
    if (m_layout.scale != scale)
    {
        m_layout.scale = scale;
        m_icons_texture_dirty = true;
    }
}

void GLGizmosManager::refresh_on_off_state()
{
    if (m_serializing || m_current == Undefined || m_gizmos.empty())
        return;

    if (m_current != Undefined
    && ! m_gizmos[m_current]->is_activable() && activate_gizmo(Undefined))
        update_data();
}

void GLGizmosManager::reset_all_states()
{
    if (! m_enabled || m_serializing)
        return;

    const EType current = get_current_type();
    if (current != Undefined)
        // close any open gizmo
        open_gizmo(current);

    activate_gizmo(Undefined);
    m_hover = Undefined;
}

bool GLGizmosManager::open_gizmo(EType type)
{
    int idx = int(type);
    if (m_gizmos[idx]->is_activable()
     && activate_gizmo(m_current == idx ? Undefined : (EType)idx)) {
        update_data();
#ifdef __WXOSX__
        m_parent.post_event(SimpleEvent(wxEVT_PAINT));
#endif
        return true;
    }
    return false;
}

bool GLGizmosManager::open_gizmo(unsigned char type)
{
    return open_gizmo((EType)type);
}

bool GLGizmosManager::check_gizmos_closed_except(EType type) const
{
    if (get_current_type() != type && get_current_type() != Undefined) {
        wxGetApp().plater()->get_notification_manager()->push_notification(
                    NotificationType::CustomSupportsAndSeamRemovedAfterRepair,
                    NotificationManager::NotificationLevel::PrintInfoNotificationLevel,
                    _u8L("Error: Please close all toolbar menus first"));
        return false;
    }
    return true;
}

void GLGizmosManager::set_hover_id(int id)
{
    if (m_current == EType::Measure || m_current == EType::Assembly) { return; }
    if (!m_enabled || m_current == Undefined)
        return;

    m_gizmos[m_current]->set_hover_id(id);
}

void GLGizmosManager::enable_grabber(EType type, unsigned int id, bool enable)
{
    if (!m_enabled || type == Undefined || m_gizmos.empty())
        return;

    if (enable)
        m_gizmos[type]->enable_grabber(id);
    else
        m_gizmos[type]->disable_grabber(id);
}

void GLGizmosManager::update(const Linef3& mouse_ray, const Point& mouse_pos)
{
    if (!m_enabled)
        return;

    GLGizmoBase* curr = get_current();
    if (curr != nullptr)
        curr->update(GLGizmoBase::UpdateData(mouse_ray, mouse_pos));
}

void GLGizmosManager::update_assemble_view_data()
{
    if (m_assemble_view_data) {
        if (!wxGetApp().plater()->get_assmeble_canvas3D()->get_wxglcanvas()->IsShown())
            m_assemble_view_data->update(AssembleViewDataID(0));
        else
            m_assemble_view_data->update(AssembleViewDataID((int)AssembleViewDataID::ModelObjectsInfo | (int)AssembleViewDataID::ModelObjectsClipper));
    }
}

void GLGizmosManager::update_data()
{
    if (!m_enabled)
        return;

    const Selection& selection = m_parent.get_selection();

    bool is_wipe_tower = selection.is_wipe_tower();
    enable_grabber(Move, 2, !is_wipe_tower);
    enable_grabber(Rotate, 0, !is_wipe_tower);
    enable_grabber(Rotate, 1, !is_wipe_tower);

    // BBS: when select multiple objects, uniform scale can be deselected, display the 0-5 grabbers
    //bool enable_scale_xyz = selection.is_single_full_instance() || selection.is_single_volume() || selection.is_single_modifier();
    //for (unsigned int i = 0; i < 6; ++i)
    //{
    //    enable_grabber(Scale, i, enable_scale_xyz);
    //}

    if (m_common_gizmos_data) {
        m_common_gizmos_data->update(get_current()
            ? get_current()->get_requirements()
            : CommonGizmosDataID(0));
    }
    if (m_current != Undefined)
        m_gizmos[m_current]->data_changed(m_serializing);

    if (selection.is_single_full_instance())
    {
        // all volumes in the selection belongs to the same instance, any of them contains the needed data, so we take the first
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        set_scale(volume->get_instance_scaling_factor());
        set_rotation(Vec3d::Zero());
        // BBS
        finish_cut_rotation();
        ModelObject* model_object = selection.get_model()->objects[selection.get_object_idx()];
        set_flattening_data(model_object);
        set_sla_support_data(model_object);
        set_brim_data(model_object);
        set_painter_gizmo_data();
    }
    else if (selection.is_single_volume() || selection.is_single_modifier())
    {
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        set_scale(volume->get_volume_scaling_factor());
        set_rotation(Vec3d::Zero());
        // BBS
        finish_cut_rotation();
        set_flattening_data(nullptr);
        set_sla_support_data(nullptr);
        set_brim_data(nullptr);
        set_painter_gizmo_data();
    }
    else if (is_wipe_tower)
    {
        DynamicPrintConfig& proj_cfg = wxGetApp().preset_bundle->project_config;
        set_scale(Vec3d::Ones());
        set_rotation(Vec3d(0., 0., (M_PI/180.) * dynamic_cast<const ConfigOptionFloat*>(proj_cfg.option("wipe_tower_rotation_angle"))->value));
        set_flattening_data(nullptr);
        set_sla_support_data(nullptr);
        set_brim_data(nullptr);
        set_painter_gizmo_data();
    }
    else
    {
        set_scale(Vec3d::Ones());
        set_rotation(Vec3d::Zero());
        set_flattening_data(selection.is_from_single_object() ? selection.get_model()->objects[selection.get_object_idx()] : nullptr);
        set_sla_support_data(selection.is_from_single_instance() ? selection.get_model()->objects[selection.get_object_idx()] : nullptr);
        set_brim_data(selection.is_from_single_instance() ? selection.get_model()->objects[selection.get_object_idx()] : nullptr);
        set_painter_gizmo_data();
    }

    //BBS: GUI refactor: add object manipulation in gizmo
    if (!selection.is_empty()) {
        m_object_manipulation.update_ui_from_settings();
        m_object_manipulation.UpdateAndShow(true);
    }
}

bool GLGizmosManager::is_running() const
{
    if (!m_enabled)
        return false;

    //GLGizmoBase* curr = get_current();
    //return (curr != nullptr) ? (curr->get_state() == GLGizmoBase::On) : false;
    return m_current != Undefined;
}

bool GLGizmosManager::handle_shortcut(int key)
{
    if (!m_enabled || m_parent.get_selection().is_empty())
        return false;

    auto it = std::find_if(m_gizmos.begin(), m_gizmos.end(),
            [key](const std::unique_ptr<GLGizmoBase>& gizmo) {
                int gizmo_key = gizmo->get_shortcut_key();
                return gizmo->is_activable()
                       && ((gizmo_key == key - 64) || (gizmo_key == key - 96));
    });

    if (it == m_gizmos.end())
        return false;

    EType gizmo_type = EType(it - m_gizmos.begin());
    return open_gizmo(gizmo_type);
}

bool GLGizmosManager::is_dragging() const
{
    if (! m_enabled || m_current == Undefined)
        return false;

    return m_gizmos[m_current]->is_dragging();
}

void GLGizmosManager::start_dragging()
{
    if (! m_enabled || m_current == Undefined)
        return;
    m_gizmos[m_current]->start_dragging();
}

void GLGizmosManager::stop_dragging()
{
    if (! m_enabled || m_current == Undefined)
        return;

    m_gizmos[m_current]->stop_dragging();
}

Vec3d GLGizmosManager::get_displacement() const
{
    if (!m_enabled)
        return Vec3d::Zero();

    return dynamic_cast<GLGizmoMove3D*>(m_gizmos[Move].get())->get_displacement();
}

Vec3d GLGizmosManager::get_scale() const
{
    if (!m_enabled)
        return Vec3d::Ones();

    return dynamic_cast<GLGizmoScale3D*>(m_gizmos[Scale].get())->get_scale();
}

void GLGizmosManager::set_scale(const Vec3d& scale)
{
    if (!m_enabled || m_gizmos.empty())
        return;

    dynamic_cast<GLGizmoScale3D*>(m_gizmos[Scale].get())->set_scale(scale);
}

Vec3d GLGizmosManager::get_scale_offset() const
{
    if (!m_enabled || m_gizmos.empty())
        return Vec3d::Zero();

    return dynamic_cast<GLGizmoScale3D*>(m_gizmos[Scale].get())->get_offset();
}

Vec3d GLGizmosManager::get_rotation() const
{
    if (!m_enabled || m_gizmos.empty())
        return Vec3d::Zero();

    return dynamic_cast<GLGizmoRotate3D*>(m_gizmos[Rotate].get())->get_rotation();
}

void GLGizmosManager::set_rotation(const Vec3d& rotation)
{
    if (!m_enabled || m_gizmos.empty())
        return;
    dynamic_cast<GLGizmoRotate3D*>(m_gizmos[Rotate].get())->set_rotation(rotation);
}

// BBS
void GLGizmosManager::finish_cut_rotation()
{
    dynamic_cast<GLGizmoAdvancedCut*>(m_gizmos[Cut].get())->finish_rotation();
}

void GLGizmosManager::update_paint_base_camera_rotate_rad()
{
    if (m_current == MmuSegmentation || m_current == Seam) {
        auto paint_gizmo = dynamic_cast<GLGizmoPainterBase*>(m_gizmos[m_current].get());
        paint_gizmo->update_front_view_radian();
    }
}

Vec3d GLGizmosManager::get_flattening_normal() const
{
    if (!m_enabled || m_gizmos.empty())
        return Vec3d::Zero();

    return dynamic_cast<GLGizmoFlatten*>(m_gizmos[Flatten].get())->get_flattening_normal();
}

void GLGizmosManager::set_flattening_data(const ModelObject* model_object)
{
    if (!m_enabled || m_gizmos.empty())
        return;

    dynamic_cast<GLGizmoFlatten*>(m_gizmos[Flatten].get())->set_flattening_data(model_object);
}

void GLGizmosManager::set_sla_support_data(ModelObject* model_object)
{
    if (! m_enabled
     || m_gizmos.empty()
     || wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptSLA)
        return;

    auto* gizmo_hollow = dynamic_cast<GLGizmoHollow*>(m_gizmos[Hollow].get());
    auto* gizmo_supports = dynamic_cast<GLGizmoSlaSupports*>(m_gizmos[SlaSupports].get());
    gizmo_hollow->set_sla_support_data(model_object, m_parent.get_selection());
    gizmo_supports->set_sla_support_data(model_object, m_parent.get_selection());
}

void GLGizmosManager::set_brim_data(ModelObject* model_object)
{
    if (!m_enabled || m_gizmos.empty())
        return;
    auto* gizmo_brim = dynamic_cast<GLGizmoBrimEars*>(m_gizmos[BrimEars].get());
    gizmo_brim->set_brim_data(model_object, m_parent.get_selection());
}

void GLGizmosManager::set_painter_gizmo_data()
{
    if (!m_enabled || m_gizmos.empty())
        return;

    dynamic_cast<GLGizmoFdmSupports*>(m_gizmos[FdmSupports].get())->set_painter_gizmo_data(m_parent.get_selection());
    dynamic_cast<GLGizmoSeam*>(m_gizmos[Seam].get())->set_painter_gizmo_data(m_parent.get_selection());
    dynamic_cast<GLGizmoMmuSegmentation*>(m_gizmos[MmuSegmentation].get())->set_painter_gizmo_data(m_parent.get_selection());
}

bool GLGizmosManager::is_gizmo_activable_when_single_full_instance() {
    if (get_current_type() == GLGizmosManager::EType::Flatten ||
        get_current_type() == GLGizmosManager::EType::Cut ||
        get_current_type() == GLGizmosManager::EType::MeshBoolean ||
        get_current_type() == GLGizmosManager::EType::Text ||
        get_current_type() == GLGizmosManager::EType::Seam ||
        get_current_type() == GLGizmosManager::EType::FdmSupports ||
        get_current_type() == GLGizmosManager::EType::MmuSegmentation ||
        get_current_type() == GLGizmosManager::EType::Simplify
        ) {
        return true;
    }
    return false;
}

bool GLGizmosManager::is_gizmo_click_empty_not_exit()
{
   if (get_current_type() == GLGizmosManager::EType::Cut ||
       get_current_type() == GLGizmosManager::EType::MeshBoolean ||
       get_current_type() == GLGizmosManager::EType::Seam ||
       get_current_type() == GLGizmosManager::EType::FdmSupports ||
       get_current_type() == GLGizmosManager::EType::MmuSegmentation ||
       get_current_type() == GLGizmosManager::EType::Measure ||
       get_current_type() == GLGizmosManager::EType::Assembly) {
        return true;
    }
    return false;
}

bool GLGizmosManager::is_show_only_active_plate()
{
    if (get_current_type() == GLGizmosManager::EType::Cut ||
        get_current_type() == GLGizmosManager::EType::Text) {
        return true;
    }
    return false;
}

bool GLGizmosManager::is_ban_move_glvolume()
{
    auto current_type = get_current_type();
    if (current_type == GLGizmosManager::EType::Undefined ||
        current_type == GLGizmosManager::EType::Move ||
        current_type == GLGizmosManager::EType::Rotate ||
        current_type == GLGizmosManager::EType::Scale) {
        return false;
    }
    return true;
}

bool GLGizmosManager::get_gizmo_active_condition(GLGizmosManager::EType type) {
    if (auto cur_gizmo = get_gizmo(type)) {
        return cur_gizmo->is_activable();
    }
    return false;
}

void GLGizmosManager::check_object_located_outside_plate(bool change_plate)
{
    PartPlateList &plate_list       = wxGetApp().plater()->get_partplate_list();
    auto           curr_plate_index = plate_list.get_curr_plate_index();
    Selection &    selection        = m_parent.get_selection();
    auto           idxs             = selection.get_volume_idxs();
    m_object_located_outside_plate  = false;
    if (idxs.size() > 0) {
        const GLVolume *v          = selection.get_volume(*idxs.begin());
        int             object_idx = v->object_idx();
        const Model *   m_model    = m_parent.get_model();
        if (0 <= object_idx && object_idx < (int) m_model->objects.size()) {
            bool         find_object  = false;
            ModelObject *model_object = m_model->objects[object_idx];
            for (size_t i = 0; i < plate_list.get_plate_count(); i++) {
                auto            plate   = plate_list.get_plate(i);
                ModelObjectPtrs objects = plate->get_objects_on_this_plate();
                for (auto object : objects) {
                    if (model_object == object) {
                        if (change_plate && curr_plate_index != i) { // confirm selected model_object at corresponding plate
                            wxGetApp().plater()->get_partplate_list().select_plate(i);
                        }
                        find_object = true;
                    }
                }
            }
            if (!find_object) {
                m_object_located_outside_plate = true;
            }
        }
    }
}

// Returns true if the gizmo used the event to do something, false otherwise.
bool GLGizmosManager::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    if (!m_enabled || m_gizmos.empty())
        return false;

    if (m_current == SlaSupports)
        return dynamic_cast<GLGizmoSlaSupports*>(m_gizmos[SlaSupports].get())->gizmo_event(action, mouse_position, shift_down, alt_down, control_down);
    else if (m_current == Hollow)
        return dynamic_cast<GLGizmoHollow*>(m_gizmos[Hollow].get())->gizmo_event(action, mouse_position, shift_down, alt_down, control_down);
    else if (m_current == FdmSupports)
        return dynamic_cast<GLGizmoFdmSupports*>(m_gizmos[FdmSupports].get())->gizmo_event(action, mouse_position, shift_down, alt_down, control_down);
    else if (m_current == Seam)
        return dynamic_cast<GLGizmoSeam*>(m_gizmos[Seam].get())->gizmo_event(action, mouse_position, shift_down, alt_down, control_down);
    else if (m_current == MmuSegmentation)
        return dynamic_cast<GLGizmoMmuSegmentation*>(m_gizmos[MmuSegmentation].get())->gizmo_event(action, mouse_position, shift_down, alt_down, control_down);
    else if (m_current == Text)
        return dynamic_cast<GLGizmoText*>(m_gizmos[Text].get())->gizmo_event(action, mouse_position, shift_down, alt_down, control_down);
    else if (m_current == Svg)
        return dynamic_cast<GLGizmoSVG*>(m_gizmos[Svg].get())->gizmo_event(action, mouse_position, shift_down, alt_down, control_down);
    else if (m_current == Measure)
        return dynamic_cast<GLGizmoMeasure *>(m_gizmos[Measure].get())->gizmo_event(action, mouse_position, shift_down, alt_down, control_down);
    else if (m_current == Assembly)
        return dynamic_cast<GLGizmoAssembly *>(m_gizmos[Assembly].get())->gizmo_event(action, mouse_position, shift_down, alt_down, control_down);
    else if (m_current == Cut)
        return dynamic_cast<GLGizmoAdvancedCut *>(m_gizmos[Cut].get())->gizmo_event(action, mouse_position, shift_down, alt_down, control_down);
    else if (m_current == MeshBoolean)
        return dynamic_cast<GLGizmoMeshBoolean*>(m_gizmos[MeshBoolean].get())->gizmo_event(action, mouse_position, shift_down, alt_down, control_down);
    else if (m_current == BrimEars)
        return dynamic_cast<GLGizmoBrimEars*>(m_gizmos[BrimEars].get())->gizmo_event(action, mouse_position, shift_down, alt_down, control_down);
    else
        return false;
}

bool GLGizmosManager::is_paint_gizmo()
{
    return m_current == EType::FdmSupports ||
           m_current == EType::MmuSegmentation ||
           m_current == EType::Seam;
}

bool GLGizmosManager::is_allow_select_all() {
    if (m_current == Undefined || m_current == EType::Move||
        m_current == EType::Rotate ||
        m_current == EType::Scale) {
        return true;
    }
    return false;
}

ClippingPlane GLGizmosManager::get_clipping_plane() const
{
    if (! m_common_gizmos_data
     || ! m_common_gizmos_data->object_clipper()
     || m_common_gizmos_data->object_clipper()->get_position() == 0.)
        return ClippingPlane::ClipsNothing();
    else {
        const ClippingPlane& clp = *m_common_gizmos_data->object_clipper()->get_clipping_plane();
        return ClippingPlane(-clp.get_normal(), clp.get_data()[3]);
    }
}

ClippingPlane GLGizmosManager::get_assemble_view_clipping_plane() const
{
    if (!m_assemble_view_data
        || !m_assemble_view_data->model_objects_clipper()
        || m_assemble_view_data->model_objects_clipper()->get_position() == 0.)
        return ClippingPlane::ClipsNothing();
    else {
        const ClippingPlane& clp = *m_assemble_view_data->model_objects_clipper()->get_clipping_plane();
        return ClippingPlane(-clp.get_normal(), clp.get_data()[3]);
    }
}

bool GLGizmosManager::wants_reslice_supports_on_undo() const
{
    return (m_current == SlaSupports
        && dynamic_cast<const GLGizmoSlaSupports*>(m_gizmos.at(SlaSupports).get())->has_backend_supports());
}

void GLGizmosManager::on_change_color_mode(bool is_dark) {
    m_is_dark = is_dark;
}

void GLGizmosManager::render_current_gizmo() const
{
    if (!m_enabled || m_current == Undefined)
        return;

    m_gizmos[m_current]->render();
}

void GLGizmosManager::render_painter_gizmo() const
{
    // This function shall only be called when current gizmo is
    // derived from GLGizmoPainterBase.

    if (!m_enabled || m_current == Undefined)
        return;

    auto *gizmo = dynamic_cast<GLGizmoPainterBase*>(get_current());
    assert(gizmo); // check the precondition
    gizmo->render_painter_gizmo();
}

void GLGizmosManager::render_painter_assemble_view() const
{
    if (m_assemble_view_data && m_assemble_view_data->model_objects_clipper())
        m_assemble_view_data->model_objects_clipper()->render_cut();
}

void GLGizmosManager::render_current_gizmo_for_picking_pass() const
{
    if (! m_enabled || m_current == Undefined)

        return;

    m_gizmos[m_current]->render_for_picking();
}

void GLGizmosManager::render_overlay()
{
    if (!m_enabled)
        return;

    if (m_icons_texture_dirty)
        generate_icons_texture();

    do_render_overlay();
}

std::string GLGizmosManager::get_tooltip() const
{
    if (!m_tooltip.empty())
        return m_tooltip;

    const GLGizmoBase* curr = get_current();
    return (curr != nullptr) ? curr->get_tooltip() : "";
}

bool GLGizmosManager::on_mouse_wheel(wxMouseEvent& evt)
{
    bool processed = false;

    if (m_current == SlaSupports || m_current == Hollow || m_current == FdmSupports || m_current == Seam || m_current == MmuSegmentation || m_current == BrimEars) {
        float rot = (float)evt.GetWheelRotation() / (float)evt.GetWheelDelta();
        if (gizmo_event((rot > 0.f ? SLAGizmoEventType::MouseWheelUp : SLAGizmoEventType::MouseWheelDown), Vec2d::Zero(), evt.ShiftDown(), evt.AltDown()
            // BBS
#ifdef __WXOSX_MAC__
            , evt.RawControlDown()
#else
            , evt.ControlDown()
#endif
            ))
            processed = true;
    }

    return processed;
}

bool GLGizmosManager::on_mouse(wxMouseEvent& evt)
{
    // used to set a right up event as processed when needed
    static bool pending_right_up = false;

    Point pos(evt.GetX(), evt.GetY());
    Vec2d mouse_pos((double)evt.GetX(), (double)evt.GetY());

    Selection& selection = m_parent.get_selection();
    int selected_object_idx = selection.get_object_idx();
    bool processed = false;

    // when control is down we allow scene pan and rotation even when clicking over some object
    bool control_down = evt.CmdDown();
    if (m_current != Undefined) {
        // check if gizmo override method could be slower than simple call virtual function
        // &m_gizmos[m_current]->on_mouse != &GLGizmoBase::on_mouse &&
        m_gizmos[m_current]->on_mouse(evt);
    }
    // mouse anywhere
    if (evt.Moving()) {
        m_tooltip = update_hover_state(mouse_pos);
        if (m_current == MmuSegmentation || m_current == FdmSupports || m_current == Text || m_current == BrimEars || m_current == Svg)
            // BBS
            gizmo_event(SLAGizmoEventType::Moving, mouse_pos, evt.ShiftDown(), evt.AltDown(), evt.ControlDown());
    } else if (evt.LeftUp()) {
        if (m_mouse_capture.left) {
            processed = true;
            m_mouse_capture.left = false;
        }
        else if (is_dragging()) {
            switch (m_current) {
            case Move:   {
                wxGetApp().plater()->take_snapshot(_u8L("Tool-Move"), UndoRedo::SnapshotType::GizmoAction);
                m_parent.do_move("");
                break;
            }
            case Scale:  {
                wxGetApp().plater()->take_snapshot(_u8L("Tool-Scale"), UndoRedo::SnapshotType::GizmoAction);
                m_parent.do_scale("");
                break;
            }
            case Rotate: {
                wxGetApp().plater()->take_snapshot(_u8L("Tool-Rotate"), UndoRedo::SnapshotType::GizmoAction);
                m_parent.do_rotate("");
                break;
            }
            default: break;
            }

            stop_dragging();
            update_data();

            // BBS
            //wxGetApp().obj_manipul()->set_dirty();
            // Let the plater know that the dragging finished, so a delayed refresh
            // of the scene with the background processing data should be performed.
            m_parent.post_event(SimpleEvent(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED));
            // updates camera target constraints
            m_parent.refresh_camera_scene_box();

            processed = true;
        }
//        else
//            return false;
    }
    else if (evt.MiddleUp()) {
        if (m_mouse_capture.middle) {
            processed = true;
            m_mouse_capture.middle = false;
        }
        else
            return false;
    }
    else if (evt.RightUp()) {
        if (pending_right_up) {
            pending_right_up = false;
            return true;
        }
        if (m_mouse_capture.right) {
            processed = true;
            m_mouse_capture.right = false;
        }
//        else
//            return false;
    }
    else if (evt.Dragging() && !is_dragging()) {
        if (m_mouse_capture.any())
            // if the button down was done on this toolbar, prevent from dragging into the scene
            processed = true;
//        else
//            return false;
    }
    else if (evt.Dragging() && is_dragging()) {
        if (!m_parent.get_wxglcanvas()->HasCapture())
            m_parent.get_wxglcanvas()->CaptureMouse();

        m_parent.set_mouse_as_dragging();
        update(m_parent.mouse_ray(pos), pos);

        switch (m_current)
        {
        case Move:
        {
            // Apply new temporary offset
            TransformationType trafo_type;
            trafo_type.set_relative();
            switch (wxGetApp().obj_manipul()->get_coordinates_type()) {
                case ECoordinatesType::Instance: {
                    trafo_type.set_instance();
                    break;
                }
                case ECoordinatesType::Local: {
                    trafo_type.set_local();
                    break;
                }
                default: {
                    break;
                }
            }
            selection.translate(get_displacement(), trafo_type);
            // BBS
            //wxGetApp().obj_manipul()->set_dirty();
            break;
        }
        case Scale: {
            // Apply new temporary scale factors
            TransformationType transformation_type;
            if (wxGetApp().obj_manipul()->is_local_coordinates()) {
                transformation_type.set_local();
            } else if (wxGetApp().obj_manipul()->is_instance_coordinates())
                transformation_type.set_instance();
            transformation_type.set_relative();

            if (evt.AltDown())
                 transformation_type.set_independent();
            selection.scale_and_translate(get_scale(), get_scale_offset(), transformation_type);

            // BBS
            //wxGetApp().obj_manipul()->set_dirty();
            break;
        }
        case Rotate:
        {
            // Apply new temporary rotations
            TransformationType transformation_type;
            if (m_parent.get_selection().is_wipe_tower())
                transformation_type = TransformationType::World_Relative_Joint;
            else {
                switch (wxGetApp().obj_manipul()->get_coordinates_type()) {
                default:
                case ECoordinatesType::World: {
                    transformation_type = TransformationType::World_Relative_Joint;
                    break;
                }
                case ECoordinatesType::Instance: {
                    transformation_type = TransformationType::Instance_Relative_Joint;
                    break;
                }
                case ECoordinatesType::Local: {
                    transformation_type = TransformationType::Local_Relative_Joint;
                    break;
                }
                }
            }
            if (evt.AltDown())
                transformation_type.set_independent();
            selection.rotate(get_rotation(), transformation_type);
            // BBS
            //wxGetApp().obj_manipul()->set_dirty();
            break;
        }
        default:
            break;
        }

        m_parent.set_as_dirty();
        processed = true;
    }

    if (get_gizmo_idx_from_mouse(mouse_pos) == Undefined) {
        // mouse is outside the toolbar
        m_tooltip.clear();

        if (evt.LeftDown() && (!control_down || grabber_contains_mouse())) {
            if ((m_current == SlaSupports || m_current == Hollow || m_current == FdmSupports || m_current == Svg ||
                m_current == Seam || m_current == MmuSegmentation || m_current == Text || m_current == Cut || m_current == MeshBoolean || m_current == BrimEars)
                && gizmo_event(SLAGizmoEventType::LeftDown, mouse_pos, evt.ShiftDown(), evt.AltDown()))
                // the gizmo got the event and took some action, there is no need to do anything more
                processed = true;
            else if (!selection.is_empty() && grabber_contains_mouse()) {
                if (!(m_current == Measure || m_current == Assembly)) {
                    update_data();
                    selection.start_dragging();
                    start_dragging();

                    // Let the plater know that the dragging started
                    m_parent.post_event(SimpleEvent(EVT_GLCANVAS_MOUSE_DRAGGING_STARTED));

                    if (m_current == Flatten) {
                        // Rotate the object so the normal points downward:
                        m_parent.do_flatten(get_flattening_normal(), L("Tool-Lay on Face"));
                        // BBS
                        // wxGetApp().obj_manipul()->set_dirty();
                    }

                    m_parent.set_as_dirty();
                }
                processed = true;
            }
        }
        else if (evt.RightDown() && selected_object_idx != -1 && (m_current == SlaSupports || m_current == Hollow || m_current == BrimEars)
            && gizmo_event(SLAGizmoEventType::RightDown, mouse_pos)) {
            // we need to set the following right up as processed to avoid showing the context menu if the user release the mouse over the object
            pending_right_up = true;
            // event was taken care of by the SlaSupports gizmo
            processed = true;
        }
        else if (evt.RightDown() && !control_down && selected_object_idx != -1
            && (m_current == FdmSupports || m_current == Seam || m_current == MmuSegmentation || m_current == Cut)
            && gizmo_event(SLAGizmoEventType::RightDown, mouse_pos)) {
            // event was taken care of by the FdmSupports / Seam / MMUPainting gizmo
            processed = true;
        }
        else if (evt.Dragging() && m_parent.get_move_volume_id() != -1
            && (m_current == SlaSupports || m_current == Hollow || m_current == FdmSupports || m_current == Seam || m_current == MmuSegmentation || m_current == BrimEars))
            // don't allow dragging objects with the Sla gizmo on
            processed = true;
        else if (evt.Dragging() && !control_down
            && (m_current == SlaSupports || m_current == Hollow || m_current == FdmSupports || m_current == Seam  || m_current == MmuSegmentation || m_current == Cut || m_current == BrimEars)
            && gizmo_event(SLAGizmoEventType::Dragging, mouse_pos, evt.ShiftDown(), evt.AltDown())) {
            // the gizmo got the event and took some action, no need to do anything more here
            m_parent.set_as_dirty();
            processed = true;
        }
        else if (evt.Dragging() && control_down && (evt.LeftIsDown() || evt.RightIsDown())) {
            // CTRL has been pressed while already dragging -> stop current action
            if (evt.LeftIsDown())
                gizmo_event(SLAGizmoEventType::LeftUp, mouse_pos, evt.ShiftDown(), evt.AltDown(), true);
            else if (evt.RightIsDown())
                gizmo_event(SLAGizmoEventType::RightUp, mouse_pos, evt.ShiftDown(), evt.AltDown(), true);
        }
        else if (evt.LeftUp()
            && (m_current == SlaSupports || m_current == Hollow || m_current == FdmSupports || m_current == Seam || m_current == MmuSegmentation || m_current == Cut || m_current == BrimEars)
            && gizmo_event(SLAGizmoEventType::LeftUp, mouse_pos, evt.ShiftDown(), evt.AltDown(), control_down)
            && !m_parent.is_mouse_dragging()) {
            // in case SLA/FDM gizmo is selected, we just pass the LeftUp event and stop processing - neither
            // object moving or selecting is suppressed in that case
            processed = true;
        } else if (evt.LeftUp() && m_current == Svg && m_gizmos[m_current]->get_hover_id() != -1) {
            // BBS
            // wxGetApp().obj_manipul()->set_dirty();
            processed = true;
        }
        else if (evt.LeftUp() && m_current == Flatten && m_gizmos[m_current]->get_hover_id() != -1) {
            // to avoid to loose the selection when user clicks an the white faces of a different object while the Flatten gizmo is active
            selection.stop_dragging();
            // BBS
            //wxGetApp().obj_manipul()->set_dirty();
            processed = true;
        }
        else if (evt.RightUp() && m_current != EType::Undefined && !m_parent.is_mouse_dragging() ) {
            gizmo_event(SLAGizmoEventType::RightUp, mouse_pos, evt.ShiftDown(), evt.AltDown(), control_down);
            processed = true;
        }
        else if (evt.LeftUp()) {
            selection.stop_dragging();
            // BBS
            //wxGetApp().obj_manipul()->set_dirty();
        }
    }
    else {
        // mouse inside toolbar
        if (evt.LeftDown() || evt.LeftDClick()) {
            m_mouse_capture.left = true;
            m_mouse_capture.parent = &m_parent;
            processed = true;
            if (!selection.is_empty()) {
                update_on_off_state(mouse_pos);
                update_data();
                m_parent.set_as_dirty();
                try {
                    if ((int)m_hover >= 0 && (int)m_hover < m_gizmos.size()) {
                        std::string name = get_name_from_gizmo_etype(m_hover);
                        int count = m_gizmos[m_hover]->get_count();
                        NetworkAgent* agent = GUI::wxGetApp().getAgent();
                        if (agent) {
                            agent->track_update_property(name, std::to_string(count));
                        }
                    }
                }
                catch (...) {}
            }
        }
        else if (evt.MiddleDown()) {
            m_mouse_capture.middle = true;
            m_mouse_capture.parent = &m_parent;
        }
        else if (evt.RightDown()) {
            m_mouse_capture.right = true;
            m_mouse_capture.parent = &m_parent;
            return true;
        }
    }

    return processed;
}

bool GLGizmosManager::on_char(wxKeyEvent& evt)
{
    // see include/wx/defs.h enum wxKeyCode
    int keyCode = evt.GetKeyCode();
    int ctrlMask = wxMOD_CONTROL;

    bool processed = false;

    if ((evt.GetModifiers() & ctrlMask) != 0)
    {
        switch (keyCode)
        {
#ifdef __APPLE__
        case 'a':
        case 'A':
#else /* __APPLE__ */
        case WXK_CONTROL_A:
#endif /* __APPLE__ */
        {
            //// Sla gizmo selects all support points
            //if ((m_current == SlaSupports || m_current == Hollow) && gizmo_event(SLAGizmoEventType::SelectAll))
            //    processed = true;

            break;
        }
        }
    }
    else if (!evt.HasModifiers())
    {
        switch (keyCode)
        {
        // key ESC
        case WXK_ESCAPE:
        {
            if (m_current != Undefined) {
                if ((m_current == Measure || m_current == Assembly) && gizmo_event(SLAGizmoEventType::Escape)) {
                    // do nothing
                } else if ((m_current != SlaSupports && m_current != BrimEars) || !gizmo_event(SLAGizmoEventType::DiscardChanges))
                    reset_all_states();

                processed = true;
            }
            break;
        }
        //skip some keys when gizmo
        case 'A':
        case 'a':
        {
            if (is_running()) {
                processed = true;
            }
            break;
        }
        //case WXK_RETURN:
        //{
        //    if ((m_current == SlaSupports) && gizmo_event(SLAGizmoEventType::ApplyChanges))
        //        processed = true;

        //    break;
        //}

        //case 'r' :
        //case 'R' :
        //{
            //if ((m_current == SlaSupports || m_current == Hollow || m_current == FdmSupports || m_current == Seam || m_current == MmuSegmentation) && gizmo_event(SLAGizmoEventType::ResetClippingPlane))
            //    processed = true;

            //break;
        //}


        //case WXK_BACK:
        case WXK_DELETE: {
            if ((m_current == Cut || m_current == Measure || m_current == Assembly || m_current == BrimEars) && gizmo_event(SLAGizmoEventType::Delete))
                processed = true;
            break;
        }
        //case 'A':
        //case 'a':
        //{
        //    if (m_current == SlaSupports)
        //    {
        //        gizmo_event(SLAGizmoEventType::AutomaticGeneration);
        //        // set as processed no matter what's returned by gizmo_event() to avoid the calling canvas to process 'A' as arrange
        //        processed = true;
        //    }
        //    break;
        //}
        //case 'M':
        //case 'm':
        //{
        //    if ((m_current == SlaSupports) && gizmo_event(SLAGizmoEventType::ManualEditing))
        //        processed = true;

        //    break;
        //}
        //case 'F':
        //case 'f':
        //{
           /* if (m_current == Scale)
            {
                if (!is_dragging())
                    wxGetApp().plater()->scale_selection_to_fit_print_volume();

                processed = true;
            }*/

            //break;
        //}
        // BBS: Skip all keys when in gizmo. This is necessary for 3D text tool.
        default:
        {
            //if (is_running() && m_current == EType::Text) {
            //    processed = true;
            //}
            break;
        }
        }
    }

    if (!processed && !evt.HasModifiers())
    {
        if (handle_shortcut(keyCode))
            processed = true;
    }

    if (processed)
        m_parent.set_as_dirty();

    return processed;
}

bool GLGizmosManager::on_key(wxKeyEvent& evt)
{
    int keyCode = evt.GetKeyCode();
    bool processed = false;

    // todo: zhimin Each gizmo should handle key event in it own on_key() function
    if (m_current == Cut) {
        if (GLGizmoAdvancedCut *gizmo_cut = dynamic_cast<GLGizmoAdvancedCut *>(get_current())) {
            return gizmo_cut->on_key(evt);
        }
    }

    if (evt.GetEventType() == wxEVT_KEY_UP)
    {
        if (m_current == SlaSupports || m_current == Hollow || m_current == BrimEars)
        {
            bool is_editing = true;
            bool is_rectangle_dragging = false;

            if (m_current == SlaSupports) {
                GLGizmoSlaSupports* gizmo = dynamic_cast<GLGizmoSlaSupports*>(get_current());
                is_editing = gizmo->is_in_editing_mode();
                is_rectangle_dragging = gizmo->is_selection_rectangle_dragging();
            } else if (m_current == BrimEars) {
                GLGizmoBrimEars* gizmo = dynamic_cast<GLGizmoBrimEars*>(get_current());
                is_rectangle_dragging = gizmo->is_selection_rectangle_dragging();
            }
            else {
                GLGizmoHollow* gizmo = dynamic_cast<GLGizmoHollow*>(get_current());
                is_rectangle_dragging = gizmo->is_selection_rectangle_dragging();
            }

            if (keyCode == WXK_SHIFT)
            {
                // shift has been just released - SLA gizmo might want to close rectangular selection.
                if (gizmo_event(SLAGizmoEventType::ShiftUp) || (is_editing && is_rectangle_dragging))
                    processed = true;
            }
            else if (keyCode == WXK_ALT)
            {
                // alt has been just released - SLA gizmo might want to close rectangular selection.
                if (gizmo_event(SLAGizmoEventType::AltUp) || (is_editing && is_rectangle_dragging))
                    processed = true;
            }

            // BBS
            if (m_current == MmuSegmentation && keyCode > '0' && keyCode <= '9') {
                // capture number key
                processed = true;
            }
        }
        if (m_current == Measure || m_current == Assembly) {
            if (keyCode == WXK_CONTROL)
                gizmo_event(SLAGizmoEventType::CtrlUp, Vec2d::Zero(), evt.ShiftDown(), evt.AltDown(), evt.CmdDown());
            else if (keyCode == WXK_SHIFT)
                gizmo_event(SLAGizmoEventType::ShiftUp, Vec2d::Zero(), evt.ShiftDown(), evt.AltDown(), evt.CmdDown());
        }
//        if (processed)
//            m_parent.set_cursor(GLCanvas3D::Standard);
    }
    else if (evt.GetEventType() == wxEVT_KEY_DOWN)
    {
        if ((m_current == SlaSupports) && ((keyCode == WXK_SHIFT) || (keyCode == WXK_ALT))
          && dynamic_cast<GLGizmoSlaSupports*>(get_current())->is_in_editing_mode())
        {
//            m_parent.set_cursor(GLCanvas3D::Cross);
            processed = true;
        }
        else if  ((m_current == BrimEars) && ((keyCode == WXK_SHIFT) || (keyCode == WXK_ALT)))
        {
            processed = true;
        }
        else if (m_current == Cut)
        {
            // BBS
#if 0
            auto do_move = [this, &processed](double delta_z) {
                GLGizmoAdvancedCut* cut = dynamic_cast<GLGizmoAdvancedCut*>(get_current());
                cut->set_cut_z(delta_z + cut->get_cut_z());
                processed = true;
            };

            switch (keyCode)
            {
            case WXK_NUMPAD_UP:   case WXK_UP:   { do_move(1.0); break; }
            case WXK_NUMPAD_DOWN: case WXK_DOWN: { do_move(-1.0); break; }
            default: { break; }
            }
#endif
        } else if (m_current == Simplify && keyCode == WXK_ESCAPE) {
            GLGizmoSimplify *simplify = dynamic_cast<GLGizmoSimplify *>(get_current());
            if (simplify != nullptr)
                processed = simplify->on_esc_key_down();
        }
        // BBS
        else if (m_current == MmuSegmentation) {
            GLGizmoMmuSegmentation* mmu_seg = dynamic_cast<GLGizmoMmuSegmentation*>(get_current());
            if (mmu_seg != nullptr && evt.ControlDown() == false) {
                if (keyCode >= WXK_NUMPAD0 && keyCode <= WXK_NUMPAD9) {
                    keyCode = keyCode- WXK_NUMPAD0+'0';
                }
                if (keyCode >= '0' && keyCode <= '9') {
                    if (keyCode == '1' && !m_timer_set_color.IsRunning()) {
                        m_timer_set_color.StartOnce(500);
                        processed = true;
                    }
                    else if (keyCode < '7' && m_timer_set_color.IsRunning()) {
                        processed = mmu_seg->on_number_key_down(keyCode - '0'+10);
                        m_timer_set_color.Stop();
                    }
                    else {
                        processed = mmu_seg->on_number_key_down(keyCode - '0');
                    }
                }
                else if (keyCode == 'F' || keyCode == 'T' || keyCode == 'S' || keyCode == 'C' || keyCode == 'H' || keyCode == 'G') {
                    processed = mmu_seg->on_key_down_select_tool_type(keyCode);
                    if (processed) {
                        // force extra frame to automatically update window size
                        wxGetApp().imgui()->set_requires_extra_frame();
                    }
                }
            }
        }
        else if (m_current == FdmSupports) {
            GLGizmoFdmSupports* fdm_support = dynamic_cast<GLGizmoFdmSupports*>(get_current());
            if (fdm_support != nullptr && keyCode == 'F' || keyCode == 'S' || keyCode == 'C' || keyCode == 'G') {
                processed = fdm_support->on_key_down_select_tool_type(keyCode);
            }
            if (processed) {
                // force extra frame to automatically update window size
                wxGetApp().imgui()->set_requires_extra_frame();
            }
        }
        else if (m_current == Seam) {
            GLGizmoSeam* seam = dynamic_cast<GLGizmoSeam*>(get_current());
            if (seam != nullptr && keyCode == 'S' || keyCode == 'C') {
                processed = seam->on_key_down_select_tool_type(keyCode);
            }
            if (processed) {
                // force extra frame to automatically update window size
                wxGetApp().imgui()->set_requires_extra_frame();
            }
        } else if (m_current == Measure || m_current == Assembly) {
            if (keyCode == WXK_CONTROL)
                gizmo_event(SLAGizmoEventType::CtrlDown, Vec2d::Zero(), evt.ShiftDown(), evt.AltDown(), evt.CmdDown());
            else if (keyCode == WXK_SHIFT)
                gizmo_event(SLAGizmoEventType::ShiftDown, Vec2d::Zero(), evt.ShiftDown(), evt.AltDown(), evt.CmdDown());
        }
    }

    if (processed)
        m_parent.set_as_dirty();

    return processed;
}

void GLGizmosManager::on_set_color_timer(wxTimerEvent& evt)
{
    if (m_current == MmuSegmentation) {
        GLGizmoMmuSegmentation* mmu_seg = dynamic_cast<GLGizmoMmuSegmentation*>(get_current());
        mmu_seg->on_number_key_down(1);
        m_parent.set_as_dirty();
    }
}

void GLGizmosManager::update_after_undo_redo(const UndoRedo::Snapshot& snapshot)
{
    update_data();
    m_serializing = false;
    if (m_current == SlaSupports
     && snapshot.snapshot_data.flags & UndoRedo::SnapshotData::RECALCULATE_SLA_SUPPORTS)
        dynamic_cast<GLGizmoSlaSupports*>(m_gizmos[SlaSupports].get())->reslice_SLA_supports(true);
}

void GLGizmosManager::render_background(float left, float top, float right, float bottom, float border) const
{
    unsigned int tex_id = m_background_texture.texture.get_id();
    float tex_width = (float)m_background_texture.texture.get_width();
    float tex_height = (float)m_background_texture.texture.get_height();
    if ((tex_id != 0) && (tex_width > 0) && (tex_height > 0))
    {
        //BBS: GUI refactor: remove the corners of gizmo
        float inv_tex_width = (tex_width != 0.0f) ? 1.0f / tex_width : 0.0f;
        float inv_tex_height = (tex_height != 0.0f) ? 1.0f / tex_height : 0.0f;

        float internal_left_uv = (float)m_background_texture.metadata.left * inv_tex_width;
        float internal_right_uv = 1.0f - (float)m_background_texture.metadata.right * inv_tex_width;
        float internal_top_uv = 1.0f - (float)m_background_texture.metadata.top * inv_tex_height;
        float internal_bottom_uv = (float)m_background_texture.metadata.bottom * inv_tex_height;

        GLTexture::render_sub_texture(tex_id, left, right, bottom, top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });

        /*
        float inv_tex_width = (tex_width != 0.0f) ? 1.0f / tex_width : 0.0f;
        float inv_tex_height = (tex_height != 0.0f) ? 1.0f / tex_height : 0.0f;

        float internal_left = left + border;
        float internal_right = right - border;
        float internal_top = top - border;
        float internal_bottom = bottom + border;

        // float left_uv = 0.0f;
        float right_uv = 1.0f;
        float top_uv = 1.0f;
        float bottom_uv = 0.0f;

        float internal_left_uv = (float)m_background_texture.metadata.left * inv_tex_width;
        float internal_right_uv = 1.0f - (float)m_background_texture.metadata.right * inv_tex_width;
        float internal_top_uv = 1.0f - (float)m_background_texture.metadata.top * inv_tex_height;
        float internal_bottom_uv = (float)m_background_texture.metadata.bottom * inv_tex_height;

        // top-left corner
        GLTexture::render_sub_texture(tex_id, left, internal_left, internal_top, top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });

        // top edge
        GLTexture::render_sub_texture(tex_id, internal_left, internal_right, internal_top, top, { { internal_left_uv, internal_top_uv }, { internal_right_uv, internal_top_uv }, { internal_right_uv, top_uv }, { internal_left_uv, top_uv } });

        // top-right corner
        GLTexture::render_sub_texture(tex_id, internal_right, right, internal_top, top, { { internal_right_uv, internal_top_uv }, { right_uv, internal_top_uv }, { right_uv, top_uv }, { internal_right_uv, top_uv } });

        // center-left edge
        GLTexture::render_sub_texture(tex_id, left, internal_left, internal_bottom, internal_top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });

        // center
        GLTexture::render_sub_texture(tex_id, internal_left, internal_right, internal_bottom, internal_top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });

        // center-right edge
        GLTexture::render_sub_texture(tex_id, internal_right, right, internal_bottom, internal_top, { { internal_right_uv, internal_bottom_uv }, { right_uv, internal_bottom_uv }, { right_uv, internal_top_uv }, { internal_right_uv, internal_top_uv } });

        // bottom-left corner
        GLTexture::render_sub_texture(tex_id, left, internal_left, bottom, internal_bottom, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });

        // bottom edge
        GLTexture::render_sub_texture(tex_id, internal_left, internal_right, bottom, internal_bottom, { { internal_left_uv, bottom_uv }, { internal_right_uv, bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_left_uv, internal_bottom_uv } });

        // bottom-right corner
        GLTexture::render_sub_texture(tex_id, internal_right, right, bottom, internal_bottom, { { internal_right_uv, bottom_uv }, { right_uv, bottom_uv }, { right_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv } });
        */
    }
}

void GLGizmosManager::render_arrow(const GLCanvas3D& parent, EType highlighted_type) const
{
    std::vector<size_t> selectable_idxs = get_selectable_idxs();
    if (selectable_idxs.empty())
        return;
    float cnv_w = (float)m_parent.get_canvas_size().get_width();
    float inv_zoom = (float)wxGetApp().plater()->get_camera().get_inv_zoom();
    float height = get_scaled_total_height();
    float zoomed_border = m_layout.scaled_border() * inv_zoom;
    float zoomed_top_x = (-0.5f * cnv_w) * inv_zoom;
    float zoomed_top_y = (0.5f * height) * inv_zoom;
    zoomed_top_x += zoomed_border;
    zoomed_top_y -= zoomed_border;
    float icons_size = m_layout.scaled_icons_size();
    float zoomed_icons_size = icons_size * inv_zoom;
    float zoomed_stride_y = m_layout.scaled_stride_y() * inv_zoom;
    for (size_t idx : selectable_idxs)
    {
        if (idx == highlighted_type) {
            int tex_width = m_icons_texture.get_width();
            int tex_height = m_icons_texture.get_height();
            unsigned int tex_id = m_arrow_texture.texture.get_id();
            float inv_tex_width = (tex_width != 0.0f) ? 1.0f / tex_width : 0.0f;
            float inv_tex_height = (tex_height != 0.0f) ? 1.0f / tex_height : 0.0f;

            float internal_left_uv = (float)m_arrow_texture.metadata.left * inv_tex_width;
            float internal_right_uv = 1.0f - (float)m_arrow_texture.metadata.right * inv_tex_width;
            float internal_top_uv = 1.0f - (float)m_arrow_texture.metadata.top * inv_tex_height;
            float internal_bottom_uv = (float)m_arrow_texture.metadata.bottom * inv_tex_height;

            float arrow_sides_ratio = (float)m_arrow_texture.texture.get_height() / (float)m_arrow_texture.texture.get_width();

            GLTexture::render_sub_texture(tex_id, zoomed_top_x + zoomed_icons_size * 1.2f, zoomed_top_x + zoomed_icons_size * 1.2f + zoomed_icons_size * 2.2f * arrow_sides_ratio, zoomed_top_y - zoomed_icons_size * 1.6f , zoomed_top_y + zoomed_icons_size * 0.6f, { { internal_left_uv, internal_bottom_uv }, { internal_left_uv, internal_top_uv }, { internal_right_uv, internal_top_uv }, { internal_right_uv, internal_bottom_uv } });
            break;
        }
        zoomed_top_y -= zoomed_stride_y;
    }
}

//BBS: GUI refactor: GLToolbar&&Gizmo adjust
//when rendering, {0, 0} is at the center, {-0.5, 0.5} at the left-top
void GLGizmosManager::do_render_overlay() const
{
    std::vector<size_t> selectable_idxs = get_selectable_idxs();
    if (selectable_idxs.empty())
        return;

    float cnv_w = (float)m_parent.get_canvas_size().get_width();
    float cnv_h = (float)m_parent.get_canvas_size().get_height();
    float zoom = (float)wxGetApp().plater()->get_camera().get_zoom();
    float inv_zoom = (float)wxGetApp().plater()->get_camera().get_inv_zoom();

    float height = get_scaled_total_height();
    float width = get_scaled_total_width();
    float zoomed_border = m_layout.scaled_border() * inv_zoom;

    float zoomed_top_x;
    if (m_parent.get_canvas_type() == GLCanvas3D::CanvasAssembleView) {
        zoomed_top_x = 0.5f * m_parent.get_assembly_paint_toolbar_width() * inv_zoom;
    }
    else {
        //BBS: GUI refactor: GLToolbar&&Gizmo adjust
#if BBS_TOOLBAR_ON_TOP
        float main_toolbar_width = (float)m_parent.get_main_toolbar_width();
        float assemble_view_width = (float)m_parent.get_assemble_view_toolbar_width();
        float collapse_width = (float)m_parent.get_collapse_toolbar_width();
        //float space_width = GLGizmosManager::Default_Icons_Size * wxGetApp().toolbar_icon_scale();
        //float zoomed_top_x = 0.5f *(cnv_w + main_toolbar_width - 2 * space_width - width) * inv_zoom;

        float main_toolbar_left = std::max(-0.5f * cnv_w, -0.5f * (main_toolbar_width + get_scaled_total_width() + assemble_view_width - collapse_width)) * inv_zoom;
        //float zoomed_top_x = 0.5f *(main_toolbar_width + collapse_width - width - assemble_view_width) * inv_zoom;
        zoomed_top_x = main_toolbar_left + (main_toolbar_width)*inv_zoom;
    }
    float zoomed_top_y = 0.5f * cnv_h * inv_zoom;
#else
    //float zoomed_top_x = (-0.5f * cnv_w) * inv_zoom;
    //float zoomed_top_y = (0.5f * height) * inv_zoom;
    float zoomed_top_x = (0.5f * cnv_w - width) * inv_zoom;
    float main_toolbar_height = (float)m_parent.get_main_toolbar_height();
    float assemble_view_height = (float)m_parent.get_assemble_view_toolbar_height();
    //float space_height = GLGizmosManager::Default_Icons_Size * wxGetApp().toolbar_icon_scale();
    float zoomed_top_y = 0.5f * (height + assemble_view_height - main_toolbar_height) * inv_zoom;
#endif
    //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": zoomed_top_y %1%, space_height %2%, main_toolbar_height %3% zoomed_top_x %4%") % zoomed_top_y % space_height % main_toolbar_height % zoomed_top_x;

    float zoomed_left = zoomed_top_x;
    float zoomed_top = zoomed_top_y;
    float zoomed_right = zoomed_left + width * inv_zoom;
    float zoomed_bottom = zoomed_top - height * inv_zoom;

    render_background(zoomed_left, zoomed_top, zoomed_right, zoomed_bottom, zoomed_border);

    zoomed_top_x += zoomed_border;
    zoomed_top_y -= zoomed_border;

    float icons_size = m_layout.scaled_icons_size();
    float zoomed_icons_size = icons_size * inv_zoom;
    float zoomed_stride_y = m_layout.scaled_stride_y() * inv_zoom;
    //BBS: GUI refactor: GLToolbar&&Gizmo adjust
    float zoomed_stride_x = m_layout.scaled_stride_x() * inv_zoom;

    unsigned int icons_texture_id = m_icons_texture.get_id();
    int tex_width = m_icons_texture.get_width();
    int tex_height = m_icons_texture.get_height();

    if ((icons_texture_id == 0) || (tex_width <= 1) || (tex_height <= 1))
        return;

    float du = (float)(tex_width - 1) / (6.0f * (float)tex_width); // 6 is the number of possible states if the icons
    float dv = (float)(tex_height - 1) / (float)(m_gizmos.size() * tex_height);

    // tiles in the texture are spaced by 1 pixel
    float u_offset = 1.0f / (float)tex_width;
    float v_offset = 1.0f / (float)tex_height;

    bool is_render_current = false;

    for (size_t idx : selectable_idxs)
    {
        GLGizmoBase* gizmo = m_gizmos[idx].get();
        unsigned int sprite_id = gizmo->get_sprite_id();
        // higlighted state needs to be decided first so its highlighting in every other state
        int icon_idx = (m_highlight.first == idx ? (m_highlight.second ? 4 : 5) : (m_current == idx) ? 2 : ((m_hover == idx) ? 1 : (gizmo->is_activable()? 0 : 3)));

        float v_top = v_offset + sprite_id * dv;
        float u_left = u_offset + icon_idx * du;
        float v_bottom = v_top + dv - v_offset;
        float u_right = u_left + du - u_offset;

        GLTexture::render_sub_texture(icons_texture_id, zoomed_top_x, zoomed_top_x + zoomed_icons_size, zoomed_top_y - zoomed_icons_size, zoomed_top_y, { { u_left, v_bottom }, { u_right, v_bottom }, { u_right, v_top }, { u_left, v_top } });

        if (idx == m_current// Orca: Show Svg dialog at the same place as emboss gizmo
            || (m_current == Svg && idx == Text)) {
            //BBS: GUI refactor: GLToolbar&&Gizmo adjust
            //render_input_window uses a different coordination(imgui)
            //1. no need to scale by camera zoom, set {0,0} at left-up corner for imgui
#if BBS_TOOLBAR_ON_TOP
            //gizmo->render_input_window(width, 0.5f * cnv_h - zoomed_top_y * zoom, toolbar_top);
             m_gizmos[m_current]->render_input_window(0.5 * cnv_w + zoomed_top_x * zoom, height, cnv_h);

            is_render_current = true;
#else
            float toolbar_top = cnv_h - wxGetApp().plater()->get_view_toolbar().get_height();
            //gizmo->render_input_window(width, 0.5f * cnv_h - zoomed_top_y * zoom, toolbar_top);
            gizmo->render_input_window(cnv_w - width, 0.5f * cnv_h - zoomed_top_y * zoom, toolbar_top);
#endif
        }
#if BBS_TOOLBAR_ON_TOP
        zoomed_top_x += zoomed_stride_x;
#else
        zoomed_top_y -= zoomed_stride_y;
#endif
    }

    // BBS simplify gizmo is not a selected gizmo and need to render input window
    if (!is_render_current && m_current != Undefined) {
        m_gizmos[m_current]->render_input_window(0.5 * cnv_w + zoomed_top_x * zoom, height, cnv_h);
    }
}

float GLGizmosManager::get_scaled_total_height() const
{
//BBS: GUI refactor: to support top layout
#if BBS_TOOLBAR_ON_TOP
    return 2.0f * m_layout.scaled_border() + m_layout.scaled_icons_size();
#else
    return m_layout.scale * (2.0f * m_layout.border + (float)get_selectable_idxs().size() * m_layout.stride_y() - m_layout.gap_y);
#endif
}

float GLGizmosManager::get_scaled_total_width() const
{
//BBS: GUI refactor: to support top layout
#if BBS_TOOLBAR_ON_TOP
    return m_layout.scale * (2.0f * m_layout.border + (float)get_selectable_idxs().size() * m_layout.stride_x() - m_layout.gap_x);
#else
    return 2.0f * m_layout.scaled_border() + m_layout.scaled_icons_size();
#endif
}

GLGizmoBase* GLGizmosManager::get_current() const
{
    return ((m_current == Undefined) || m_gizmos.empty()) ? nullptr : m_gizmos[m_current].get();
}

GLGizmoBase* GLGizmosManager::get_gizmo(GLGizmosManager::EType type) const
{
    return ((type == Undefined) || m_gizmos.empty()) ? nullptr : m_gizmos[type].get();
}

GLGizmosManager::EType GLGizmosManager::get_gizmo_from_name(const std::string& gizmo_name) const
{
    std::vector<size_t> selectable_idxs = get_selectable_idxs();
    for (size_t idx = 0; idx < selectable_idxs.size(); ++idx)
    {
        std::string filename = m_gizmos[selectable_idxs[idx]]->get_icon_filename();
        filename = filename.substr(0, filename.find_first_of('.'));
        if (filename == gizmo_name)
            return (GLGizmosManager::EType)selectable_idxs[idx];
    }
    return GLGizmosManager::EType::Undefined;
}

bool GLGizmosManager::generate_icons_texture() const
{
    std::string path = resources_dir() + "/images/";
    std::vector<std::string> filenames;
    for (size_t idx=0; idx<m_gizmos.size(); ++idx)
    {
        if (m_gizmos[idx] != nullptr)
        {
            const std::string& icon_filename = m_gizmos[idx]->get_icon_filename();
            if (!icon_filename.empty())
                filenames.push_back(path + icon_filename);
        }
    }

    std::vector<std::pair<int, bool>> states;
    states.push_back(std::make_pair(1, false)); // Activable
    states.push_back(std::make_pair(0, false)); // Hovered
    states.push_back(std::make_pair(0, true));  // Selected
    states.push_back(std::make_pair(2, false)); // Disabled
    states.push_back(std::make_pair(0, false)); // HighlightedShown
    states.push_back(std::make_pair(2, false)); // HighlightedHidden

    unsigned int sprite_size_px = (unsigned int)m_layout.scaled_icons_size();
//    // force even size
//    if (sprite_size_px % 2 != 0)
//        sprite_size_px += 1;

    bool res = m_icons_texture.load_from_svg_files_as_sprites_array(filenames, states, sprite_size_px, false);
    if (res)
        m_icons_texture_dirty = false;

    return res;
}

void GLGizmosManager::update_on_off_state(const Vec2d& mouse_pos)
{
    if (!m_enabled)
        return;

    size_t idx = get_gizmo_idx_from_mouse(mouse_pos);
    if (idx != Undefined && m_gizmos[idx]->is_activable() && m_hover == idx) {
        activate_gizmo(m_current == idx ? Undefined : (EType)idx);
        // BBS
        wxGetApp().obj_list()->select_object_item((EType) idx <= Scale || (EType) idx == Text);
    }
}

std::string GLGizmosManager::update_hover_state(const Vec2d& mouse_pos)
{
    std::string name = "";

    if (!m_enabled)
        return name;

    m_hover = Undefined;

    size_t idx = get_gizmo_idx_from_mouse(mouse_pos);
    if (idx != Undefined) {
        name = m_gizmos[idx]->get_name();

        if (m_gizmos[idx]->is_activable())
            m_hover = (EType)idx;
    }

    return name;
}

bool GLGizmosManager::activate_gizmo(EType type)
{
    if (m_gizmos.empty() || m_current == type)
        return true;

    GLGizmoBase* old_gizmo = m_current == Undefined ? nullptr : m_gizmos[m_current].get();
    GLGizmoBase* new_gizmo = type == Undefined ? nullptr : m_gizmos[type].get();

    if (old_gizmo) {
        //if (m_current == Text) {
        //    wxGetApp().imgui()->destroy_fonts_texture();
        //}
        old_gizmo->set_state(GLGizmoBase::Off);
        if (old_gizmo->get_state() != GLGizmoBase::Off)
            return false; // gizmo refused to be turned off, do nothing.

        if (! m_parent.get_gizmos_manager().is_serializing()
         && old_gizmo->wants_enter_leave_snapshots())
            Plater::TakeSnapshot snapshot(wxGetApp().plater(),
                old_gizmo->get_gizmo_leaving_text(),
                UndoRedo::SnapshotType::LeavingGizmoWithAction);
    }

    if (new_gizmo && ! m_parent.get_gizmos_manager().is_serializing()
     && new_gizmo->wants_enter_leave_snapshots())
        Plater::TakeSnapshot snapshot(wxGetApp().plater(),
            new_gizmo->get_gizmo_entering_text(),
            UndoRedo::SnapshotType::EnteringGizmo);

    m_current = type;

    if (new_gizmo) {
        //if (m_current == Text) {
        //    wxGetApp().imgui()->load_fonts_texture();
        //}
        new_gizmo->set_state(GLGizmoBase::On);
        if (is_show_only_active_plate()) {
            check_object_located_outside_plate();
        }
    }
    return true;
}


bool GLGizmosManager::grabber_contains_mouse() const
{
    if (!m_enabled)
        return false;

    GLGizmoBase* curr = get_current();
    return (curr != nullptr) ? (curr->get_hover_id() != -1) : false;
}


bool GLGizmosManager::is_in_editing_mode(bool error_notification) const
{
    if (m_current == SlaSupports && dynamic_cast<GLGizmoSlaSupports*>(get_current())->is_in_editing_mode()) {
        return true;
    } else if (m_current == BrimEars) {
        dynamic_cast<GLGizmoBrimEars*>(get_current())->save_model();
        return false;
    } else {
        return false;
    }

}


bool GLGizmosManager::is_hiding_instances() const
{
    return (m_common_gizmos_data
         && m_common_gizmos_data->instances_hider()
         && m_common_gizmos_data->instances_hider()->is_valid());
}


int GLGizmosManager::get_shortcut_key(GLGizmosManager::EType type) const
{
    return m_gizmos[type]->get_shortcut_key();
}

std::string get_name_from_gizmo_etype(GLGizmosManager::EType type)
{
    switch (type) {
    case GLGizmosManager::EType::Move:
        return "Move";
    case GLGizmosManager::EType::Rotate:
        return "Rotate";
    case GLGizmosManager::EType::Scale:
        return "Scale";
    case GLGizmosManager::EType::Flatten:
        return "Flatten";
    case GLGizmosManager::EType::Cut:
        return "Cut";
    case GLGizmosManager::EType::MeshBoolean:
        return "MeshBoolean";
    case GLGizmosManager::EType::FdmSupports:
        return "FdmSupports";
    case GLGizmosManager::EType::Seam:
        return "Seam";
    case GLGizmosManager::EType::Text:
        return "Text";
    case GLGizmosManager::EType::MmuSegmentation:
        return "Color Painting";
    default:
        return "";
    }
    return "";
}

} // namespace GUI
} // namespace Slic3r
