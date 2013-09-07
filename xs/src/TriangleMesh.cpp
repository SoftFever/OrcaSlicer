#include "TriangleMesh.hpp"
#include <vector>
#include <map>
#include <utility>
#include <algorithm>
#include <math.h>
#include <assert.h>

namespace Slic3r {

TriangleMesh::TriangleMesh() {}
TriangleMesh::~TriangleMesh() {
    stl_close(&stl);
}

void
TriangleMesh::ReadSTLFile(char* input_file) {
    stl_open(&stl, input_file);
}

void TriangleMesh::ReadFromPerl(SV* vertices, SV* facets)
{
    stl_initialize(&stl);
    stl.stats.type = inmemory;
    
    // count facets and allocate memory
    AV* facets_av = (AV*)SvRV(facets);
    stl.stats.number_of_facets = av_len(facets_av)+1;
    stl.stats.original_num_facets = stl.stats.number_of_facets;
    stl_allocate(&stl);
    
    // read geometry
    AV* vertices_av = (AV*)SvRV(vertices);
    for (unsigned int i = 0; i < stl.stats.number_of_facets; i++) {
        AV* facet_av = (AV*)SvRV(*av_fetch(facets_av, i, 0));
        stl_facet facet;
        facet.normal.x = 0;
        facet.normal.y = 0;
        facet.normal.z = 0;
        for (unsigned int v = 0; v <= 2; v++) {
            AV* vertex_av = (AV*)SvRV(*av_fetch(vertices_av, SvIV(*av_fetch(facet_av, v, 0)), 0));
            facet.vertex[v].x = SvNV(*av_fetch(vertex_av, 0, 0));
            facet.vertex[v].y = SvNV(*av_fetch(vertex_av, 1, 0));
            facet.vertex[v].z = SvNV(*av_fetch(vertex_av, 2, 0));
        }
        facet.extra[0] = 0;
        facet.extra[1] = 0;
        
        stl.facet_start[i] = facet;
    }
    
    stl_get_size(&(this->stl));
}

void
TriangleMesh::Repair() {
    int i;
    
    // checking exact
    stl_check_facets_exact(&stl);
    stl.stats.facets_w_1_bad_edge = (stl.stats.connected_facets_2_edge - stl.stats.connected_facets_3_edge);
    stl.stats.facets_w_2_bad_edge = (stl.stats.connected_facets_1_edge - stl.stats.connected_facets_2_edge);
    stl.stats.facets_w_3_bad_edge = (stl.stats.number_of_facets - stl.stats.connected_facets_1_edge);
    
    // checking nearby
    int last_edges_fixed = 0;
    float tolerance = stl.stats.shortest_edge;
    float increment = stl.stats.bounding_diameter / 10000.0;
    int iterations = 2;
    if (stl.stats.connected_facets_3_edge < stl.stats.number_of_facets) {
        for (i = 0; i < iterations; i++) {
            if (stl.stats.connected_facets_3_edge < stl.stats.number_of_facets) {
                //printf("Checking nearby. Tolerance= %f Iteration=%d of %d...", tolerance, i + 1, iterations);
                stl_check_facets_nearby(&stl, tolerance);
                //printf("  Fixed %d edges.\n", stl.stats.edges_fixed - last_edges_fixed);
                last_edges_fixed = stl.stats.edges_fixed;
                tolerance += increment;
            } else {
                break;
            }
        }
    }
    
    // remove_unconnected
    if (stl.stats.connected_facets_3_edge <  stl.stats.number_of_facets) {
        stl_remove_unconnected_facets(&stl);
    }
    
    // fill_holes
    if (stl.stats.connected_facets_3_edge < stl.stats.number_of_facets) {
        stl_fill_holes(&stl);
    }
    
    // normal_directions
    stl_fix_normal_directions(&stl);
    
    // normal_values
    stl_fix_normal_values(&stl);
    
    // always calculate the volume and reverse all normals if volume is negative
    stl_calculate_volume(&stl);
    
    // neighbors
    stl_verify_neighbors(&stl);
}

void
TriangleMesh::WriteOBJFile(char* output_file) {
    stl_generate_shared_vertices(&stl);
    stl_write_obj(&stl, output_file);
}

void TriangleMesh::scale(float factor)
{
    stl_scale(&(this->stl), factor);
}

void TriangleMesh::translate(float x, float y, float z)
{
    stl_translate(&(this->stl), x, y, z);
}

void TriangleMesh::align_to_origin()
{
    this->translate(
        -(this->stl.stats.min.x),
        -(this->stl.stats.min.y),
        -(this->stl.stats.min.z)
    );
}

void TriangleMesh::rotate(double angle, Point* center)
{
    this->translate(-center->x, -center->y, 0);
    stl_rotate_z(&(this->stl), (float)angle);
    this->translate(+center->x, +center->y, 0);
}

std::vector<Polygons>*
TriangleMesh::slice(const std::vector<double> &z)
{
    /*
       This method gets called with a list of Z coordinates and outputs
       a vector pointer having the same number of items as the original list.
       Each item is a vector of polygons created by slicing our mesh at the 
       given heights.
       
       This method should basically combine the behavior of the existing
       Perl methods defined in lib/Slic3r/TriangleMesh.pm:
       
       - analyze(): this creates the 'facets_edges' and the 'edges_facets'
            tables (we don't need the 'edges' table)
       
       - slice_facet(): this has to be done for each facet. It generates 
            intersection lines with each plane identified by the Z list.
            The get_layer_range() binary search used to identify the Z range
            of the facet is already ported to C++ (see Object.xsp)
       
       - make_loops(): this has to be done for each layer. It creates polygons
            from the lines generated by the previous step.
        
        At the end, we free the tables generated by analyze() as we don't 
        need them anymore.
        FUTURE: parallelize slice_facet() and make_loops()
    */
    
    // build a table to map a facet_idx to its three edge indices
    if (this->stl.v_shared == NULL) stl_generate_shared_vertices(&(this->stl));
    typedef std::pair<int,int>              t_edge;
    typedef std::vector<t_edge>             t_edges;  // edge_idx => a_id,b_id
    typedef std::map<t_edge,int>            t_edges_map;  // a_id,b_id => edge_idx
    typedef std::vector< std::vector<int> > t_facets_edges;
    t_edges        edges;
    t_facets_edges facets_edges;
    
    // reserve() instad of resize() because otherwise we couldn't read .size() below to assign edge_idx
    edges.reserve(this->stl.stats.number_of_facets * 3);  // number of edges = number of facets * 3
    facets_edges.resize(this->stl.stats.number_of_facets);
    
    {
        t_edges_map edges_map;
        for (int facet_idx = 0; facet_idx < this->stl.stats.number_of_facets; facet_idx++) {
            facets_edges[facet_idx].resize(3);
            for (int i = 0; i <= 2; i++) {
                int a_id = this->stl.v_indices[facet_idx].vertex[i];
                int b_id = this->stl.v_indices[facet_idx].vertex[(i+1) % 3];
                
                int edge_idx;
                t_edges_map::const_iterator my_edge = edges_map.find(std::make_pair(b_id,a_id));
                if (my_edge == edges_map.end()) {
                    // edge isn't listed in table, so we insert it
                    edge_idx = edges.size();
                    edges.push_back(std::make_pair(a_id,b_id));
                    edges_map[ edges[edge_idx] ] = edge_idx;
                } else {
                    edge_idx = my_edge->second;
                }
                facets_edges[facet_idx][i] = edge_idx;
                
                #ifdef SLIC3R_DEBUG
                printf("  [facet %d, edge %d] a_id = %d, b_id = %d   --> edge %d\n", facet_idx, i, a_id, b_id, edge_idx);
                #endif
            }
        }
    }
    
    std::vector<IntersectionLines> lines(z.size());
    
    for (int facet_idx = 0; facet_idx < this->stl.stats.number_of_facets; facet_idx++) {
        stl_facet facet = this->stl.facet_start[facet_idx];  // this is a copy
        
        /* reorder vertices so that the first one is the one with lowest Z
           this is needed to get all intersection lines in a consistent order
           (external on the right of the line) */
        /*
        float min_z;
        if (facet.vertex[1].z < facet.vertex[0].z && facet.vertex[1].z < facet.vertex[2].z) {
            // vertex 1 has lowest Z
            min_z = facet.vertex[1].z;
            stl_vertex v0 = facet.vertex[0];
            facet.vertex[0] = facet.vertex[1];
            facet.vertex[1] = facet.vertex[2];
            facet.vertex[2] = v0;
        } else if (facet.vertex[2].z < facet.vertex[0].z && facet.vertex[2].z < facet.vertex[1].z) {
            // vertex 2 has lowest Z
            min_z = facet.vertex[2].z;
            stl_vertex v0 = facet.vertex[0];
            facet.vertex[0] = facet.vertex[2];
            facet.vertex[2] = facet.vertex[1];
            facet.vertex[1] = v0;
        } else {
            min_z = facet.vertex[0].z;
        }
        */
        float min_z = fminf(facet.vertex[0].z, fminf(facet.vertex[1].z, facet.vertex[2].z));
        float max_z = fmaxf(facet.vertex[0].z, fmaxf(facet.vertex[1].z, facet.vertex[2].z));
        
        #ifdef SLIC3R_DEBUG
        printf("\n==> FACET %d (%f,%f,%f - %f,%f,%f - %f,%f,%f):\n", facet_idx,
            facet.vertex[0].x, facet.vertex[0].y, facet.vertex[0].z,
            facet.vertex[1].x, facet.vertex[1].y, facet.vertex[1].z,
            facet.vertex[2].x, facet.vertex[2].y, facet.vertex[2].z);
        printf("z: min = %.2f, max = %.2f\n", min_z, max_z);
        #endif
        
        if (min_z == max_z) {
            #ifdef SLIC3R_DEBUG
            printf("Facet is horizontal; ignoring\n");
            #endif
            continue;
        }
        
        std::vector<double>::const_iterator min_layer, max_layer;
        min_layer = std::lower_bound(z.begin(), z.end(), min_z); // first layer whose slice_z is >= min_z
        max_layer = std::upper_bound(z.begin() + (min_layer - z.begin()), z.end(), max_z) - 1; // last layer whose slice_z is <= max_z
        #ifdef SLIC3R_DEBUG
        printf("layers: min = %d, max = %d\n", (int)(min_layer - z.begin()), (int)(max_layer - z.begin()));
        #endif
        
        for (std::vector<double>::const_iterator it = min_layer; it != max_layer + 1; ++it) {
            std::vector<double>::size_type layer_idx = it - z.begin();
            double slice_z = *it;
            std::vector<IntersectionPoint> points;
            std::vector< std::vector<IntersectionPoint>::size_type > points_on_layer, intersection_points;
            
            for (int i = 0; i <= 2; i++) {  // loop through facet edges
                int edge_id = facets_edges[facet_idx][i];
                t_edge edge = edges[edge_id];
                
                stl_vertex* a = &(this->stl.v_shared[edge.first]);
                stl_vertex* b = &(this->stl.v_shared[edge.second]);
                
                #ifdef SLIC3R_DEBUG
                printf("  a = %f, b = %f, slice_z = %f\n", a->z, b->z, slice_z);
                #endif
                
                if (a->z == b->z && a->z == slice_z) {
                    // edge is horizontal and belongs to the current layer
                    #ifdef SLIC3R_DEBUG
                    printf("Edge is horizontal!\n");
                    #endif
                    
                    /* We assume that this method is never being called for horizontal
                       facets, so no other edge is going to be on this layer. */
                    IntersectionLine line;
                    line.a.x    = a->x;
                    line.a.y    = a->y;
                    line.b.x    = b->x;
                    line.b.y    = b->y;
                    line.a_id   = edge.first;
                    line.b_id   = edge.second;
                    
                    if (this->stl.v_indices[facet_idx].vertex[0] < slice_z
                        || this->stl.v_indices[facet_idx].vertex[1] < slice_z
                        || this->stl.v_indices[facet_idx].vertex[2] < slice_z) {
                        line.edge_type = feTop;
                    } else {
                        line.edge_type = feBottom;
                    }
                    lines[layer_idx].push_back(line);
                } else if (a->z == slice_z) {
                    #ifdef SLIC3R_DEBUG
                    printf("A point on plane!\n");
                    #endif
                    
                    IntersectionPoint point;
                    point.x         = a->x;
                    point.y         = a->y;
                    point.point_id  = edge.first;
                    points.push_back(point);
                    points_on_layer.push_back(points.size()-1);
                } else if (b->z == slice_z) {
                    #ifdef SLIC3R_DEBUG
                    printf("B point on plane!\n");
                    #endif
                    
                    IntersectionPoint point;
                    point.x         = b->x;
                    point.y         = b->y;
                    point.point_id  = edge.second;
                    points.push_back(point);
                    points_on_layer.push_back(points.size()-1);
                } else if ((a->z < slice_z && b->z > slice_z) || (b->z < slice_z && a->z > slice_z)) {
                    // edge intersects the current layer; calculate intersection
                    #ifdef SLIC3R_DEBUG
                    printf("Intersects!\n");
                    #endif
                    
                    IntersectionPoint point;
                    point.x         = b->x + (a->x - b->x) * (slice_z - b->z) / (a->z - b->z);
                    point.y         = b->y + (a->y - b->y) * (slice_z - b->z) / (a->z - b->z);
                    point.edge_id  = edge_id;
                    points.push_back(point);
                    intersection_points.push_back(points.size()-1);
                }
            }
            
            if (points_on_layer.size() == 2) {
                if (intersection_points.size() == 1) {
                    
                } else if (intersection_points.empty()) {
                    if (points[ points_on_layer[0] ].coincides_with(&points[ points_on_layer[1] ])) continue;
                }
            }
            
            if (!points.empty()) {
                assert(points.size() == 2); // facets must intersect each plane 0 or 2 times
                
                IntersectionLine line;
                line.a.x        = points[1].x;
                line.a.y        = points[1].y;
                line.b.x        = points[0].x;
                line.b.y        = points[0].y;
                line.a_id       = points[1].point_id;
                line.b_id       = points[0].point_id;
                line.edge_a_id  = points[1].edge_id;
                line.edge_b_id  = points[0].edge_id;
                lines[layer_idx].push_back(line);
            }
        }
    }
    
    // build loops
    std::vector<Polygons>* layers = new std::vector<Polygons>(z.size());
    for (std::vector<IntersectionLines>::const_iterator it = lines.begin(); it != lines.end(); ++it) {
        
    }
    
    // ...
    // add a Polygon p to layer n:
    // (*layers)[n].push_back(p);
    
    return layers;
}

}
