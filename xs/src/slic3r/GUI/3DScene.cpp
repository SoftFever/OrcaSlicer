#include <GL/glew.h>

#include "3DScene.hpp"

#include "../../libslic3r/libslic3r.h"
#include "../../libslic3r/ExtrusionEntity.hpp"
#include "../../libslic3r/ExtrusionEntityCollection.hpp"
#include "../../libslic3r/Geometry.hpp"
#include "../../libslic3r/Print.hpp"
#include "../../libslic3r/Slicing.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utility>
#include <assert.h>

#include <boost/log/trivial.hpp>

#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>

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
    glCullFace(GL_BACK);
    glPushMatrix();
    glTranslated(this->origin.x, this->origin.y, this->origin.z);
    if (this->indexed_vertex_array.indexed())
        this->indexed_vertex_array.render(this->tverts_range, this->qverts_range);
    else
        this->indexed_vertex_array.render();
    glPopMatrix();
}

void GLVolume::generate_layer_height_texture(PrintObject *print_object, bool force)
{
    GLTexture *tex = this->layer_height_texture.get();
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
    std::shared_ptr<GLTexture> layer_height_texture = std::make_shared<GLTexture>();
    
    std::vector<int> volumes_idx;
    for (int volume_idx = 0; volume_idx < int(model_object->volumes.size()); ++ volume_idx) {
        const ModelVolume *model_volume = model_object->volumes[volume_idx];
        for (int instance_idx : instance_idxs) {
            const ModelInstance *instance = model_object->instances[instance_idx];
            TriangleMesh mesh = model_volume->mesh;
            instance->transform_mesh(&mesh);
            volumes_idx.push_back(int(this->volumes.size()));
            float color[4];
            memcpy(color, colors[((color_by == "volume") ? volume_idx : obj_idx) % 4], sizeof(float) * 3);
            color[3] = model_volume->modifier ? 0.5f : 1.f;
            this->volumes.emplace_back(new GLVolume(color));
            GLVolume &v = *this->volumes.back();
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
            if (! model_volume->modifier)
                v.layer_height_texture = layer_height_texture;
        }
    }
    
    return volumes_idx; 
}


int GLVolumeCollection::load_wipe_tower_preview(
    int obj_idx, float pos_x, float pos_y, float width, float depth, float height, bool use_VBOs)
{
    float color[4] = { 1.0f, 1.0f, 0.0f, 0.5f };
    this->volumes.emplace_back(new GLVolume(color));
    GLVolume &v = *this->volumes.back();
    auto mesh = make_cube(width, depth, height);
    v.indexed_vertex_array.load_mesh_flat_shading(mesh);
    v.origin = Pointf3(pos_x, pos_y, 0.);
    // finalize_geometry() clears the vertex arrays, therefore the bounding box has to be computed before finalize_geometry().
    v.bounding_box = v.indexed_vertex_array.bounding_box();
    v.indexed_vertex_array.finalize_geometry(use_VBOs);
    v.composite_id = obj_idx * 1000000;
    v.select_group_id = obj_idx * 1000000;
    v.drag_group_id = obj_idx * 1000;
    return int(this->volumes.size() - 1);
}

