#include <GL/glew.h>

#include "3DScene.hpp"

#include "../../libslic3r/ExtrusionEntity.hpp"
#include "../../libslic3r/ExtrusionEntityCollection.hpp"
#include "../../libslic3r/Geometry.hpp"
#include "../../libslic3r/GCode/PreviewData.hpp"
#include "../../libslic3r/Print.hpp"
#include "../../libslic3r/Slicing.hpp"
#include "../../slic3r/GUI/PresetBundle.hpp"
#include "GCode/Analyzer.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utility>
#include <assert.h>

#include <boost/log/trivial.hpp>

#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>

#include <Eigen/Dense>

#include "GUI.hpp"

static const float UNIT_MATRIX[] = { 1.0f, 0.0f, 0.0f, 0.0f,
                                     0.0f, 1.0f, 0.0f, 0.0f,
                                     0.0f, 0.0f, 1.0f, 0.0f,
                                     0.0f, 0.0f, 0.0f, 1.0f };

namespace Slic3r {

void GLIndexedVertexArray::load_mesh_flat_shading(const TriangleMesh &mesh)
{
    assert(triangle_indices.empty() && vertices_and_normals_interleaved_size == 0);
    assert(quad_indices.empty() && triangle_indices_size == 0);
    assert(vertices_and_normals_interleaved.size() % 6 == 0 && quad_indices_size == vertices_and_normals_interleaved.size());

    this->vertices_and_normals_interleaved.reserve(this->vertices_and_normals_interleaved.size() + 3 * 3 * 2 * mesh.facets_count());
    
    for (int i = 0; i < mesh.stl.stats.number_of_facets; ++ i) {
        const stl_facet &facet = mesh.stl.facet_start[i];
        for (int j = 0; j < 3; ++ j)
            this->push_geometry(facet.vertex[j].x, facet.vertex[j].y, facet.vertex[j].z, facet.normal.x, facet.normal.y, facet.normal.z);
    }
}

void GLIndexedVertexArray::load_mesh_full_shading(const TriangleMesh &mesh)
{
    assert(triangle_indices.empty() && vertices_and_normals_interleaved_size == 0);
    assert(quad_indices.empty() && triangle_indices_size == 0);
    assert(vertices_and_normals_interleaved.size() % 6 == 0 && quad_indices_size == vertices_and_normals_interleaved.size());

    this->vertices_and_normals_interleaved.reserve(this->vertices_and_normals_interleaved.size() + 3 * 3 * 2 * mesh.facets_count());

    unsigned int vertices_count = 0;
    for (int i = 0; i < mesh.stl.stats.number_of_facets; ++i) {
        const stl_facet &facet = mesh.stl.facet_start[i];
        for (int j = 0; j < 3; ++j)
            this->push_geometry(facet.vertex[j].x, facet.vertex[j].y, facet.vertex[j].z, facet.normal.x, facet.normal.y, facet.normal.z);

        this->push_triangle(vertices_count, vertices_count + 1, vertices_count + 2);
        vertices_count += 3;
    }
}

void GLIndexedVertexArray::finalize_geometry(bool use_VBOs)
{
    assert(this->vertices_and_normals_interleaved_VBO_id == 0);
    assert(this->triangle_indices_VBO_id == 0);
    assert(this->quad_indices_VBO_id == 0);

    this->setup_sizes();

    if (use_VBOs) {
        if (! empty()) {
            glGenBuffers(1, &this->vertices_and_normals_interleaved_VBO_id);
            glBindBuffer(GL_ARRAY_BUFFER, this->vertices_and_normals_interleaved_VBO_id);
            glBufferData(GL_ARRAY_BUFFER, this->vertices_and_normals_interleaved.size() * 4, this->vertices_and_normals_interleaved.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            this->vertices_and_normals_interleaved.clear();
        }
        if (! this->triangle_indices.empty()) {
            glGenBuffers(1, &this->triangle_indices_VBO_id);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->triangle_indices_VBO_id);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, this->triangle_indices.size() * 4, this->triangle_indices.data(), GL_STATIC_DRAW);
            this->triangle_indices.clear();
        }
        if (! this->quad_indices.empty()) {
            glGenBuffers(1, &this->quad_indices_VBO_id);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->quad_indices_VBO_id);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, this->quad_indices.size() * 4, this->quad_indices.data(), GL_STATIC_DRAW);
            this->quad_indices.clear();
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
    this->shrink_to_fit();
}

void GLIndexedVertexArray::release_geometry()
{
    if (this->vertices_and_normals_interleaved_VBO_id)
        glDeleteBuffers(1, &this->vertices_and_normals_interleaved_VBO_id);
    if (this->triangle_indices_VBO_id)
        glDeleteBuffers(1, &this->triangle_indices_VBO_id);
    if (this->quad_indices_VBO_id)
        glDeleteBuffers(1, &this->quad_indices_VBO_id);
    this->clear();
    this->shrink_to_fit();
}

void GLIndexedVertexArray::render() const
{
    if (this->vertices_and_normals_interleaved_VBO_id) {
        glBindBuffer(GL_ARRAY_BUFFER, this->vertices_and_normals_interleaved_VBO_id);
        glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), (const void*)(3 * sizeof(float)));
        glNormalPointer(GL_FLOAT, 6 * sizeof(float), nullptr);
    } else {
        glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), this->vertices_and_normals_interleaved.data() + 3);
        glNormalPointer(GL_FLOAT, 6 * sizeof(float), this->vertices_and_normals_interleaved.data());
    }
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);

    if (this->indexed()) {
        if (this->vertices_and_normals_interleaved_VBO_id) {
            // Render using the Vertex Buffer Objects.
            if (this->triangle_indices_size > 0) {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->triangle_indices_VBO_id);
                glDrawElements(GL_TRIANGLES, GLsizei(this->triangle_indices_size), GL_UNSIGNED_INT, nullptr);
            }
            if (this->quad_indices_size > 0) {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->quad_indices_VBO_id);
                glDrawElements(GL_QUADS, GLsizei(this->quad_indices_size), GL_UNSIGNED_INT, nullptr);
            }
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        } else {
            // Render in an immediate mode.
            if (! this->triangle_indices.empty())
                glDrawElements(GL_TRIANGLES, GLsizei(this->triangle_indices_size), GL_UNSIGNED_INT, this->triangle_indices.data());
            if (! this->quad_indices.empty())
                glDrawElements(GL_QUADS, GLsizei(this->quad_indices_size), GL_UNSIGNED_INT, this->quad_indices.data());
        }
    } else
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(this->vertices_and_normals_interleaved_size / 6));

    if (this->vertices_and_normals_interleaved_VBO_id)
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
}

void GLIndexedVertexArray::render(
    const std::pair<size_t, size_t> &tverts_range,
    const std::pair<size_t, size_t> &qverts_range) const 
{
    assert(this->indexed());
    if (! this->indexed())
        return;

    if (this->vertices_and_normals_interleaved_VBO_id) {
        // Render using the Vertex Buffer Objects.
        glBindBuffer(GL_ARRAY_BUFFER, this->vertices_and_normals_interleaved_VBO_id);
        glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), (const void*)(3 * sizeof(float)));
        glNormalPointer(GL_FLOAT, 6 * sizeof(float), nullptr);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_NORMAL_ARRAY);
        if (this->triangle_indices_size > 0) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->triangle_indices_VBO_id);
            glDrawElements(GL_TRIANGLES, GLsizei(std::min(this->triangle_indices_size, tverts_range.second - tverts_range.first)), GL_UNSIGNED_INT, (const void*)(tverts_range.first * 4));
        }
        if (this->quad_indices_size > 0) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->quad_indices_VBO_id);
            glDrawElements(GL_QUADS, GLsizei(std::min(this->quad_indices_size, qverts_range.second - qverts_range.first)), GL_UNSIGNED_INT, (const void*)(qverts_range.first * 4));
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    } else {
        // Render in an immediate mode.
        glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), this->vertices_and_normals_interleaved.data() + 3);
        glNormalPointer(GL_FLOAT, 6 * sizeof(float), this->vertices_and_normals_interleaved.data());
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_NORMAL_ARRAY);
        if (! this->triangle_indices.empty())
            glDrawElements(GL_TRIANGLES, GLsizei(std::min(this->triangle_indices_size, tverts_range.second - tverts_range.first)), GL_UNSIGNED_INT, (const void*)(this->triangle_indices.data() + tverts_range.first));
        if (! this->quad_indices.empty())
            glDrawElements(GL_QUADS, GLsizei(std::min(this->quad_indices_size, qverts_range.second - qverts_range.first)), GL_UNSIGNED_INT, (const void*)(this->quad_indices.data() + qverts_range.first));
    }

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
}

