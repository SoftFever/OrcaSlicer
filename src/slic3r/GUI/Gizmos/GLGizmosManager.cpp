#include "libslic3r/libslic3r.h"
#include "GLGizmosManager.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectManipulation.hpp"
#include "slic3r/GUI/PresetBundle.hpp"

#include <GL/glew.h>
#include <wx/glcanvas.h>

namespace Slic3r {
namespace GUI {

#if ENABLE_SVG_ICONS
    const float GLGizmosManager::Default_Icons_Size = 64;
#endif // ENABLE_SVG_ICONS

GLGizmosManager::GLGizmosManager()
    : m_enabled(false)
#if ENABLE_SVG_ICONS
    , m_icons_texture_dirty(true)
#endif // ENABLE_SVG_ICONS
    , m_current(Undefined)
#if ENABLE_SVG_ICONS
    , m_overlay_icons_size(Default_Icons_Size)
    , m_overlay_scale(1.0f)
    , m_overlay_border(5.0f)
    , m_overlay_gap_y(5.0f)
    , m_tooltip("")
{
}
#else
{
    set_overlay_scale(1.0);
}
#endif // ENABLE_SVG_ICONS

GLGizmosManager::~GLGizmosManager()
{
    reset();
}

bool GLGizmosManager::init(GLCanvas3D& parent)
{
#if !ENABLE_SVG_ICONS
    m_icons_texture.metadata.filename = "gizmos.png";
    m_icons_texture.metadata.icon_size = 64;

    if (!m_icons_texture.metadata.filename.empty())
    {
        if (!m_icons_texture.texture.load_from_file(resources_dir() + "/icons/" + m_icons_texture.metadata.filename, false))
        {
            reset();
            return false;
        }
    }
#endif // !ENABLE_SVG_ICONS

    m_background_texture.metadata.filename = "toolbar_background.png";
    m_background_texture.metadata.left = 16;
    m_background_texture.metadata.top = 16;
    m_background_texture.metadata.right = 16;
    m_background_texture.metadata.bottom = 16;

    if (!m_background_texture.metadata.filename.empty())
    {
        if (!m_background_texture.texture.load_from_file(resources_dir() + "/icons/" + m_background_texture.metadata.filename, false, true))
        {
            reset();
            return false;
        }
    }

#if ENABLE_SVG_ICONS
    GLGizmoBase* gizmo = new GLGizmoMove3D(parent, "move.svg", 0);
#else
    GLGizmoBase* gizmo = new GLGizmoMove3D(parent, 0);
#endif // ENABLE_SVG_ICONS
    if (gizmo == nullptr)
        return false;

    if (!gizmo->init())
        return false;

    m_gizmos.insert(GizmosMap::value_type(Move, gizmo));

#if ENABLE_SVG_ICONS
    gizmo = new GLGizmoScale3D(parent, "scale.svg", 1);
#else
    gizmo = new GLGizmoScale3D(parent, 1);
#endif // ENABLE_SVG_ICONS
    if (gizmo == nullptr)
        return false;

    if (!gizmo->init())
        return false;

    m_gizmos.insert(GizmosMap::value_type(Scale, gizmo));

#if ENABLE_SVG_ICONS
    gizmo = new GLGizmoRotate3D(parent, "rotate.svg", 2);
#else
    gizmo = new GLGizmoRotate3D(parent, 2);
#endif // ENABLE_SVG_ICONS
    if (gizmo == nullptr)
    {
        reset();
        return false;
    }

    if (!gizmo->init())
    {
        reset();
        return false;
    }

    m_gizmos.insert(GizmosMap::value_type(Rotate, gizmo));

#if ENABLE_SVG_ICONS
    gizmo = new GLGizmoFlatten(parent, "place.svg", 3);
#else
    gizmo = new GLGizmoFlatten(parent, 3);
#endif // ENABLE_SVG_ICONS
    if (gizmo == nullptr)
        return false;

    if (!gizmo->init()) {
        reset();
        return false;
    }

    m_gizmos.insert(GizmosMap::value_type(Flatten, gizmo));

#if ENABLE_SVG_ICONS
    gizmo = new GLGizmoCut(parent, "cut.svg", 4);
#else
    gizmo = new GLGizmoCut(parent, 4);
#endif // ENABLE_SVG_ICONS
    if (gizmo == nullptr)
        return false;

    if (!gizmo->init()) {
        reset();
        return false;
    }

    m_gizmos.insert(GizmosMap::value_type(Cut, gizmo));

#if ENABLE_SVG_ICONS
    gizmo = new GLGizmoSlaSupports(parent, "sla_supports.svg", 5);
#else
    gizmo = new GLGizmoSlaSupports(parent, 5);
#endif // ENABLE_SVG_ICONS
    if (gizmo == nullptr)
        return false;

    if (!gizmo->init()) {
        reset();
        return false;
    }

    m_gizmos.insert(GizmosMap::value_type(SlaSupports, gizmo));

    return true;
}

#if ENABLE_SVG_ICONS
void GLGizmosManager::set_overlay_icon_size(float size)
{
    if (m_overlay_icons_size != size)
    {
        m_overlay_icons_size = size;
        m_icons_texture_dirty = true;
    }
}
#endif // ENABLE_SVG_ICONS

void GLGizmosManager::set_overlay_scale(float scale)
{
#if ENABLE_SVG_ICONS
    if (m_overlay_scale != scale)
    {
        m_overlay_scale = scale;
        m_icons_texture_dirty = true;
    }
#else
    m_overlay_icons_scale = scale;
    m_overlay_border = 5.0f * scale;
    m_overlay_gap_y = 5.0f * scale;
#endif // ENABLE_SVG_ICONS
}

void GLGizmosManager::refresh_on_off_state(const Selection& selection)
{
    GizmosMap::iterator it = m_gizmos.find(m_current);
    if ((it != m_gizmos.end()) && (it->second != nullptr))
    {
        if (!it->second->is_activable(selection))
        {
            it->second->set_state(GLGizmoBase::Off);
            m_current = Undefined;
        }
    }
}

void GLGizmosManager::reset_all_states()
{
    if (!m_enabled)
        return;

    for (GizmosMap::const_iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        if (it->second != nullptr)
        {
            it->second->set_state(GLGizmoBase::Off);
            it->second->set_hover_id(-1);
        }
    }

    m_current = Undefined;
}

void GLGizmosManager::set_hover_id(int id)
{
    if (!m_enabled)
        return;

    for (GizmosMap::const_iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        if ((it->second != nullptr) && (it->second->get_state() == GLGizmoBase::On))
            it->second->set_hover_id(id);
    }
}

void GLGizmosManager::enable_grabber(EType type, unsigned int id, bool enable)
{
    if (!m_enabled)
        return;

    GizmosMap::const_iterator it = m_gizmos.find(type);
    if (it != m_gizmos.end())
    {
        if (enable)
            it->second->enable_grabber(id);
        else
            it->second->disable_grabber(id);
    }
}

void GLGizmosManager::update(const Linef3& mouse_ray, const Selection& selection, const Point* mouse_pos)
{
    if (!m_enabled)
        return;

    GLGizmoBase* curr = get_current();
    if (curr != nullptr)
        curr->update(GLGizmoBase::UpdateData(mouse_ray, mouse_pos), selection);
}

void GLGizmosManager::update_data(GLCanvas3D& canvas)
{
    if (!m_enabled)
        return;

    const Selection& selection = canvas.get_selection();

    bool is_wipe_tower = selection.is_wipe_tower();
    enable_grabber(Move, 2, !is_wipe_tower);
    enable_grabber(Rotate, 0, !is_wipe_tower);
    enable_grabber(Rotate, 1, !is_wipe_tower);

    bool enable_scale_xyz = selection.is_single_full_instance() || selection.is_single_volume() || selection.is_single_modifier();
    for (int i = 0; i < 6; ++i)
    {
        enable_grabber(Scale, i, enable_scale_xyz);
    }

    if (selection.is_single_full_instance())
    {
        // all volumes in the selection belongs to the same instance, any of them contains the needed data, so we take the first
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        set_scale(volume->get_instance_scaling_factor());
        set_rotation(Vec3d::Zero());
        ModelObject* model_object = selection.get_model()->objects[selection.get_object_idx()];
        set_flattening_data(model_object);
        set_sla_support_data(model_object, selection);
    }
    else if (selection.is_single_volume() || selection.is_single_modifier())
    {
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        set_scale(volume->get_volume_scaling_factor());
        set_rotation(Vec3d::Zero());
        set_flattening_data(nullptr);
        set_sla_support_data(nullptr, selection);
    }
    else if (is_wipe_tower)
    {
        DynamicPrintConfig& config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        set_scale(Vec3d::Ones());
        set_rotation(Vec3d(0., 0., (M_PI/180.) * dynamic_cast<const ConfigOptionFloat*>(config.option("wipe_tower_rotation_angle"))->value));
        set_flattening_data(nullptr);
        set_sla_support_data(nullptr, selection);
    }
    else
    {
        set_scale(Vec3d::Ones());
        set_rotation(Vec3d::Zero());
        set_flattening_data(selection.is_from_single_object() ? selection.get_model()->objects[selection.get_object_idx()] : nullptr);
        set_sla_support_data(nullptr, selection);
    }
}

bool GLGizmosManager::is_running() const
{
    if (!m_enabled)
        return false;

    GLGizmoBase* curr = get_current();
    return (curr != nullptr) ? (curr->get_state() == GLGizmoBase::On) : false;
}

bool GLGizmosManager::handle_shortcut(int key, const Selection& selection)
{
    if (!m_enabled || selection.is_empty())
        return false;

    EType old_current = m_current;
    bool handled = false;
    for (GizmosMap::iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        if ((it->second == nullptr) || !it->second->is_selectable())
            continue;

        int it_key = it->second->get_shortcut_key();

        if (it->second->is_activable(selection) && ((it_key == key - 64) || (it_key == key - 96)))
        {
            if ((it->second->get_state() == GLGizmoBase::On))
            {
                it->second->set_state(GLGizmoBase::Off);
                m_current = Undefined;
                handled = true;
            }
            else if ((it->second->get_state() == GLGizmoBase::Off))
            {
                it->second->set_state(GLGizmoBase::On);
                m_current = it->first;
                handled = true;
            }
        }
    }

    if (handled && (old_current != Undefined) && (old_current != m_current))
    {
        GizmosMap::const_iterator it = m_gizmos.find(old_current);
        if (it != m_gizmos.end())
            it->second->set_state(GLGizmoBase::Off);
    }

    return handled;
}

bool GLGizmosManager::is_dragging() const
{
    if (!m_enabled)
        return false;

    GLGizmoBase* curr = get_current();
    return (curr != nullptr) ? curr->is_dragging() : false;
}

void GLGizmosManager::start_dragging(const Selection& selection)
{
    if (!m_enabled)
        return;

    GLGizmoBase* curr = get_current();
    if (curr != nullptr)
        curr->start_dragging(selection);
}

void GLGizmosManager::stop_dragging()
{
    if (!m_enabled)
        return;

    GLGizmoBase* curr = get_current();
    if (curr != nullptr)
        curr->stop_dragging();
}

Vec3d GLGizmosManager::get_displacement() const
{
    if (!m_enabled)
        return Vec3d::Zero();

    GizmosMap::const_iterator it = m_gizmos.find(Move);
    return (it != m_gizmos.end()) ? reinterpret_cast<GLGizmoMove3D*>(it->second)->get_displacement() : Vec3d::Zero();
}

Vec3d GLGizmosManager::get_scale() const
{
    if (!m_enabled)
        return Vec3d::Ones();

    GizmosMap::const_iterator it = m_gizmos.find(Scale);
    return (it != m_gizmos.end()) ? reinterpret_cast<GLGizmoScale3D*>(it->second)->get_scale() : Vec3d::Ones();
}

void GLGizmosManager::set_scale(const Vec3d& scale)
{
    if (!m_enabled)
        return;

    GizmosMap::const_iterator it = m_gizmos.find(Scale);
    if (it != m_gizmos.end())
        reinterpret_cast<GLGizmoScale3D*>(it->second)->set_scale(scale);
}

Vec3d GLGizmosManager::get_scale_offset() const
{
    if (!m_enabled)
        return Vec3d::Zero();

    GizmosMap::const_iterator it = m_gizmos.find(Scale);
    return (it != m_gizmos.end()) ? reinterpret_cast<GLGizmoScale3D*>(it->second)->get_offset() : Vec3d::Zero();
}

Vec3d GLGizmosManager::get_rotation() const
{
    if (!m_enabled)
        return Vec3d::Zero();

    GizmosMap::const_iterator it = m_gizmos.find(Rotate);
    return (it != m_gizmos.end()) ? reinterpret_cast<GLGizmoRotate3D*>(it->second)->get_rotation() : Vec3d::Zero();
}

void GLGizmosManager::set_rotation(const Vec3d& rotation)
{
    if (!m_enabled)
        return;

    GizmosMap::const_iterator it = m_gizmos.find(Rotate);
    if (it != m_gizmos.end())
        reinterpret_cast<GLGizmoRotate3D*>(it->second)->set_rotation(rotation);
}

Vec3d GLGizmosManager::get_flattening_normal() const
{
    if (!m_enabled)
        return Vec3d::Zero();

    GizmosMap::const_iterator it = m_gizmos.find(Flatten);
    return (it != m_gizmos.end()) ? reinterpret_cast<GLGizmoFlatten*>(it->second)->get_flattening_normal() : Vec3d::Zero();
}

void GLGizmosManager::set_flattening_data(const ModelObject* model_object)
{
    if (!m_enabled)
        return;

    GizmosMap::const_iterator it = m_gizmos.find(Flatten);
    if (it != m_gizmos.end())
        reinterpret_cast<GLGizmoFlatten*>(it->second)->set_flattening_data(model_object);
}

void GLGizmosManager::set_sla_support_data(ModelObject* model_object, const Selection& selection)
{
    if (!m_enabled)
        return;

    GizmosMap::const_iterator it = m_gizmos.find(SlaSupports);
    if (it != m_gizmos.end())
        reinterpret_cast<GLGizmoSlaSupports*>(it->second)->set_sla_support_data(model_object, selection);
}

// Returns true if the gizmo used the event to do something, false otherwise.
bool GLGizmosManager::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    if (!m_enabled)
        return false;

