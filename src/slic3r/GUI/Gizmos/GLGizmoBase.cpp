#include "GLGizmoBase.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "slic3r/GUI/MeshUtils.hpp"





// TODO: Display tooltips quicker on Linux



namespace Slic3r {
namespace GUI {

const float GLGizmoBase::Grabber::SizeFactor = 0.05f;
const float GLGizmoBase::Grabber::MinHalfSize = 1.5f;
const float GLGizmoBase::Grabber::DraggingScaleFactor = 1.25f;

GLGizmoBase::Grabber::Grabber()
    : center(Vec3d::Zero())
    , angles(Vec3d::Zero())
    , dragging(false)
    , enabled(true)
{
    color[0] = 1.0f;
    color[1] = 1.0f;
    color[2] = 1.0f;
    color[3] = 1.0f;
}

void GLGizmoBase::Grabber::render(bool hover, float size) const
{
    float render_color[4];
    if (hover)
    {
        render_color[0] = 1.0f - color[0];
        render_color[1] = 1.0f - color[1];
        render_color[2] = 1.0f - color[2];
        render_color[3] = color[3];
    }
    else
        ::memcpy((void*)render_color, (const void*)color, 4 * sizeof(float));

    render(size, render_color, true);
}

float GLGizmoBase::Grabber::get_half_size(float size) const
{
    return std::max(size * SizeFactor, MinHalfSize);
}

float GLGizmoBase::Grabber::get_dragging_half_size(float size) const
{
    return get_half_size(size) * DraggingScaleFactor;
}

void GLGizmoBase::Grabber::render(float size, const float* render_color, bool use_lighting) const
{
    float half_size = dragging ? get_dragging_half_size(size) : get_half_size(size);

    if (use_lighting)
        glsafe(::glEnable(GL_LIGHTING));

    glsafe(::glColor4fv(render_color));

    glsafe(::glPushMatrix());
    glsafe(::glTranslated(center(0), center(1), center(2)));

    glsafe(::glRotated(Geometry::rad2deg(angles(2)), 0.0, 0.0, 1.0));
    glsafe(::glRotated(Geometry::rad2deg(angles(1)), 0.0, 1.0, 0.0));
    glsafe(::glRotated(Geometry::rad2deg(angles(0)), 1.0, 0.0, 0.0));

    // face min x
    glsafe(::glPushMatrix());
    glsafe(::glTranslatef(-(GLfloat)half_size, 0.0f, 0.0f));
    glsafe(::glRotatef(-90.0f, 0.0f, 1.0f, 0.0f));
    render_face(half_size);
    glsafe(::glPopMatrix());

    // face max x
    glsafe(::glPushMatrix());
    glsafe(::glTranslatef((GLfloat)half_size, 0.0f, 0.0f));
    glsafe(::glRotatef(90.0f, 0.0f, 1.0f, 0.0f));
    render_face(half_size);
    glsafe(::glPopMatrix());

    // face min y
    glsafe(::glPushMatrix());
    glsafe(::glTranslatef(0.0f, -(GLfloat)half_size, 0.0f));
    glsafe(::glRotatef(90.0f, 1.0f, 0.0f, 0.0f));
    render_face(half_size);
    glsafe(::glPopMatrix());

    // face max y
    glsafe(::glPushMatrix());
    glsafe(::glTranslatef(0.0f, (GLfloat)half_size, 0.0f));
    glsafe(::glRotatef(-90.0f, 1.0f, 0.0f, 0.0f));
    render_face(half_size);
    glsafe(::glPopMatrix());

    // face min z
    glsafe(::glPushMatrix());
    glsafe(::glTranslatef(0.0f, 0.0f, -(GLfloat)half_size));
    glsafe(::glRotatef(180.0f, 1.0f, 0.0f, 0.0f));
    render_face(half_size);
    glsafe(::glPopMatrix());

    // face max z
    glsafe(::glPushMatrix());
    glsafe(::glTranslatef(0.0f, 0.0f, (GLfloat)half_size));
    render_face(half_size);
    glsafe(::glPopMatrix());

    glsafe(::glPopMatrix());

    if (use_lighting)
        glsafe(::glDisable(GL_LIGHTING));
}

void GLGizmoBase::Grabber::render_face(float half_size) const
{
    ::glBegin(GL_TRIANGLES);
    ::glNormal3f(0.0f, 0.0f, 1.0f);
    ::glVertex3f(-(GLfloat)half_size, -(GLfloat)half_size, 0.0f);
    ::glVertex3f((GLfloat)half_size, -(GLfloat)half_size, 0.0f);
    ::glVertex3f((GLfloat)half_size, (GLfloat)half_size, 0.0f);
    ::glVertex3f((GLfloat)half_size, (GLfloat)half_size, 0.0f);
    ::glVertex3f(-(GLfloat)half_size, (GLfloat)half_size, 0.0f);
    ::glVertex3f(-(GLfloat)half_size, -(GLfloat)half_size, 0.0f);
    glsafe(::glEnd());
}


GLGizmoBase::GLGizmoBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, CommonGizmosData* common_data_ptr)
    : m_parent(parent)
    , m_group_id(-1)
    , m_state(Off)
    , m_shortcut_key(0)
    , m_icon_filename(icon_filename)
    , m_sprite_id(sprite_id)
    , m_hover_id(-1)
    , m_dragging(false)
    , m_imgui(wxGetApp().imgui())
    , m_first_input_window_render(true)
    , m_c(common_data_ptr)
{
    ::memcpy((void*)m_base_color, (const void*)DEFAULT_BASE_COLOR, 4 * sizeof(float));
    ::memcpy((void*)m_drag_color, (const void*)DEFAULT_DRAG_COLOR, 4 * sizeof(float));
    ::memcpy((void*)m_highlight_color, (const void*)DEFAULT_HIGHLIGHT_COLOR, 4 * sizeof(float));
}

void GLGizmoBase::set_hover_id(int id)
{
    if (m_grabbers.empty() || (id < (int)m_grabbers.size()))
    {
        m_hover_id = id;
        on_set_hover_id();
    }
}

void GLGizmoBase::set_highlight_color(const float* color)
{
    if (color != nullptr)
        ::memcpy((void*)m_highlight_color, (const void*)color, 4 * sizeof(float));
}

void GLGizmoBase::enable_grabber(unsigned int id)
{
    if (id < m_grabbers.size())
        m_grabbers[id].enabled = true;

    on_enable_grabber(id);
}

void GLGizmoBase::disable_grabber(unsigned int id)
{
    if (id < m_grabbers.size())
        m_grabbers[id].enabled = false;

    on_disable_grabber(id);
}

void GLGizmoBase::start_dragging()
{
    m_dragging = true;

    for (int i = 0; i < (int)m_grabbers.size(); ++i)
    {
        m_grabbers[i].dragging = (m_hover_id == i);
    }

    on_start_dragging();
}

void GLGizmoBase::stop_dragging()
{
    m_dragging = false;

    for (int i = 0; i < (int)m_grabbers.size(); ++i)
    {
        m_grabbers[i].dragging = false;
    }

    on_stop_dragging();
}

void GLGizmoBase::update(const UpdateData& data)
{
    if (m_hover_id != -1)
        on_update(data);
}

std::array<float, 4> GLGizmoBase::picking_color_component(unsigned int id) const
{
    static const float INV_255 = 1.0f / 255.0f;

    id = BASE_ID - id;

    if (m_group_id > -1)
        id -= m_group_id;

    // color components are encoded to match the calculation of volume_id made into GLCanvas3D::_picking_pass()
    return std::array<float, 4> { 
		float((id >> 0) & 0xff) * INV_255, // red
		float((id >> 8) & 0xff) * INV_255, // green
		float((id >> 16) & 0xff) * INV_255, // blue
		float(picking_checksum_alpha_channel(id & 0xff, (id >> 8) & 0xff, (id >> 16) & 0xff))* INV_255 // checksum for validating against unwanted alpha blending and multi sampling
	};
}

void GLGizmoBase::render_grabbers(const BoundingBoxf3& box) const
{
    render_grabbers((float)((box.size()(0) + box.size()(1) + box.size()(2)) / 3.0));
}

void GLGizmoBase::render_grabbers(float size) const
{
    for (int i = 0; i < (int)m_grabbers.size(); ++i)
    {
        if (m_grabbers[i].enabled)
            m_grabbers[i].render((m_hover_id == i), size);
    }
}

void GLGizmoBase::render_grabbers_for_picking(const BoundingBoxf3& box) const
{
    float mean_size = (float)((box.size()(0) + box.size()(1) + box.size()(2)) / 3.0);

    for (unsigned int i = 0; i < (unsigned int)m_grabbers.size(); ++i)
    {
        if (m_grabbers[i].enabled)
        {
            std::array<float, 4> color = picking_color_component(i);
            m_grabbers[i].color[0] = color[0];
            m_grabbers[i].color[1] = color[1];
            m_grabbers[i].color[2] = color[2];
            m_grabbers[i].color[3] = color[3];
            m_grabbers[i].render_for_picking(mean_size);
        }
    }
}


void GLGizmoBase::set_tooltip(const std::string& tooltip) const
{
    m_parent.set_tooltip(tooltip);
}

std::string GLGizmoBase::format(float value, unsigned int decimals) const
{
    return Slic3r::string_printf("%.*f", decimals, value);
}

void GLGizmoBase::render_input_window(float x, float y, float bottom_limit)
{
    on_render_input_window(x, y, bottom_limit);
    if (m_first_input_window_render)
    {
        // for some reason, the imgui dialogs are not shown on screen in the 1st frame where they are rendered, but show up only with the 2nd rendered frame
        // so, we forces another frame rendering the first time the imgui window is shown
        m_parent.set_as_dirty();
        m_first_input_window_render = false;
    }
}

// Produce an alpha channel checksum for the red green blue components. The alpha channel may then be used to verify, whether the rgb components
// were not interpolated by alpha blending or multi sampling.
unsigned char picking_checksum_alpha_channel(unsigned char red, unsigned char green, unsigned char blue)
{
	// 8 bit hash for the color
	unsigned char b = ((((37 * red) + green) & 0x0ff) * 37 + blue) & 0x0ff;
	// Increase enthropy by a bit reversal
	b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
	b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
	b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
	// Flip every second bit to increase the enthropy even more.
	b ^= 0x55;
	return b;
}



bool CommonGizmosData::update_from_backend(GLCanvas3D& canvas, ModelObject* model_object)
{
    recent_update = false;

    if (m_model_object != model_object
    || (model_object && m_model_object_id != model_object->id())) {
        m_model_object = model_object;
        m_print_object_idx = -1;
        m_mesh_raycaster.reset();
        m_object_clipper.reset();
        m_supports_clipper.reset();
        if (m_model_object) {
            m_active_instance = canvas.get_selection().get_instance_idx();
            m_active_instance_bb_radius = m_model_object->instance_bounding_box(m_active_instance).radius();
        }

        recent_update = true;
    }


    if (! m_model_object || ! canvas.get_selection().is_from_single_instance())
        return false;

    int old_po_idx = m_print_object_idx;

    // First we need a pointer to the respective SLAPrintObject. The index into objects vector is
    // cached so we don't have todo it on each render. We only search for the po if needed:
    if (m_print_object_idx < 0 || (int)canvas.sla_print()->objects().size() != m_print_objects_count) {
        m_print_objects_count = canvas.sla_print()->objects().size();
        m_print_object_idx = -1;
        for (const SLAPrintObject* po : canvas.sla_print()->objects()) {
            ++m_print_object_idx;
            if (po->model_object()->id() == m_model_object->id())
                break;
        }
    }

    m_mesh = nullptr;
    // Load either the model_object mesh, or one provided by the backend
    // This mesh does not account for the possible Z up SLA offset.
    // The backend mesh needs to be transformed and because a pointer to it is
    // saved, a copy is stored as a member (FIXME)
    if (m_print_object_idx >=0) {
        const SLAPrintObject* po = canvas.sla_print()->objects()[m_print_object_idx];
        if (po->is_step_done(slaposHollowing)) {
            m_backend_mesh_transformed = po->get_mesh_to_print();
            m_backend_mesh_transformed.transform(canvas.sla_print()->sla_trafo(*m_model_object).inverse());
            m_mesh = &m_backend_mesh_transformed;
        }
    }

    if (! m_mesh) {
        m_mesh = &m_model_object->volumes.front()->mesh();
        m_backend_mesh_transformed.clear();
    }

    m_model_object_id = m_model_object->id();

    if (m_mesh != m_old_mesh) {
        m_mesh_raycaster.reset(new MeshRaycaster(*m_mesh));
        m_object_clipper.reset();
        m_supports_clipper.reset();
        m_old_mesh = m_mesh;
        recent_update = true;
        return true;
    }
    if (! recent_update)
        recent_update = m_print_object_idx < 0 && old_po_idx >= 0;

    return m_print_object_idx < 0 ? old_po_idx >=0 : false;
}



} // namespace GUI
} // namespace Slic3r