const float GLVolume::SELECTED_COLOR[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
const float GLVolume::HOVER_COLOR[4] = { 0.4f, 0.9f, 0.1f, 1.0f };
const float GLVolume::OUTSIDE_COLOR[4] = { 0.0f, 0.38f, 0.8f, 1.0f };
const float GLVolume::SELECTED_OUTSIDE_COLOR[4] = { 0.19f, 0.58f, 1.0f, 1.0f };

GLVolume::GLVolume(float r, float g, float b, float a)
    : m_angle_z(0.0f)
    , m_scale_factor(1.0f)
    , m_dirty(true)
    , composite_id(-1)
    , select_group_id(-1)
    , drag_group_id(-1)
    , extruder_id(0)
    , selected(false)
    , is_active(true)
    , zoom_to_volumes(true)
    , shader_outside_printer_detection_enabled(false)
    , is_outside(false)
    , hover(false)
    , is_modifier(false)
    , is_wipe_tower(false)
    , is_extrusion_path(false)
    , tverts_range(0, size_t(-1))
    , qverts_range(0, size_t(-1))
{
    m_world_mat = std::vector<float>(UNIT_MATRIX, std::end(UNIT_MATRIX));

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
    size = std::min((unsigned int)4, size);
    for (int i = 0; i < size; ++i)
    {
        render_color[i] = rgba[i];
    }
}

void GLVolume::set_render_color()
{
    if (selected)
        set_render_color(is_outside ? SELECTED_OUTSIDE_COLOR : SELECTED_COLOR, 4);
    else if (hover)
        set_render_color(HOVER_COLOR, 4);
    else if (is_outside && shader_outside_printer_detection_enabled)
        set_render_color(OUTSIDE_COLOR, 4);
    else
        set_render_color(color, 4);
}

const Pointf3& GLVolume::get_origin() const
{
    return m_origin;
}

void GLVolume::set_origin(const Pointf3& origin)
{
    m_origin = origin;
    m_dirty = true;
}

void GLVolume::set_angle_z(float angle_z)
{
    m_angle_z = angle_z;
    m_dirty = true;
}

void GLVolume::set_scale_factor(float scale_factor)
{
    m_scale_factor = scale_factor;
    m_dirty = true;
}

const std::vector<float>& GLVolume::world_matrix() const
{
    if (m_dirty)
    {
        Eigen::Transform<float, 3, Eigen::Affine> m = Eigen::Transform<float, 3, Eigen::Affine>::Identity();
        m.translate(Eigen::Vector3f((float)m_origin.x, (float)m_origin.y, (float)m_origin.z));
        m.rotate(Eigen::AngleAxisf(m_angle_z, Eigen::Vector3f::UnitZ()));
        m.scale(m_scale_factor);
        ::memcpy((void*)m_world_mat.data(), (const void*)m.data(), 16 * sizeof(float));
        m_dirty = false;
    }

    return m_world_mat;
}

BoundingBoxf3 GLVolume::transformed_bounding_box() const
{
    if (m_dirty)
        m_transformed_bounding_box = bounding_box.transformed(world_matrix());

    return m_transformed_bounding_box;
}

void GLVolume::set_range(double min_z, double max_z)
{
    this->qverts_range.first  = 0;
    this->qverts_range.second = this->indexed_vertex_array.quad_indices_size;
    this->tverts_range.first  = 0;
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

    ::glCullFace(GL_BACK);
    ::glPushMatrix();
    ::glTranslated(m_origin.x, m_origin.y, m_origin.z);
    ::glRotatef(m_angle_z * 180.0f / PI, 0.0f, 0.0f, 1.0f);
    ::glScalef(m_scale_factor, m_scale_factor, m_scale_factor);
    if (this->indexed_vertex_array.indexed())
        this->indexed_vertex_array.render(this->tverts_range, this->qverts_range);
    else
        this->indexed_vertex_array.render();
    ::glPopMatrix();
}

void GLVolume::render_using_layer_height() const
{
    if (!is_active)
        return;

    GLint current_program_id;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current_program_id);

    if ((layer_height_texture_data.shader_id > 0) && (layer_height_texture_data.shader_id != current_program_id))
        glUseProgram(layer_height_texture_data.shader_id);

    GLint z_to_texture_row_id = (layer_height_texture_data.shader_id > 0) ? glGetUniformLocation(layer_height_texture_data.shader_id, "z_to_texture_row") : -1;
    GLint z_texture_row_to_normalized_id = (layer_height_texture_data.shader_id > 0) ? glGetUniformLocation(layer_height_texture_data.shader_id, "z_texture_row_to_normalized") : -1;
    GLint z_cursor_id = (layer_height_texture_data.shader_id > 0) ? glGetUniformLocation(layer_height_texture_data.shader_id, "z_cursor") : -1;
    GLint z_cursor_band_width_id = (layer_height_texture_data.shader_id > 0) ? glGetUniformLocation(layer_height_texture_data.shader_id, "z_cursor_band_width") : -1;
    GLint world_matrix_id = (layer_height_texture_data.shader_id > 0) ? glGetUniformLocation(layer_height_texture_data.shader_id, "volume_world_matrix") : -1;

    if (z_to_texture_row_id  >= 0)
        glUniform1f(z_to_texture_row_id, (GLfloat)layer_height_texture_z_to_row_id());

    if (z_texture_row_to_normalized_id >= 0)
        glUniform1f(z_texture_row_to_normalized_id, (GLfloat)(1.0f / layer_height_texture_height()));

    if (z_cursor_id >= 0)
        glUniform1f(z_cursor_id, (GLfloat)(layer_height_texture_data.print_object->model_object()->bounding_box().max.z * layer_height_texture_data.z_cursor_relative));

    if (z_cursor_band_width_id >= 0)
        glUniform1f(z_cursor_band_width_id, (GLfloat)layer_height_texture_data.edit_band_width);

    if (world_matrix_id >= 0)
        ::glUniformMatrix4fv(world_matrix_id, 1, GL_FALSE, (const GLfloat*)world_matrix().data());

    GLsizei w = (GLsizei)layer_height_texture_width();
    GLsizei h = (GLsizei)layer_height_texture_height();
    GLsizei half_w = w / 2;
    GLsizei half_h = h / 2;

    ::glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, layer_height_texture_data.texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, half_w, half_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, layer_height_texture_data_ptr_level0());
    glTexSubImage2D(GL_TEXTURE_2D, 1, 0, 0, half_w, half_h, GL_RGBA, GL_UNSIGNED_BYTE, layer_height_texture_data_ptr_level1());

    render();

    glBindTexture(GL_TEXTURE_2D, 0);

    if ((current_program_id > 0) && (layer_height_texture_data.shader_id != current_program_id))
        glUseProgram(current_program_id);
}