    GizmosMap::const_iterator it = m_gizmos.find(SlaSupports);
    if (it != m_gizmos.end())
        return reinterpret_cast<GLGizmoSlaSupports*>(it->second)->gizmo_event(action, mouse_position, shift_down, alt_down, control_down);

    return false;
}

ClippingPlane GLGizmosManager::get_sla_clipping_plane() const
{
    if (!m_enabled || m_current != SlaSupports)
        return ClippingPlane::ClipsNothing();

    GizmosMap::const_iterator it = m_gizmos.find(SlaSupports);
    if (it != m_gizmos.end())
        return reinterpret_cast<GLGizmoSlaSupports*>(it->second)->get_sla_clipping_plane();

    return ClippingPlane::ClipsNothing();
}


void GLGizmosManager::render_current_gizmo(const Selection& selection) const
{
    if (!m_enabled)
        return;

    GLGizmoBase* curr = get_current();
    if (curr != nullptr)
        curr->render(selection);
}

void GLGizmosManager::render_current_gizmo_for_picking_pass(const Selection& selection) const
{
    if (!m_enabled)
        return;

    GLGizmoBase* curr = get_current();
    if (curr != nullptr)
        curr->render_for_picking(selection);
}

void GLGizmosManager::render_overlay(const GLCanvas3D& canvas, const Selection& selection) const
{
    if (!m_enabled)
        return;

#if ENABLE_SVG_ICONS
    if (m_icons_texture_dirty)
        generate_icons_texture();
#endif // ENABLE_SVG_ICONS

    do_render_overlay(canvas, selection);
}

bool GLGizmosManager::on_mouse_wheel(wxMouseEvent& evt, GLCanvas3D& canvas)
{
    bool processed = false;

    if (m_current == SlaSupports) {
        float rot = (float)evt.GetWheelRotation() / (float)evt.GetWheelDelta();
        if (gizmo_event((rot > 0.f ? SLAGizmoEventType::MouseWheelUp : SLAGizmoEventType::MouseWheelDown), Vec2d::Zero(), evt.ShiftDown(), evt.AltDown(), evt.ControlDown()))
            processed = true;
    }

    return processed;
}



bool GLGizmosManager::on_mouse(wxMouseEvent& evt, GLCanvas3D& canvas)
{
    Point pos(evt.GetX(), evt.GetY());
    Vec2d mouse_pos((double)evt.GetX(), (double)evt.GetY());

    Selection& selection = canvas.get_selection();
    int selected_object_idx = selection.get_object_idx();
    bool processed = false;

    // mouse anywhere
    if (!evt.Dragging() && !evt.Leaving() && !evt.Entering() && (m_mouse_capture.parent != nullptr))
    {
        if (m_mouse_capture.any() && (evt.LeftUp() || evt.MiddleUp() || evt.RightUp()))
            // prevents loosing selection into the scene if mouse down was done inside the toolbar and mouse up was down outside it
            processed = true;

        m_mouse_capture.reset();
    }

    // mouse anywhere
    if (evt.Moving())
        m_tooltip = update_hover_state(canvas, mouse_pos);
    else if (evt.LeftUp())
        m_mouse_capture.left = false;
    else if (evt.MiddleUp())
        m_mouse_capture.middle = false;
    else if (evt.RightUp())
        m_mouse_capture.right = false;
    else if (evt.Dragging() && m_mouse_capture.any())
        // if the button down was done on this toolbar, prevent from dragging into the scene
        processed = true;

    if (!overlay_contains_mouse(canvas, mouse_pos))
    {
        // mouse is outside the toolbar
        m_tooltip = "";

        if (evt.LeftDown())
        {
            if ((m_current == SlaSupports) && gizmo_event(SLAGizmoEventType::LeftDown, mouse_pos, evt.ShiftDown(), evt.AltDown(), evt.ControlDown()))
                // the gizmo got the event and took some action, there is no need to do anything more
                processed = true;
            else if (!selection.is_empty() && grabber_contains_mouse())
            {
                update_data(canvas);
                selection.start_dragging();
                start_dragging(selection);

                if (m_current == Flatten)
                {
                    // Rotate the object so the normal points downward:
				    wxGetApp().plater()->take_snapshot(_(L("Place on Face")));
                    selection.flattening_rotate(get_flattening_normal());
                    canvas.do_flatten();
                    wxGetApp().obj_manipul()->set_dirty();
                }

                canvas.set_as_dirty();
                processed = true;
            }
        }
        else if (evt.RightDown() && (selected_object_idx != -1) && (m_current == SlaSupports) && gizmo_event(SLAGizmoEventType::RightDown))
            // event was taken care of by the SlaSupports gizmo
            processed = true;
        else if (evt.Dragging() && (canvas.get_move_volume_id() != -1) && (m_current == SlaSupports))
            // don't allow dragging objects with the Sla gizmo on
            processed = true;
        else if (evt.Dragging() && (m_current == SlaSupports) && gizmo_event(SLAGizmoEventType::Dragging, mouse_pos, evt.ShiftDown(), evt.AltDown(), evt.ControlDown()))
        {
            // the gizmo got the event and took some action, no need to do anything more here
            canvas.set_as_dirty();
            processed = true;
        }
        else if (evt.Dragging() && is_dragging())
        {
            if (!canvas.get_wxglcanvas()->HasCapture())
                canvas.get_wxglcanvas()->CaptureMouse();

            canvas.set_mouse_as_dragging();
            update(canvas.mouse_ray(pos), selection, &pos);

            switch (m_current)
            {
            case Move:
            {
                // Apply new temporary offset
                selection.translate(get_displacement());
                wxGetApp().obj_manipul()->set_dirty();
                break;
            }
            case Scale:
            {
                // Apply new temporary scale factors
				TransformationType transformation_type(TransformationType::Local_Absolute_Joint);
				if (evt.AltDown())
					transformation_type.set_independent();
				selection.scale(get_scale(), transformation_type);
                if (evt.ControlDown())
                    selection.translate(get_scale_offset(), true);
                wxGetApp().obj_manipul()->set_dirty();
                break;
            }
            case Rotate:
            {
                // Apply new temporary rotations
                TransformationType transformation_type(TransformationType::World_Relative_Joint);
                if (evt.AltDown())
                    transformation_type.set_independent();
                selection.rotate(get_rotation(), transformation_type);
                wxGetApp().obj_manipul()->set_dirty();
                break;
            }
            default:
                break;
            }

            canvas.set_as_dirty();
            processed = true;
        }
        else if (evt.LeftUp() && is_dragging())
        {
            switch (m_current)
            {
            case Move:
            {
			    wxGetApp().plater()->take_snapshot(_(L("Move Object")));
                canvas.disable_regenerate_volumes();
                canvas.do_move();
                break;
            }
            case Scale:
            {
                canvas.do_scale();
                break;
            }
            case Rotate:
            {
			    wxGetApp().plater()->take_snapshot(_(L("Rotate Object")));
                canvas.do_rotate();
                break;
            }
            default:
                break;
            }

            stop_dragging();
            update_data(canvas);

            wxGetApp().obj_manipul()->set_dirty();
            // Let the platter know that the dragging finished, so a delayed refresh
            // of the scene with the background processing data should be performed.
            canvas.post_event(SimpleEvent(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED));
            // updates camera target constraints
            canvas.refresh_camera_scene_box();

            processed = true;
        }
        else if (evt.LeftUp() && (m_current == SlaSupports) && !canvas.is_mouse_dragging())
        {
            // in case SLA gizmo is selected, we just pass the LeftUp event and stop processing - neither
            // object moving or selecting is suppressed in that case
            gizmo_event(SLAGizmoEventType::LeftUp, mouse_pos, evt.ShiftDown(), evt.AltDown(), evt.ControlDown());
            processed = true;
        }
        else if (evt.LeftUp() && (m_current == Flatten) && ((canvas.get_first_hover_volume_idx() != -1) || grabber_contains_mouse()))
        {
            // to avoid to loose the selection when user clicks an object while the Flatten gizmo is active
            processed = true;
        }
    }
    else
    {
        // mouse inside toolbar
        if (evt.LeftDown() || evt.LeftDClick())
        {
            m_mouse_capture.left = true;
            m_mouse_capture.parent = &canvas;
            processed = true;
            if (!selection.is_empty())
            {
                update_on_off_state(canvas, mouse_pos, selection);
                update_data(canvas);
                canvas.set_as_dirty();
            }
        }
        else if (evt.MiddleDown())
        {
            m_mouse_capture.middle = true;
            m_mouse_capture.parent = &canvas;
        }
        else if (evt.RightDown())
        {
            m_mouse_capture.right = true;
            m_mouse_capture.parent = &canvas;
        }
        else if (evt.LeftUp())
            processed = true;
    }

    return processed;
}

bool GLGizmosManager::on_char(wxKeyEvent& evt, GLCanvas3D& canvas)
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
            // Sla gizmo selects all support points
            if ((m_current == SlaSupports) && gizmo_event(SLAGizmoEventType::SelectAll))
                processed = true;

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
            if (m_current != Undefined)
            {
                if ((m_current != SlaSupports) || !gizmo_event(SLAGizmoEventType::DiscardChanges))
                    reset_all_states();

                processed = true;
            }
            break;
        }
        case WXK_RETURN:
        {
            if ((m_current == SlaSupports) && gizmo_event(SLAGizmoEventType::ApplyChanges))
                processed = true;

            break;
        }

        case 'r' :
        case 'R' :
        {
            if ((m_current == SlaSupports) && gizmo_event(SLAGizmoEventType::ResetClippingPlane))
                processed = true;

            break;
        }

