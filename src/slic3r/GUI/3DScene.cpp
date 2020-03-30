#include <GL/glew.h>

#include "3DScene.hpp"

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/GCode/PreviewData.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/GCode/Analyzer.hpp"
#include "slic3r/GUI/BitmapCache.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Utils.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <boost/log/trivial.hpp>

#include <boost/filesystem/operations.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <boost/nowide/cstdio.hpp>

#include <Eigen/Dense>

#ifdef HAS_GLSAFE
void glAssertRecentCallImpl(const char *file_name, unsigned int line, const char *function_name)
{
    GLenum err = glGetError();
    if (err == GL_NO_ERROR)
        return;
    const char *sErr = 0;
    switch (err) {
    case GL_INVALID_ENUM:       sErr = "Invalid Enum";      break;
    case GL_INVALID_VALUE:      sErr = "Invalid Value";     break;
    // be aware that GL_INVALID_OPERATION is generated if glGetError is executed between the execution of glBegin and the corresponding execution of glEnd 
    case GL_INVALID_OPERATION:  sErr = "Invalid Operation"; break;
    case GL_STACK_OVERFLOW:     sErr = "Stack Overflow";    break;
    case GL_STACK_UNDERFLOW:    sErr = "Stack Underflow";   break;
    case GL_OUT_OF_MEMORY:      sErr = "Out Of Memory";     break;
    default:                    sErr = "Unknown";           break;
    }
	BOOST_LOG_TRIVIAL(error) << "OpenGL error in " << file_name << ":" << line << ", function " << function_name << "() : " << (int)err << " - " << sErr;
    assert(false);
}
#endif

namespace Slic3r {

void GLIndexedVertexArray::load_mesh_full_shading(const TriangleMesh &mesh)
{
    assert(triangle_indices.empty() && vertices_and_normals_interleaved_size == 0);
    assert(quad_indices.empty() && triangle_indices_size == 0);
    assert(vertices_and_normals_interleaved.size() % 6 == 0 && quad_indices_size == vertices_and_normals_interleaved.size());

    this->vertices_and_normals_interleaved.reserve(this->vertices_and_normals_interleaved.size() + 3 * 3 * 2 * mesh.facets_count());

    unsigned int vertices_count = 0;
    for (int i = 0; i < (int)mesh.stl.stats.number_of_facets; ++i) {
        const stl_facet &facet = mesh.stl.facet_start[i];
        for (int j = 0; j < 3; ++j)
            this->push_geometry(facet.vertex[j](0), facet.vertex[j](1), facet.vertex[j](2), facet.normal(0), facet.normal(1), facet.normal(2));

        this->push_triangle(vertices_count, vertices_count + 1, vertices_count + 2);
        vertices_count += 3;
    }
}

void GLIndexedVertexArray::finalize_geometry(bool opengl_initialized)
{
    assert(this->vertices_and_normals_interleaved_VBO_id == 0);
    assert(this->triangle_indices_VBO_id == 0);
    assert(this->quad_indices_VBO_id == 0);

	if (! opengl_initialized) {
		// Shrink the data vectors to conserve memory in case the data cannot be transfered to the OpenGL driver yet.
		this->shrink_to_fit();
		return;
	}

    if (! this->vertices_and_normals_interleaved.empty()) {
        glsafe(::glGenBuffers(1, &this->vertices_and_normals_interleaved_VBO_id));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, this->vertices_and_normals_interleaved_VBO_id));
        glsafe(::glBufferData(GL_ARRAY_BUFFER, this->vertices_and_normals_interleaved.size() * 4, this->vertices_and_normals_interleaved.data(), GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
        this->vertices_and_normals_interleaved.clear();
    }
    if (! this->triangle_indices.empty()) {
        glsafe(::glGenBuffers(1, &this->triangle_indices_VBO_id));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->triangle_indices_VBO_id));
        glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, this->triangle_indices.size() * 4, this->triangle_indices.data(), GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
        this->triangle_indices.clear();
    }
    if (! this->quad_indices.empty()) {
        glsafe(::glGenBuffers(1, &this->quad_indices_VBO_id));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->quad_indices_VBO_id));
        glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, this->quad_indices.size() * 4, this->quad_indices.data(), GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
        this->quad_indices.clear();
    }
}

void GLIndexedVertexArray::release_geometry()
{
    if (this->vertices_and_normals_interleaved_VBO_id) {
        glsafe(::glDeleteBuffers(1, &this->vertices_and_normals_interleaved_VBO_id));
        this->vertices_and_normals_interleaved_VBO_id = 0;
    }
    if (this->triangle_indices_VBO_id) {
        glsafe(::glDeleteBuffers(1, &this->triangle_indices_VBO_id));
        this->triangle_indices_VBO_id = 0;
    }
    if (this->quad_indices_VBO_id) {
        glsafe(::glDeleteBuffers(1, &this->quad_indices_VBO_id));
        this->quad_indices_VBO_id = 0;
    }
    this->clear();
}

void GLIndexedVertexArray::render() const
{
    assert(this->vertices_and_normals_interleaved_VBO_id != 0);
    assert(this->triangle_indices_VBO_id != 0 || this->quad_indices_VBO_id != 0);

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, this->vertices_and_normals_interleaved_VBO_id));
    glsafe(::glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), (const void*)(3 * sizeof(float))));
    glsafe(::glNormalPointer(GL_FLOAT, 6 * sizeof(float), nullptr));

    glsafe(::glEnableClientState(GL_VERTEX_ARRAY));
    glsafe(::glEnableClientState(GL_NORMAL_ARRAY));

    // Render using the Vertex Buffer Objects.
    if (this->triangle_indices_size > 0) {
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->triangle_indices_VBO_id));
        glsafe(::glDrawElements(GL_TRIANGLES, GLsizei(this->triangle_indices_size), GL_UNSIGNED_INT, nullptr));
        glsafe(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    }
    if (this->quad_indices_size > 0) {
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->quad_indices_VBO_id));
        glsafe(::glDrawElements(GL_QUADS, GLsizei(this->quad_indices_size), GL_UNSIGNED_INT, nullptr));
        glsafe(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    }

    glsafe(::glDisableClientState(GL_VERTEX_ARRAY));
    glsafe(::glDisableClientState(GL_NORMAL_ARRAY));

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
}

void GLIndexedVertexArray::render(
    const std::pair<size_t, size_t>& tverts_range,
    const std::pair<size_t, size_t>& qverts_range) const
{
    assert(this->vertices_and_normals_interleaved_VBO_id != 0);
    assert(this->triangle_indices_VBO_id != 0 || this->quad_indices_VBO_id != 0);

    // Render using the Vertex Buffer Objects.
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, this->vertices_and_normals_interleaved_VBO_id));
    glsafe(::glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), (const void*)(3 * sizeof(float))));
    glsafe(::glNormalPointer(GL_FLOAT, 6 * sizeof(float), nullptr));

    glsafe(::glEnableClientState(GL_VERTEX_ARRAY));
    glsafe(::glEnableClientState(GL_NORMAL_ARRAY));

    if (this->triangle_indices_size > 0) {
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->triangle_indices_VBO_id));
        glsafe(::glDrawElements(GL_TRIANGLES, GLsizei(std::min(this->triangle_indices_size, tverts_range.second - tverts_range.first)), GL_UNSIGNED_INT, (const void*)(tverts_range.first * 4)));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    }
    if (this->quad_indices_size > 0) {
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->quad_indices_VBO_id));
        glsafe(::glDrawElements(GL_QUADS, GLsizei(std::min(this->quad_indices_size, qverts_range.second - qverts_range.first)), GL_UNSIGNED_INT, (const void*)(qverts_range.first * 4)));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    }

    glsafe(::glDisableClientState(GL_VERTEX_ARRAY));
    glsafe(::glDisableClientState(GL_NORMAL_ARRAY));
    
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
}