void GLVolume::render_VBOs(int color_id, int detection_id, int worldmatrix_id) const
{
    if (!is_active)
        return;

    if (!indexed_vertex_array.vertices_and_normals_interleaved_VBO_id)
        return;

    if (layer_height_texture_data.can_use())
    {
        ::glDisableClientState(GL_VERTEX_ARRAY);
        ::glDisableClientState(GL_NORMAL_ARRAY);
        render_using_layer_height();
        ::glEnableClientState(GL_VERTEX_ARRAY);
        ::glEnableClientState(GL_NORMAL_ARRAY);
        return;
    }

    GLsizei n_triangles = GLsizei(std::min(indexed_vertex_array.triangle_indices_size, tverts_range.second - tverts_range.first));
    GLsizei n_quads = GLsizei(std::min(indexed_vertex_array.quad_indices_size, qverts_range.second - qverts_range.first));
    if (n_triangles + n_quads == 0)
    {
        ::glDisableClientState(GL_VERTEX_ARRAY);
        ::glDisableClientState(GL_NORMAL_ARRAY);

        if (color_id >= 0)
        {
            float color[4];
            ::memcpy((void*)color, (const void*)render_color, 4 * sizeof(float));
            ::glUniform4fv(color_id, 1, (const GLfloat*)color);
        }
        else
            ::glColor4f(render_color[0], render_color[1], render_color[2], render_color[3]);

        if (detection_id != -1)
            ::glUniform1i(detection_id, shader_outside_printer_detection_enabled ? 1 : 0);

        if (worldmatrix_id != -1)
            ::glUniformMatrix4fv(worldmatrix_id, 1, GL_FALSE, (const GLfloat*)world_matrix().data());

        render();

        ::glEnableClientState(GL_VERTEX_ARRAY);
        ::glEnableClientState(GL_NORMAL_ARRAY);

        return;
    }

    if (color_id >= 0)
        ::glUniform4fv(color_id, 1, (const GLfloat*)render_color);
    else
        ::glColor4f(render_color[0], render_color[1], render_color[2], render_color[3]);

    if (detection_id != -1)
        ::glUniform1i(detection_id, shader_outside_printer_detection_enabled ? 1 : 0);

    if (worldmatrix_id != -1)
        ::glUniformMatrix4fv(worldmatrix_id, 1, GL_FALSE, (const GLfloat*)world_matrix().data());

    ::glBindBuffer(GL_ARRAY_BUFFER, indexed_vertex_array.vertices_and_normals_interleaved_VBO_id);
    ::glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), (const void*)(3 * sizeof(float)));
    ::glNormalPointer(GL_FLOAT, 6 * sizeof(float), nullptr);

    ::glPushMatrix();
    ::glTranslated(m_origin.x, m_origin.y, m_origin.z);
    ::glRotatef(m_angle_z * 180.0f / PI, 0.0f, 0.0f, 1.0f);
    ::glScalef(m_scale_factor, m_scale_factor, m_scale_factor);

    if (n_triangles > 0)
    {
        ::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexed_vertex_array.triangle_indices_VBO_id);
        ::glDrawElements(GL_TRIANGLES, n_triangles, GL_UNSIGNED_INT, (const void*)(tverts_range.first * 4));
    }
    if (n_quads > 0)
    {
        ::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexed_vertex_array.quad_indices_VBO_id);
        ::glDrawElements(GL_QUADS, n_quads, GL_UNSIGNED_INT, (const void*)(qverts_range.first * 4));
    }

    ::glPopMatrix();
}

void GLVolume::render_legacy() const
{
    assert(!indexed_vertex_array.vertices_and_normals_interleaved_VBO_id);
    if (!is_active)
        return;

    GLsizei n_triangles = GLsizei(std::min(indexed_vertex_array.triangle_indices_size, tverts_range.second - tverts_range.first));
    GLsizei n_quads = GLsizei(std::min(indexed_vertex_array.quad_indices_size, qverts_range.second - qverts_range.first));
    if (n_triangles + n_quads == 0)
    {
        ::glDisableClientState(GL_VERTEX_ARRAY);
        ::glDisableClientState(GL_NORMAL_ARRAY);

        ::glColor4f(render_color[0], render_color[1], render_color[2], render_color[3]);
        render();

        ::glEnableClientState(GL_VERTEX_ARRAY);
        ::glEnableClientState(GL_NORMAL_ARRAY);

        return;
    }

    ::glColor4f(render_color[0], render_color[1], render_color[2], render_color[3]);
    ::glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), indexed_vertex_array.vertices_and_normals_interleaved.data() + 3);
    ::glNormalPointer(GL_FLOAT, 6 * sizeof(float), indexed_vertex_array.vertices_and_normals_interleaved.data());

    ::glPushMatrix();
    ::glTranslated(m_origin.x, m_origin.y, m_origin.z);
    ::glRotatef(m_angle_z * 180.0f / PI, 0.0f, 0.0f, 1.0f);
    ::glScalef(m_scale_factor, m_scale_factor, m_scale_factor);

    if (n_triangles > 0)
        ::glDrawElements(GL_TRIANGLES, n_triangles, GL_UNSIGNED_INT, indexed_vertex_array.triangle_indices.data() + tverts_range.first);

    if (n_quads > 0)
        ::glDrawElements(GL_QUADS, n_quads, GL_UNSIGNED_INT, indexed_vertex_array.quad_indices.data() + qverts_range.first);

    ::glPopMatrix();
}

double GLVolume::layer_height_texture_z_to_row_id() const
{
    return (this->layer_height_texture.get() == nullptr) ? 0.0 : double(this->layer_height_texture->cells - 1) / (double(this->layer_height_texture->width) * this->layer_height_texture_data.print_object->model_object()->bounding_box().max.z);
}

void GLVolume::generate_layer_height_texture(PrintObject *print_object, bool force)
{
    LayersTexture *tex = this->layer_height_texture.get();
    if (tex == nullptr)
		// No layer_height_texture is assigned to this GLVolume, therefore the layer height texture cannot be filled.
		return;

	// Always try to update the layer height profile.
	bool update = print_object->update_layer_height_profile(print_object->model_object()->layer_height_profile) || force;
	// Update if the layer height profile was changed, or when the texture is not valid.
	if (! update && ! tex->data.empty() && tex->cells > 0)
        // Texture is valid, don't update.
        return; 

    if (tex->data.empty()) {
        tex->width  = 1024;
        tex->height = 1024;
        tex->levels = 2;
        tex->data.assign(tex->width * tex->height * 5, 0);
    }

    SlicingParameters slicing_params = print_object->slicing_parameters();
    bool level_of_detail_2nd_level = true;
    tex->cells = Slic3r::generate_layer_height_texture(
        slicing_params, 
        Slic3r::generate_object_layers(slicing_params, print_object->model_object()->layer_height_profile), 
        tex->data.data(), tex->height, tex->width, level_of_detail_2nd_level);
}

// 512x512 bitmaps are supported everywhere, but that may not be sufficent for super large print volumes.
#define LAYER_HEIGHT_TEXTURE_WIDTH  1024
#define LAYER_HEIGHT_TEXTURE_HEIGHT 1024