#ifdef __APPLE__
        case WXK_BACK: // the low cost Apple solutions are not equipped with a Delete key, use Backspace instead.
#else /* __APPLE__ */
        case WXK_DELETE:
#endif /* __APPLE__ */
        {
            if ((m_current == SlaSupports) && gizmo_event(SLAGizmoEventType::Delete))
                processed = true;

            break;
        }
        case 'A':
        case 'a':
        {
            if (m_current == SlaSupports)
            {
                gizmo_event(SLAGizmoEventType::AutomaticGeneration);
                // set as processed no matter what's returned by gizmo_event() to avoid the calling canvas to process 'A' as arrange
                processed = true;
            }
            break;
        }
        case 'M':
        case 'm':
        {
            if ((m_current == SlaSupports) && gizmo_event(SLAGizmoEventType::ManualEditing))
                processed = true;
                
            break;
        }
        case 'F':
        case 'f':
        {
            if (m_current == Scale)
            {
                if (!is_dragging())
                    wxGetApp().plater()->scale_selection_to_fit_print_volume();

                processed = true;
            }

            break;
        }
        }
    }

    if (!processed && !evt.HasModifiers())
    {
        if (handle_shortcut(keyCode, canvas.get_selection()))
        {
            update_data(canvas);
            processed = true;
        }
    }

    if (processed)
        canvas.set_as_dirty();

    return processed;
}

