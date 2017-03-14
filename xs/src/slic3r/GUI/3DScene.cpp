#include "3DScene.hpp"

#include "../../libslic3r/libslic3r.h"
#include "../../libslic3r/ExtrusionEntity.hpp"
#include "../../libslic3r/ExtrusionEntityCollection.hpp"
#include "../../libslic3r/Print.hpp"
#include "../../libslic3r/Slicing.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <utility>

#include <boost/log/trivial.hpp>

#include <tbb/parallel_for.h>
#include <tbb/atomic.h>

namespace Slic3r {

void GLVertexArray::load_mesh(const TriangleMesh &mesh)
{
    this->reserve_more(3 * 3 * mesh.facets_count());
    
    for (int i = 0; i < mesh.stl.stats.number_of_facets; ++ i) {
        stl_facet &facet = mesh.stl.facet_start[i];
        for (int j = 0; j < 3; ++ j) {
            this->push_norm(facet.normal.x, facet.normal.y, facet.normal.z);
            this->push_vert(facet.vertex[j].x, facet.vertex[j].y, facet.vertex[j].z);
        }
    }
}

void GLVolume::set_range(double min_z, double max_z)
{
    this->qverts_range.first  = 0;
    this->qverts_range.second = this->qverts.size();
    this->tverts_range.first  = 0;
    this->tverts_range.second = this->tverts.size();
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
    const std::string       &drag_by)
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
			v.tverts.load_mesh(mesh);
            v.bounding_box = v.tverts.bounding_box();
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

// caller is responsible for supplying NO lines with zero length
static void thick_lines_to_verts(
    const Lines                 &lines, 
    const std::vector<double>   &widths,
    const std::vector<double>   &heights, 
    bool                         closed,
    double                       top_z,
    GLVolume                    &volume)
{
    /* It looks like it's faster without reserving capacity...
    // each segment has 4 quads, thus 16 vertices; + 2 caps
    volume.qverts.reserve_more(3 * 4 * (4 * lines.size() + 2));
    
    // two triangles for each corner 
    volume.tverts.reserve_more(3 * 3 * 2 * (lines.size() + 1));
    */

    assert(! lines.empty());
    if (lines.empty())
        return;
    
    Line prev_line;
    Pointf prev_b1, prev_b2;
    Vectorf3 prev_xy_left_normal, prev_xy_right_normal;
    
    // loop once more in case of closed loops
    for (size_t ii = 0; ii <= lines.size(); ++ ii) {
        size_t i = ii; 
        if (ii == lines.size()) {
            if (! closed)
                break;
            i = 0;
        }
        
        const Line &line = lines[i];
        double len = line.length();
        double unscaled_len = unscale(len);
        double bottom_z = top_z - heights[i];
        double middle_z = (top_z + bottom_z) / 2;
        double dist = widths.at(i)/2;  // scaled
        
        Vectorf v = Vectorf::new_unscale(line.vector());
        v.scale(1. / unscaled_len);
        
        Pointf a = Pointf::new_unscale(line.a);
        Pointf b = Pointf::new_unscale(line.b);
        Pointf a1 = a;
        Pointf a2 = a;
        a1.translate(+dist*v.y, -dist*v.x);
        a2.translate(-dist*v.y, +dist*v.x);
        Pointf b1 = b;
        Pointf b2 = b;
        b1.translate(+dist*v.y, -dist*v.x);
        b2.translate(-dist*v.y, +dist*v.x);
        
        // calculate new XY normals
        Vector n = line.normal();
        Vectorf3 xy_right_normal = Vectorf3::new_unscale(n.x, n.y, 0);
        xy_right_normal.scale(1.f / unscaled_len);
        Vectorf3 xy_left_normal = xy_right_normal;
        xy_left_normal.scale(-1.f);
        
        if (ii > 0) {
            // if we're making a ccw turn, draw the triangles on the right side, otherwise draw them on the left side
            double ccw = line.b.ccw(prev_line);
            if (ccw > EPSILON) {
                // top-right vertex triangle between previous line and this one
                {
                    // use the normal going to the right calculated for the previous line
                    volume.tverts.push_norm(prev_xy_right_normal);
                    volume.tverts.push_vert(prev_b1.x, prev_b1.y, middle_z);
            
                    // use the normal going to the right calculated for this line
                    volume.tverts.push_norm(xy_right_normal);
                    volume.tverts.push_vert(a1.x, a1.y, middle_z);
            
                    // normal going upwards
                    volume.tverts.push_norm(0.f, 0.f, 1.f);
                    volume.tverts.push_vert(a.x, a.y, top_z);
                }
                // bottom-right vertex triangle between previous line and this one
                {
                    // use the normal going to the right calculated for the previous line
                    volume.tverts.push_norm(prev_xy_right_normal);
                    volume.tverts.push_vert(prev_b1.x, prev_b1.y, middle_z);
            
                    // normal going downwards
                    volume.tverts.push_norm(0.f, 0.f, -1.f);
                    volume.tverts.push_vert(a.x, a.y, bottom_z);
            
                    // use the normal going to the right calculated for this line
                    volume.tverts.push_norm(xy_right_normal);
                    volume.tverts.push_vert(a1.x, a1.y, middle_z);
                }
            } else if (ccw < -EPSILON) {
                // top-left vertex triangle between previous line and this one
                {
                    // use the normal going to the left calculated for the previous line
                    volume.tverts.push_norm(prev_xy_left_normal);
                    volume.tverts.push_vert(prev_b2.x, prev_b2.y, middle_z);
            
                    // normal going upwards
                    volume.tverts.push_norm(0.f, 0.f, 1.f);
                    volume.tverts.push_vert(a.x, a.y, top_z);
            
                    // use the normal going to the right calculated for this line
                    volume.tverts.push_norm(xy_left_normal);
                    volume.tverts.push_vert(a2.x, a2.y, middle_z);
                }
                // bottom-left vertex triangle between previous line and this one
                {
                    // use the normal going to the left calculated for the previous line
                    volume.tverts.push_norm(prev_xy_left_normal);
                    volume.tverts.push_vert(prev_b2.x, prev_b2.y, middle_z);
            
                    // use the normal going to the right calculated for this line
                    volume.tverts.push_norm(xy_left_normal);
                    volume.tverts.push_vert(a2.x, a2.y, middle_z);
            
                    // normal going downwards
                    volume.tverts.push_norm(0.f, 0.f, -1.f);
                    volume.tverts.push_vert(a.x, a.y, bottom_z);
                }
            }
        }
        
        // if this was the extra iteration we were only interested in the triangles
        if (ii == lines.size())
            break;
        
        prev_line = line;
        prev_b1 = b1;
        prev_b2 = b2;
        prev_xy_right_normal = xy_right_normal;
        prev_xy_left_normal  = xy_left_normal;
        
        if (! closed) {
            // terminate open paths with caps
            if (i == 0) {
                // normal pointing downwards
                volume.qverts.push_norm(0,0,-1);
                volume.qverts.push_vert(a.x, a.y, bottom_z);
            
                // normal pointing to the right
                volume.qverts.push_norm(xy_right_normal);
                volume.qverts.push_vert(a1.x, a1.y, middle_z);
            
                // normal pointing upwards
                volume.qverts.push_norm(0,0,1);
                volume.qverts.push_vert(a.x, a.y, top_z);
            
                // normal pointing to the left
                volume.qverts.push_norm(xy_left_normal);
                volume.qverts.push_vert(a2.x, a2.y, middle_z);
            }
            // we don't use 'else' because both cases are true if we have only one line
            if (i + 1 == lines.size()) {
                // normal pointing downwards
                volume.qverts.push_norm(0,0,-1);
                volume.qverts.push_vert(b.x, b.y, bottom_z);
            
                // normal pointing to the left
                volume.qverts.push_norm(xy_left_normal);
                volume.qverts.push_vert(b2.x, b2.y, middle_z);
            
                // normal pointing upwards
                volume.qverts.push_norm(0,0,1);
                volume.qverts.push_vert(b.x, b.y, top_z);
            
                // normal pointing to the right
                volume.qverts.push_norm(xy_right_normal);
                volume.qverts.push_vert(b1.x, b1.y, middle_z);
            }
        }
        
        // bottom-right face
        {
            // normal going downwards
            volume.qverts.push_norm(0,0,-1);
            volume.qverts.push_norm(0,0,-1);
            volume.qverts.push_vert(a.x, a.y, bottom_z);
            volume.qverts.push_vert(b.x, b.y, bottom_z);
            
            volume.qverts.push_norm(xy_right_normal);
            volume.qverts.push_norm(xy_right_normal);
            volume.qverts.push_vert(b1.x, b1.y, middle_z);
            volume.qverts.push_vert(a1.x, a1.y, middle_z);
        }
        
        // top-right face
        {
            volume.qverts.push_norm(xy_right_normal);
            volume.qverts.push_norm(xy_right_normal);
            volume.qverts.push_vert(a1.x, a1.y, middle_z);
            volume.qverts.push_vert(b1.x, b1.y, middle_z);
            
            // normal going upwards
            volume.qverts.push_norm(0,0,1);
            volume.qverts.push_norm(0,0,1);
            volume.qverts.push_vert(b.x, b.y, top_z);
            volume.qverts.push_vert(a.x, a.y, top_z);
        }
         
        // top-left face
        {
            volume.qverts.push_norm(0,0,1);
            volume.qverts.push_norm(0,0,1);
            volume.qverts.push_vert(a.x, a.y, top_z);
            volume.qverts.push_vert(b.x, b.y, top_z);
            
            volume.qverts.push_norm(xy_left_normal);
            volume.qverts.push_norm(xy_left_normal);
            volume.qverts.push_vert(b2.x, b2.y, middle_z);
            volume.qverts.push_vert(a2.x, a2.y, middle_z);
        }
        
        // bottom-left face
        {
            volume.qverts.push_norm(xy_left_normal);
            volume.qverts.push_norm(xy_left_normal);
            volume.qverts.push_vert(a2.x, a2.y, middle_z);
            volume.qverts.push_vert(b2.x, b2.y, middle_z);
            
            // normal going downwards
            volume.qverts.push_norm(0,0,-1);
            volume.qverts.push_norm(0,0,-1);
            volume.qverts.push_vert(b.x, b.y, bottom_z);
            volume.qverts.push_vert(a.x, a.y, bottom_z);
        }
    }
}

// caller is responsible for supplying NO lines with zero length
static void thick_lines_to_VBOs(
    const Lines                 &lines, 
    const std::vector<double>   &widths,
    const std::vector<double>   &heights, 
    bool                         closed,
    double                       top_z,
    GLVolume                    &volume)
{
    assert(! lines.empty());
    if (lines.empty())
        return;
    
    Line prev_line;
    Pointf prev_b1, prev_b2;
    Vectorf3 prev_xy_left_normal, prev_xy_right_normal;
    
    // loop once more in case of closed loops
    for (size_t ii = 0; ii <= lines.size(); ++ ii) {
        size_t i = ii;
        if (ii == lines.size()) {
            if (! closed)
                break;
            i = 0;
        }
        
        const Line &line = lines[i];
        double len = line.length();
        double unscaled_len = unscale(len);
        double bottom_z = top_z - heights[i];
        double middle_z = (top_z + bottom_z) / 2;
        double dist = widths.at(i)/2;  // scaled
        
        Vectorf v = Vectorf::new_unscale(line.vector());
        v.scale(1. / unscaled_len);
        
        Pointf a = Pointf::new_unscale(line.a);
        Pointf b = Pointf::new_unscale(line.b);
        Pointf a1 = a;
        Pointf a2 = a;
        a1.translate(+dist*v.y, -dist*v.x);
        a2.translate(-dist*v.y, +dist*v.x);
        Pointf b1 = b;
        Pointf b2 = b;
        b1.translate(+dist*v.y, -dist*v.x);
        b2.translate(-dist*v.y, +dist*v.x);
        
        // calculate new XY normals
        Vector n = line.normal();
        Vectorf3 xy_right_normal = Vectorf3::new_unscale(n.x, n.y, 0);
        xy_right_normal.scale(1.f / unscaled_len);
        Vectorf3 xy_left_normal = xy_right_normal;
        xy_left_normal.scale(-1.f);
        
        if (ii > 0) {
            // if we're making a ccw turn, draw the triangles on the right side, otherwise draw them on the left side
            double ccw = line.b.ccw(prev_line);
            if (ccw > EPSILON) {
                // top-right vertex triangle between previous line and this one
                {
                    // use the normal going to the right calculated for the previous line
                    volume.tverts.push_norm(prev_xy_right_normal);
                    volume.tverts.push_vert(prev_b1.x, prev_b1.y, middle_z);
            
                    // use the normal going to the right calculated for this line
                    volume.tverts.push_norm(xy_right_normal);
                    volume.tverts.push_vert(a1.x, a1.y, middle_z);
            
                    // normal going upwards
                    volume.tverts.push_norm(0.f, 0.f, 1.f);
                    volume.tverts.push_vert(a.x, a.y, top_z);
                }
                // bottom-right vertex triangle between previous line and this one
                {
                    // use the normal going to the right calculated for the previous line
                    volume.tverts.push_norm(prev_xy_right_normal);
                    volume.tverts.push_vert(prev_b1.x, prev_b1.y, middle_z);
            
                    // normal going downwards
                    volume.tverts.push_norm(0.f, 0.f, -1.f);
                    volume.tverts.push_vert(a.x, a.y, bottom_z);
            
                    // use the normal going to the right calculated for this line
                    volume.tverts.push_norm(xy_right_normal);
                    volume.tverts.push_vert(a1.x, a1.y, middle_z);
                }
            } else if (ccw < -EPSILON) {
                // top-left vertex triangle between previous line and this one
                {
                    // use the normal going to the left calculated for the previous line
                    volume.tverts.push_norm(prev_xy_left_normal);
                    volume.tverts.push_vert(prev_b2.x, prev_b2.y, middle_z);
            
                    // normal going upwards
                    volume.tverts.push_norm(0.f, 0.f, 1.f);
                    volume.tverts.push_vert(a.x, a.y, top_z);
            
                    // use the normal going to the right calculated for this line
                    volume.tverts.push_norm(xy_left_normal);
                    volume.tverts.push_vert(a2.x, a2.y, middle_z);
                }
                // bottom-left vertex triangle between previous line and this one
                {
                    // use the normal going to the left calculated for the previous line
                    volume.tverts.push_norm(prev_xy_left_normal);
                    volume.tverts.push_vert(prev_b2.x, prev_b2.y, middle_z);
            
                    // use the normal going to the right calculated for this line
                    volume.tverts.push_norm(xy_left_normal);
                    volume.tverts.push_vert(a2.x, a2.y, middle_z);
            
                    // normal going downwards
                    volume.tverts.push_norm(0.f, 0.f, -1.f);
                    volume.tverts.push_vert(a.x, a.y, bottom_z);
                }
            }
        }
        
        // if this was the extra iteration we were only interested in the triangles
        if (ii == lines.size())
            break;
        
        prev_line = line;
        prev_b1 = b1;
        prev_b2 = b2;
        prev_xy_right_normal = xy_right_normal;
        prev_xy_left_normal  = xy_left_normal;
        
        if (! closed) {
            // terminate open paths with caps
            if (i == 0) {
                // normal pointing downwards
                volume.qverts.push_norm(0,0,-1);
                volume.qverts.push_vert(a.x, a.y, bottom_z);
            
                // normal pointing to the right
                volume.qverts.push_norm(xy_right_normal);
                volume.qverts.push_vert(a1.x, a1.y, middle_z);
            
                // normal pointing upwards
                volume.qverts.push_norm(0,0,1);
                volume.qverts.push_vert(a.x, a.y, top_z);
            
                // normal pointing to the left
                volume.qverts.push_norm(xy_left_normal);
                volume.qverts.push_vert(a2.x, a2.y, middle_z);
            }
            // we don't use 'else' because both cases are true if we have only one line
            if (i + 1 == lines.size()) {
                // normal pointing downwards
                volume.qverts.push_norm(0,0,-1);
                volume.qverts.push_vert(b.x, b.y, bottom_z);
            
                // normal pointing to the left
                volume.qverts.push_norm(xy_left_normal);
                volume.qverts.push_vert(b2.x, b2.y, middle_z);
            
                // normal pointing upwards
                volume.qverts.push_norm(0,0,1);
                volume.qverts.push_vert(b.x, b.y, top_z);
            
                // normal pointing to the right
                volume.qverts.push_norm(xy_right_normal);
                volume.qverts.push_vert(b1.x, b1.y, middle_z);
            }
        }
        
        // bottom-right face
        {
            // normal going downwards
            volume.qverts.push_norm(0,0,-1);
            volume.qverts.push_norm(0,0,-1);
            volume.qverts.push_vert(a.x, a.y, bottom_z);
            volume.qverts.push_vert(b.x, b.y, bottom_z);
            
            volume.qverts.push_norm(xy_right_normal);
            volume.qverts.push_norm(xy_right_normal);
            volume.qverts.push_vert(b1.x, b1.y, middle_z);
            volume.qverts.push_vert(a1.x, a1.y, middle_z);
        }
        
        // top-right face
        {
            volume.qverts.push_norm(xy_right_normal);
            volume.qverts.push_norm(xy_right_normal);
            volume.qverts.push_vert(a1.x, a1.y, middle_z);
            volume.qverts.push_vert(b1.x, b1.y, middle_z);
            
            // normal going upwards
            volume.qverts.push_norm(0,0,1);
            volume.qverts.push_norm(0,0,1);
            volume.qverts.push_vert(b.x, b.y, top_z);
            volume.qverts.push_vert(a.x, a.y, top_z);
        }
         
        // top-left face
        {
            volume.qverts.push_norm(0,0,1);
            volume.qverts.push_norm(0,0,1);
            volume.qverts.push_vert(a.x, a.y, top_z);
            volume.qverts.push_vert(b.x, b.y, top_z);
            
            volume.qverts.push_norm(xy_left_normal);
            volume.qverts.push_norm(xy_left_normal);
            volume.qverts.push_vert(b2.x, b2.y, middle_z);
            volume.qverts.push_vert(a2.x, a2.y, middle_z);
        }
        
        // bottom-left face
        {
            volume.qverts.push_norm(xy_left_normal);
            volume.qverts.push_norm(xy_left_normal);
            volume.qverts.push_vert(a2.x, a2.y, middle_z);
            volume.qverts.push_vert(b2.x, b2.y, middle_z);
            
            // normal going downwards
            volume.qverts.push_norm(0,0,-1);
            volume.qverts.push_norm(0,0,-1);
            volume.qverts.push_vert(b.x, b.y, bottom_z);
            volume.qverts.push_vert(a.x, a.y, bottom_z);
        }
    }
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

// Create 3D thick extrusion lines for a skirt and brim.
// Adds a new Slic3r::GUI::3DScene::Volume to volumes.
void _3DScene::_load_print_toolpaths(
    const Print         *print, 
    GLVolumeCollection  *volumes,
    bool                 use_VBOs)
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
    std::sort(print_zs.begin(), print_zs.end());
    print_zs.erase(std::unique(print_zs.begin(), print_zs.end()), print_zs.end());
    if (print_zs.size() > skirt_height)
        print_zs.erase(print_zs.begin() + skirt_height, print_zs.end());
    
    volumes->volumes.emplace_back(new GLVolume(color));
    GLVolume &volume = *volumes->volumes.back();
    for (size_t i = 0; i < skirt_height; ++ i) {
        volume.print_zs.push_back(print_zs[i]);
        volume.offsets.push_back(volume.qverts.size());
        volume.offsets.push_back(volume.tverts.size());
        if (i == 0)
            extrusionentity_to_verts(print->brim, print_zs[i], Point(0, 0), volume);
        extrusionentity_to_verts(print->skirt, print_zs[i], Point(0, 0), volume);
    }
    auto bb = print->bounding_box();
    volume.bounding_box.merge(Pointf3(unscale(bb.min.x), unscale(bb.min.y), 0.f));
    volume.bounding_box.merge(Pointf3(unscale(bb.max.x), unscale(bb.max.y), 0.f));
}

// Create 3D thick extrusion lines for object forming extrusions.
// Adds a new Slic3r::GUI::3DScene::Volume to $self->volumes,
// one for perimeters, one for infill and one for supports.
void _3DScene::_load_print_object_toolpaths(
    const PrintObject   *print_object,
    GLVolumeCollection  *volumes,
    bool                 use_VBOs)
{
    struct Ctxt
    {
        const Points                *shifted_copies;
        std::vector<const Layer*>    layers;
        // Bounding box of the object and its copies.
        BoundingBoxf3                bbox;
        bool                         has_perimeters;
        bool                         has_infill;
        bool                         has_support;

//        static const size_t          alloc_size_max    () { return 32 * 1048576 / 4; }
        static const size_t          alloc_size_max    () { return 4 * 1048576 / 4; }
        static const size_t          alloc_size_reserve() { return alloc_size_max() * 2; }

        static const float*          color_perimeters  () { static float color[4] = { 1.0f, 1.0f, 0.0f, 1.f }; return color; } // yellow
        static const float*          color_infill      () { static float color[4] = { 1.0f, 0.5f, 0.5f, 1.f }; return color; } // redish
        static const float*          color_support     () { static float color[4] = { 0.5f, 1.0f, 0.5f, 1.f }; return color; } // greenish
    } ctxt;

    ctxt.shifted_copies = &print_object->_shifted_copies;

    // order layers by print_z
    ctxt.layers.reserve(print_object->layers.size() + print_object->support_layers.size());
    for (const Layer *layer : print_object->layers)
        ctxt.layers.push_back(layer);
    for (const Layer *layer : print_object->support_layers)
        ctxt.layers.push_back(layer);
    std::sort(ctxt.layers.begin(), ctxt.layers.end(), [](const Layer *l1, const Layer *l2) { return l1->print_z < l2->print_z; });
    
    for (const Point &copy: print_object->_shifted_copies) {
        BoundingBox cbb = print_object->bounding_box();
        cbb.translate(copy.x, copy.y);
        ctxt.bbox.merge(Pointf3(unscale(cbb.min.x), unscale(cbb.min.y), 0.f));
        ctxt.bbox.merge(Pointf3(unscale(cbb.max.x), unscale(cbb.max.y), 0.f));
    }

    // Maximum size of an allocation block: 32MB / sizeof(float)
    ctxt.has_perimeters = print_object->state.is_done(posPerimeters);
    ctxt.has_infill     = print_object->state.is_done(posInfill);
    ctxt.has_support    = print_object->state.is_done(posSupportMaterial);
    
    BOOST_LOG_TRIVIAL(debug) << "Loading print object toolpaths in parallel - start";

    //FIXME Improve the heuristics for a grain size.
    size_t grain_size = std::max(ctxt.layers.size() / 16, size_t(1));
    std::vector<GLVolumeCollection> volumes_per_thread(ctxt.layers.size(), GLVolumeCollection());
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, ctxt.layers.size(), grain_size),
        [&ctxt, &volumes_per_thread](const tbb::blocked_range<size_t>& range) {
            std::vector<GLVolume*> &volumes = volumes_per_thread[range.begin()].volumes;
            volumes.emplace_back(new GLVolume(ctxt.color_perimeters()));
            volumes.emplace_back(new GLVolume(ctxt.color_infill()));
            volumes.emplace_back(new GLVolume(ctxt.color_support()));
            size_t vols[3] = { 0, 1, 2 };
            for (size_t i = 0; i < 3; ++ i) {
                GLVolume &volume = *volumes[i];
                volume.bounding_box = ctxt.bbox;
                volume.qverts.reserve(ctxt.alloc_size_reserve());
                volume.tverts.reserve(ctxt.alloc_size_reserve());
            }
            for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
                const Layer *layer = ctxt.layers[idx_layer];
                for (size_t i = 0; i < 3; ++ i) {
                    GLVolume &vol = *volumes[vols[i]];
                    if (vol.print_zs.empty() || vol.print_zs.back() != layer->print_z) {
                        vol.print_zs.push_back(layer->print_z);
                        vol.offsets.push_back(vol.qverts.size());
                        vol.offsets.push_back(vol.tverts.size());
                    }
                }
                for (const Point &copy: *ctxt.shifted_copies) {
                    for (const LayerRegion *layerm : layer->regions) {
                        if (ctxt.has_perimeters)
                            extrusionentity_to_verts(layerm->perimeters, float(layer->print_z), copy, *volumes[vols[0]]);
                        if (ctxt.has_infill)
                            extrusionentity_to_verts(layerm->fills, float(layer->print_z), copy, *volumes[vols[1]]);
                    }
                    if (ctxt.has_support) {
                        const SupportLayer *support_layer = dynamic_cast<const SupportLayer*>(layer);
                        if (support_layer) {
                            extrusionentity_to_verts(support_layer->support_fills, float(layer->print_z), copy, *volumes[vols[2]]);
                            extrusionentity_to_verts(support_layer->support_interface_fills, float(layer->print_z), copy, *volumes[vols[2]]);
                        }
                    }
                }
                for (size_t i = 0; i < 3; ++ i) {
                    GLVolume &vol = *volumes[vols[i]];
                    if (vol.qverts.size() > ctxt.alloc_size_max() || vol.tverts.size() > ctxt.alloc_size_max()) {
                        // Shrink the old vectors to preserve memory.
                        vol.qverts.shrink_to_fit();
                        vol.tverts.shrink_to_fit();
                        // Store the vertex arrays and restart their containers.
                        vols[i] = volumes.size();
                        volumes.emplace_back(new GLVolume(vol.color));
                        GLVolume &vol_new = *volumes.back();
                        vol_new.bounding_box = ctxt.bbox;
                        vol_new.qverts.reserve(ctxt.alloc_size_reserve());
                        vol_new.tverts.reserve(ctxt.alloc_size_reserve());
                    }
                }
            }
            while (! volumes.empty() && volumes.back()->empty()) {
                delete volumes.back();
                volumes.pop_back();
            }
        });

    BOOST_LOG_TRIVIAL(debug) << "Loading print object toolpaths in parallel - merging results";

    size_t volume_ptr  = volumes->volumes.size();
    size_t num_volumes = volume_ptr;
    for (const GLVolumeCollection &v : volumes_per_thread)
        num_volumes += v.volumes.size();
    volumes->volumes.resize(num_volumes, nullptr);
    for (GLVolumeCollection &v : volumes_per_thread) {
        memcpy(volumes->volumes.data() + volume_ptr, v.volumes.data(), v.volumes.size() * sizeof(void*));
        volume_ptr += v.volumes.size();
        v.volumes.clear();
    }
 
    BOOST_LOG_TRIVIAL(debug) << "Loading print object toolpaths in parallel - end"; 
}

}