void GLVolumeCollection::render_VBOs() const
{
//    glEnable(GL_BLEND);
//    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glCullFace(GL_BACK);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
 
    GLint current_program_id;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current_program_id);
    GLint color_id = (current_program_id > 0) ? glGetUniformLocation(current_program_id, "uniform_color") : -1;

    for (GLVolume *volume : this->volumes) {
        if (! volume->indexed_vertex_array.vertices_and_normals_interleaved_VBO_id)
            continue;
        GLsizei n_triangles = GLsizei(std::min(volume->indexed_vertex_array.triangle_indices_size, volume->tverts_range.second - volume->tverts_range.first));
        GLsizei n_quads     = GLsizei(std::min(volume->indexed_vertex_array.quad_indices_size,     volume->qverts_range.second - volume->qverts_range.first));
        if (n_triangles + n_quads == 0)
            continue;
        if (color_id >= 0)
            glUniform4fv(color_id, 1, (const GLfloat*)volume->color);
        else
            glColor4f(volume->color[0], volume->color[1], volume->color[2], volume->color[3]);            
        glBindBuffer(GL_ARRAY_BUFFER, volume->indexed_vertex_array.vertices_and_normals_interleaved_VBO_id);
        glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), (const void*)(3 * sizeof(float)));
        glNormalPointer(GL_FLOAT, 6 * sizeof(float), nullptr);
        if (n_triangles > 0) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, volume->indexed_vertex_array.triangle_indices_VBO_id);
            glDrawElements(GL_TRIANGLES, n_triangles, GL_UNSIGNED_INT, (const void*)(volume->tverts_range.first * 4));
        }
        if (n_quads > 0) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, volume->indexed_vertex_array.quad_indices_VBO_id);
            glDrawElements(GL_QUADS, n_quads, GL_UNSIGNED_INT, (const void*)(volume->qverts_range.first * 4));
        }
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
//    glDisable(GL_BLEND);
}