bool GLGizmosManager::on_key(wxKeyEvent& evt, GLCanvas3D& canvas)
{
    const int keyCode = evt.GetKeyCode();
    bool processed = false;

    if (evt.GetEventType() == wxEVT_KEY_UP)
    {
        if (m_current == SlaSupports)
        {
            GLGizmoSlaSupports* gizmo = reinterpret_cast<GLGizmoSlaSupports*>(get_current());

            if (keyCode == WXK_SHIFT)
            {
                // shift has been just released - SLA gizmo might want to close rectangular selection.
                if (gizmo_event(SLAGizmoEventType::ShiftUp) || (gizmo->is_in_editing_mode() && gizmo->is_selection_rectangle_dragging()))
                    processed = true;
            }
            else if (keyCode == WXK_ALT)
            {
                // alt has been just released - SLA gizmo might want to close rectangular selection.
                if (gizmo_event(SLAGizmoEventType::AltUp) || (gizmo->is_in_editing_mode() && gizmo->is_selection_rectangle_dragging()))
                    processed = true;
            }
        }

//        if (processed)
//            canvas.set_cursor(GLCanvas3D::Standard);
    }
    else if (evt.GetEventType() == wxEVT_KEY_DOWN)
    {
        if ((m_current == SlaSupports) && ((keyCode == WXK_SHIFT) || (keyCode == WXK_ALT)) && reinterpret_cast<GLGizmoSlaSupports*>(get_current())->is_in_editing_mode())
        {
//            canvas.set_cursor(GLCanvas3D::Cross);
            processed = true;
        }
    }

    if (processed)
        canvas.set_as_dirty();

    return processed;
}

