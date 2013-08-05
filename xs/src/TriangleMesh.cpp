#include "TriangleMesh.hpp"

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
        facet.normal.x = NULL;
        facet.normal.y = NULL;
        facet.normal.z = NULL;
        for (unsigned int v = 0; v <= 2; v++) {
            AV* vertex_av = (AV*)SvRV(*av_fetch(vertices_av, SvIV(*av_fetch(facet_av, v, 0)), 0));
            facet.vertex[v].x = SvNV(*av_fetch(vertex_av, 0, 0));
            facet.vertex[v].y = SvNV(*av_fetch(vertex_av, 1, 0));
            facet.vertex[v].z = SvNV(*av_fetch(vertex_av, 2, 0));
        }
        facet.extra[0] = NULL;
        facet.extra[1] = NULL;
        
        stl.facet_start[i] = facet;
    }
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

}