const float GLVolume::SELECTED_COLOR[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
const float GLVolume::HOVER_SELECT_COLOR[4] = { 0.4f, 0.9f, 0.1f, 1.0f };
const float GLVolume::HOVER_DESELECT_COLOR[4] = { 1.0f, 0.75f, 0.75f, 1.0f };
const float GLVolume::OUTSIDE_COLOR[4] = { 0.0f, 0.38f, 0.8f, 1.0f };
const float GLVolume::SELECTED_OUTSIDE_COLOR[4] = { 0.19f, 0.58f, 1.0f, 1.0f };
const float GLVolume::DISABLED_COLOR[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
const float GLVolume::MODEL_COLOR[4][4] = {
    { 1.0f, 1.0f, 0.0f, 1.f },
    { 1.0f, 0.5f, 0.5f, 1.f },
    { 0.5f, 1.0f, 0.5f, 1.f },
    { 0.5f, 0.5f, 1.0f, 1.f }
};
const float GLVolume::SLA_SUPPORT_COLOR[4] = { 0.75f, 0.75f, 0.75f, 1.0f };
const float GLVolume::SLA_PAD_COLOR[4] = { 0.0f, 0.2f, 0.0f, 1.0f };

GLVolume::GLVolume(float r, float g, float b, float a)
    : m_transformed_bounding_box_dirty(true)
    , m_sla_shift_z(0.0)
    , m_transformed_convex_hull_bounding_box_dirty(true)
    // geometry_id == 0 -> invalid
    , geometry_id(std::pair<size_t, size_t>(0, 0))
    , extruder_id(0)
    , selected(false)
    , disabled(false)
    , printable(true)
    , is_active(true)
    , zoom_to_volumes(true)
    , shader_outside_printer_detection_enabled(false)
    , is_outside(false)
    , hover(HS_None)
    , is_modifier(false)
    , is_wipe_tower(false)
    , is_extrusion_path(false)
    , force_transparent(false)
    , force_native_color(false)
    , tverts_range(0, size_t(-1))
    , qverts_range(0, size_t(-1))
{
    color[0] = r;
    color[1] = g;
    color[2] = b;
    color[3] = a;
    set_render_color(r, g, b, a);
}

void GLVolume::set_render_color(float r, float g, float b, float a)
{
    render_color[0] = r;
    render_color[1] = g;
    render_color[2] = b;
    render_color[3] = a;
}

void GLVolume::set_render_color(const float* rgba, unsigned int size)
{
    ::memcpy((void*)render_color, (const void*)rgba, (size_t)(std::min((unsigned int)4, size) * sizeof(float)));
}

void GLVolume::set_render_color()
{
    if (force_native_color)
    {
        if (is_outside && shader_outside_printer_detection_enabled)
            set_render_color(OUTSIDE_COLOR, 4);
        else
            set_render_color(color, 4);
    }
    else {
        if (hover == HS_Select)
            set_render_color(HOVER_SELECT_COLOR, 4);
        else if (hover == HS_Deselect)
            set_render_color(HOVER_DESELECT_COLOR, 4);
        else if (selected)
            set_render_color(is_outside ? SELECTED_OUTSIDE_COLOR : SELECTED_COLOR, 4);
        else if (disabled)
            set_render_color(DISABLED_COLOR, 4);
        else if (is_outside && shader_outside_printer_detection_enabled)
            set_render_color(OUTSIDE_COLOR, 4);
        else
            set_render_color(color, 4);
    }

    if (!printable)
    {
        render_color[0] /= 4;
        render_color[1] /= 4;
        render_color[2] /= 4;
    }

    if (force_transparent)
        render_color[3] = color[3];
}

void GLVolume::set_color_from_model_volume(const ModelVolume *model_volume)
{
    if (model_volume->is_modifier()) {
        color[0] = 0.2f;
        color[1] = 1.0f;
        color[2] = 0.2f;
    }
    else if (model_volume->is_support_blocker()) {
        color[0] = 1.0f;
        color[1] = 0.2f;
        color[2] = 0.2f;
    }
    else if (model_volume->is_support_enforcer()) {
        color[0] = 0.2f;
        color[1] = 0.2f;
        color[2] = 1.0f;
    }
    color[3] = model_volume->is_model_part() ? 1.f : 0.5f;
}

Transform3d GLVolume::world_matrix() const
{
    Transform3d m = m_instance_transformation.get_matrix() * m_volume_transformation.get_matrix();
    m.translation()(2) += m_sla_shift_z;
    return m;
}

bool GLVolume::is_left_handed() const
{
    const Vec3d &m1 = m_instance_transformation.get_mirror();
    const Vec3d &m2 = m_volume_transformation.get_mirror();
    return m1.x() * m1.y() * m1.z() * m2.x() * m2.y() * m2.z() < 0.;
}

const BoundingBoxf3& GLVolume::transformed_bounding_box() const
{
    const BoundingBoxf3& box = bounding_box();
    assert(box.defined || box.min(0) >= box.max(0) || box.min(1) >= box.max(1) || box.min(2) >= box.max(2));

    if (m_transformed_bounding_box_dirty)
    {
        m_transformed_bounding_box = box.transformed(world_matrix());
        m_transformed_bounding_box_dirty = false;
    }

    return m_transformed_bounding_box;
}

const BoundingBoxf3& GLVolume::transformed_convex_hull_bounding_box() const
{
	if (m_transformed_convex_hull_bounding_box_dirty)
		m_transformed_convex_hull_bounding_box = this->transformed_convex_hull_bounding_box(world_matrix());
    return m_transformed_convex_hull_bounding_box;
}

BoundingBoxf3 GLVolume::transformed_convex_hull_bounding_box(const Transform3d &trafo) const
{
	return (m_convex_hull && m_convex_hull->stl.stats.number_of_facets > 0) ? 
		m_convex_hull->transformed_bounding_box(trafo) :
        bounding_box().transformed(trafo);
}


void GLVolume::set_range(double min_z, double max_z)
{
    this->qverts_range.first = 0;
    this->qverts_range.second = this->indexed_vertex_array.quad_indices_size;
    this->tverts_range.first = 0;
    this->tverts_range.second = this->indexed_vertex_array.triangle_indices_size;
    if (! this->print_zs.empty()) {
        // The Z layer range is specified.
        // First test whether the Z span of this object is not out of (min_z, max_z) completely.
        if (this->print_zs.front() > max_z || this->print_zs.back() < min_z) {
            this->qverts_range.second = 0;
            this->tverts_range.second = 0;
        } else {
            // Then find the lowest layer to be displayed.
            size_t i = 0;
            for (; i < this->print_zs.size() && this->print_zs[i] < min_z; ++ i);
            if (i == this->print_zs.size()) {
                // This shall not happen.
                this->qverts_range.second = 0;
                this->tverts_range.second = 0;
            } else {
                // Remember start of the layer.
                this->qverts_range.first = this->offsets[i * 2];
                this->tverts_range.first = this->offsets[i * 2 + 1];
                // Some layers are above $min_z. Which?
                for (; i < this->print_zs.size() && this->print_zs[i] <= max_z; ++ i);
                if (i < this->print_zs.size()) {
                    this->qverts_range.second = this->offsets[i * 2];
                    this->tverts_range.second = this->offsets[i * 2 + 1];
                }
            }
        }
    }
}

void GLVolume::render() const
{
    if (!is_active)
        return;

    if (this->is_left_handed())
        glFrontFace(GL_CW);
    glsafe(::glCullFace(GL_BACK));
    glsafe(::glPushMatrix());
    glsafe(::glMultMatrixd(world_matrix().data()));

    this->indexed_vertex_array.render(this->tverts_range, this->qverts_range);

    glsafe(::glPopMatrix());
    if (this->is_left_handed())
        glFrontFace(GL_CCW);
}

#if !ENABLE_SLOPE_RENDERING
void GLVolume::render(int color_id, int detection_id, int worldmatrix_id) const
{
    if (color_id >= 0)
        glsafe(::glUniform4fv(color_id, 1, (const GLfloat*)render_color));
    else
        glsafe(::glColor4fv(render_color));

    if (detection_id != -1)
        glsafe(::glUniform1i(detection_id, shader_outside_printer_detection_enabled ? 1 : 0));

    if (worldmatrix_id != -1)
        glsafe(::glUniformMatrix4fv(worldmatrix_id, 1, GL_FALSE, (const GLfloat*)world_matrix().cast<float>().data()));

    render();
}
#endif // !ENABLE_SLOPE_RENDERING

bool GLVolume::is_sla_support() const { return this->composite_id.volume_id == -int(slaposSupportTree); }
bool GLVolume::is_sla_pad() const { return this->composite_id.volume_id == -int(slaposPad); }

std::vector<int> GLVolumeCollection::load_object(
    const ModelObject       *model_object,
    int                      obj_idx,
    const std::vector<int>  &instance_idxs,
    const std::string       &color_by,
    bool 					 opengl_initialized)
{
    std::vector<int> volumes_idx;
    for (int volume_idx = 0; volume_idx < int(model_object->volumes.size()); ++volume_idx)
        for (int instance_idx : instance_idxs)
            volumes_idx.emplace_back(this->GLVolumeCollection::load_object_volume(model_object, obj_idx, volume_idx, instance_idx, color_by, opengl_initialized));
    return volumes_idx;
}

int GLVolumeCollection::load_object_volume(
    const ModelObject   *model_object,
    int                  obj_idx,
    int                  volume_idx,
    int                  instance_idx,
    const std::string   &color_by,
    bool 				 opengl_initialized)
{
    const ModelVolume   *model_volume = model_object->volumes[volume_idx];
    const int            extruder_id  = model_volume->extruder_id();
    const ModelInstance *instance 	  = model_object->instances[instance_idx];
    const TriangleMesh  &mesh 		  = model_volume->mesh();
    float 				 color[4];
    memcpy(color, GLVolume::MODEL_COLOR[((color_by == "volume") ? volume_idx : obj_idx) % 4], sizeof(float) * 3);
    /*    if (model_volume->is_support_blocker()) {
            color[0] = 1.0f;
            color[1] = 0.2f;
            color[2] = 0.2f;
        } else if (model_volume->is_support_enforcer()) {
            color[0] = 0.2f;
            color[1] = 0.2f;
            color[2] = 1.0f;
        }
        color[3] = model_volume->is_model_part() ? 1.f : 0.5f; */
    color[3] = model_volume->is_model_part() ? 1.f : 0.5f;
    this->volumes.emplace_back(new GLVolume(color));
    GLVolume& v = *this->volumes.back();
    v.set_color_from_model_volume(model_volume);
    v.indexed_vertex_array.load_mesh(mesh);
    v.indexed_vertex_array.finalize_geometry(opengl_initialized);
    v.composite_id = GLVolume::CompositeID(obj_idx, volume_idx, instance_idx);
    if (model_volume->is_model_part())
    {
        // GLVolume will reference a convex hull from model_volume!
        v.set_convex_hull(model_volume->get_convex_hull_shared_ptr());
        if (extruder_id != -1)
            v.extruder_id = extruder_id;
    }
    v.is_modifier = !model_volume->is_model_part();
    v.shader_outside_printer_detection_enabled = model_volume->is_model_part();
    v.set_instance_transformation(instance->get_transformation());
    v.set_volume_transformation(model_volume->get_transformation());

    return int(this->volumes.size() - 1);
}

// Load SLA auxiliary GLVolumes (for support trees or pad).
// This function produces volumes for multiple instances in a single shot,
// as some object specific mesh conversions may be expensive.
void GLVolumeCollection::load_object_auxiliary(
    const SLAPrintObject 		   *print_object,
    int                             obj_idx,
    // pairs of <instance_idx, print_instance_idx>
    const std::vector<std::pair<size_t, size_t>>& instances,
    SLAPrintObjectStep              milestone,
    // Timestamp of the last change of the milestone
    size_t                          timestamp,
    bool 				 			opengl_initialized)
{
    assert(print_object->is_step_done(milestone));
    Transform3d  mesh_trafo_inv = print_object->trafo().inverse();
    // Get the support mesh.
    TriangleMesh mesh = print_object->get_mesh(milestone);
    mesh.transform(mesh_trafo_inv);
    // Convex hull is required for out of print bed detection.
    TriangleMesh convex_hull = mesh.convex_hull_3d();
    for (const std::pair<size_t, size_t>& instance_idx : instances) {
        const ModelInstance& model_instance = *print_object->model_object()->instances[instance_idx.first];
        this->volumes.emplace_back(new GLVolume((milestone == slaposPad) ? GLVolume::SLA_PAD_COLOR : GLVolume::SLA_SUPPORT_COLOR));
        GLVolume& v = *this->volumes.back();
        v.indexed_vertex_array.load_mesh(mesh);
	    v.indexed_vertex_array.finalize_geometry(opengl_initialized);
        v.composite_id = GLVolume::CompositeID(obj_idx, -int(milestone), (int)instance_idx.first);
        v.geometry_id = std::pair<size_t, size_t>(timestamp, model_instance.id().id);
        // Create a copy of the convex hull mesh for each instance. Use a move operator on the last instance.
        if (&instance_idx == &instances.back())
            v.set_convex_hull(std::move(convex_hull));
        else
            v.set_convex_hull(convex_hull);
        v.is_modifier = false;
        v.shader_outside_printer_detection_enabled = (milestone == slaposSupportTree);
        v.set_instance_transformation(model_instance.get_transformation());
        // Leave the volume transformation at identity.
        // v.set_volume_transformation(model_volume->get_transformation());
    }
}

int GLVolumeCollection::load_wipe_tower_preview(
    int obj_idx, float pos_x, float pos_y, float width, float depth, float height, float rotation_angle, bool size_unknown, float brim_width, bool opengl_initialized)
{
    if (depth < 0.01f)
        return int(this->volumes.size() - 1);
    if (height == 0.0f)
        height = 0.1f;
    Point origin_of_rotation(0.f, 0.f);
    TriangleMesh mesh;
    float color[4] = { 0.5f, 0.5f, 0.0f, 1.f };

    // In case we don't know precise dimensions of the wipe tower yet, we'll draw the box with different color with one side jagged:
    if (size_unknown) {
        color[0] = 0.9f;
        color[1] = 0.6f;

        depth = std::max(depth, 10.f); // Too narrow tower would interfere with the teeth. The estimate is not precise anyway.
        float min_width = 30.f;
        // We'll now create the box with jagged edge. y-coordinates of the pre-generated model are shifted so that the front
        // edge has y=0 and centerline of the back edge has y=depth:
        Pointf3s points;
        std::vector<Vec3i> facets;
        float out_points_idx[][3] = { { 0, -depth, 0 }, { 0, 0, 0 }, { 38.453f, 0, 0 }, { 61.547f, 0, 0 }, { 100.0f, 0, 0 }, { 100.0f, -depth, 0 }, { 55.7735f, -10.0f, 0 }, { 44.2265f, 10.0f, 0 },
        { 38.453f, 0, 1 }, { 0, 0, 1 }, { 0, -depth, 1 }, { 100.0f, -depth, 1 }, { 100.0f, 0, 1 }, { 61.547f, 0, 1 }, { 55.7735f, -10.0f, 1 }, { 44.2265f, 10.0f, 1 } };
        int out_facets_idx[][3] = { { 0, 1, 2 }, { 3, 4, 5 }, { 6, 5, 0 }, { 3, 5, 6 }, { 6, 2, 7 }, { 6, 0, 2 }, { 8, 9, 10 }, { 11, 12, 13 }, { 10, 11, 14 }, { 14, 11, 13 }, { 15, 8, 14 },
                                   {8, 10, 14}, {3, 12, 4}, {3, 13, 12}, {6, 13, 3}, {6, 14, 13}, {7, 14, 6}, {7, 15, 14}, {2, 15, 7}, {2, 8, 15}, {1, 8, 2}, {1, 9, 8},
                                   {0, 9, 1}, {0, 10, 9}, {5, 10, 0}, {5, 11, 10}, {4, 11, 5}, {4, 12, 11} };
        for (int i = 0; i < 16; ++i)
            points.emplace_back(out_points_idx[i][0] / (100.f / min_width), out_points_idx[i][1] + depth, out_points_idx[i][2]);
        for (int i = 0; i < 28; ++i)
            facets.emplace_back(out_facets_idx[i][0], out_facets_idx[i][1], out_facets_idx[i][2]);
        TriangleMesh tooth_mesh(points, facets);

        // We have the mesh ready. It has one tooth and width of min_width. We will now append several of these together until we are close to
        // the required width of the block. Than we can scale it precisely.
        size_t n = std::max(1, int(width / min_width)); // How many shall be merged?
        for (size_t i = 0; i < n; ++i) {
            mesh.merge(tooth_mesh);
            tooth_mesh.translate(min_width, 0.f, 0.f);
        }

        mesh.scale(Vec3d(width / (n * min_width), 1.f, height)); // Scaling to proper width
    }
    else
        mesh = make_cube(width, depth, height);

    // We'll make another mesh to show the brim (fixed layer height):
    TriangleMesh brim_mesh = make_cube(width + 2.f * brim_width, depth + 2.f * brim_width, 0.2f);
    brim_mesh.translate(-brim_width, -brim_width, 0.f);
    mesh.merge(brim_mesh);

    this->volumes.emplace_back(new GLVolume(color));
    GLVolume& v = *this->volumes.back();
    v.indexed_vertex_array.load_mesh(mesh);
    v.indexed_vertex_array.finalize_geometry(opengl_initialized);
    v.set_volume_offset(Vec3d(pos_x, pos_y, 0.0));
    v.set_volume_rotation(Vec3d(0., 0., (M_PI / 180.) * rotation_angle));
    v.composite_id = GLVolume::CompositeID(obj_idx, 0, 0);
    v.geometry_id.first = 0;
    v.geometry_id.second = wipe_tower_instance_id().id;
    v.is_wipe_tower = true;
    v.shader_outside_printer_detection_enabled = !size_unknown;
    return int(this->volumes.size() - 1);
}

GLVolume* GLVolumeCollection::new_toolpath_volume(const float *rgba, size_t reserve_vbo_floats)
{
	GLVolume *out = new_nontoolpath_volume(rgba, reserve_vbo_floats);
	out->is_extrusion_path = true;
	return out;
}

GLVolume* GLVolumeCollection::new_nontoolpath_volume(const float *rgba, size_t reserve_vbo_floats)
{
	GLVolume *out = new GLVolume(rgba);
	out->is_extrusion_path = false;
	// Reserving number of vertices (3x position + 3x color)
	out->indexed_vertex_array.reserve(reserve_vbo_floats / 6);
	this->volumes.emplace_back(out);
	return out;
}

GLVolumeWithIdAndZList volumes_to_render(const GLVolumePtrs& volumes, GLVolumeCollection::ERenderType type, const Transform3d& view_matrix, std::function<bool(const GLVolume&)> filter_func)
{
    GLVolumeWithIdAndZList list;
    list.reserve(volumes.size());

    for (unsigned int i = 0; i < (unsigned int)volumes.size(); ++i)
    {
        GLVolume* volume = volumes[i];
        bool is_transparent = (volume->render_color[3] < 1.0f);
        if ((((type == GLVolumeCollection::Opaque) && !is_transparent) ||
             ((type == GLVolumeCollection::Transparent) && is_transparent) ||
             (type == GLVolumeCollection::All)) &&
            (! filter_func || filter_func(*volume)))
            list.emplace_back(std::make_pair(volume, std::make_pair(i, 0.0)));
    }

    if ((type == GLVolumeCollection::Transparent) && (list.size() > 1))
    {
        for (GLVolumeWithIdAndZ& volume : list)
        {
            volume.second.second = volume.first->bounding_box().transformed(view_matrix * volume.first->world_matrix()).max(2);
        }

        std::sort(list.begin(), list.end(),
            [](const GLVolumeWithIdAndZ& v1, const GLVolumeWithIdAndZ& v2) -> bool { return v1.second.second < v2.second.second; }
        );
    }
    else if ((type == GLVolumeCollection::Opaque) && (list.size() > 1))
    {
        std::sort(list.begin(), list.end(),
            [](const GLVolumeWithIdAndZ& v1, const GLVolumeWithIdAndZ& v2) -> bool { return v1.first->selected && !v2.first->selected; }
        );
    }

    return list;
}

void GLVolumeCollection::render(GLVolumeCollection::ERenderType type, bool disable_cullface, const Transform3d& view_matrix, std::function<bool(const GLVolume&)> filter_func) const
{
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    glsafe(::glCullFace(GL_BACK));
    if (disable_cullface)
        glsafe(::glDisable(GL_CULL_FACE));

    glsafe(::glEnableClientState(GL_VERTEX_ARRAY));
    glsafe(::glEnableClientState(GL_NORMAL_ARRAY));
 
    GLint current_program_id;
    glsafe(::glGetIntegerv(GL_CURRENT_PROGRAM, &current_program_id));
    GLint color_id = (current_program_id > 0) ? ::glGetUniformLocation(current_program_id, "uniform_color") : -1;
    GLint z_range_id = (current_program_id > 0) ? ::glGetUniformLocation(current_program_id, "z_range") : -1;
    GLint clipping_plane_id = (current_program_id > 0) ? ::glGetUniformLocation(current_program_id, "clipping_plane") : -1;

    GLint print_box_min_id = (current_program_id > 0) ? ::glGetUniformLocation(current_program_id, "print_box.min") : -1;
    GLint print_box_max_id = (current_program_id > 0) ? ::glGetUniformLocation(current_program_id, "print_box.max") : -1;
    GLint print_box_active_id = (current_program_id > 0) ? ::glGetUniformLocation(current_program_id, "print_box.active") : -1;
    GLint print_box_worldmatrix_id = (current_program_id > 0) ? ::glGetUniformLocation(current_program_id, "print_box.volume_world_matrix") : -1;

#if ENABLE_SLOPE_RENDERING
    GLint slope_active_id = (current_program_id > 0) ? ::glGetUniformLocation(current_program_id, "slope.active") : -1;
    GLint slope_normal_matrix_id = (current_program_id > 0) ? ::glGetUniformLocation(current_program_id, "slope.volume_world_normal_matrix") : -1;
    GLint slope_z_range_id = (current_program_id > 0) ? ::glGetUniformLocation(current_program_id, "slope.z_range") : -1;
#endif // ENABLE_SLOPE_RENDERING
    glcheck();

    if (print_box_min_id != -1)
        glsafe(::glUniform3fv(print_box_min_id, 1, (const GLfloat*)m_print_box_min));

    if (print_box_max_id != -1)
        glsafe(::glUniform3fv(print_box_max_id, 1, (const GLfloat*)m_print_box_max));

    if (z_range_id != -1)
        glsafe(::glUniform2fv(z_range_id, 1, (const GLfloat*)m_z_range));

    if (clipping_plane_id != -1)
        glsafe(::glUniform4fv(clipping_plane_id, 1, (const GLfloat*)m_clipping_plane));

#if ENABLE_SLOPE_RENDERING
    if (slope_z_range_id != -1)
        glsafe(::glUniform2fv(slope_z_range_id, 1, (const GLfloat*)m_slope.z_range.data()));
#endif // ENABLE_SLOPE_RENDERING

    GLVolumeWithIdAndZList to_render = volumes_to_render(this->volumes, type, view_matrix, filter_func);
    for (GLVolumeWithIdAndZ& volume : to_render) {
        volume.first->set_render_color();
#if ENABLE_SLOPE_RENDERING
        if (color_id >= 0)
            glsafe(::glUniform4fv(color_id, 1, (const GLfloat*)volume.first->render_color));
        else
            glsafe(::glColor4fv(volume.first->render_color));

        if (print_box_active_id != -1)
            glsafe(::glUniform1i(print_box_active_id, volume.first->shader_outside_printer_detection_enabled ? 1 : 0));

        if (print_box_worldmatrix_id != -1)
            glsafe(::glUniformMatrix4fv(print_box_worldmatrix_id, 1, GL_FALSE, (const GLfloat*)volume.first->world_matrix().cast<float>().data()));

        if (slope_active_id != -1)
            glsafe(::glUniform1i(slope_active_id, m_slope.active && !volume.first->is_modifier && !volume.first->is_wipe_tower ? 1 : 0));

        if (slope_normal_matrix_id != -1)
        {
            Matrix3f normal_matrix = volume.first->world_matrix().matrix().block(0, 0, 3, 3).inverse().transpose().cast<float>();
            glsafe(::glUniformMatrix3fv(slope_normal_matrix_id, 1, GL_FALSE, (const GLfloat*)normal_matrix.data()));
        }

        volume.first->render();
#else
        volume.first->render(color_id, print_box_detection_id, print_box_worldmatrix_id);
#endif // ENABLE_SLOPE_RENDERING
    }

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

    glsafe(::glDisableClientState(GL_VERTEX_ARRAY));
    glsafe(::glDisableClientState(GL_NORMAL_ARRAY));

    if (disable_cullface)
        glsafe(::glEnable(GL_CULL_FACE));

    glsafe(::glDisable(GL_BLEND));
}

bool GLVolumeCollection::check_outside_state(const DynamicPrintConfig* config, ModelInstance::EPrintVolumeState* out_state)
{
    if (config == nullptr)
        return false;

    const ConfigOptionPoints* opt = dynamic_cast<const ConfigOptionPoints*>(config->option("bed_shape"));
    if (opt == nullptr)
        return false;

    BoundingBox bed_box_2D = get_extents(Polygon::new_scale(opt->values));
    BoundingBoxf3 print_volume(Vec3d(unscale<double>(bed_box_2D.min(0)), unscale<double>(bed_box_2D.min(1)), 0.0), Vec3d(unscale<double>(bed_box_2D.max(0)), unscale<double>(bed_box_2D.max(1)), config->opt_float("max_print_height")));
    // Allow the objects to protrude below the print bed
    print_volume.min(2) = -1e10;

    ModelInstance::EPrintVolumeState state = ModelInstance::PVS_Inside;

    bool contained_min_one = false;

    for (GLVolume* volume : this->volumes)
    {
        if ((volume == nullptr) || volume->is_modifier || (volume->is_wipe_tower && !volume->shader_outside_printer_detection_enabled) || ((volume->composite_id.volume_id < 0) && !volume->shader_outside_printer_detection_enabled))
            continue;

        const BoundingBoxf3& bb = volume->transformed_convex_hull_bounding_box();
        bool contained = print_volume.contains(bb);

        volume->is_outside = !contained;
        if (!volume->printable)
            continue;

        if (contained)
            contained_min_one = true;

        if ((state == ModelInstance::PVS_Inside) && volume->is_outside)
            state = ModelInstance::PVS_Fully_Outside;

        if ((state == ModelInstance::PVS_Fully_Outside) && volume->is_outside && print_volume.intersects(bb))
            state = ModelInstance::PVS_Partly_Outside;
    }

    if (out_state != nullptr)
        *out_state = state;

    return contained_min_one;
}

void GLVolumeCollection::reset_outside_state()
{
    for (GLVolume* volume : this->volumes)
    {
        if (volume != nullptr)
            volume->is_outside = false;
    }
}

void GLVolumeCollection::update_colors_by_extruder(const DynamicPrintConfig* config)
{
    static const float inv_255 = 1.0f / 255.0f;

    struct Color
    {
        std::string text;
        unsigned char rgb[3];

        Color()
            : text("")
        {
            rgb[0] = 255;
            rgb[1] = 255;
            rgb[2] = 255;
        }

        void set(const std::string& text, unsigned char* rgb)
        {
            this->text = text;
            ::memcpy((void*)this->rgb, (const void*)rgb, 3 * sizeof(unsigned char));
        }
    };

    if (config == nullptr)
        return;

    const ConfigOptionStrings* extruders_opt = dynamic_cast<const ConfigOptionStrings*>(config->option("extruder_colour"));
    if (extruders_opt == nullptr)
        return;

    const ConfigOptionStrings* filamemts_opt = dynamic_cast<const ConfigOptionStrings*>(config->option("filament_colour"));
    if (filamemts_opt == nullptr)
        return;

    unsigned int colors_count = std::max((unsigned int)extruders_opt->values.size(), (unsigned int)filamemts_opt->values.size());
    if (colors_count == 0)
        return;

    std::vector<Color> colors(colors_count);

    unsigned char rgb[3];
    for (unsigned int i = 0; i < colors_count; ++i)
    {
        const std::string& txt_color = config->opt_string("extruder_colour", i);
        if (Slic3r::GUI::BitmapCache::parse_color(txt_color, rgb))
        {
            colors[i].set(txt_color, rgb);
        }
        else
        {
            const std::string& txt_color = config->opt_string("filament_colour", i);
            if (Slic3r::GUI::BitmapCache::parse_color(txt_color, rgb))
                colors[i].set(txt_color, rgb);
        }
    }

    for (GLVolume* volume : volumes)
    {
        if ((volume == nullptr) || volume->is_modifier || volume->is_wipe_tower || (volume->volume_idx() < 0))
            continue;

        int extruder_id = volume->extruder_id - 1;
        if ((extruder_id < 0) || ((int)colors.size() <= extruder_id))
            extruder_id = 0;

        const Color& color = colors[extruder_id];
        if (!color.text.empty())
        {
            for (int i = 0; i < 3; ++i)
            {
                volume->color[i] = (float)color.rgb[i] * inv_255;
            }
        }
    }
}

std::vector<double> GLVolumeCollection::get_current_print_zs(bool active_only) const
{
    // Collect layer top positions of all volumes.
    std::vector<double> print_zs;
    for (GLVolume *vol : this->volumes)
    {
        if (!active_only || vol->is_active)
            append(print_zs, vol->print_zs);
    }
    std::sort(print_zs.begin(), print_zs.end());

    // Replace intervals of layers with similar top positions with their average value.
    int n = int(print_zs.size());
    int k = 0;
    for (int i = 0; i < n;) {
        int j = i + 1;
        coordf_t zmax = print_zs[i] + EPSILON;
        for (; j < n && print_zs[j] <= zmax; ++ j) ;
        print_zs[k ++] = (j > i + 1) ? (0.5 * (print_zs[i] + print_zs[j - 1])) : print_zs[i];
        i = j;
    }
    if (k < n)
        print_zs.erase(print_zs.begin() + k, print_zs.end());

    return print_zs;
}

size_t GLVolumeCollection::cpu_memory_used() const 
{
	size_t memsize = sizeof(*this) + this->volumes.capacity() * sizeof(GLVolume);
	for (const GLVolume *volume : this->volumes)
		memsize += volume->cpu_memory_used();
	return memsize;
}

size_t GLVolumeCollection::gpu_memory_used() const 
{
	size_t memsize = 0;
	for (const GLVolume *volume : this->volumes)
		memsize += volume->gpu_memory_used();
	return memsize;
}

std::string GLVolumeCollection::log_memory_info() const 
{ 
	return " (GLVolumeCollection RAM: " + format_memsize_MB(this->cpu_memory_used()) + " GPU: " + format_memsize_MB(this->gpu_memory_used()) + " Both: " + format_memsize_MB(this->gpu_memory_used()) + ")";
}

bool can_export_to_obj(const GLVolume& volume)
{
    if (!volume.is_active || !volume.is_extrusion_path)
        return false;

    bool has_triangles = !volume.indexed_vertex_array.triangle_indices.empty() || (std::min(volume.indexed_vertex_array.triangle_indices_size, volume.tverts_range.second - volume.tverts_range.first) > 0);
    bool has_quads = !volume.indexed_vertex_array.quad_indices.empty() || (std::min(volume.indexed_vertex_array.quad_indices_size, volume.qverts_range.second - volume.qverts_range.first) > 0);

    return has_triangles || has_quads;
}

bool GLVolumeCollection::has_toolpaths_to_export() const
{
    for (const GLVolume* volume : this->volumes)
    {
        if (can_export_to_obj(*volume))
            return true;
    }

    return false;
}

void GLVolumeCollection::export_toolpaths_to_obj(const char* filename) const
{
    if (filename == nullptr)
        return;

    if (!has_toolpaths_to_export())
        return;

    // collect color information to generate materials
    typedef std::array<float, 4> Color;
    std::set<Color> colors;
    for (const GLVolume* volume : this->volumes)
    {
        if (!can_export_to_obj(*volume))
            continue;

        Color color;
        ::memcpy((void*)color.data(), (const void*)volume->color, 4 * sizeof(float));
        colors.insert(color);
    }

    // save materials file
    boost::filesystem::path mat_filename(filename);
    mat_filename.replace_extension("mtl");
    FILE* fp = boost::nowide::fopen(mat_filename.string().c_str(), "w");
    if (fp == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "GLVolumeCollection::export_toolpaths_to_obj: Couldn't open " << mat_filename.string().c_str() << " for writing";
        return;
    }

    fprintf(fp, "# G-Code Toolpaths Materials\n");
    fprintf(fp, "# Generated by %s based on Slic3r\n", SLIC3R_BUILD_ID);

    unsigned int colors_count = 1;
    for (const Color& color : colors)
    {
        fprintf(fp, "\nnewmtl material_%d\n", colors_count++);
        fprintf(fp, "Ka 1 1 1\n");
        fprintf(fp, "Kd %f %f %f\n", color[0], color[1], color[2]);
        fprintf(fp, "Ks 0 0 0\n");
    }

    fclose(fp);

    // save geometry file
    fp = boost::nowide::fopen(filename, "w");
    if (fp == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "GLVolumeCollection::export_toolpaths_to_obj: Couldn't open " << filename << " for writing";
        return;
    }

    fprintf(fp, "# G-Code Toolpaths\n");
    fprintf(fp, "# Generated by %s based on Slic3r\n", SLIC3R_BUILD_ID);
    fprintf(fp, "\nmtllib ./%s\n", mat_filename.filename().string().c_str());

    unsigned int vertices_count = 0;
    unsigned int normals_count = 0;
    unsigned int volumes_count = 0;

    for (const GLVolume* volume : this->volumes)
    {
        if (!can_export_to_obj(*volume))
            continue;

        std::vector<float> src_vertices_and_normals_interleaved;
        std::vector<int>   src_triangle_indices;
        std::vector<int>   src_quad_indices;

        if (!volume->indexed_vertex_array.vertices_and_normals_interleaved.empty())
            // data are in CPU memory
            src_vertices_and_normals_interleaved = volume->indexed_vertex_array.vertices_and_normals_interleaved;
        else if ((volume->indexed_vertex_array.vertices_and_normals_interleaved_VBO_id != 0) && (volume->indexed_vertex_array.vertices_and_normals_interleaved_size != 0))
        {
            // data are in GPU memory
            src_vertices_and_normals_interleaved = std::vector<float>(volume->indexed_vertex_array.vertices_and_normals_interleaved_size, 0.0f);

            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, volume->indexed_vertex_array.vertices_and_normals_interleaved_VBO_id));
            glsafe(::glGetBufferSubData(GL_ARRAY_BUFFER, 0, src_vertices_and_normals_interleaved.size() * sizeof(float), src_vertices_and_normals_interleaved.data()));
            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
        }
        else
            continue;

        if (!volume->indexed_vertex_array.triangle_indices.empty())
        {
            // data are in CPU memory
            size_t size = std::min(volume->indexed_vertex_array.triangle_indices.size(), volume->tverts_range.second - volume->tverts_range.first);
            if (size != 0)
            {
                std::vector<int>::const_iterator it_begin = volume->indexed_vertex_array.triangle_indices.begin() + volume->tverts_range.first;
                std::vector<int>::const_iterator it_end = volume->indexed_vertex_array.triangle_indices.begin() + volume->tverts_range.first + size;
                std::copy(it_begin, it_end, std::back_inserter(src_triangle_indices));
            }
        }
        else if ((volume->indexed_vertex_array.triangle_indices_VBO_id != 0) && (volume->indexed_vertex_array.triangle_indices_size != 0))
        {
            // data are in GPU memory
            size_t size = std::min(volume->indexed_vertex_array.triangle_indices_size, volume->tverts_range.second - volume->tverts_range.first);
            if (size != 0)
            {
                src_triangle_indices = std::vector<int>(size, 0);

                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, volume->indexed_vertex_array.triangle_indices_VBO_id));
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, volume->tverts_range.first * sizeof(int), size * sizeof(int), src_triangle_indices.data()));
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
            }
        }

        if (!volume->indexed_vertex_array.quad_indices.empty())
        {
            // data are in CPU memory
            size_t size = std::min(volume->indexed_vertex_array.quad_indices.size(), volume->qverts_range.second - volume->qverts_range.first);
            if (size != 0)
            {
                std::vector<int>::const_iterator it_begin = volume->indexed_vertex_array.quad_indices.begin() + volume->qverts_range.first;
                std::vector<int>::const_iterator it_end = volume->indexed_vertex_array.quad_indices.begin() + volume->qverts_range.first + size;
                std::copy(it_begin, it_end, std::back_inserter(src_quad_indices));
            }
        }
        else if ((volume->indexed_vertex_array.quad_indices_VBO_id != 0) && (volume->indexed_vertex_array.quad_indices_size != 0))
        {
            // data are in GPU memory
            size_t size = std::min(volume->indexed_vertex_array.quad_indices_size, volume->qverts_range.second - volume->qverts_range.first);
            if (size != 0)
            {
                src_quad_indices = std::vector<int>(size, 0);

                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, volume->indexed_vertex_array.quad_indices_VBO_id));
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, volume->qverts_range.first * sizeof(int), size * sizeof(int), src_quad_indices.data()));
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
            }
        }

        if (src_triangle_indices.empty() && src_quad_indices.empty())
            continue;

        ++volumes_count;

        // reduce output size by keeping only used vertices and normals

        struct Vector
        {
            std::array<coord_t, 3> vector;

            explicit Vector(float* ptr)
            {
                vector[0] = scale_(*(ptr + 0));
                vector[1] = scale_(*(ptr + 1));
                vector[2] = scale_(*(ptr + 2));
            }
        };
        typedef std::vector<Vector> Vectors;

        auto vector_less = [](const Vector& v1, const Vector& v2)->bool {
            return v1.vector < v2.vector;
        };

        auto vector_equal = [](const Vector& v1, const Vector& v2)->bool {
            return (v1.vector[0] == v2.vector[0]) && (v1.vector[1] == v2.vector[1]) && (v1.vector[2] == v2.vector[2]);
        };

        // copy used vertices and normals data
        Vectors dst_normals;
        Vectors dst_vertices;

        unsigned int src_triangle_indices_size = (unsigned int)src_triangle_indices.size();
        for (unsigned int i = 0; i < src_triangle_indices_size; ++i)
        {
            float* src_ptr = src_vertices_and_normals_interleaved.data() + src_triangle_indices[i] * 6;
            dst_normals.emplace_back(src_ptr + 0);
            dst_vertices.emplace_back(src_ptr + 3);
        }

        unsigned int src_quad_indices_size = (unsigned int)src_quad_indices.size();
        for (unsigned int i = 0; i < src_quad_indices_size; ++i)
        {
            float* src_ptr = src_vertices_and_normals_interleaved.data() + src_quad_indices[i] * 6;
            dst_normals.emplace_back(src_ptr + 0);
            dst_vertices.emplace_back(src_ptr + 3);
        }

        // sort vertices and normals
        std::sort(dst_normals.begin(), dst_normals.end(), vector_less);
        std::sort(dst_vertices.begin(), dst_vertices.end(), vector_less);

        // remove duplicated vertices and normals
        dst_normals.erase(std::unique(dst_normals.begin(), dst_normals.end(), vector_equal), dst_normals.end());
        dst_vertices.erase(std::unique(dst_vertices.begin(), dst_vertices.end(), vector_equal), dst_vertices.end());

        // reindex triangles and quads
        struct IndicesPair
        {
            int vertex;
            int normal;
            IndicesPair(int vertex, int normal) : vertex(vertex), normal(normal) {}
        };
        typedef std::vector<IndicesPair> Indices;

        unsigned int src_vertices_count = (unsigned int)src_vertices_and_normals_interleaved.size() / 6;
        std::vector<int> src_dst_vertex_indices_map(src_vertices_count, -1);
        std::vector<int> src_dst_normal_indices_map(src_vertices_count, -1);

        for (unsigned int i = 0; i < src_vertices_count; ++i)
        {
            float* src_ptr = src_vertices_and_normals_interleaved.data() + i * 6;
            src_dst_normal_indices_map[i] = std::distance(dst_normals.begin(), std::lower_bound(dst_normals.begin(), dst_normals.end(), Vector(src_ptr + 0), vector_less));
            src_dst_vertex_indices_map[i] = std::distance(dst_vertices.begin(), std::lower_bound(dst_vertices.begin(), dst_vertices.end(), Vector(src_ptr + 3), vector_less));
        }

        Indices dst_triangle_indices;
        if (src_triangle_indices_size > 0)
            dst_triangle_indices.reserve(src_triangle_indices_size);

        for (unsigned int i = 0; i < src_triangle_indices_size; ++i)
        {
            int id = src_triangle_indices[i];
            dst_triangle_indices.emplace_back(src_dst_vertex_indices_map[id], src_dst_normal_indices_map[id]);
        }

        Indices dst_quad_indices;
        if (src_quad_indices_size > 0)
            dst_quad_indices.reserve(src_quad_indices_size);

        for (unsigned int i = 0; i < src_quad_indices_size; ++i)
        {
            int id = src_quad_indices[i];
            dst_quad_indices.emplace_back(src_dst_vertex_indices_map[id], src_dst_normal_indices_map[id]);
        }

        // save to file
        fprintf(fp, "\n# vertices volume %d\n", volumes_count);
        for (const Vector& v : dst_vertices)
        {
            fprintf(fp, "v %g %g %g\n", unscale<float>(v.vector[0]), unscale<float>(v.vector[1]), unscale<float>(v.vector[2]));
        }

        fprintf(fp, "\n# normals volume %d\n", volumes_count);
        for (const Vector& n : dst_normals)
        {
            fprintf(fp, "vn %g %g %g\n", unscale<float>(n.vector[0]), unscale<float>(n.vector[1]), unscale<float>(n.vector[2]));
        }

        Color color;
        ::memcpy((void*)color.data(), (const void*)volume->color, 4 * sizeof(float));
        fprintf(fp, "\n# material volume %d\n", volumes_count);
        fprintf(fp, "usemtl material_%lld\n", (long long)(1 + std::distance(colors.begin(), colors.find(color))));

        int base_vertex_id = vertices_count + 1;
        int base_normal_id = normals_count + 1;

        if (!dst_triangle_indices.empty())
        {
            fprintf(fp, "\n# triangular facets volume %d\n", volumes_count);
            for (unsigned int i = 0; i < (unsigned int)dst_triangle_indices.size(); i += 3)
            {
                fprintf(fp, "f %d//%d %d//%d %d//%d\n", 
                    base_vertex_id + dst_triangle_indices[i + 0].vertex, base_normal_id + dst_triangle_indices[i + 0].normal,
                    base_vertex_id + dst_triangle_indices[i + 1].vertex, base_normal_id + dst_triangle_indices[i + 1].normal,
                    base_vertex_id + dst_triangle_indices[i + 2].vertex, base_normal_id + dst_triangle_indices[i + 2].normal);
            }
        }

        if (!dst_quad_indices.empty())
        {
            fprintf(fp, "\n# quadrangular facets volume %d\n", volumes_count);
            for (unsigned int i = 0; i < (unsigned int)src_quad_indices.size(); i += 4)
            {
                fprintf(fp, "f %d//%d %d//%d %d//%d %d//%d\n", 
                    base_vertex_id + dst_quad_indices[i + 0].vertex, base_normal_id + dst_quad_indices[i + 0].normal,
                    base_vertex_id + dst_quad_indices[i + 1].vertex, base_normal_id + dst_quad_indices[i + 1].normal,
                    base_vertex_id + dst_quad_indices[i + 2].vertex, base_normal_id + dst_quad_indices[i + 2].normal,
                    base_vertex_id + dst_quad_indices[i + 3].vertex, base_normal_id + dst_quad_indices[i + 3].normal);
            }
        }

        vertices_count += (unsigned int)dst_vertices.size();
        normals_count += (unsigned int)dst_normals.size();
    }

    fclose(fp);
}

