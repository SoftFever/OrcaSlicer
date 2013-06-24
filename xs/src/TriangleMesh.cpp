#include "TriangleMesh.hpp"

TriangleMesh::TriangleMesh() {}
TriangleMesh::~TriangleMesh() {
    stl_close(&stl);
}

void
TriangleMesh::ReadSTLFile(char* input_file) {
    stl_open(&stl, input_file);
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
                printf("Checking nearby. Tolerance= %f Iteration=%d of %d...", tolerance, i + 1, iterations);
                stl_check_facets_nearby(&stl, tolerance);
                printf("  Fixed %d edges.\n", stl.stats.edges_fixed - last_edges_fixed);
                last_edges_fixed = stl.stats.edges_fixed;
                tolerance += increment;
            } else {
                printf("All facets connected.  No further nearby check necessary.\n");
                break;
            }
        }
    } else {
        printf("All facets connected.  No nearby check necessary.\n");
    }
    
    // remove_unconnected
    if (stl.stats.connected_facets_3_edge <  stl.stats.number_of_facets) {
        printf("Removing unconnected facets...\n");
        stl_remove_unconnected_facets(&stl);
    } else
        printf("No unconnected need to be removed.\n");
    
    // fill_holes
    if (stl.stats.connected_facets_3_edge < stl.stats.number_of_facets) {
        printf("Filling holes...\n");
        stl_fill_holes(&stl);
    } else
        printf("No holes need to be filled.\n");
    
    // normal_directions
    printf("Checking normal directions...\n");
    stl_fix_normal_directions(&stl);
    
    // normal_values
    printf("Checking normal values...\n");
    stl_fix_normal_values(&stl);
}

void
TriangleMesh::WriteOBJFile(char* output_file) {
    stl_generate_shared_vertices(&stl);
    stl_write_obj(&stl, output_file);
}