void GLGizmosManager::reset()
{
    for (GizmosMap::value_type& gizmo : m_gizmos)
    {
        delete gizmo.second;
        gizmo.second = nullptr;
    }

    m_gizmos.clear();
}

void GLGizmosManager::do_render_overlay(const GLCanvas3D& canvas, const Selection& selection) const
{
    if (m_gizmos.empty())
        return;

    float cnv_w = (float)canvas.get_canvas_size().get_width();
    float cnv_h = (float)canvas.get_canvas_size().get_height();
    float zoom = (float)canvas.get_camera().get_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    float height = get_total_overlay_height();
    float width = get_total_overlay_width();
#if ENABLE_SVG_ICONS
    float scaled_border = m_overlay_border * m_overlay_scale * inv_zoom;
#else
    float scaled_border = m_overlay_border * inv_zoom;
#endif // ENABLE_SVG_ICONS

    float top_x = (-0.5f * cnv_w) * inv_zoom;
    float top_y = (0.5f * height) * inv_zoom;

    float left = top_x;
    float top = top_y;
    float right = left + width * inv_zoom;
    float bottom = top - height * inv_zoom;

    // renders background
    unsigned int bg_tex_id = m_background_texture.texture.get_id();
    float bg_tex_width = (float)m_background_texture.texture.get_width();
    float bg_tex_height = (float)m_background_texture.texture.get_height();
    if ((bg_tex_id != 0) && (bg_tex_width > 0) && (bg_tex_height > 0))
    {
        float inv_bg_tex_width = (bg_tex_width != 0.0f) ? 1.0f / bg_tex_width : 0.0f;
        float inv_bg_tex_height = (bg_tex_height != 0.0f) ? 1.0f / bg_tex_height : 0.0f;

        float bg_uv_left = 0.0f;
        float bg_uv_right = 1.0f;
        float bg_uv_top = 1.0f;
        float bg_uv_bottom = 0.0f;

        float bg_left = left;
        float bg_right = right;
        float bg_top = top;
        float bg_bottom = bottom;
        float bg_width = right - left;
        float bg_height = top - bottom;
        float bg_min_size = std::min(bg_width, bg_height);

        float bg_uv_i_left = (float)m_background_texture.metadata.left * inv_bg_tex_width;
        float bg_uv_i_right = 1.0f - (float)m_background_texture.metadata.right * inv_bg_tex_width;
        float bg_uv_i_top = 1.0f - (float)m_background_texture.metadata.top * inv_bg_tex_height;
        float bg_uv_i_bottom = (float)m_background_texture.metadata.bottom * inv_bg_tex_height;

        float bg_i_left = bg_left + scaled_border;
        float bg_i_right = bg_right - scaled_border;
        float bg_i_top = bg_top - scaled_border;
        float bg_i_bottom = bg_bottom + scaled_border;

        bg_uv_left = bg_uv_i_left;
        bg_i_left = bg_left;

        if ((m_overlay_border > 0) && (bg_uv_top != bg_uv_i_top))
        {
            if (bg_uv_left != bg_uv_i_left)
                GLTexture::render_sub_texture(bg_tex_id, bg_left, bg_i_left, bg_i_top, bg_top, { { bg_uv_left, bg_uv_i_top }, { bg_uv_i_left, bg_uv_i_top }, { bg_uv_i_left, bg_uv_top }, { bg_uv_left, bg_uv_top } });

            GLTexture::render_sub_texture(bg_tex_id, bg_i_left, bg_i_right, bg_i_top, bg_top, { { bg_uv_i_left, bg_uv_i_top }, { bg_uv_i_right, bg_uv_i_top }, { bg_uv_i_right, bg_uv_top }, { bg_uv_i_left, bg_uv_top } });

            if (bg_uv_right != bg_uv_i_right)
                GLTexture::render_sub_texture(bg_tex_id, bg_i_right, bg_right, bg_i_top, bg_top, { { bg_uv_i_right, bg_uv_i_top }, { bg_uv_right, bg_uv_i_top }, { bg_uv_right, bg_uv_top }, { bg_uv_i_right, bg_uv_top } });
        }

        if ((m_overlay_border > 0) && (bg_uv_left != bg_uv_i_left))
            GLTexture::render_sub_texture(bg_tex_id, bg_left, bg_i_left, bg_i_bottom, bg_i_top, { { bg_uv_left, bg_uv_i_bottom }, { bg_uv_i_left, bg_uv_i_bottom }, { bg_uv_i_left, bg_uv_i_top }, { bg_uv_left, bg_uv_i_top } });

        GLTexture::render_sub_texture(bg_tex_id, bg_i_left, bg_i_right, bg_i_bottom, bg_i_top, { { bg_uv_i_left, bg_uv_i_bottom }, { bg_uv_i_right, bg_uv_i_bottom }, { bg_uv_i_right, bg_uv_i_top }, { bg_uv_i_left, bg_uv_i_top } });

        if ((m_overlay_border > 0) && (bg_uv_right != bg_uv_i_right))
            GLTexture::render_sub_texture(bg_tex_id, bg_i_right, bg_right, bg_i_bottom, bg_i_top, { { bg_uv_i_right, bg_uv_i_bottom }, { bg_uv_right, bg_uv_i_bottom }, { bg_uv_right, bg_uv_i_top }, { bg_uv_i_right, bg_uv_i_top } });

        if ((m_overlay_border > 0) && (bg_uv_bottom != bg_uv_i_bottom))
        {
            if (bg_uv_left != bg_uv_i_left)
                GLTexture::render_sub_texture(bg_tex_id, bg_left, bg_i_left, bg_bottom, bg_i_bottom, { { bg_uv_left, bg_uv_bottom }, { bg_uv_i_left, bg_uv_bottom }, { bg_uv_i_left, bg_uv_i_bottom }, { bg_uv_left, bg_uv_i_bottom } });

            GLTexture::render_sub_texture(bg_tex_id, bg_i_left, bg_i_right, bg_bottom, bg_i_bottom, { { bg_uv_i_left, bg_uv_bottom }, { bg_uv_i_right, bg_uv_bottom }, { bg_uv_i_right, bg_uv_i_bottom }, { bg_uv_i_left, bg_uv_i_bottom } });

            if (bg_uv_right != bg_uv_i_right)
                GLTexture::render_sub_texture(bg_tex_id, bg_i_right, bg_right, bg_bottom, bg_i_bottom, { { bg_uv_i_right, bg_uv_bottom }, { bg_uv_right, bg_uv_bottom }, { bg_uv_right, bg_uv_i_bottom }, { bg_uv_i_right, bg_uv_i_bottom } });
        }
    }

#if ENABLE_SVG_ICONS
    top_x += scaled_border;
    top_y -= scaled_border;
    float scaled_gap_y = m_overlay_gap_y * m_overlay_scale * inv_zoom;

    float scaled_icons_size = m_overlay_icons_size * m_overlay_scale * inv_zoom;
    float scaled_stride_y = scaled_icons_size + scaled_gap_y;
    unsigned int icons_texture_id = m_icons_texture.get_id();
    unsigned int tex_width = m_icons_texture.get_width();
    unsigned int tex_height = m_icons_texture.get_height();
    float inv_tex_width = (tex_width != 0) ? 1.0f / (float)tex_width : 0.0f;
    float inv_tex_height = (tex_height != 0) ? 1.0f / (float)tex_height : 0.0f;
#else
    top_x += m_overlay_border * inv_zoom;
    top_y -= m_overlay_border * inv_zoom;
    float scaled_gap_y = m_overlay_gap_y * inv_zoom;

    float scaled_icons_size = (float)m_icons_texture.metadata.icon_size * m_overlay_icons_scale * inv_zoom;
    unsigned int icons_texture_id = m_icons_texture.texture.get_id();
    unsigned int texture_size = m_icons_texture.texture.get_width();
    float inv_texture_size = (texture_size != 0) ? 1.0f / (float)texture_size : 0.0f;
#endif // ENABLE_SVG_ICONS

#if ENABLE_SVG_ICONS
    if ((icons_texture_id == 0) || (tex_width <= 0) || (tex_height <= 0))
        return;
#endif // ENABLE_SVG_ICONS

    for (GizmosMap::const_iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        if ((it->second == nullptr) || !it->second->is_selectable())
            continue;

        unsigned int sprite_id = it->second->get_sprite_id();
        GLGizmoBase::EState state = it->second->get_state();

#if ENABLE_SVG_ICONS
        float u_icon_size = m_overlay_icons_size * m_overlay_scale * inv_tex_width;
        float v_icon_size = m_overlay_icons_size * m_overlay_scale * inv_tex_height;
        float v_top = sprite_id * v_icon_size;
        float u_left = state * u_icon_size;
        float v_bottom = v_top + v_icon_size;
        float u_right = u_left + u_icon_size;
#else
        float uv_icon_size = (float)m_icons_texture.metadata.icon_size * inv_texture_size;
        float v_top = sprite_id * uv_icon_size;
        float u_left = state * uv_icon_size;
        float v_bottom = v_top + uv_icon_size;
        float u_right = u_left + uv_icon_size;
#endif // ENABLE_SVG_ICONS

        GLTexture::render_sub_texture(icons_texture_id, top_x, top_x + scaled_icons_size, top_y - scaled_icons_size, top_y, { { u_left, v_bottom }, { u_right, v_bottom }, { u_right, v_top }, { u_left, v_top } });
        if (it->second->get_state() == GLGizmoBase::On) {
            float toolbar_top = (float)cnv_h - canvas.get_view_toolbar_height();
#if ENABLE_SVG_ICONS
            it->second->render_input_window(width, 0.5f * cnv_h - top_y * zoom, toolbar_top, selection);
#else
            it->second->render_input_window(2.0f * m_overlay_border + icon_size * zoom, 0.5f * cnv_h - top_y * zoom, toolbar_top, selection);
#endif // ENABLE_SVG_ICONS
        }
#if ENABLE_SVG_ICONS
        top_y -= scaled_stride_y;
#else
        top_y -= (scaled_icons_size + scaled_gap_y);
#endif // ENABLE_SVG_ICONS
    }
}