// caller is responsible for supplying NO lines with zero length
static void thick_lines_to_indexed_vertex_array(
    const Lines                 &lines, 
    const std::vector<double>   &widths,
    const std::vector<double>   &heights, 
    bool                         closed,
    double                       top_z,
    GLIndexedVertexArray        &volume)
{
    assert(! lines.empty());
    if (lines.empty())
        return;

#define LEFT    0
#define RIGHT   1
#define TOP     2
#define BOTTOM  3

    // right, left, top, bottom
    int     idx_prev[4]      = { -1, -1, -1, -1 };
    double  bottom_z_prev    = 0.;
    Vec2d   b1_prev(Vec2d::Zero());
    Vec2d   v_prev(Vec2d::Zero());
    int     idx_initial[4]   = { -1, -1, -1, -1 };
    double  width_initial    = 0.;
    double  bottom_z_initial = 0.0;
    double  len_prev = 0.0;

    // loop once more in case of closed loops
    size_t lines_end = closed ? (lines.size() + 1) : lines.size();
    for (size_t ii = 0; ii < lines_end; ++ ii) {
        size_t i = (ii == lines.size()) ? 0 : ii;
        const Line &line = lines[i];
        double bottom_z = top_z - heights[i];
        double middle_z = 0.5 * (top_z + bottom_z);
        double width = widths[i];

        bool is_first = (ii == 0);
        bool is_last = (ii == lines_end - 1);
        bool is_closing = closed && is_last;

        Vec2d v = unscale(line.vector()).normalized();
        double len = unscale<double>(line.length());

        Vec2d a = unscale(line.a);
        Vec2d b = unscale(line.b);
        Vec2d a1 = a;
        Vec2d a2 = a;
        Vec2d b1 = b;
        Vec2d b2 = b;
        {
            double dist = 0.5 * width;  // scaled
            double dx = dist * v(0);
            double dy = dist * v(1);
            a1 += Vec2d(+dy, -dx);
            a2 += Vec2d(-dy, +dx);
            b1 += Vec2d(+dy, -dx);
            b2 += Vec2d(-dy, +dx);
        }

        // calculate new XY normals
        Vec2d xy_right_normal = unscale(line.normal()).normalized();

        int idx_a[4] = { 0, 0, 0, 0 }; // initialized to avoid warnings
        int idx_b[4] = { 0, 0, 0, 0 }; // initialized to avoid warnings
        int idx_last = int(volume.vertices_and_normals_interleaved.size() / 6);

        bool bottom_z_different = bottom_z_prev != bottom_z;
        bottom_z_prev = bottom_z;

        if (!is_first && bottom_z_different)
        {
            // Found a change of the layer thickness -> Add a cap at the end of the previous segment.
            volume.push_quad(idx_b[BOTTOM], idx_b[LEFT], idx_b[TOP], idx_b[RIGHT]);
        }

        // Share top / bottom vertices if possible.
        if (is_first) {
            idx_a[TOP] = idx_last++;
            volume.push_geometry(a(0), a(1), top_z   , 0., 0.,  1.); 
        } else {
            idx_a[TOP] = idx_prev[TOP];
        }

        if (is_first || bottom_z_different) {
            // Start of the 1st line segment or a change of the layer thickness while maintaining the print_z.
            idx_a[BOTTOM] = idx_last ++;
            volume.push_geometry(a(0), a(1), bottom_z, 0., 0., -1.);
            idx_a[LEFT ] = idx_last ++;
            volume.push_geometry(a2(0), a2(1), middle_z, -xy_right_normal(0), -xy_right_normal(1), 0.0);
            idx_a[RIGHT] = idx_last ++;
            volume.push_geometry(a1(0), a1(1), middle_z, xy_right_normal(0), xy_right_normal(1), 0.0);
        }
        else {
            idx_a[BOTTOM] = idx_prev[BOTTOM];
        }

        if (is_first) {
            // Start of the 1st line segment.
            width_initial    = width;
            bottom_z_initial = bottom_z;
            memcpy(idx_initial, idx_a, sizeof(int) * 4);
        } else {
            // Continuing a previous segment.
            // Share left / right vertices if possible.
			double v_dot    = v_prev.dot(v);
            // To reduce gpu memory usage, we try to reuse vertices
            // To reduce the visual artifacts, due to averaged normals, we allow to reuse vertices only when any of two adjacent edges 
            // is longer than a fixed threshold.
            // The following value is arbitrary, it comes from tests made on a bunch of models showing the visual artifacts
            double len_threshold = 2.5;

            // Generate new vertices if the angle between adjacent edges is greater than 45 degrees or thresholds conditions are met
            bool sharp = (v_dot < 0.707) || (len_prev > len_threshold) || (len > len_threshold);
            if (sharp) {
                if (!bottom_z_different)
                {
                    // Allocate new left / right points for the start of this segment as these points will receive their own normals to indicate a sharp turn.
                    idx_a[RIGHT] = idx_last++;
                    volume.push_geometry(a1(0), a1(1), middle_z, xy_right_normal(0), xy_right_normal(1), 0.0);
                    idx_a[LEFT] = idx_last++;
                    volume.push_geometry(a2(0), a2(1), middle_z, -xy_right_normal(0), -xy_right_normal(1), 0.0);
                    if (cross2(v_prev, v) > 0.) {
                        // Right turn. Fill in the right turn wedge.
                        volume.push_triangle(idx_prev[RIGHT], idx_a[RIGHT], idx_prev[TOP]);
                        volume.push_triangle(idx_prev[RIGHT], idx_prev[BOTTOM], idx_a[RIGHT]);
                    }
                    else {
                        // Left turn. Fill in the left turn wedge.
                        volume.push_triangle(idx_prev[LEFT], idx_prev[TOP], idx_a[LEFT]);
                        volume.push_triangle(idx_prev[LEFT], idx_a[LEFT], idx_prev[BOTTOM]);
                    }
                }
            }
            else
            {
                if (!bottom_z_different)
                {
                    // The two successive segments are nearly collinear.
                    idx_a[LEFT ] = idx_prev[LEFT];
                    idx_a[RIGHT] = idx_prev[RIGHT];
                }
            }
            if (is_closing) {
                if (!sharp) {
                    if (!bottom_z_different)
                    {
                        // Closing a loop with smooth transition. Unify the closing left / right vertices.
                        memcpy(volume.vertices_and_normals_interleaved.data() + idx_initial[LEFT ] * 6, volume.vertices_and_normals_interleaved.data() + idx_prev[LEFT ] * 6, sizeof(float) * 6);
                        memcpy(volume.vertices_and_normals_interleaved.data() + idx_initial[RIGHT] * 6, volume.vertices_and_normals_interleaved.data() + idx_prev[RIGHT] * 6, sizeof(float) * 6);
                        volume.vertices_and_normals_interleaved.erase(volume.vertices_and_normals_interleaved.end() - 12, volume.vertices_and_normals_interleaved.end());
                        // Replace the left / right vertex indices to point to the start of the loop. 
                        for (size_t u = volume.quad_indices.size() - 16; u < volume.quad_indices.size(); ++ u) {
                            if (volume.quad_indices[u] == idx_prev[LEFT])
                                volume.quad_indices[u] = idx_initial[LEFT];
                            else if (volume.quad_indices[u] == idx_prev[RIGHT])
                                volume.quad_indices[u] = idx_initial[RIGHT];
                        }
                    }
                }
                // This is the last iteration, only required to solve the transition.
                break;
            }
        }

        // Only new allocate top / bottom vertices, if not closing a loop.
        if (is_closing) {
            idx_b[TOP] = idx_initial[TOP];
        } else {
            idx_b[TOP] = idx_last ++;
            volume.push_geometry(b(0), b(1), top_z   , 0., 0.,  1.);
        }

        if (is_closing && (width == width_initial) && (bottom_z == bottom_z_initial)) {
            idx_b[BOTTOM] = idx_initial[BOTTOM];
        } else {
            idx_b[BOTTOM] = idx_last ++;
            volume.push_geometry(b(0), b(1), bottom_z, 0., 0., -1.);
        }
        // Generate new vertices for the end of this line segment.
        idx_b[LEFT  ] = idx_last ++;
        volume.push_geometry(b2(0), b2(1), middle_z, -xy_right_normal(0), -xy_right_normal(1), 0.0);
        idx_b[RIGHT ] = idx_last ++;
        volume.push_geometry(b1(0), b1(1), middle_z, xy_right_normal(0), xy_right_normal(1), 0.0);

        memcpy(idx_prev, idx_b, 4 * sizeof(int));
        bottom_z_prev = bottom_z;
        b1_prev = b1;
        v_prev = v;
        len_prev = len;

        if (bottom_z_different && (closed || (!is_first && !is_last)))
        {
            // Found a change of the layer thickness -> Add a cap at the beginning of this segment.
            volume.push_quad(idx_a[BOTTOM], idx_a[RIGHT], idx_a[TOP], idx_a[LEFT]);
        }

        if (! closed) {
            // Terminate open paths with caps.
            if (is_first)
                volume.push_quad(idx_a[BOTTOM], idx_a[RIGHT], idx_a[TOP], idx_a[LEFT]);
            // We don't use 'else' because both cases are true if we have only one line.
            if (is_last)
                volume.push_quad(idx_b[BOTTOM], idx_b[LEFT], idx_b[TOP], idx_b[RIGHT]);
        }

        // Add quads for a straight hollow tube-like segment.
        // bottom-right face
        volume.push_quad(idx_a[BOTTOM], idx_b[BOTTOM], idx_b[RIGHT], idx_a[RIGHT]);
        // top-right face
        volume.push_quad(idx_a[RIGHT], idx_b[RIGHT], idx_b[TOP], idx_a[TOP]);
        // top-left face
        volume.push_quad(idx_a[TOP], idx_b[TOP], idx_b[LEFT], idx_a[LEFT]);
        // bottom-left face
        volume.push_quad(idx_a[LEFT], idx_b[LEFT], idx_b[BOTTOM], idx_a[BOTTOM]);
    }

#undef LEFT
#undef RIGHT
#undef TOP
#undef BOTTOM
}