void GLVolumeCollection::render_legacy() const
{
    glCullFace(GL_BACK);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
 
    for (GLVolume *volume : this->volumes) {
        assert(! volume->indexed_vertex_array.vertices_and_normals_interleaved_VBO_id);
        GLsizei n_triangles = GLsizei(std::min(volume->indexed_vertex_array.triangle_indices_size, volume->tverts_range.second - volume->tverts_range.first));
        GLsizei n_quads     = GLsizei(std::min(volume->indexed_vertex_array.quad_indices_size,     volume->qverts_range.second - volume->qverts_range.first));
        if (n_triangles + n_quads == 0)
            continue;
        glColor4f(volume->color[0], volume->color[1], volume->color[2], volume->color[3]);
        glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), volume->indexed_vertex_array.vertices_and_normals_interleaved.data() + 3);
        glNormalPointer(GL_FLOAT, 6 * sizeof(float), volume->indexed_vertex_array.vertices_and_normals_interleaved.data());
        bool has_offset = volume->origin.x != 0 || volume->origin.y != 0 || volume->origin.z != 0;
        if (has_offset) {
            glPushMatrix();
            glTranslated(volume->origin.x, volume->origin.y, volume->origin.z);
        }
        if (n_triangles > 0)
            glDrawElements(GL_TRIANGLES, n_triangles, GL_UNSIGNED_INT, volume->indexed_vertex_array.triangle_indices.data() + volume->tverts_range.first);
        if (n_quads > 0)
            glDrawElements(GL_QUADS, n_quads, GL_UNSIGNED_INT, volume->indexed_vertex_array.quad_indices.data() + volume->qverts_range.first);
        if (has_offset)
            glPushMatrix();
    }

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
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

    Line prev_line;
    // right, left, top, bottom
    int     idx_prev[4]      = { -1, -1, -1, -1 };
    double  bottom_z_prev    = 0.;
    Pointf  b1_prev;
    Pointf  b2_prev;
    Vectorf v_prev;
    int     idx_initial[4]   = { -1, -1, -1, -1 };
    double  width_initial    = 0.;

    // loop once more in case of closed loops
    size_t lines_end = closed ? (lines.size() + 1) : lines.size();
    for (size_t ii = 0; ii < lines_end; ++ ii) {
        size_t i = (ii == lines.size()) ? 0 : ii;
        const Line &line = lines[i];
        double len = unscale(line.length());
        double bottom_z = top_z - heights[i];
        double middle_z = (top_z + bottom_z) / 2.;
        double width = widths[i];
        
        Vectorf v = Vectorf::new_unscale(line.vector());
        v.scale(1. / len);
        
        Pointf a = Pointf::new_unscale(line.a);
        Pointf b = Pointf::new_unscale(line.b);
        Pointf a1 = a;
        Pointf a2 = a;
        Pointf b1 = b;
        Pointf b2 = b;
        {
            double dist = width / 2.;  // scaled
            a1.translate(+dist*v.y, -dist*v.x);
            a2.translate(-dist*v.y, +dist*v.x);
            b1.translate(+dist*v.y, -dist*v.x);
            b2.translate(-dist*v.y, +dist*v.x);
        }

        // calculate new XY normals
        Vector n = line.normal();
        Vectorf3 xy_right_normal = Vectorf3::new_unscale(n.x, n.y, 0);
        xy_right_normal.scale(1.f / len);

        int idx_a[4];
        int idx_b[4];
        int idx_last = int(volume.vertices_and_normals_interleaved.size() / 6);

        bool bottom_z_different = bottom_z_prev != bottom_z;
        bottom_z_prev = bottom_z;

        // Share top / bottom vertices if possible.
        if (ii == 0) {
            idx_a[TOP] = idx_last ++;
            volume.push_geometry(a.x, a.y, top_z   , 0., 0.,  1.); 
        } else {
            idx_a[TOP] = idx_prev[TOP];
        }
        if (ii == 0 || bottom_z_different) {
            // Start of the 1st line segment or a change of the layer thickness while maintaining the print_z.
            idx_a[BOTTOM] = idx_last ++;
            volume.push_geometry(a.x, a.y, bottom_z, 0., 0., -1.);
            idx_a[LEFT ] = idx_last ++;
            volume.push_geometry(a2.x, a2.y, middle_z, -xy_right_normal.x, -xy_right_normal.y, -xy_right_normal.z);
            idx_a[RIGHT] = idx_last ++;
            volume.push_geometry(a1.x, a1.y, middle_z, xy_right_normal.x, xy_right_normal.y, xy_right_normal.z);
        } else {
            idx_a[BOTTOM] = idx_prev[BOTTOM];
        }

        if (ii == 0) {
            // Start of the 1st line segment.
            width_initial    = width;
            memcpy(idx_initial, idx_a, sizeof(int) * 4);
        } else {
            // Continuing a previous segment.
            // Share left / right vertices if possible.
			double v_dot    = dot(v_prev, v);
            bool   sharp    = v_dot < 0.707; // sin(45 degrees)
            if (sharp) {
                // Allocate new left / right points for the start of this segment as these points will receive their own normals to indicate a sharp turn.
                idx_a[RIGHT] = idx_last ++;
                volume.push_geometry(a1.x, a1.y, middle_z, xy_right_normal.x, xy_right_normal.y, xy_right_normal.z);
                idx_a[LEFT ] = idx_last ++;
                volume.push_geometry(a2.x, a2.y, middle_z, -xy_right_normal.x, -xy_right_normal.y, -xy_right_normal.z);
            }
            if (v_dot > 0.9) {
                // The two successive segments are nearly collinear.
                idx_a[LEFT ] = idx_prev[LEFT];
                idx_a[RIGHT] = idx_prev[RIGHT];
            } else if (! sharp) {
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
            } else if (cross(v_prev, v) > 0.) {
                // Right turn. Fill in the right turn wedge.
                volume.push_triangle(idx_prev[RIGHT], idx_a   [RIGHT],  idx_prev[TOP]   );
                volume.push_triangle(idx_prev[RIGHT], idx_prev[BOTTOM], idx_a   [RIGHT] );
            } else {
                // Left turn. Fill in the left turn wedge.
                volume.push_triangle(idx_prev[LEFT],  idx_prev[TOP],    idx_a   [LEFT]  );
                volume.push_triangle(idx_prev[LEFT],  idx_a   [LEFT],   idx_prev[BOTTOM]);
            }
            if (ii == lines.size()) {
                if (! sharp) {
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
                // This is the last iteration, only required to solve the transition.
                break;
            }
        }

        // Only new allocate top / bottom vertices, if not closing a loop.
        if (closed && ii + 1 == lines.size()) {
            idx_b[TOP] = idx_initial[TOP];
        } else {
            idx_b[TOP] = idx_last ++;
            volume.push_geometry(b.x, b.y, top_z   , 0., 0.,  1.);
        }
        if (closed && ii + 1 == lines.size() && width == width_initial) {
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

        prev_line = line;
        memcpy(idx_prev, idx_b, 4 * sizeof(int));
        bottom_z_prev = bottom_z;
        b1_prev = b1;
        b2_prev = b2;
        v_prev  = v;

        if (! closed) {
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

static void thick_lines_to_verts(
    const Lines                 &lines, 
    const std::vector<double>   &widths,
    const std::vector<double>   &heights, 
    bool                         closed,
    double                       top_z,
    GLVolume                    &volume)
{
    thick_lines_to_indexed_vertex_array(lines, widths, heights, closed, top_z, volume.indexed_vertex_array);
}

// Fill in the qverts and tverts with quads and triangles for the extrusion_path.
static inline void extrusionentity_to_verts(const ExtrusionPath &extrusion_path, float print_z, const Point &copy, GLVolume &volume)
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
static inline void extrusionentity_to_verts(const ExtrusionLoop &extrusion_loop, float print_z, const Point &copy, GLVolume &volume)
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
static inline void extrusionentity_to_verts(const ExtrusionMultiPath &extrusion_multi_path, float print_z, const Point &copy, GLVolume &volume)
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

static void extrusionentity_to_verts(const ExtrusionEntity *extrusion_entity, float print_z, const Point &copy, GLVolume &volume);

static inline void extrusionentity_to_verts(const ExtrusionEntityCollection &extrusion_entity_collection, float print_z, const Point &copy, GLVolume &volume)
{
    for (const ExtrusionEntity *extrusion_entity : extrusion_entity_collection.entities)
        extrusionentity_to_verts(extrusion_entity, print_z, copy, volume);
}

static void extrusionentity_to_verts(const ExtrusionEntity *extrusion_entity, float print_z, const Point &copy, GLVolume &volume)
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

void _3DScene::_glew_init()
{ 
    glewInit();
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

// Create 3D thick extrusion lines for a skirt and brim.
// Adds a new Slic3r::GUI::3DScene::Volume to volumes.
void _3DScene::_load_print_toolpaths(
    const Print                     *print, 
    GLVolumeCollection              *volumes,
    const std::vector<std::string>  &tool_colors,
    bool                             use_VBOs)
{
    if (! print->has_skirt() && print->config.brim_width.value == 0)
        return;
    
    const float color[] = { 0.5f, 1.0f, 0.5f, 1.f }; // greenish

    // number of skirt layers
    size_t total_layer_count = 0;
    for (const PrintObject *print_object : print->objects)
        total_layer_count = std::max(total_layer_count, print_object->total_layer_count());
    size_t skirt_height = print->has_infinite_skirt() ? 
        total_layer_count :
        std::min<size_t>(print->config.skirt_height.value, total_layer_count);
    if (skirt_height == 0 && print->config.brim_width.value > 0)
        skirt_height = 1;

    // get first skirt_height layers (maybe this should be moved to a PrintObject method?)
    const PrintObject *object0 = print->objects.front();
    std::vector<float> print_zs;
    print_zs.reserve(skirt_height * 2);
    for (size_t i = 0; i < std::min(skirt_height, object0->layers.size()); ++ i)
        print_zs.push_back(float(object0->layers[i]->print_z));
    //FIXME why there are support layers?
    for (size_t i = 0; i < std::min(skirt_height, object0->support_layers.size()); ++ i)
        print_zs.push_back(float(object0->support_layers[i]->print_z));
    sort_remove_duplicates(print_zs);
    if (print_zs.size() > skirt_height)
        print_zs.erase(print_zs.begin() + skirt_height, print_zs.end());
    
    volumes->volumes.emplace_back(new GLVolume(color));
    GLVolume &volume = *volumes->volumes.back();
    for (size_t i = 0; i < skirt_height; ++ i) {
        volume.print_zs.push_back(print_zs[i]);
        volume.offsets.push_back(volume.indexed_vertex_array.quad_indices.size());
        volume.offsets.push_back(volume.indexed_vertex_array.triangle_indices.size());
        if (i == 0)
            extrusionentity_to_verts(print->brim, print_zs[i], Point(0, 0), volume);
        extrusionentity_to_verts(print->skirt, print_zs[i], Point(0, 0), volume);
    }
    volume.bounding_box = volume.indexed_vertex_array.bounding_box();
    volume.indexed_vertex_array.finalize_geometry(use_VBOs);
}

// Create 3D thick extrusion lines for object forming extrusions.
// Adds a new Slic3r::GUI::3DScene::Volume to $self->volumes,
// one for perimeters, one for infill and one for supports.
void _3DScene::_load_print_object_toolpaths(
    const PrintObject              *print_object,
    GLVolumeCollection             *volumes,
    const std::vector<std::string> &tool_colors_str,
    bool                            use_VBOs)
{
    std::vector<float> tool_colors = parse_colors(tool_colors_str);

    struct Ctxt
    {
        const Points                *shifted_copies;
        std::vector<const Layer*>    layers;
        bool                         has_perimeters;
        bool                         has_infill;
        bool                         has_support;
        const std::vector<float>*    tool_colors;

        // Number of vertices (each vertex is 6x4=24 bytes long)
        static const size_t          alloc_size_max    () { return 131072; } // 3.15MB
//        static const size_t          alloc_size_max    () { return 65536; } // 1.57MB 
//        static const size_t          alloc_size_max    () { return 32768; } // 786kB
        static const size_t          alloc_size_reserve() { return alloc_size_max() * 2; }

        static const float*          color_perimeters  () { static float color[4] = { 1.0f, 1.0f, 0.0f, 1.f }; return color; } // yellow
        static const float*          color_infill      () { static float color[4] = { 1.0f, 0.5f, 0.5f, 1.f }; return color; } // redish
        static const float*          color_support     () { static float color[4] = { 0.5f, 1.0f, 0.5f, 1.f }; return color; } // greenish

        // For cloring by a tool, return a parsed color.
        bool                         color_by_tool() const { return tool_colors != nullptr; }
        size_t                       number_tools()  const { return this->color_by_tool() ? tool_colors->size() / 4 : 0; }
        const float*                 color_tool(size_t tool) const { return tool_colors->data() + tool * 4; }
        int                          volume_idx(int extruder, int feature) const 
            { return this->color_by_tool() ? std::min<int>(this->number_tools() - 1, std::max<int>(extruder - 1, 0)) : feature; }
    } ctxt;

    ctxt.shifted_copies = &print_object->_shifted_copies;

    // order layers by print_z
    ctxt.layers.reserve(print_object->layers.size() + print_object->support_layers.size());
    for (const Layer *layer : print_object->layers)
        ctxt.layers.push_back(layer);
    for (const Layer *layer : print_object->support_layers)
        ctxt.layers.push_back(layer);
    std::sort(ctxt.layers.begin(), ctxt.layers.end(), [](const Layer *l1, const Layer *l2) { return l1->print_z < l2->print_z; });

    // Maximum size of an allocation block: 32MB / sizeof(float)
    ctxt.has_perimeters = print_object->state.is_done(posPerimeters);
    ctxt.has_infill     = print_object->state.is_done(posInfill);
    ctxt.has_support    = print_object->state.is_done(posSupportMaterial);
    ctxt.tool_colors    = tool_colors.empty() ? nullptr : &tool_colors;
    
    BOOST_LOG_TRIVIAL(debug) << "Loading print object toolpaths in parallel - start";

    //FIXME Improve the heuristics for a grain size.
    size_t          grain_size = std::max(ctxt.layers.size() / 16, size_t(1));
    tbb::spin_mutex new_volume_mutex;
    auto            new_volume = [volumes, &new_volume_mutex](const float *color) -> GLVolume* {
        auto *volume = new GLVolume(color);
        new_volume_mutex.lock();
        volumes->volumes.emplace_back(volume);
        new_volume_mutex.unlock();
        return volume;
    };
    const size_t   volumes_cnt_initial = volumes->volumes.size();
    std::vector<GLVolumeCollection> volumes_per_thread(ctxt.layers.size());
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, ctxt.layers.size(), grain_size),
        [&ctxt, &new_volume](const tbb::blocked_range<size_t>& range) {
            std::vector<GLVolume*> vols;
            if (ctxt.color_by_tool()) {
                for (size_t i = 0; i < ctxt.number_tools(); ++ i)
                    vols.emplace_back(new_volume(ctxt.color_tool(i)));
            } else
                vols = { new_volume(ctxt.color_perimeters()), new_volume(ctxt.color_infill()), new_volume(ctxt.color_support()) };
            for (GLVolume *vol : vols)
                vol->indexed_vertex_array.reserve(ctxt.alloc_size_reserve());
            for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
                const Layer *layer = ctxt.layers[idx_layer];
                for (size_t i = 0; i < vols.size(); ++ i) {
                    GLVolume &vol = *vols[i];
                    if (vol.print_zs.empty() || vol.print_zs.back() != layer->print_z) {
                        vol.print_zs.push_back(layer->print_z);
                        vol.offsets.push_back(vol.indexed_vertex_array.quad_indices.size());
                        vol.offsets.push_back(vol.indexed_vertex_array.triangle_indices.size());
                    }
                }
                for (const Point &copy: *ctxt.shifted_copies) {
                    for (const LayerRegion *layerm : layer->regions) {
                        if (ctxt.has_perimeters)
                            extrusionentity_to_verts(layerm->perimeters, float(layer->print_z), copy, 
                                *vols[ctxt.volume_idx(layerm->region()->config.perimeter_extruder.value, 0)]);
                        if (ctxt.has_infill) {
                            for (const ExtrusionEntity *ee : layerm->fills.entities) {
                                // fill represents infill extrusions of a single island.
                                const auto *fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                                if (! fill->entities.empty())
                                    extrusionentity_to_verts(*fill, float(layer->print_z), copy, 
                                        *vols[ctxt.volume_idx(
                                            is_solid_infill(fill->entities.front()->role()) ? 
                                                layerm->region()->config.solid_infill_extruder : 
                                                layerm->region()->config.infill_extruder,
                                        1)]);
                            }
                        }
                    }
                    if (ctxt.has_support) {
                        const SupportLayer *support_layer = dynamic_cast<const SupportLayer*>(layer);
                        if (support_layer) {
                            for (const ExtrusionEntity *extrusion_entity : support_layer->support_fills.entities)
                                extrusionentity_to_verts(extrusion_entity, float(layer->print_z), copy, 
                                    *vols[ctxt.volume_idx(
                                            (extrusion_entity->role() == erSupportMaterial) ? 
                                                support_layer->object()->config.support_material_extruder : 
                                                support_layer->object()->config.support_material_interface_extruder,
                                            2)]);
                        }
                    }
                }
                for (size_t i = 0; i < vols.size(); ++ i) {
                    GLVolume &vol = *vols[i];
                    if (vol.indexed_vertex_array.vertices_and_normals_interleaved.size() / 6 > ctxt.alloc_size_max()) {
                        // Store the vertex arrays and restart their containers, 
                        vols[i] = new_volume(vol.color);
                        GLVolume &vol_new = *vols[i];
                        // Assign the large pre-allocated buffers to the new GLVolume.
                        vol_new.indexed_vertex_array = std::move(vol.indexed_vertex_array);
                        // Copy the content back to the old GLVolume.
                        vol.indexed_vertex_array = vol_new.indexed_vertex_array;
                        // Finalize a bounding box of the old GLVolume.
                        vol.bounding_box = vol.indexed_vertex_array.bounding_box();
                        // Clear the buffers, but keep them pre-allocated.
                        vol_new.indexed_vertex_array.clear();
                        // Just make sure that clear did not clear the reserved memory.
                        vol_new.indexed_vertex_array.reserve(ctxt.alloc_size_reserve());
                    }
                }
            }
            for (GLVolume *vol : vols) {
                vol->bounding_box = vol->indexed_vertex_array.bounding_box();
                vol->indexed_vertex_array.shrink_to_fit();
            }
        });

    BOOST_LOG_TRIVIAL(debug) << "Loading print object toolpaths in parallel - finalizing results";
    // Remove empty volumes from the newly added volumes.
    volumes->volumes.erase(
        std::remove_if(volumes->volumes.begin() + volumes_cnt_initial, volumes->volumes.end(), 
            [](const GLVolume *volume) { return volume->empty(); }),
        volumes->volumes.end());
    for (size_t i = volumes_cnt_initial; i < volumes->volumes.size(); ++ i)
        volumes->volumes[i]->indexed_vertex_array.finalize_geometry(use_VBOs);
  
    BOOST_LOG_TRIVIAL(debug) << "Loading print object toolpaths in parallel - end"; 
}

void _3DScene::_load_wipe_tower_toolpaths(
    const Print                    *print,
    GLVolumeCollection             *volumes,
    const std::vector<std::string> &tool_colors_str,
    bool                            use_VBOs)
{
    if (print->m_wipe_tower_tool_changes.empty())
        return;

    std::vector<float> tool_colors = parse_colors(tool_colors_str);

    struct Ctxt
    {
        const Print                 *print;
        const std::vector<float>    *tool_colors;

        // Number of vertices (each vertex is 6x4=24 bytes long)
        static const size_t          alloc_size_max    () { return 131072; } // 3.15MB
        static const size_t          alloc_size_reserve() { return alloc_size_max() * 2; }

        static const float*          color_support     () { static float color[4] = { 0.5f, 1.0f, 0.5f, 1.f }; return color; } // greenish

        // For cloring by a tool, return a parsed color.
        bool                         color_by_tool() const { return tool_colors != nullptr; }
        size_t                       number_tools()  const { return this->color_by_tool() ? tool_colors->size() / 4 : 0; }
        const float*                 color_tool(size_t tool) const { return tool_colors->data() + tool * 4; }
        int                          volume_idx(int tool, int feature) const 
            { return this->color_by_tool() ? std::min<int>(this->number_tools() - 1, std::max<int>(tool, 0)) : feature; }

        const std::vector<WipeTower::ToolChangeResult>& tool_change(size_t idx) { 
            return priming.empty() ? 
                ((idx == print->m_wipe_tower_tool_changes.size()) ? final : print->m_wipe_tower_tool_changes[idx]) :
                ((idx == 0) ? priming : (idx == print->m_wipe_tower_tool_changes.size() + 1) ? final : print->m_wipe_tower_tool_changes[idx - 1]);
        }
        std::vector<WipeTower::ToolChangeResult> priming;
        std::vector<WipeTower::ToolChangeResult> final;
    } ctxt;

    ctxt.print          = print;
    ctxt.tool_colors    = tool_colors.empty() ? nullptr : &tool_colors;
	if (print->m_wipe_tower_priming)
		ctxt.priming.emplace_back(*print->m_wipe_tower_priming.get());
	if (print->m_wipe_tower_final_purge)
		ctxt.final.emplace_back(*print->m_wipe_tower_final_purge.get());
    
    BOOST_LOG_TRIVIAL(debug) << "Loading wipe tower toolpaths in parallel - start";

    //FIXME Improve the heuristics for a grain size.
    size_t          n_items    = print->m_wipe_tower_tool_changes.size() + (ctxt.priming.empty() ? 0 : 1);
    size_t          grain_size = std::max(n_items / 128, size_t(1));
    tbb::spin_mutex new_volume_mutex;
    auto            new_volume = [volumes, &new_volume_mutex](const float *color) -> GLVolume* {
        auto *volume = new GLVolume(color);
        new_volume_mutex.lock();
        volumes->volumes.emplace_back(volume);
        new_volume_mutex.unlock();
        return volume;
    };
    const size_t   volumes_cnt_initial = volumes->volumes.size();
    std::vector<GLVolumeCollection> volumes_per_thread(n_items);
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, n_items, grain_size),
        [&ctxt, &new_volume](const tbb::blocked_range<size_t>& range) {
            // Bounding box of this slab of a wipe tower.
            std::vector<GLVolume*> vols;
            if (ctxt.color_by_tool()) {
                for (size_t i = 0; i < ctxt.number_tools(); ++ i)
                    vols.emplace_back(new_volume(ctxt.color_tool(i)));
            } else
                vols = { new_volume(ctxt.color_support()) };
            for (GLVolume *volume : vols)
                volume->indexed_vertex_array.reserve(ctxt.alloc_size_reserve());
            for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
                const std::vector<WipeTower::ToolChangeResult> &layer = ctxt.tool_change(idx_layer);
                for (size_t i = 0; i < vols.size(); ++ i) {
                    GLVolume &vol = *vols[i];
                    if (vol.print_zs.empty() || vol.print_zs.back() != layer.front().print_z) {
                        vol.print_zs.push_back(layer.front().print_z);
                        vol.offsets.push_back(vol.indexed_vertex_array.quad_indices.size());
                        vol.offsets.push_back(vol.indexed_vertex_array.triangle_indices.size());
                    }
                }
                for (const WipeTower::ToolChangeResult &extrusions : layer) {
                    for (size_t i = 1; i < extrusions.extrusions.size();) {
                        const WipeTower::Extrusion &e = extrusions.extrusions[i];
                        if (e.width == 0.) {
                            ++ i;
                            continue;
                        }
                        size_t j = i + 1;
                        if (ctxt.color_by_tool())
                            for (; j < extrusions.extrusions.size() && extrusions.extrusions[j].tool == e.tool && extrusions.extrusions[j].width > 0.f; ++ j) ;
                        else
                            for (; j < extrusions.extrusions.size() && extrusions.extrusions[j].width > 0.f; ++ j) ;
                        size_t              n_lines = j - i;
                        Lines               lines;
                        std::vector<double> widths;
                        std::vector<double> heights;
                        lines.reserve(n_lines);
                        widths.reserve(n_lines);
                        heights.assign(n_lines, extrusions.layer_height);
                        for (; i < j; ++ i) {
                            const WipeTower::Extrusion &e = extrusions.extrusions[i];
                            assert(e.width > 0.f);
                            const WipeTower::Extrusion &e_prev = *(&e - 1);
                            lines.emplace_back(Point::new_scale(e_prev.pos.x, e_prev.pos.y), Point::new_scale(e.pos.x, e.pos.y));
                            widths.emplace_back(e.width);
                        }
                        thick_lines_to_verts(lines, widths, heights, lines.front().a == lines.back().b, extrusions.print_z, 
                            *vols[ctxt.volume_idx(e.tool, 0)]);
                    }
                }
            }
            for (size_t i = 0; i < vols.size(); ++ i) {
                GLVolume &vol = *vols[i];
                if (vol.indexed_vertex_array.vertices_and_normals_interleaved.size() / 6 > ctxt.alloc_size_max()) {
                    // Store the vertex arrays and restart their containers, 
                    vols[i] = new_volume(vol.color);
                    GLVolume &vol_new = *vols[i];
                    // Assign the large pre-allocated buffers to the new GLVolume.
                    vol_new.indexed_vertex_array = std::move(vol.indexed_vertex_array);
                    // Copy the content back to the old GLVolume.
                    vol.indexed_vertex_array = vol_new.indexed_vertex_array;
                    // Finalize a bounding box of the old GLVolume.
                    vol.bounding_box = vol.indexed_vertex_array.bounding_box();
                    // Clear the buffers, but keep them pre-allocated.
                    vol_new.indexed_vertex_array.clear();
                    // Just make sure that clear did not clear the reserved memory.
                    vol_new.indexed_vertex_array.reserve(ctxt.alloc_size_reserve());
                }
            }
            for (GLVolume *vol : vols) {
                vol->bounding_box = vol->indexed_vertex_array.bounding_box();
                vol->indexed_vertex_array.shrink_to_fit();
            }
        });

    BOOST_LOG_TRIVIAL(debug) << "Loading wipe tower toolpaths in parallel - finalizing results";
    // Remove empty volumes from the newly added volumes.
    volumes->volumes.erase(
        std::remove_if(volumes->volumes.begin() + volumes_cnt_initial, volumes->volumes.end(), 
            [](const GLVolume *volume) { return volume->empty(); }),
        volumes->volumes.end());
    for (size_t i = volumes_cnt_initial; i < volumes->volumes.size(); ++ i)
        volumes->volumes[i]->indexed_vertex_array.finalize_geometry(use_VBOs);
  
    BOOST_LOG_TRIVIAL(debug) << "Loading wipe tower toolpaths in parallel - end"; 
}

}