float GLGizmosManager::get_total_overlay_height() const
{
#if ENABLE_SVG_ICONS
    float scaled_icons_size = m_overlay_icons_size * m_overlay_scale;
    float scaled_border = m_overlay_border * m_overlay_scale;
    float scaled_gap_y = m_overlay_gap_y * m_overlay_scale;
    float scaled_stride_y = scaled_icons_size + scaled_gap_y;
    float height = 2.0f * scaled_border;
#else
    float height = 2.0f * m_overlay_border;

    float scaled_icons_size = (float)m_icons_texture.metadata.icon_size * m_overlay_icons_scale;
#endif // ENABLE_SVG_ICONS

    for (GizmosMap::const_iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        if ((it->second == nullptr) || !it->second->is_selectable())
            continue;

#if ENABLE_SVG_ICONS
        height += scaled_stride_y;
#else
        height += (scaled_icons_size + m_overlay_gap_y);
#endif // ENABLE_SVG_ICONS
    }

#if ENABLE_SVG_ICONS
    return height - scaled_gap_y;
#else
    return height - m_overlay_gap_y;
#endif // ENABLE_SVG_ICONS
}

float GLGizmosManager::get_total_overlay_width() const
{
#if ENABLE_SVG_ICONS
    return (2.0f * m_overlay_border + m_overlay_icons_size) * m_overlay_scale;
#else
    return (float)m_icons_texture.metadata.icon_size * m_overlay_icons_scale + 2.0f * m_overlay_border;
#endif // ENABLE_SVG_ICONS
}