// caller is responsible for supplying NO lines with zero length
static void thick_lines_to_indexed_vertex_array(const Lines3& lines,
    const std::vector<double>& widths,
    const std::vector<double>& heights,
    bool closed,
    GLIndexedVertexArray& volume)
{
    assert(!lines.empty());
    if (lines.empty())
        return;

#define LEFT    0
#define RIGHT   1
#define TOP     2
#define BOTTOM  3

    // left, right, top, bottom
    int      idx_initial[4] = { -1, -1, -1, -1 };
    int      idx_prev[4] = { -1, -1, -1, -1 };
    double   z_prev = 0.0;
    double   len_prev = 0.0;
    Vec3d    n_right_prev = Vec3d::Zero();
    Vec3d    n_top_prev = Vec3d::Zero();
    Vec3d    unit_v_prev = Vec3d::Zero();
    double   width_initial = 0.0;

    // new vertices around the line endpoints
    // left, right, top, bottom
    Vec3d a[4] = { Vec3d::Zero(), Vec3d::Zero(), Vec3d::Zero(), Vec3d::Zero() };
    Vec3d b[4] = { Vec3d::Zero(), Vec3d::Zero(), Vec3d::Zero(), Vec3d::Zero() };

    // loop once more in case of closed loops
    size_t lines_end = closed ? (lines.size() + 1) : lines.size();
    for (size_t ii = 0; ii < lines_end; ++ii)
    {
        size_t i = (ii == lines.size()) ? 0 : ii;

        const Line3& line = lines[i];
        double height = heights[i];
        double width = widths[i];

        Vec3d unit_v = unscale(line.vector()).normalized();
        double len = unscale<double>(line.length());

        Vec3d n_top = Vec3d::Zero();
        Vec3d n_right = Vec3d::Zero();
        
        if ((line.a(0) == line.b(0)) && (line.a(1) == line.b(1)))
        {
            // vertical segment
            n_top = Vec3d::UnitY();
            n_right = Vec3d::UnitX();
            if (line.a(2) < line.b(2))
                n_right = -n_right;
        }
        else
        {
            // horizontal segment
            n_right = unit_v.cross(Vec3d::UnitZ()).normalized();
            n_top = n_right.cross(unit_v).normalized();
        }

        Vec3d rl_displacement = 0.5 * width * n_right;
        Vec3d tb_displacement = 0.5 * height * n_top;
        Vec3d l_a = unscale(line.a);
        Vec3d l_b = unscale(line.b);

        a[RIGHT] = l_a + rl_displacement;
        a[LEFT] = l_a - rl_displacement;
        a[TOP] = l_a + tb_displacement;
        a[BOTTOM] = l_a - tb_displacement;
        b[RIGHT] = l_b + rl_displacement;
        b[LEFT] = l_b - rl_displacement;
        b[TOP] = l_b + tb_displacement;
        b[BOTTOM] = l_b - tb_displacement;

        Vec3d n_bottom = -n_top;
        Vec3d n_left = -n_right;

        int idx_a[4];
        int idx_b[4];
        int idx_last = int(volume.vertices_and_normals_interleaved.size() / 6);

        bool z_different = (z_prev != l_a(2));
        z_prev = l_b(2);

        // Share top / bottom vertices if possible.
        if (ii == 0)
        {
            idx_a[TOP] = idx_last++;
            volume.push_geometry(a[TOP], n_top);
        }
        else
            idx_a[TOP] = idx_prev[TOP];

        if ((ii == 0) || z_different)
        {
            // Start of the 1st line segment or a change of the layer thickness while maintaining the print_z.
            idx_a[BOTTOM] = idx_last++;
            volume.push_geometry(a[BOTTOM], n_bottom);
            idx_a[LEFT] = idx_last++;
            volume.push_geometry(a[LEFT], n_left);
            idx_a[RIGHT] = idx_last++;
            volume.push_geometry(a[RIGHT], n_right);
        }
        else
            idx_a[BOTTOM] = idx_prev[BOTTOM];

        if (ii == 0)
        {
            // Start of the 1st line segment.
            width_initial = width;
            ::memcpy(idx_initial, idx_a, sizeof(int) * 4);
        }
        else
        {
            // Continuing a previous segment.
            // Share left / right vertices if possible.
            double v_dot = unit_v_prev.dot(unit_v);
            bool is_right_turn = n_top_prev.dot(unit_v_prev.cross(unit_v)) > 0.0;

            // To reduce gpu memory usage, we try to reuse vertices
            // To reduce the visual artifacts, due to averaged normals, we allow to reuse vertices only when any of two adjacent edges 
            // is longer than a fixed threshold.
            // The following value is arbitrary, it comes from tests made on a bunch of models showing the visual artifacts
            double len_threshold = 2.5;

            // Generate new vertices if the angle between adjacent edges is greater than 45 degrees or thresholds conditions are met
            bool is_sharp = (v_dot < 0.707) || (len_prev > len_threshold) || (len > len_threshold);
            if (is_sharp)
            {
                // Allocate new left / right points for the start of this segment as these points will receive their own normals to indicate a sharp turn.
                idx_a[RIGHT] = idx_last++;
                volume.push_geometry(a[RIGHT], n_right);
                idx_a[LEFT] = idx_last++;
                volume.push_geometry(a[LEFT], n_left);

                if (is_right_turn)
                {
                    // Right turn. Fill in the right turn wedge.
                    volume.push_triangle(idx_prev[RIGHT], idx_a[RIGHT], idx_prev[TOP]);
                    volume.push_triangle(idx_prev[RIGHT], idx_prev[BOTTOM], idx_a[RIGHT]);
                }
                else
                {
                    // Left turn. Fill in the left turn wedge.
                    volume.push_triangle(idx_prev[LEFT], idx_prev[TOP], idx_a[LEFT]);
                    volume.push_triangle(idx_prev[LEFT], idx_a[LEFT], idx_prev[BOTTOM]);
                }
            }
            else
            {
                // The two successive segments are nearly collinear.
                idx_a[LEFT] = idx_prev[LEFT];
                idx_a[RIGHT] = idx_prev[RIGHT];
            }

            if (ii == lines.size())
            {
                if (!is_sharp)
                {
                    // Closing a loop with smooth transition. Unify the closing left / right vertices.
                    ::memcpy(volume.vertices_and_normals_interleaved.data() + idx_initial[LEFT] * 6, volume.vertices_and_normals_interleaved.data() + idx_prev[LEFT] * 6, sizeof(float) * 6);
                    ::memcpy(volume.vertices_and_normals_interleaved.data() + idx_initial[RIGHT] * 6, volume.vertices_and_normals_interleaved.data() + idx_prev[RIGHT] * 6, sizeof(float) * 6);
                    volume.vertices_and_normals_interleaved.erase(volume.vertices_and_normals_interleaved.end() - 12, volume.vertices_and_normals_interleaved.end());
                    // Replace the left / right vertex indices to point to the start of the loop. 
                    for (size_t u = volume.quad_indices.size() - 16; u < volume.quad_indices.size(); ++u)
                    {
                        if (volume.quad_indices[u] == idx_prev[LEFT])
                            volume.quad_indices[u] = idx_initial[LEFT];
                        else if (volume.quad_indices[u] == idx_prev[RIGHT])
                            volume.quad_indices[u] = idx_initial[RIGHT];
                    }
                }

                // This is the last iteration, only required to solve the transition.
                break;
            }
        }

        // Only new allocate top / bottom vertices, if not closing a loop.
        if (closed && (ii + 1 == lines.size()))
            idx_b[TOP] = idx_initial[TOP];
        else
        {
            idx_b[TOP] = idx_last++;
            volume.push_geometry(b[TOP], n_top);
        }

        if (closed && (ii + 1 == lines.size()) && (width == width_initial))
            idx_b[BOTTOM] = idx_initial[BOTTOM];
        else
        {
            idx_b[BOTTOM] = idx_last++;
            volume.push_geometry(b[BOTTOM], n_bottom);
        }

        // Generate new vertices for the end of this line segment.
        idx_b[LEFT] = idx_last++;
        volume.push_geometry(b[LEFT], n_left);
        idx_b[RIGHT] = idx_last++;
        volume.push_geometry(b[RIGHT], n_right);

        ::memcpy(idx_prev, idx_b, 4 * sizeof(int));
        n_right_prev = n_right;
        n_top_prev = n_top;
        unit_v_prev = unit_v;
        len_prev = len;

        if (!closed)
        {
            // Terminate open paths with caps.
            if (i == 0)
                volume.push_quad(idx_a[BOTTOM], idx_a[RIGHT], idx_a[TOP], idx_a[LEFT]);

            // We don't use 'else' because both cases are true if we have only one line.
            if (i + 1 == lines.size())
                volume.push_quad(idx_b[BOTTOM], idx_b[LEFT], idx_b[TOP], idx_b[RIGHT]);
        }

        // Add quads for a straight hollow tube-like segment.
        // bottom-right face
        volume.push_quad(idx_a[BOTTOM], idx_b[BOTTOM], idx_b[RIGHT], idx_a[RIGHT]);
        // top-right face
        volume.push_quad(idx_a[RIGHT], idx_b[RIGHT], idx_b[TOP], idx_a[TOP]);
        // top-left face
        volume.push_quad(idx_a[TOP], idx_b[TOP], idx_b[LEFT], idx_a[LEFT]);
        // bottom-left face
        volume.push_quad(idx_a[LEFT], idx_b[LEFT], idx_b[BOTTOM], idx_a[BOTTOM]);
    }

#undef LEFT
#undef RIGHT
#undef TOP
#undef BOTTOM
}