std::vector<int> GLVolumeCollection::load_object(
    const ModelObject       *model_object, 
    int                      obj_idx,
    const std::vector<int>  &instance_idxs,
    const std::string       &color_by,
    const std::string       &select_by,
    const std::string       &drag_by,
    bool                     use_VBOs)
{
    static float colors[4][4] = {
        { 1.0f, 1.0f, 0.0f, 1.f }, 
        { 1.0f, 0.5f, 0.5f, 1.f },
        { 0.5f, 1.0f, 0.5f, 1.f }, 
        { 0.5f, 0.5f, 1.0f, 1.f }
    };

    // Object will have a single common layer height texture for all volumes.
    std::shared_ptr<LayersTexture> layer_height_texture = std::make_shared<LayersTexture>();

    std::vector<int> volumes_idx;
    for (int volume_idx = 0; volume_idx < int(model_object->volumes.size()); ++ volume_idx) {
        const ModelVolume *model_volume = model_object->volumes[volume_idx];

        int extruder_id = -1;
        if (!model_volume->modifier)
        {
            extruder_id = model_volume->config.has("extruder") ? model_volume->config.option("extruder")->getInt() : 0;
            if (extruder_id == 0)
                extruder_id = model_object->config.has("extruder") ? model_object->config.option("extruder")->getInt() : 0;
        }

        for (int instance_idx : instance_idxs) {
            const ModelInstance *instance = model_object->instances[instance_idx];
            TriangleMesh mesh = model_volume->mesh;
            volumes_idx.push_back(int(this->volumes.size()));
            float color[4];
            memcpy(color, colors[((color_by == "volume") ? volume_idx : obj_idx) % 4], sizeof(float) * 3);
            color[3] = model_volume->modifier ? 0.5f : 1.f;
            this->volumes.emplace_back(new GLVolume(color));
            GLVolume &v = *this->volumes.back();
            if (use_VBOs)
                v.indexed_vertex_array.load_mesh_full_shading(mesh);
            else
                v.indexed_vertex_array.load_mesh_flat_shading(mesh);

            // finalize_geometry() clears the vertex arrays, therefore the bounding box has to be computed before finalize_geometry().
            v.bounding_box = v.indexed_vertex_array.bounding_box();
            v.indexed_vertex_array.finalize_geometry(use_VBOs);
            v.composite_id = obj_idx * 1000000 + volume_idx * 1000 + instance_idx;
            if (select_by == "object")
                v.select_group_id = obj_idx * 1000000;
            else if (select_by == "volume")
                v.select_group_id = obj_idx * 1000000 + volume_idx * 1000;
            else if (select_by == "instance")
                v.select_group_id = v.composite_id;
            if (drag_by == "object")
                v.drag_group_id = obj_idx * 1000;
            else if (drag_by == "instance")
                v.drag_group_id = obj_idx * 1000 + instance_idx;

            if (!model_volume->modifier)
            {
                v.layer_height_texture = layer_height_texture;
                if (extruder_id != -1)
                    v.extruder_id = extruder_id;
            }
            v.is_modifier = model_volume->modifier;
            v.shader_outside_printer_detection_enabled = !model_volume->modifier;
            v.set_origin(Pointf3(instance->offset.x, instance->offset.y, 0.0));
            v.set_angle_z(instance->rotation);
            v.set_scale_factor(instance->scaling_factor);
        }
    }
    
    return volumes_idx; 
}


int GLVolumeCollection::load_wipe_tower_preview(
    int obj_idx, float pos_x, float pos_y, float width, float depth, float height, float rotation_angle, bool use_VBOs, bool size_unknown, float brim_width)
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
        std::vector<Point3> facets;
        float out_points_idx[][3] = {{0, -depth, 0}, {0, 0, 0}, {38.453, 0, 0}, {61.547, 0, 0}, {100, 0, 0}, {100, -depth, 0}, {55.7735, -10, 0}, {44.2265, 10, 0},
                                     {38.453, 0, 1}, {0, 0, 1}, {0, -depth, 1}, {100, -depth, 1}, {100, 0, 1}, {61.547, 0, 1}, {55.7735, -10, 1}, {44.2265, 10, 1}};
        int out_facets_idx[][3] = {{0, 1, 2}, {3, 4, 5}, {6, 5, 0}, {3, 5, 6}, {6, 2, 7}, {6, 0, 2}, {8, 9, 10}, {11, 12, 13}, {10, 11, 14}, {14, 11, 13}, {15, 8, 14},
                                   {8, 10, 14}, {3, 12, 4}, {3, 13, 12}, {6, 13, 3}, {6, 14, 13}, {7, 14, 6}, {7, 15, 14}, {2, 15, 7}, {2, 8, 15}, {1, 8, 2}, {1, 9, 8},
                                   {0, 9, 1}, {0, 10, 9}, {5, 10, 0}, {5, 11, 10}, {4, 11, 5}, {4, 12, 11}};
        for (int i=0;i<16;++i)
            points.push_back(Pointf3(out_points_idx[i][0] / (100.f/min_width), out_points_idx[i][1] + depth, out_points_idx[i][2]));
        for (int i=0;i<28;++i)
            facets.push_back(Point3(out_facets_idx[i][0], out_facets_idx[i][1], out_facets_idx[i][2]));
        TriangleMesh tooth_mesh(points, facets);

        // We have the mesh ready. It has one tooth and width of min_width. We will now append several of these together until we are close to
        // the required width of the block. Than we can scale it precisely.
        size_t n = std::max(1, int(width/min_width)); // How many shall be merged?
        for (size_t i=0;i<n;++i) {
            mesh.merge(tooth_mesh);
            tooth_mesh.translate(min_width, 0.f, 0.f);
        }

        mesh.scale(Pointf3(width/(n*min_width), 1.f, height)); // Scaling to proper width
    }
    else
        mesh = make_cube(width, depth, height);

    // We'll make another mesh to show the brim (fixed layer height):
    TriangleMesh brim_mesh = make_cube(width+2.f*brim_width, depth+2.f*brim_width, 0.2f);
    brim_mesh.translate(-brim_width, -brim_width, 0.f);
    mesh.merge(brim_mesh);

    mesh.rotate(rotation_angle, &origin_of_rotation); // rotates the box according to the config rotation setting

    this->volumes.emplace_back(new GLVolume(color));
    GLVolume &v = *this->volumes.back();

    if (use_VBOs)
        v.indexed_vertex_array.load_mesh_full_shading(mesh);
    else
        v.indexed_vertex_array.load_mesh_flat_shading(mesh);

    v.set_origin(Pointf3(pos_x, pos_y, 0.));

    // finalize_geometry() clears the vertex arrays, therefore the bounding box has to be computed before finalize_geometry().
    v.bounding_box = v.indexed_vertex_array.bounding_box();
    v.indexed_vertex_array.finalize_geometry(use_VBOs);
    v.composite_id = obj_idx * 1000000;
    v.select_group_id = obj_idx * 1000000;
    v.drag_group_id = obj_idx * 1000;
    v.is_wipe_tower = true;
    v.shader_outside_printer_detection_enabled = ! size_unknown;
    return int(this->volumes.size() - 1);
}

void GLVolumeCollection::render_VBOs() const
{
    ::glEnable(GL_BLEND);
    ::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    ::glCullFace(GL_BACK);
    ::glEnableClientState(GL_VERTEX_ARRAY);
    ::glEnableClientState(GL_NORMAL_ARRAY);
 
    GLint current_program_id;
    ::glGetIntegerv(GL_CURRENT_PROGRAM, &current_program_id);
    GLint color_id = (current_program_id > 0) ? glGetUniformLocation(current_program_id, "uniform_color") : -1;
    GLint print_box_min_id = (current_program_id > 0) ? glGetUniformLocation(current_program_id, "print_box.min") : -1;
    GLint print_box_max_id = (current_program_id > 0) ? glGetUniformLocation(current_program_id, "print_box.max") : -1;
    GLint print_box_detection_id = (current_program_id > 0) ? glGetUniformLocation(current_program_id, "print_box.volume_detection") : -1;
    GLint print_box_worldmatrix_id = (current_program_id > 0) ? glGetUniformLocation(current_program_id, "print_box.volume_world_matrix") : -1;

    if (print_box_min_id != -1)
        ::glUniform3fv(print_box_min_id, 1, (const GLfloat*)print_box_min);

    if (print_box_max_id != -1)
        ::glUniform3fv(print_box_max_id, 1, (const GLfloat*)print_box_max);

    for (GLVolume *volume : this->volumes)
    {
        if (volume->layer_height_texture_data.can_use())
            volume->generate_layer_height_texture(volume->layer_height_texture_data.print_object, false);
        else
            volume->set_render_color();

        volume->render_VBOs(color_id, print_box_detection_id, print_box_worldmatrix_id);
    }

    ::glBindBuffer(GL_ARRAY_BUFFER, 0);
    ::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    ::glDisableClientState(GL_VERTEX_ARRAY);
    ::glDisableClientState(GL_NORMAL_ARRAY);

    ::glDisable(GL_BLEND);
}