GLGizmoBase* GLGizmosManager::get_current() const
{
    GizmosMap::const_iterator it = m_gizmos.find(m_current);
    return (it != m_gizmos.end()) ? it->second : nullptr;
}

#if ENABLE_SVG_ICONS
bool GLGizmosManager::generate_icons_texture() const
{
    std::string path = resources_dir() + "/icons/";
    std::vector<std::string> filenames;
    for (GizmosMap::const_iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        if (it->second != nullptr)
        {
            const std::string& icon_filename = it->second->get_icon_filename();
            if (!icon_filename.empty())
                filenames.push_back(path + icon_filename);
        }
    }

    std::vector<std::pair<int, bool>> states;
    states.push_back(std::make_pair(1, false));
    states.push_back(std::make_pair(0, false));
    states.push_back(std::make_pair(0, true));

    bool res = m_icons_texture.load_from_svg_files_as_sprites_array(filenames, states, (unsigned int)(m_overlay_icons_size * m_overlay_scale), true);
    if (res)
        m_icons_texture_dirty = false;

    return res;
}
#endif // ENABLE_SVG_ICONS

void GLGizmosManager::update_on_off_state(const GLCanvas3D& canvas, const Vec2d& mouse_pos, const Selection& selection)
{
    if (!m_enabled)
        return;

    float cnv_h = (float)canvas.get_canvas_size().get_height();
    float height = get_total_overlay_height();

#if ENABLE_SVG_ICONS
    float scaled_icons_size = m_overlay_icons_size * m_overlay_scale;
    float scaled_border = m_overlay_border * m_overlay_scale;
    float scaled_gap_y = m_overlay_gap_y * m_overlay_scale;
    float scaled_stride_y = scaled_icons_size + scaled_gap_y;
    float top_y = 0.5f * (cnv_h - height) + scaled_border;
#else
    float top_y = 0.5f * (cnv_h - height) + m_overlay_border;
    float scaled_icons_size = (float)m_icons_texture.metadata.icon_size * m_overlay_icons_scale;
#endif // ENABLE_SVG_ICONS

    for (GizmosMap::iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        if ((it->second == nullptr) || !it->second->is_selectable())
            continue;

#if ENABLE_SVG_ICONS
        bool inside = (scaled_border <= (float)mouse_pos(0)) && ((float)mouse_pos(0) <= scaled_border + scaled_icons_size) && (top_y <= (float)mouse_pos(1)) && ((float)mouse_pos(1) <= top_y + scaled_icons_size);
#else
        bool inside = (m_overlay_border <= (float)mouse_pos(0)) && ((float)mouse_pos(0) <= m_overlay_border + scaled_icons_size) && (top_y <= (float)mouse_pos(1)) && ((float)mouse_pos(1) <= top_y + scaled_icons_size);
#endif // ENABLE_SVG_ICONS
        if (it->second->is_activable(selection) && inside)
        {
            if ((it->second->get_state() == GLGizmoBase::On))
            {
                it->second->set_state(GLGizmoBase::Hover);
                m_current = Undefined;
            }
            else if ((it->second->get_state() == GLGizmoBase::Hover))
            {
                it->second->set_state(GLGizmoBase::On);
                m_current = it->first;
            }
        }
        else
            it->second->set_state(GLGizmoBase::Off);

#if ENABLE_SVG_ICONS
        top_y += scaled_stride_y;
#else
        top_y += (scaled_icons_size + m_overlay_gap_y);
#endif // ENABLE_SVG_ICONS
    }

    GizmosMap::iterator it = m_gizmos.find(m_current);
    if ((it != m_gizmos.end()) && (it->second != nullptr) && (it->second->get_state() != GLGizmoBase::On))
        it->second->set_state(GLGizmoBase::On);
}