static void point_to_indexed_vertex_array(const Vec3crd& point,
    double width,
    double height,
    GLIndexedVertexArray& volume)
{
    // builds a double piramid, with vertices on the local axes, around the point

    Vec3d center = unscale(point);

    double scale_factor = 1.0;
    double w = scale_factor * width;
    double h = scale_factor * height;

    // new vertices ids
    int idx_last = int(volume.vertices_and_normals_interleaved.size() / 6);
    int idxs[6];
    for (int i = 0; i < 6; ++i)
    {
        idxs[i] = idx_last + i;
    }

    Vec3d displacement_x(w, 0.0, 0.0);
    Vec3d displacement_y(0.0, w, 0.0);
    Vec3d displacement_z(0.0, 0.0, h);

    Vec3d unit_x(1.0, 0.0, 0.0);
    Vec3d unit_y(0.0, 1.0, 0.0);
    Vec3d unit_z(0.0, 0.0, 1.0);

    // vertices
    volume.push_geometry(center - displacement_x, -unit_x); // idxs[0]
    volume.push_geometry(center + displacement_x, unit_x);  // idxs[1]
    volume.push_geometry(center - displacement_y, -unit_y); // idxs[2]
    volume.push_geometry(center + displacement_y, unit_y);  // idxs[3]
    volume.push_geometry(center - displacement_z, -unit_z); // idxs[4]
    volume.push_geometry(center + displacement_z, unit_z);  // idxs[5]

    // top piramid faces
    volume.push_triangle(idxs[0], idxs[2], idxs[5]);
    volume.push_triangle(idxs[2], idxs[1], idxs[5]);
    volume.push_triangle(idxs[1], idxs[3], idxs[5]);
    volume.push_triangle(idxs[3], idxs[0], idxs[5]);

    // bottom piramid faces
    volume.push_triangle(idxs[2], idxs[0], idxs[4]);
    volume.push_triangle(idxs[1], idxs[2], idxs[4]);
    volume.push_triangle(idxs[3], idxs[1], idxs[4]);
    volume.push_triangle(idxs[0], idxs[3], idxs[4]);
}