void GLVolumeCollection::render_legacy() const
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glCullFace(GL_BACK);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
 
    for (GLVolume *volume : this->volumes)
    {
        volume->set_render_color();
        volume->render_legacy();
    }

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);

    glDisable(GL_BLEND);
}

bool GLVolumeCollection::check_outside_state(const DynamicPrintConfig* config, ModelInstance::EPrintVolumeState* out_state)
{
    if (config == nullptr)
        return false;

    const ConfigOptionPoints* opt = dynamic_cast<const ConfigOptionPoints*>(config->option("bed_shape"));
    if (opt == nullptr)
        return false;

    BoundingBox bed_box_2D = get_extents(Polygon::new_scale(opt->values));
    BoundingBoxf3 print_volume(Pointf3(unscale(bed_box_2D.min.x), unscale(bed_box_2D.min.y), 0.0), Pointf3(unscale(bed_box_2D.max.x), unscale(bed_box_2D.max.y), config->opt_float("max_print_height")));
    // Allow the objects to protrude below the print bed
    print_volume.min.z = -1e10;

    ModelInstance::EPrintVolumeState state = ModelInstance::PVS_Inside;
    bool all_contained = true;

    for (GLVolume* volume : this->volumes)
    {
        if ((volume != nullptr) && !volume->is_modifier)
        {
            const BoundingBoxf3& bb = volume->transformed_bounding_box();
            bool contained = print_volume.contains(bb);
            all_contained &= contained;

            volume->is_outside = !contained;

            if ((state == ModelInstance::PVS_Inside) && volume->is_outside)
                state = ModelInstance::PVS_Fully_Outside;

            if ((state == ModelInstance::PVS_Fully_Outside) && volume->is_outside && print_volume.intersects(bb))
                state = ModelInstance::PVS_Partly_Outside;
        }
    }

    if (out_state != nullptr)
        *out_state = state;

    return all_contained;
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
        if (PresetBundle::parse_color(txt_color, rgb))
        {
            colors[i].set(txt_color, rgb);
        }
        else
        {
            const std::string& txt_color = config->opt_string("filament_colour", i);
            if (PresetBundle::parse_color(txt_color, rgb))
                colors[i].set(txt_color, rgb);
        }
    }

    for (GLVolume* volume : volumes)
    {
        if ((volume == nullptr) || volume->is_modifier || volume->is_wipe_tower)
            continue;

        int extruder_id = volume->extruder_id - 1;
        if ((extruder_id < 0) || ((unsigned int)colors.size() <= extruder_id))
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
    Pointf  b1_prev;
    Vectorf v_prev;
    int     idx_initial[4]   = { -1, -1, -1, -1 };
    double  width_initial    = 0.;
    double  bottom_z_initial = 0.0;

    // loop once more in case of closed loops
    size_t lines_end = closed ? (lines.size() + 1) : lines.size();
    for (size_t ii = 0; ii < lines_end; ++ ii) {
        size_t i = (ii == lines.size()) ? 0 : ii;
        const Line &line = lines[i];
        double len = unscale(line.length());
        double inv_len = 1.0 / len;
        double bottom_z = top_z - heights[i];
        double middle_z = 0.5 * (top_z + bottom_z);
        double width = widths[i];

        bool is_first = (ii == 0);
        bool is_last = (ii == lines_end - 1);
        bool is_closing = closed && is_last;

        Vectorf v = Vectorf::new_unscale(line.vector());
        v.scale(inv_len);

        Pointf a = Pointf::new_unscale(line.a);
        Pointf b = Pointf::new_unscale(line.b);
        Pointf a1 = a;
        Pointf a2 = a;
        Pointf b1 = b;
        Pointf b2 = b;
        {
            double dist = 0.5 * width;  // scaled
            double dx = dist * v.x;
            double dy = dist * v.y;
            a1.translate(+dy, -dx);
            a2.translate(-dy, +dx);
            b1.translate(+dy, -dx);
            b2.translate(-dy, +dx);
        }

        // calculate new XY normals
        Vector n = line.normal();
        Vectorf3 xy_right_normal = Vectorf3::new_unscale(n.x, n.y, 0);
        xy_right_normal.scale(inv_len);

        int idx_a[4];
        int idx_b[4];
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
            volume.push_geometry(a.x, a.y, top_z   , 0., 0.,  1.); 
        } else {
            idx_a[TOP] = idx_prev[TOP];
        }

        if (is_first || bottom_z_different) {
            // Start of the 1st line segment or a change of the layer thickness while maintaining the print_z.
            idx_a[BOTTOM] = idx_last ++;
            volume.push_geometry(a.x, a.y, bottom_z, 0., 0., -1.);
            idx_a[LEFT ] = idx_last ++;
            volume.push_geometry(a2.x, a2.y, middle_z, -xy_right_normal.x, -xy_right_normal.y, -xy_right_normal.z);
            idx_a[RIGHT] = idx_last ++;
            volume.push_geometry(a1.x, a1.y, middle_z, xy_right_normal.x, xy_right_normal.y, xy_right_normal.z);
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
			double v_dot    = dot(v_prev, v);
            bool   sharp    = v_dot < 0.707; // sin(45 degrees)
            if (sharp) {
                if (!bottom_z_different)
                {
                    // Allocate new left / right points for the start of this segment as these points will receive their own normals to indicate a sharp turn.
                    idx_a[RIGHT] = idx_last++;
                    volume.push_geometry(a1.x, a1.y, middle_z, xy_right_normal.x, xy_right_normal.y, xy_right_normal.z);
                    idx_a[LEFT] = idx_last++;
                    volume.push_geometry(a2.x, a2.y, middle_z, -xy_right_normal.x, -xy_right_normal.y, -xy_right_normal.z);
                }
            }
            if (v_dot > 0.9) {
                if (!bottom_z_different)
                {
                    // The two successive segments are nearly collinear.
                    idx_a[LEFT ] = idx_prev[LEFT];
                    idx_a[RIGHT] = idx_prev[RIGHT];
                }
            }
            else if (!sharp) {
                if (!bottom_z_different)
                {
                    // Create a sharp corner with an overshot and average the left / right normals.
                    // At the crease angle of 45 degrees, the overshot at the corner will be less than (1-1/cos(PI/8)) = 8.2% over an arc.
                    Pointf intersection;
                    Geometry::ray_ray_intersection(b1_prev, v_prev, a1, v, intersection);
                    a1 = intersection;
                    a2 = 2. * a - intersection;
                    assert(length(a1.vector_to(a)) < width);
                    assert(length(a2.vector_to(a)) < width);
                    float *n_left_prev  = volume.vertices_and_normals_interleaved.data() + idx_prev[LEFT ] * 6;
                    float *p_left_prev  = n_left_prev  + 3;
                    float *n_right_prev = volume.vertices_and_normals_interleaved.data() + idx_prev[RIGHT] * 6;
                    float *p_right_prev = n_right_prev + 3;
                    p_left_prev [0] = float(a2.x);
                    p_left_prev [1] = float(a2.y);
                    p_right_prev[0] = float(a1.x);
                    p_right_prev[1] = float(a1.y);
                    xy_right_normal.x += n_right_prev[0];
                    xy_right_normal.y += n_right_prev[1];
                    xy_right_normal.scale(1. / length(xy_right_normal));
                    n_left_prev [0] = float(-xy_right_normal.x);
                    n_left_prev [1] = float(-xy_right_normal.y);
                    n_right_prev[0] = float( xy_right_normal.x);
                    n_right_prev[1] = float( xy_right_normal.y);
                    idx_a[LEFT ] = idx_prev[LEFT ];
                    idx_a[RIGHT] = idx_prev[RIGHT];
                }
            }
            else if (cross(v_prev, v) > 0.) {
                // Right turn. Fill in the right turn wedge.
                volume.push_triangle(idx_prev[RIGHT], idx_a   [RIGHT],  idx_prev[TOP]   );
                volume.push_triangle(idx_prev[RIGHT], idx_prev[BOTTOM], idx_a   [RIGHT] );
            } else {
                // Left turn. Fill in the left turn wedge.
                volume.push_triangle(idx_prev[LEFT],  idx_prev[TOP],    idx_a   [LEFT]  );
                volume.push_triangle(idx_prev[LEFT],  idx_a   [LEFT],   idx_prev[BOTTOM]);
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
            volume.push_geometry(b.x, b.y, top_z   , 0., 0.,  1.);
        }

        if (is_closing && (width == width_initial) && (bottom_z == bottom_z_initial)) {
            idx_b[BOTTOM] = idx_initial[BOTTOM];
        } else {
            idx_b[BOTTOM] = idx_last ++;
            volume.push_geometry(b.x, b.y, bottom_z, 0., 0., -1.);
        }
        // Generate new vertices for the end of this line segment.
        idx_b[LEFT  ] = idx_last ++;
        volume.push_geometry(b2.x, b2.y, middle_z, -xy_right_normal.x, -xy_right_normal.y, -xy_right_normal.z);
        idx_b[RIGHT ] = idx_last ++;
        volume.push_geometry(b1.x, b1.y, middle_z, xy_right_normal.x, xy_right_normal.y, xy_right_normal.z);

        memcpy(idx_prev, idx_b, 4 * sizeof(int));
        bottom_z_prev = bottom_z;
        b1_prev = b1;
        v_prev = v;

        if (bottom_z_different)
        {
            // Found a change of the layer thickness -> Add a cap at the beginning of this segment.
            volume.push_quad(idx_a[BOTTOM], idx_a[RIGHT], idx_a[TOP], idx_a[LEFT]);
        }

        if (! closed) {
            // Terminate open paths with caps.
            if (is_first && !bottom_z_different)
                volume.push_quad(idx_a[BOTTOM], idx_a[RIGHT], idx_a[TOP], idx_a[LEFT]);
            // We don't use 'else' because both cases are true if we have only one line.
            if (is_last && !bottom_z_different)
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
    Vectorf3 n_right_prev;
    Vectorf3 n_top_prev;
    Vectorf3 unit_v_prev;
    double   width_initial = 0.0;

    // new vertices around the line endpoints
    // left, right, top, bottom
    Pointf3 a[4];
    Pointf3 b[4];

    // loop once more in case of closed loops
    size_t lines_end = closed ? (lines.size() + 1) : lines.size();
    for (size_t ii = 0; ii < lines_end; ++ii)
    {
        size_t i = (ii == lines.size()) ? 0 : ii;

        const Line3& line = lines[i];
        double height = heights[i];
        double width = widths[i];

        Vectorf3 unit_v = normalize(Vectorf3::new_unscale(line.vector()));

        Vectorf3 n_top;
        Vectorf3 n_right;
        Vectorf3 unit_positive_z(0.0, 0.0, 1.0);

        if ((line.a.x == line.b.x) && (line.a.y == line.b.y))
        {
            // vertical segment
            n_right = (line.a.z < line.b.z) ? Vectorf3(-1.0, 0.0, 0.0) : Vectorf3(1.0, 0.0, 0.0);
            n_top = Vectorf3(0.0, 1.0, 0.0);
        }
        else
        {
            // generic segment
            n_right = normalize(cross(unit_v, unit_positive_z));
            n_top = normalize(cross(n_right, unit_v));
        }

        Vectorf3 rl_displacement = 0.5 * width * n_right;
        Vectorf3 tb_displacement = 0.5 * height * n_top;
        Pointf3 l_a = Pointf3::new_unscale(line.a);
        Pointf3 l_b = Pointf3::new_unscale(line.b);

        a[RIGHT] = l_a + rl_displacement;
        a[LEFT] = l_a - rl_displacement;
        a[TOP] = l_a + tb_displacement;
        a[BOTTOM] = l_a - tb_displacement;
        b[RIGHT] = l_b + rl_displacement;
        b[LEFT] = l_b - rl_displacement;
        b[TOP] = l_b + tb_displacement;
        b[BOTTOM] = l_b - tb_displacement;

        Vectorf3 n_bottom = -n_top;
        Vectorf3 n_left = -n_right;

        int idx_a[4];
        int idx_b[4];
        int idx_last = int(volume.vertices_and_normals_interleaved.size() / 6);

        bool z_different = (z_prev != l_a.z);
        z_prev = l_b.z;

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
            double v_dot = dot(unit_v_prev, unit_v);
            bool is_sharp = v_dot < 0.707; // sin(45 degrees)
            bool is_right_turn = dot(n_top_prev, cross(unit_v_prev, unit_v)) > 0.0;

            if (is_sharp)
            {
                // Allocate new left / right points for the start of this segment as these points will receive their own normals to indicate a sharp turn.
                idx_a[RIGHT] = idx_last++;
                volume.push_geometry(a[RIGHT], n_right);
                idx_a[LEFT] = idx_last++;
                volume.push_geometry(a[LEFT], n_left);
            }

            if (v_dot > 0.9)
            {
                // The two successive segments are nearly collinear.
                idx_a[LEFT] = idx_prev[LEFT];
                idx_a[RIGHT] = idx_prev[RIGHT];
            }
            else if (!is_sharp)
            {
                // Create a sharp corner with an overshot and average the left / right normals.
                // At the crease angle of 45 degrees, the overshot at the corner will be less than (1-1/cos(PI/8)) = 8.2% over an arc.

                // averages normals
                Vectorf3 average_n_right = normalize(0.5 * (n_right + n_right_prev));
                Vectorf3 average_n_left = -average_n_right;
                Vectorf3 average_rl_displacement = 0.5 * width * average_n_right;

                // updates vertices around a
                a[RIGHT] = l_a + average_rl_displacement;
                a[LEFT] = l_a - average_rl_displacement;

                // updates previous line normals
                float* normal_left_prev = volume.vertices_and_normals_interleaved.data() + idx_prev[LEFT] * 6;
                normal_left_prev[0] = float(average_n_left.x);
                normal_left_prev[1] = float(average_n_left.y);
                normal_left_prev[2] = float(average_n_left.z);

                float* normal_right_prev = volume.vertices_and_normals_interleaved.data() + idx_prev[RIGHT] * 6;
                normal_right_prev[0] = float(average_n_right.x);
                normal_right_prev[1] = float(average_n_right.y);
                normal_right_prev[2] = float(average_n_right.z);

                // updates previous line's vertices around b
                float* b_left_prev = normal_left_prev + 3;
                b_left_prev[0] = float(a[LEFT].x);
                b_left_prev[1] = float(a[LEFT].y);
                b_left_prev[2] = float(a[LEFT].z);

                float* b_right_prev = normal_right_prev + 3;
                b_right_prev[0] = float(a[RIGHT].x);
                b_right_prev[1] = float(a[RIGHT].y);
                b_right_prev[2] = float(a[RIGHT].z);

                idx_a[LEFT] = idx_prev[LEFT];
                idx_a[RIGHT] = idx_prev[RIGHT];
            }
            else if (is_right_turn)
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

static void point_to_indexed_vertex_array(const Point3& point,
    double width,
    double height,
    GLIndexedVertexArray& volume)
{
    // builds a double piramid, with vertices on the local axes, around the point

    Pointf3 center = Pointf3::new_unscale(point);

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

    Vectorf3 displacement_x(w, 0.0, 0.0);
    Vectorf3 displacement_y(0.0, w, 0.0);
    Vectorf3 displacement_z(0.0, 0.0, h);

    Vectorf3 unit_x(1.0, 0.0, 0.0);
    Vectorf3 unit_y(0.0, 1.0, 0.0);
    Vectorf3 unit_z(0.0, 0.0, 1.0);

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

static void thick_point_to_verts(const Point3& point,
    double width,
    double height,
    GLVolume& volume)
{
    point_to_indexed_vertex_array(point, width, height, volume.indexed_vertex_array);
}

// Fill in the qverts and tverts with quads and triangles for the extrusion_path.
void _3DScene::extrusionentity_to_verts(const ExtrusionPath &extrusion_path, float print_z, GLVolume &volume)
{
    Lines               lines = extrusion_path.polyline.lines();
    std::vector<double> widths(lines.size(), extrusion_path.width);
    std::vector<double> heights(lines.size(), extrusion_path.height);
    thick_lines_to_verts(lines, widths, heights, false, print_z, volume);
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
                        CONFESS("Unexpected extrusion_entity type in to_verts()");
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

void _3DScene::point3_to_verts(const Point3& point, double width, double height, GLVolume& volume)
{
    thick_point_to_verts(point, width, height, volume);
}

GUI::GLCanvas3DManager _3DScene::s_canvas_mgr;

void _3DScene::init_gl()
{
    s_canvas_mgr.init_gl();
}

std::string _3DScene::get_gl_info(bool format_as_html, bool extensions)
{
    return s_canvas_mgr.get_gl_info(format_as_html, extensions);
}

bool _3DScene::use_VBOs()
{
    return s_canvas_mgr.use_VBOs();
}

bool _3DScene::add_canvas(wxGLCanvas* canvas)
{
    return s_canvas_mgr.add(canvas);
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

void _3DScene::set_as_dirty(wxGLCanvas* canvas)
{
    s_canvas_mgr.set_as_dirty(canvas);
}

unsigned int _3DScene::get_volumes_count(wxGLCanvas* canvas)
{
    return s_canvas_mgr.get_volumes_count(canvas);
}

void _3DScene::reset_volumes(wxGLCanvas* canvas)
{
    s_canvas_mgr.reset_volumes(canvas);
}

void _3DScene::deselect_volumes(wxGLCanvas* canvas)
{
    s_canvas_mgr.deselect_volumes(canvas);
}

void _3DScene::select_volume(wxGLCanvas* canvas, unsigned int id)
{
    s_canvas_mgr.select_volume(canvas, id);
}

void _3DScene::update_volumes_selection(wxGLCanvas* canvas, const std::vector<int>& selections)
{
    s_canvas_mgr.update_volumes_selection(canvas, selections);
}

int _3DScene::check_volumes_outside_state(wxGLCanvas* canvas, const DynamicPrintConfig* config)
{
    return s_canvas_mgr.check_volumes_outside_state(canvas, config);
}

bool _3DScene::move_volume_up(wxGLCanvas* canvas, unsigned int id)
{
    return s_canvas_mgr.move_volume_up(canvas, id);
}

bool _3DScene::move_volume_down(wxGLCanvas* canvas, unsigned int id)
{
    return s_canvas_mgr.move_volume_down(canvas, id);
}

void _3DScene::set_objects_selections(wxGLCanvas* canvas, const std::vector<int>& selections)
{
    s_canvas_mgr.set_objects_selections(canvas, selections);
}

void _3DScene::set_config(wxGLCanvas* canvas, DynamicPrintConfig* config)
{
    s_canvas_mgr.set_config(canvas, config);
}

void _3DScene::set_print(wxGLCanvas* canvas, Print* print)
{
    s_canvas_mgr.set_print(canvas, print);
}

void _3DScene::set_model(wxGLCanvas* canvas, Model* model)
{
    s_canvas_mgr.set_model(canvas, model);
}

void _3DScene::set_bed_shape(wxGLCanvas* canvas, const Pointfs& shape)
{
    return s_canvas_mgr.set_bed_shape(canvas, shape);
}

void _3DScene::set_auto_bed_shape(wxGLCanvas* canvas)
{
    return s_canvas_mgr.set_auto_bed_shape(canvas);
}

BoundingBoxf3 _3DScene::get_volumes_bounding_box(wxGLCanvas* canvas)
{
    return s_canvas_mgr.get_volumes_bounding_box(canvas);
}

void _3DScene::set_axes_length(wxGLCanvas* canvas, float length)
{
    s_canvas_mgr.set_axes_length(canvas, length);
}

void _3DScene::set_cutting_plane(wxGLCanvas* canvas, float z, const ExPolygons& polygons)
{
    return s_canvas_mgr.set_cutting_plane(canvas, z, polygons);
}

void _3DScene::set_color_by(wxGLCanvas* canvas, const std::string& value)
{
    return s_canvas_mgr.set_color_by(canvas, value);
}

void _3DScene::set_select_by(wxGLCanvas* canvas, const std::string& value)
{
    return s_canvas_mgr.set_select_by(canvas, value);
}

void _3DScene::set_drag_by(wxGLCanvas* canvas, const std::string& value)
{
    return s_canvas_mgr.set_drag_by(canvas, value);
}

bool _3DScene::is_layers_editing_enabled(wxGLCanvas* canvas)
{
    return s_canvas_mgr.is_layers_editing_enabled(canvas);
}

bool _3DScene::is_layers_editing_allowed(wxGLCanvas* canvas)
{
    return s_canvas_mgr.is_layers_editing_allowed(canvas);
}

bool _3DScene::is_shader_enabled(wxGLCanvas* canvas)
{
    return s_canvas_mgr.is_shader_enabled(canvas);
}

bool _3DScene::is_reload_delayed(wxGLCanvas* canvas)
{
    return s_canvas_mgr.is_reload_delayed(canvas);
}

void _3DScene::enable_layers_editing(wxGLCanvas* canvas, bool enable)
{
    s_canvas_mgr.enable_layers_editing(canvas, enable);
}

void _3DScene::enable_warning_texture(wxGLCanvas* canvas, bool enable)
{
    s_canvas_mgr.enable_warning_texture(canvas, enable);
}

void _3DScene::enable_legend_texture(wxGLCanvas* canvas, bool enable)
{
    s_canvas_mgr.enable_legend_texture(canvas, enable);
}

void _3DScene::enable_picking(wxGLCanvas* canvas, bool enable)
{
    s_canvas_mgr.enable_picking(canvas, enable);
}

void _3DScene::enable_moving(wxGLCanvas* canvas, bool enable)
{
    s_canvas_mgr.enable_moving(canvas, enable);
}

void _3DScene::enable_gizmos(wxGLCanvas* canvas, bool enable)
{
    s_canvas_mgr.enable_gizmos(canvas, enable);
}

void _3DScene::enable_shader(wxGLCanvas* canvas, bool enable)
{
    s_canvas_mgr.enable_shader(canvas, enable);
}

void _3DScene::enable_force_zoom_to_bed(wxGLCanvas* canvas, bool enable)
{
    s_canvas_mgr.enable_force_zoom_to_bed(canvas, enable);
}

void _3DScene::enable_dynamic_background(wxGLCanvas* canvas, bool enable)
{
    s_canvas_mgr.enable_dynamic_background(canvas, enable);
}

void _3DScene::allow_multisample(wxGLCanvas* canvas, bool allow)
{
    s_canvas_mgr.allow_multisample(canvas, allow);
}

void _3DScene::zoom_to_bed(wxGLCanvas* canvas)
{
    s_canvas_mgr.zoom_to_bed(canvas);
}

void _3DScene::zoom_to_volumes(wxGLCanvas* canvas)
{
    s_canvas_mgr.zoom_to_volumes(canvas);
}

void _3DScene::select_view(wxGLCanvas* canvas, const std::string& direction)
{
    s_canvas_mgr.select_view(canvas, direction);
}

void _3DScene::set_viewport_from_scene(wxGLCanvas* canvas, wxGLCanvas* other)
{
    s_canvas_mgr.set_viewport_from_scene(canvas, other);
}

void _3DScene::update_volumes_colors_by_extruder(wxGLCanvas* canvas)
{
    s_canvas_mgr.update_volumes_colors_by_extruder(canvas);
}

void _3DScene::update_gizmos_data(wxGLCanvas* canvas)
{
    s_canvas_mgr.update_gizmos_data(canvas);
}

void _3DScene::render(wxGLCanvas* canvas)
{
    s_canvas_mgr.render(canvas);
}

std::vector<double> _3DScene::get_current_print_zs(wxGLCanvas* canvas, bool active_only)
{
    return s_canvas_mgr.get_current_print_zs(canvas, active_only);
}

void _3DScene::set_toolpaths_range(wxGLCanvas* canvas, double low, double high)
{
    s_canvas_mgr.set_toolpaths_range(canvas, low, high);
}

void _3DScene::register_on_viewport_changed_callback(wxGLCanvas* canvas, void* callback)
{
    s_canvas_mgr.register_on_viewport_changed_callback(canvas, callback);
}

void _3DScene::register_on_double_click_callback(wxGLCanvas* canvas, void* callback)
{
    s_canvas_mgr.register_on_double_click_callback(canvas, callback);
}

void _3DScene::register_on_right_click_callback(wxGLCanvas* canvas, void* callback)
{
    s_canvas_mgr.register_on_right_click_callback(canvas, callback);
}

void _3DScene::register_on_select_object_callback(wxGLCanvas* canvas, void* callback)
{
    s_canvas_mgr.register_on_select_object_callback(canvas, callback);
}

void _3DScene::register_on_model_update_callback(wxGLCanvas* canvas, void* callback)
{
    s_canvas_mgr.register_on_model_update_callback(canvas, callback);
}

void _3DScene::register_on_remove_object_callback(wxGLCanvas* canvas, void* callback)
{
    s_canvas_mgr.register_on_remove_object_callback(canvas, callback);
}

void _3DScene::register_on_arrange_callback(wxGLCanvas* canvas, void* callback)
{
    s_canvas_mgr.register_on_arrange_callback(canvas, callback);
}

void _3DScene::register_on_rotate_object_left_callback(wxGLCanvas* canvas, void* callback)
{
    s_canvas_mgr.register_on_rotate_object_left_callback(canvas, callback);
}

void _3DScene::register_on_rotate_object_right_callback(wxGLCanvas* canvas, void* callback)
{
    s_canvas_mgr.register_on_rotate_object_right_callback(canvas, callback);
}

void _3DScene::register_on_scale_object_uniformly_callback(wxGLCanvas* canvas, void* callback)
{
    s_canvas_mgr.register_on_scale_object_uniformly_callback(canvas, callback);
}

void _3DScene::register_on_increase_objects_callback(wxGLCanvas* canvas, void* callback)
{
    s_canvas_mgr.register_on_increase_objects_callback(canvas, callback);
}

void _3DScene::register_on_decrease_objects_callback(wxGLCanvas* canvas, void* callback)
{
    s_canvas_mgr.register_on_decrease_objects_callback(canvas, callback);
}

void _3DScene::register_on_instance_moved_callback(wxGLCanvas* canvas, void* callback)
{
    s_canvas_mgr.register_on_instance_moved_callback(canvas, callback);
}

void _3DScene::register_on_wipe_tower_moved_callback(wxGLCanvas* canvas, void* callback)
{
    s_canvas_mgr.register_on_wipe_tower_moved_callback(canvas, callback);
}

void _3DScene::register_on_enable_action_buttons_callback(wxGLCanvas* canvas, void* callback)
{
    s_canvas_mgr.register_on_enable_action_buttons_callback(canvas, callback);
}

void _3DScene::register_on_gizmo_scale_uniformly_callback(wxGLCanvas* canvas, void* callback)
{
    s_canvas_mgr.register_on_gizmo_scale_uniformly_callback(canvas, callback);
}

void _3DScene::register_on_gizmo_rotate_callback(wxGLCanvas* canvas, void* callback)
{
    s_canvas_mgr.register_on_gizmo_rotate_callback(canvas, callback);
}

void _3DScene::register_on_update_geometry_info_callback(wxGLCanvas* canvas, void* callback)
{
    s_canvas_mgr.register_on_update_geometry_info_callback(canvas, callback);
}

static inline int hex_digit_to_int(const char c)
{
    return 
        (c >= '0' && c <= '9') ? int(c - '0') : 
        (c >= 'A' && c <= 'F') ? int(c - 'A') + 10 :
		(c >= 'a' && c <= 'f') ? int(c - 'a') + 10 : -1;
}

static inline std::vector<float> parse_colors(const std::vector<std::string> &scolors)
{
    std::vector<float> output(scolors.size() * 4, 1.f);
    for (size_t i = 0; i < scolors.size(); ++ i) {
        const std::string &scolor = scolors[i];
        const char        *c      = scolor.data() + 1;
        if (scolor.size() == 7 && scolor.front() == '#') {
			for (size_t j = 0; j < 3; ++j) {
                int digit1 = hex_digit_to_int(*c ++);
                int digit2 = hex_digit_to_int(*c ++);
                if (digit1 == -1 || digit2 == -1)
                    break;
                output[i * 4 + j] = float(digit1 * 16 + digit2) / 255.f;
            }
        }
    }
    return output;
}

std::vector<int> _3DScene::load_object(wxGLCanvas* canvas, const ModelObject* model_object, int obj_idx, std::vector<int> instance_idxs)
{
    return s_canvas_mgr.load_object(canvas, model_object, obj_idx, instance_idxs);
}

std::vector<int> _3DScene::load_object(wxGLCanvas* canvas, const Model* model, int obj_idx)
{
    return s_canvas_mgr.load_object(canvas, model, obj_idx);
}

void _3DScene::reload_scene(wxGLCanvas* canvas, bool force)
{
    s_canvas_mgr.reload_scene(canvas, force);
}

void _3DScene::load_gcode_preview(wxGLCanvas* canvas, const GCodePreviewData* preview_data, const std::vector<std::string>& str_tool_colors)
{
    s_canvas_mgr.load_gcode_preview(canvas, preview_data, str_tool_colors);
}

void _3DScene::load_preview(wxGLCanvas* canvas, const std::vector<std::string>& str_tool_colors)
{
    s_canvas_mgr.load_preview(canvas, str_tool_colors);
}

void _3DScene::reset_legend_texture()
{
    s_canvas_mgr.reset_legend_texture();
}

} // namespace Slic3r