std::string GLGizmosManager::update_hover_state(const GLCanvas3D& canvas, const Vec2d& mouse_pos)
{
    std::string name = "";

    if (!m_enabled)
        return name;

    const Selection& selection = canvas.get_selection();

    float cnv_h = (float)canvas.get_canvas_size().get_height();
    float height = get_total_overlay_height();
#if ENABLE_SVG_ICONS
    float scaled_icons_size = m_overlay_icons_size * m_overlay_scale;
    float scaled_border = m_overlay_border * m_overlay_scale;
    float scaled_gap_y = m_overlay_gap_y * m_overlay_scale;
    float scaled_stride_y = scaled_icons_size + scaled_gap_y;
    float top_y = 0.5f * (cnv_h - height) + scaled_border;
#else
    float top_y = 0.5f * (cnv_h - height) + m_overlay_border;
    float scaled_icons_size = (float)m_icons_texture.metadata.icon_size * m_overlay_icons_scale;
#endif // ENABLE_SVG_ICONS

    for (GizmosMap::iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        if ((it->second == nullptr) || !it->second->is_selectable())
            continue;

#if ENABLE_SVG_ICONS
        bool inside = (scaled_border <= (float)mouse_pos(0)) && ((float)mouse_pos(0) <= scaled_border + scaled_icons_size) && (top_y <= (float)mouse_pos(1)) && ((float)mouse_pos(1) <= top_y + scaled_icons_size);
#else
        bool inside = (m_overlay_border <= (float)mouse_pos(0)) && ((float)mouse_pos(0) <= m_overlay_border + scaled_icons_size) && (top_y <= (float)mouse_pos(1)) && ((float)mouse_pos(1) <= top_y + scaled_icons_size);
#endif // ENABLE_SVG_ICONS
        if (inside)
            name = it->second->get_name();

        if (it->second->is_activable(selection) && (it->second->get_state() != GLGizmoBase::On))
            it->second->set_state(inside ? GLGizmoBase::Hover : GLGizmoBase::Off);

#if ENABLE_SVG_ICONS
        top_y += scaled_stride_y;
#else
        top_y += (scaled_icons_size + m_overlay_gap_y);
#endif // ENABLE_SVG_ICONS
    }

    return name;
}

bool GLGizmosManager::overlay_contains_mouse(const GLCanvas3D& canvas, const Vec2d& mouse_pos) const
{
    if (!m_enabled)
        return false;

    float cnv_h = (float)canvas.get_canvas_size().get_height();
    float height = get_total_overlay_height();

#if ENABLE_SVG_ICONS
    float scaled_icons_size = m_overlay_icons_size * m_overlay_scale;
    float scaled_border = m_overlay_border * m_overlay_scale;
    float scaled_gap_y = m_overlay_gap_y * m_overlay_scale;
    float scaled_stride_y = scaled_icons_size + scaled_gap_y;
    float top_y = 0.5f * (cnv_h - height) + scaled_border;
#else
    float top_y = 0.5f * (cnv_h - height) + m_overlay_border;
    float scaled_icons_size = (float)m_icons_texture.metadata.icon_size * m_overlay_icons_scale;
#endif // ENABLE_SVG_ICONS

    for (GizmosMap::const_iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        if ((it->second == nullptr) || !it->second->is_selectable())
            continue;

#if ENABLE_SVG_ICONS
        if ((scaled_border <= (float)mouse_pos(0)) && ((float)mouse_pos(0) <= scaled_border + scaled_icons_size) && (top_y <= (float)mouse_pos(1)) && ((float)mouse_pos(1) <= top_y + scaled_icons_size))
#else
        if ((m_overlay_border <= (float)mouse_pos(0)) && ((float)mouse_pos(0) <= m_overlay_border + scaled_icons_size) && (top_y <= (float)mouse_pos(1)) && ((float)mouse_pos(1) <= top_y + scaled_icons_size))
#endif // ENABLE_SVG_ICONS
            return true;

#if ENABLE_SVG_ICONS
        top_y += scaled_stride_y;
#else
        top_y += (scaled_icons_size + m_overlay_gap_y);
#endif // ENABLE_SVG_ICONS
    }

    return false;
}

bool GLGizmosManager::grabber_contains_mouse() const
{
    if (!m_enabled)
        return false;

    GLGizmoBase* curr = get_current();
    return (curr != nullptr) ? (curr->get_hover_id() != -1) : false;
}

} // namespace GUI
} // namespace Slic3r