void _3DScene::thick_lines_to_verts(
    const Lines                 &lines,
    const std::vector<double>   &widths,
    const std::vector<double>   &heights, 
    bool                         closed,
    double                       top_z,
    GLVolume                    &volume)
{
    thick_lines_to_indexed_vertex_array(lines, widths, heights, closed, top_z, volume.indexed_vertex_array);
}

void _3DScene::thick_lines_to_verts(const Lines3& lines,
    const std::vector<double>& widths,
    const std::vector<double>& heights,
    bool closed,
    GLVolume& volume)
{
    thick_lines_to_indexed_vertex_array(lines, widths, heights, closed, volume.indexed_vertex_array);
}

static void thick_point_to_verts(const Vec3crd& point,
    double width,
    double height,
    GLVolume& volume)
{
    point_to_indexed_vertex_array(point, width, height, volume.indexed_vertex_array);
}

void _3DScene::extrusionentity_to_verts(const Polyline &polyline, float width, float height, float print_z, GLVolume& volume)
{
	if (polyline.size() >= 2) {
		size_t num_segments = polyline.size() - 1;
		thick_lines_to_verts(polyline.lines(), std::vector<double>(num_segments, width), std::vector<double>(num_segments, height), false, print_z, volume);
	}
}

// Fill in the qverts and tverts with quads and triangles for the extrusion_path.
void _3DScene::extrusionentity_to_verts(const ExtrusionPath &extrusion_path, float print_z, GLVolume &volume)
{
	extrusionentity_to_verts(extrusion_path.polyline, extrusion_path.width, extrusion_path.height, print_z, volume);
}

// Fill in the qverts and tverts with quads and triangles for the extrusion_path.
void _3DScene::extrusionentity_to_verts(const ExtrusionPath &extrusion_path, float print_z, const Point &copy, GLVolume &volume)
{
    Polyline            polyline = extrusion_path.polyline;
    polyline.remove_duplicate_points();
    polyline.translate(copy);
    Lines               lines = polyline.lines();
    std::vector<double> widths(lines.size(), extrusion_path.width);
    std::vector<double> heights(lines.size(), extrusion_path.height);
    thick_lines_to_verts(lines, widths, heights, false, print_z, volume);
}

// Fill in the qverts and tverts with quads and triangles for the extrusion_loop.
void _3DScene::extrusionentity_to_verts(const ExtrusionLoop &extrusion_loop, float print_z, const Point &copy, GLVolume &volume)
{
    Lines               lines;
    std::vector<double> widths;
    std::vector<double> heights;
    for (const ExtrusionPath &extrusion_path : extrusion_loop.paths) {
        Polyline            polyline = extrusion_path.polyline;
        polyline.remove_duplicate_points();
        polyline.translate(copy);
        Lines lines_this = polyline.lines();
        append(lines, lines_this);
        widths.insert(widths.end(), lines_this.size(), extrusion_path.width);
        heights.insert(heights.end(), lines_this.size(), extrusion_path.height);
    }
    thick_lines_to_verts(lines, widths, heights, true, print_z, volume);
}

// Fill in the qverts and tverts with quads and triangles for the extrusion_multi_path.
void _3DScene::extrusionentity_to_verts(const ExtrusionMultiPath &extrusion_multi_path, float print_z, const Point &copy, GLVolume &volume)
{
    Lines               lines;
    std::vector<double> widths;
    std::vector<double> heights;
    for (const ExtrusionPath &extrusion_path : extrusion_multi_path.paths) {
        Polyline            polyline = extrusion_path.polyline;
        polyline.remove_duplicate_points();
        polyline.translate(copy);
        Lines lines_this = polyline.lines();
        append(lines, lines_this);
        widths.insert(widths.end(), lines_this.size(), extrusion_path.width);
        heights.insert(heights.end(), lines_this.size(), extrusion_path.height);
    }
    thick_lines_to_verts(lines, widths, heights, false, print_z, volume);
}

void _3DScene::extrusionentity_to_verts(const ExtrusionEntityCollection &extrusion_entity_collection, float print_z, const Point &copy, GLVolume &volume)
{
    for (const ExtrusionEntity *extrusion_entity : extrusion_entity_collection.entities)
        extrusionentity_to_verts(extrusion_entity, print_z, copy, volume);
}

void _3DScene::extrusionentity_to_verts(const ExtrusionEntity *extrusion_entity, float print_z, const Point &copy, GLVolume &volume)
{
    if (extrusion_entity != nullptr) {
        auto *extrusion_path = dynamic_cast<const ExtrusionPath*>(extrusion_entity);
        if (extrusion_path != nullptr)
            extrusionentity_to_verts(*extrusion_path, print_z, copy, volume);
        else {
            auto *extrusion_loop = dynamic_cast<const ExtrusionLoop*>(extrusion_entity);
            if (extrusion_loop != nullptr)
                extrusionentity_to_verts(*extrusion_loop, print_z, copy, volume);
            else {
                auto *extrusion_multi_path = dynamic_cast<const ExtrusionMultiPath*>(extrusion_entity);
                if (extrusion_multi_path != nullptr)
                    extrusionentity_to_verts(*extrusion_multi_path, print_z, copy, volume);
                else {
                    auto *extrusion_entity_collection = dynamic_cast<const ExtrusionEntityCollection*>(extrusion_entity);
                    if (extrusion_entity_collection != nullptr)
                        extrusionentity_to_verts(*extrusion_entity_collection, print_z, copy, volume);
                    else {
                        throw std::runtime_error("Unexpected extrusion_entity type in to_verts()");
                    }
                }
            }
        }
    }
}

void _3DScene::polyline3_to_verts(const Polyline3& polyline, double width, double height, GLVolume& volume)
{
    Lines3 lines = polyline.lines();
    std::vector<double> widths(lines.size(), width);
    std::vector<double> heights(lines.size(), height);
    thick_lines_to_verts(lines, widths, heights, false, volume);
}

void _3DScene::point3_to_verts(const Vec3crd& point, double width, double height, GLVolume& volume)
{
    thick_point_to_verts(point, width, height, volume);
}

#if !ENABLE_NON_STATIC_CANVAS_MANAGER
GUI::GLCanvas3DManager _3DScene::s_canvas_mgr;
#endif // !ENABLE_NON_STATIC_CANVAS_MANAGER

GLModel::GLModel()
    : m_filename("")
{
    m_volume.shader_outside_printer_detection_enabled = false;
}

GLModel::~GLModel()
{
    reset();
}

void GLModel::set_color(const float* color, unsigned int size)
{
    ::memcpy((void*)m_volume.color, (const void*)color, (size_t)(std::min((unsigned int)4, size) * sizeof(float)));
    m_volume.set_render_color(color, size);
}

const Vec3d& GLModel::get_offset() const
{
    return m_volume.get_volume_offset();
}

void GLModel::set_offset(const Vec3d& offset)
{
    m_volume.set_volume_offset(offset);
}

const Vec3d& GLModel::get_rotation() const
{
    return m_volume.get_volume_rotation();
}

void GLModel::set_rotation(const Vec3d& rotation)
{
    m_volume.set_volume_rotation(rotation);
}

const Vec3d& GLModel::get_scale() const
{
    return m_volume.get_volume_scaling_factor();
}

void GLModel::set_scale(const Vec3d& scale)
{
    m_volume.set_volume_scaling_factor(scale);
}

void GLModel::reset()
{
    m_volume.indexed_vertex_array.release_geometry();
    m_filename = "";
}

void GLModel::render() const
{
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    glsafe(::glCullFace(GL_BACK));
    glsafe(::glEnableClientState(GL_VERTEX_ARRAY));
    glsafe(::glEnableClientState(GL_NORMAL_ARRAY));

    GLint current_program_id;
    glsafe(::glGetIntegerv(GL_CURRENT_PROGRAM, &current_program_id));
    GLint color_id = (current_program_id > 0) ? ::glGetUniformLocation(current_program_id, "uniform_color") : -1;
    glcheck();

#if ENABLE_SLOPE_RENDERING
    if (color_id >= 0)
        glsafe(::glUniform4fv(color_id, 1, (const GLfloat*)m_volume.render_color));
    else
        glsafe(::glColor4fv(m_volume.render_color));

    m_volume.render();
#else
    m_volume.render(color_id, -1, -1);
#endif // ENABLE_SLOPE_RENDERING

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

    glsafe(::glDisableClientState(GL_VERTEX_ARRAY));
    glsafe(::glDisableClientState(GL_NORMAL_ARRAY));

    glsafe(::glDisable(GL_BLEND));
}

bool GLArrow::on_init()
{
    Pointf3s vertices;
    std::vector<Vec3i> triangles;

    // bottom face
    vertices.emplace_back(0.5, 0.0, -0.1);
    vertices.emplace_back(0.5, 2.0, -0.1);
    vertices.emplace_back(1.0, 2.0, -0.1);
    vertices.emplace_back(0.0, 3.0, -0.1);
    vertices.emplace_back(-1.0, 2.0, -0.1);
    vertices.emplace_back(-0.5, 2.0, -0.1);
    vertices.emplace_back(-0.5, 0.0, -0.1);

    // top face
    vertices.emplace_back(0.5, 0.0, 0.1);
    vertices.emplace_back(0.5, 2.0, 0.1);
    vertices.emplace_back(1.0, 2.0, 0.1);
    vertices.emplace_back(0.0, 3.0, 0.1);
    vertices.emplace_back(-1.0, 2.0, 0.1);
    vertices.emplace_back(-0.5, 2.0, 0.1);
    vertices.emplace_back(-0.5, 0.0, 0.1);

    // bottom face
    triangles.emplace_back(0, 6, 1);
    triangles.emplace_back(6, 5, 1);
    triangles.emplace_back(5, 4, 3);
    triangles.emplace_back(5, 3, 1);
    triangles.emplace_back(1, 3, 2);

    // top face
    triangles.emplace_back(7, 8, 13);
    triangles.emplace_back(13, 8, 12);
    triangles.emplace_back(12, 10, 11);
    triangles.emplace_back(8, 10, 12);
    triangles.emplace_back(8, 9, 10);

    // side face
    triangles.emplace_back(0, 1, 8);
    triangles.emplace_back(8, 7, 0);
    triangles.emplace_back(1, 2, 9);
    triangles.emplace_back(9, 8, 1);
    triangles.emplace_back(2, 3, 10);
    triangles.emplace_back(10, 9, 2);
    triangles.emplace_back(3, 4, 11);
    triangles.emplace_back(11, 10, 3);
    triangles.emplace_back(4, 5, 12);
    triangles.emplace_back(12, 11, 4);
    triangles.emplace_back(5, 6, 13);
    triangles.emplace_back(13, 12, 5);
    triangles.emplace_back(6, 0, 7);
    triangles.emplace_back(7, 13, 6);

    m_volume.indexed_vertex_array.load_mesh(TriangleMesh(vertices, triangles));
	m_volume.indexed_vertex_array.finalize_geometry(true);
    return true;
}

GLCurvedArrow::GLCurvedArrow(unsigned int resolution)
    : GLModel()
    , m_resolution(resolution)
{
    if (m_resolution == 0)
        m_resolution = 1;
}

bool GLCurvedArrow::on_init()
{
    Pointf3s vertices;
    std::vector<Vec3i> triangles;

    double ext_radius = 2.5;
    double int_radius = 1.5;
    double step = 0.5 * (double)PI / (double)m_resolution;

    unsigned int vertices_per_level = 4 + 2 * m_resolution;

    // bottom face
    vertices.emplace_back(0.0, 1.5, -0.1);
    vertices.emplace_back(0.0, 1.0, -0.1);
    vertices.emplace_back(-1.0, 2.0, -0.1);
    vertices.emplace_back(0.0, 3.0, -0.1);
    vertices.emplace_back(0.0, 2.5, -0.1);

    for (unsigned int i = 1; i <= m_resolution; ++i)
    {
        double angle = (double)i * step;
        double x = ext_radius * ::sin(angle);
        double y = ext_radius * ::cos(angle);

        vertices.emplace_back(x, y, -0.1);
    }

    for (unsigned int i = 0; i < m_resolution; ++i)
    {
        double angle = (double)i * step;
        double x = int_radius * ::cos(angle);
        double y = int_radius * ::sin(angle);

        vertices.emplace_back(x, y, -0.1);
    }

    // top face
    vertices.emplace_back(0.0, 1.5, 0.1);
    vertices.emplace_back(0.0, 1.0, 0.1);
    vertices.emplace_back(-1.0, 2.0, 0.1);
    vertices.emplace_back(0.0, 3.0, 0.1);
    vertices.emplace_back(0.0, 2.5, 0.1);

    for (unsigned int i = 1; i <= m_resolution; ++i)
    {
        double angle = (double)i * step;
        double x = ext_radius * ::sin(angle);
        double y = ext_radius * ::cos(angle);

        vertices.emplace_back(x, y, 0.1);
    }

    for (unsigned int i = 0; i < m_resolution; ++i)
    {
        double angle = (double)i * step;
        double x = int_radius * ::cos(angle);
        double y = int_radius * ::sin(angle);

        vertices.emplace_back(x, y, 0.1);
    }

    // bottom face
    triangles.emplace_back(0, 1, 2);
    triangles.emplace_back(0, 2, 4);
    triangles.emplace_back(4, 2, 3);

    int first_id = 4;
    int last_id = (int)vertices_per_level;
    triangles.emplace_back(last_id, 0, first_id);
    triangles.emplace_back(last_id, first_id, first_id + 1);
    for (unsigned int i = 1; i < m_resolution; ++i)
    {
        triangles.emplace_back(last_id - i, last_id - i + 1, first_id + i);
        triangles.emplace_back(last_id - i, first_id + i, first_id + i + 1);
    }

    // top face
    last_id += 1;
    triangles.emplace_back(last_id + 0, last_id + 2, last_id + 1);
    triangles.emplace_back(last_id + 0, last_id + 4, last_id + 2);
    triangles.emplace_back(last_id + 4, last_id + 3, last_id + 2);

    first_id = last_id + 4;
    last_id = last_id + 4 + 2 * (int)m_resolution;
    triangles.emplace_back(last_id, first_id, (int)vertices_per_level + 1);
    triangles.emplace_back(last_id, first_id + 1, first_id);
    for (unsigned int i = 1; i < m_resolution; ++i)
    {
        triangles.emplace_back(last_id - i, first_id + i, last_id - i + 1);
        triangles.emplace_back(last_id - i, first_id + i + 1, first_id + i);
    }

    // side face
	for (unsigned int i = 0; i < 4 + 2 * (unsigned int)m_resolution; ++i)
    {
        triangles.emplace_back(i, vertices_per_level + 2 + i, i + 1);
        triangles.emplace_back(i, vertices_per_level + 1 + i, vertices_per_level + 2 + i);
    }
    triangles.emplace_back(vertices_per_level, vertices_per_level + 1, 0);
    triangles.emplace_back(vertices_per_level, 2 * vertices_per_level + 1, vertices_per_level + 1);

    m_volume.indexed_vertex_array.load_mesh(TriangleMesh(vertices, triangles));
	m_volume.indexed_vertex_array.finalize_geometry(true);
    return true;
}

bool GLBed::on_init_from_file(const std::string& filename)
{
    reset();

    if (!boost::filesystem::exists(filename))
        return false;

    if (!boost::algorithm::iends_with(filename, ".stl"))
        return false;

    Model model;
    try
    {
        model = Model::read_from_file(filename);
    }
    catch (std::exception & /* ex */)
    {
        return false;
    }

    m_filename = filename;

    m_volume.indexed_vertex_array.load_mesh(model.mesh());
	m_volume.indexed_vertex_array.finalize_geometry(true);

    float color[4] = { 0.235f, 0.235f, 0.235f, 1.0f };
    set_color(color, 4);

    return true;
}

#if !ENABLE_NON_STATIC_CANVAS_MANAGER
std::string _3DScene::get_gl_info(bool format_as_html, bool extensions)
{
    return Slic3r::GUI::GLCanvas3DManager::get_gl_info().to_string(format_as_html, extensions);
}

bool _3DScene::add_canvas(wxGLCanvas* canvas, GUI::Bed3D& bed, GUI::Camera& camera, GUI::GLToolbar& view_toolbar)
{
    return s_canvas_mgr.add(canvas, bed, camera, view_toolbar);
}

bool _3DScene::remove_canvas(wxGLCanvas* canvas)
{
    return s_canvas_mgr.remove(canvas);
}

void _3DScene::remove_all_canvases()
{
    s_canvas_mgr.remove_all();
}

bool _3DScene::init(wxGLCanvas* canvas)
{
    return s_canvas_mgr.init(canvas);
}

void _3DScene::destroy()
{
    s_canvas_mgr.destroy();
}

GUI::GLCanvas3D* _3DScene::get_canvas(wxGLCanvas* canvas)
{
    return s_canvas_mgr.get_canvas(canvas);
}
#endif // !ENABLE_NON_STATIC_CANVAS_MANAGER

} // namespace Slic3r
