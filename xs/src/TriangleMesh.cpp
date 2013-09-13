#include "TriangleMesh.hpp"
#include <queue>
#include <deque>
#include <set>
#include <vector>
#include <map>
#include <utility>
#include <algorithm>
#include <math.h>
#include <assert.h>

namespace Slic3r {

TriangleMesh::TriangleMesh()
    : repaired(false)
{
    stl_initialize(&this->stl);
}

TriangleMesh::TriangleMesh(const TriangleMesh &other)
    : stl(other.stl), repaired(other.repaired)
{
    this->stl.heads = NULL;
    this->stl.tail  = NULL;
    if (other.stl.facet_start != NULL) {
        this->stl.facet_start = new stl_facet[other.stl.stats.number_of_facets];
        std::copy(other.stl.facet_start, other.stl.facet_start + other.stl.stats.number_of_facets, this->stl.facet_start);
    }
    if (other.stl.neighbors_start != NULL) {
        this->stl.neighbors_start = new stl_neighbors[other.stl.stats.number_of_facets];
        std::copy(other.stl.neighbors_start, other.stl.neighbors_start + other.stl.stats.number_of_facets, this->stl.neighbors_start);
    }
    if (other.stl.v_indices != NULL) {
        this->stl.v_indices = new v_indices_struct[other.stl.stats.number_of_facets];
        std::copy(other.stl.v_indices, other.stl.v_indices + other.stl.stats.number_of_facets, this->stl.v_indices);
    }
    if (other.stl.v_shared != NULL) {
        this->stl.v_shared = new stl_vertex[other.stl.stats.shared_vertices];
        std::copy(other.stl.v_shared, other.stl.v_shared + other.stl.stats.shared_vertices, this->stl.v_shared);
    }
}

TriangleMesh::~TriangleMesh() {
    stl_close(&this->stl);
}

void
TriangleMesh::ReadSTLFile(char* input_file) {
    stl_open(&stl, input_file);
}

void
TriangleMesh::write_ascii(char* output_file)
{
    stl_write_ascii(&this->stl, output_file, "");
}

void
TriangleMesh::write_binary(char* output_file)
{
    stl_write_binary(&this->stl, output_file, "");
}


void
TriangleMesh::repair() {
    if (this->repaired) return;
    
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
        for (int i = 0; i < iterations; i++) {
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
    
    this->repaired = true;
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

void TriangleMesh::scale(std::vector<double> versor)
{
    float fversor[3];
    fversor[0] = versor[0];
    fversor[1] = versor[1];
    fversor[2] = versor[2];
    stl_scale(&this->stl, fversor);
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
    t_facets_edges facets_edges;
    
    facets_edges.resize(this->stl.stats.number_of_facets);
    
    {
        t_edges edges;
        // reserve() instad of resize() because otherwise we couldn't read .size() below to assign edge_idx
        edges.reserve(this->stl.stats.number_of_facets * 3);  // number of edges = number of facets * 3
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
        stl_facet* facet = &(this->stl.facet_start[facet_idx]);
        float min_z = fminf(facet->vertex[0].z, fminf(facet->vertex[1].z, facet->vertex[2].z));
        float max_z = fmaxf(facet->vertex[0].z, fmaxf(facet->vertex[1].z, facet->vertex[2].z));
        
        #ifdef SLIC3R_DEBUG
        printf("\n==> FACET %d (%f,%f,%f - %f,%f,%f - %f,%f,%f):\n", facet_idx,
            facet->vertex[0].x, facet->vertex[0].y, facet->vertex[0].z,
            facet->vertex[1].x, facet->vertex[1].y, facet->vertex[1].z,
            facet->vertex[2].x, facet->vertex[2].y, facet->vertex[2].z);
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
            bool found_horizontal_edge = false;
            
            /* reorder vertices so that the first one is the one with lowest Z
               this is needed to get all intersection lines in a consistent order
               (external on the right of the line) */
            int i = 0;
            if (facet->vertex[1].z == min_z) {
                // vertex 1 has lowest Z
                i = 1;
            } else if (facet->vertex[2].z == min_z) {
                // vertex 2 has lowest Z
                i = 2;
            }
            for (int j = i; (j-i) < 3; j++) {  // loop through facet edges
                int edge_id = facets_edges[facet_idx][j % 3];
                int a_id = this->stl.v_indices[facet_idx].vertex[j % 3];
                int b_id = this->stl.v_indices[facet_idx].vertex[(j+1) % 3];
                stl_vertex* a = &(this->stl.v_shared[a_id]);
                stl_vertex* b = &(this->stl.v_shared[b_id]);
                
                if (a->z == b->z && a->z == slice_z) {
                    // edge is horizontal and belongs to the current layer
                    #ifdef SLIC3R_DEBUG
                    printf("Edge is horizontal!\n");
                    #endif
                    
                    /* We assume that this method is never being called for horizontal
                       facets, so no other edge is going to be on this layer. */
                    IntersectionLine line;
                    if (this->stl.v_indices[facet_idx].vertex[0] < slice_z
                        || this->stl.v_indices[facet_idx].vertex[1] < slice_z
                        || this->stl.v_indices[facet_idx].vertex[2] < slice_z) {
                        line.edge_type = feTop;
                        std::swap(a, b);
                        std::swap(a_id, b_id);
                    } else {
                        line.edge_type = feBottom;
                    }
                    line.a.x    = a->x;
                    line.a.y    = a->y;
                    line.b.x    = b->x;
                    line.b.y    = b->y;
                    line.a_id   = a_id;
                    line.b_id   = b_id;
                    
                    lines[layer_idx].push_back(line);
                    found_horizontal_edge = true;
                    break;
                } else if (a->z == slice_z) {
                    #ifdef SLIC3R_DEBUG
                    printf("A point on plane!\n");
                    #endif
                    
                    IntersectionPoint point;
                    point.x         = a->x;
                    point.y         = a->y;
                    point.point_id  = a_id;
                    points.push_back(point);
                    points_on_layer.push_back(points.size()-1);
                } else if (b->z == slice_z) {
                    #ifdef SLIC3R_DEBUG
                    printf("B point on plane!\n");
                    #endif
                    
                    IntersectionPoint point;
                    point.x         = b->x;
                    point.y         = b->y;
                    point.point_id  = b_id;
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
                    point.edge_id   = edge_id;
                    points.push_back(point);
                    intersection_points.push_back(points.size()-1);
                }
            }
            if (found_horizontal_edge) continue;
            
            if (points_on_layer.size() == 2) {
                if (intersection_points.size() == 1) {
                    points.erase( points.begin() + points_on_layer[1] );
                } else if (intersection_points.empty()) {
                    if (points[ points_on_layer[0] ].coincides_with(&points[ points_on_layer[1] ])) {
                        continue;
                    }
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
    for (std::vector<IntersectionLines>::iterator it = lines.begin(); it != lines.end(); ++it) {
        int layer_idx = it - lines.begin();
        
        // remove tangent edges
        for (IntersectionLines::iterator line = it->begin(); line != it->end(); ++line) {
            if (line->skip || line->edge_type == feNone) continue;
            
            /* if the line is a facet edge, find another facet edge
               having the same endpoints but in reverse order */
            for (IntersectionLines::iterator line2 = line + 1; line2 != it->end(); ++line2) {
                if (line2->skip || line2->edge_type == feNone) continue;
                
                // are these facets adjacent? (sharing a common edge on this layer)
                if (line->a_id == line2->a_id && line->b_id == line2->b_id) {
                    line2->skip = true;
                    
                    /* if they are both oriented upwards or downwards (like a 'V')
                       then we can remove both edges from this layer since it won't 
                       affect the sliced shape */
                    /* if one of them is oriented upwards and the other is oriented
                       downwards, let's only keep one of them (it doesn't matter which
                       one since all 'top' lines were reversed at slicing) */
                    if (line->edge_type == line2->edge_type) {
                        line->skip = true;
                        break;
                    }
                }
            }
        }
        
        // build a map of lines by edge_a_id and a_id
        std::vector<IntersectionLinePtrs> by_edge_a_id, by_a_id;
        by_edge_a_id.resize(this->stl.stats.number_of_facets * 3);
        by_a_id.resize(this->stl.stats.shared_vertices);
        for (IntersectionLines::iterator line = it->begin(); line != it->end(); ++line) {
            if (line->skip) continue;
            if (line->edge_a_id != -1) by_edge_a_id[line->edge_a_id].push_back(&(*line));
            if (line->a_id != -1) by_a_id[line->a_id].push_back(&(*line));
        }
        
        CYCLE: while (1) {
            // take first spare line and start a new loop
            IntersectionLine* first_line = NULL;
            for (IntersectionLines::iterator line = it->begin(); line != it->end(); ++line) {
                if (line->skip) continue;
                first_line = &(*line);
                break;
            }
            if (first_line == NULL) break;
            first_line->skip = true;
            IntersectionLinePtrs loop;
            loop.push_back(first_line);
            
            /*
            printf("first_line edge_a_id = %d, edge_b_id = %d, a_id = %d, b_id = %d, a = %d,%d, b = %d,%d\n", 
                first_line->edge_a_id, first_line->edge_b_id, first_line->a_id, first_line->b_id,
                first_line->a.x, first_line->a.y, first_line->b.x, first_line->b.y);
            */
            
            while (1) {
                // find a line starting where last one finishes
                IntersectionLine* next_line = NULL;
                if (loop.back()->edge_b_id != -1) {
                    IntersectionLinePtrs* candidates = &(by_edge_a_id[loop.back()->edge_b_id]);
                    for (IntersectionLinePtrs::iterator lineptr = candidates->begin(); lineptr != candidates->end(); ++lineptr) {
                        if ((*lineptr)->skip) continue;
                        next_line = *lineptr;
                        break;
                    }
                }
                if (next_line == NULL && loop.back()->b_id != -1) {
                    IntersectionLinePtrs* candidates = &(by_a_id[loop.back()->b_id]);
                    for (IntersectionLinePtrs::iterator lineptr = candidates->begin(); lineptr != candidates->end(); ++lineptr) {
                        if ((*lineptr)->skip) continue;
                        next_line = *lineptr;
                        break;
                    }
                }
                
                if (next_line == NULL) {
                    // check whether we closed this loop
                    if ((loop.front()->edge_a_id != -1 && loop.front()->edge_a_id == loop.back()->edge_b_id)
                        || (loop.front()->a_id != -1 && loop.front()->a_id == loop.back()->b_id)) {
                        // loop is complete
                        Polygon p;
                        p.points.reserve(loop.size());
                        for (IntersectionLinePtrs::iterator lineptr = loop.begin(); lineptr != loop.end(); ++lineptr) {
                            p.points.push_back((*lineptr)->a);
                        }
                        (*layers)[layer_idx].push_back(p);
                        
                        #ifdef SLIC3R_DEBUG
                        printf("  Discovered %s polygon of %d points\n", (p.is_counter_clockwise() ? "ccw" : "cw"), (int)p.points.size());
                        #endif
                        
                        goto CYCLE;
                    }
                    
                    // we can't close this loop!
                    //// push @failed_loops, [@loop];
                    goto CYCLE;
                }
                /*
                printf("next_line edge_a_id = %d, edge_b_id = %d, a_id = %d, b_id = %d, a = %d,%d, b = %d,%d\n", 
                    next_line->edge_a_id, next_line->edge_b_id, next_line->a_id, next_line->b_id,
                    next_line->a.x, next_line->a.y, next_line->b.x, next_line->b.y);
                */
                loop.push_back(next_line);
                next_line->skip = true;
            }
        }
    }
    
    return layers;
}

TriangleMeshPtrs
TriangleMesh::split() const
{
    TriangleMeshPtrs meshes;
    std::set<int> seen_facets;
    
    // we need neighbors
    if (!this->repaired) CONFESS("split() requires repair()");
    
    // loop while we have remaining facets
    while (1) {
        // get the first facet
        std::queue<int> facet_queue;
        std::deque<int> facets;
        for (int facet_idx = 0; facet_idx < this->stl.stats.number_of_facets; facet_idx++) {
            if (seen_facets.find(facet_idx) == seen_facets.end()) {
                // if facet was not seen put it into queue and start searching
                facet_queue.push(facet_idx);
                break;
            }
        }
        if (facet_queue.empty()) break;
        
        while (!facet_queue.empty()) {
            int facet_idx = facet_queue.front();
            facet_queue.pop();
            if (seen_facets.find(facet_idx) != seen_facets.end()) continue;
            facets.push_back(facet_idx);
            for (int j = 0; j <= 2; j++) {
                facet_queue.push(this->stl.neighbors_start[facet_idx].neighbor[j]);
            }
            seen_facets.insert(facet_idx);
        }
        
        TriangleMesh* mesh = new TriangleMesh;
        meshes.push_back(mesh);
        mesh->stl.stats.type = inmemory;
        mesh->stl.stats.number_of_facets = facets.size();
        mesh->stl.stats.original_num_facets = mesh->stl.stats.number_of_facets;
        stl_allocate(&mesh->stl);
        
        for (std::deque<int>::const_iterator facet = facets.begin(); facet != facets.end(); facet++) {
            mesh->stl.facet_start[facet - facets.begin()] = this->stl.facet_start[*facet];
        }
    }
    
    return meshes;
}

void
TriangleMesh::merge(const TriangleMesh* mesh)
{
    // reset stats and metadata
    int number_of_facets = this->stl.stats.number_of_facets;
    stl_invalidate_shared_vertices(&this->stl);
    this->repaired = false;
    
    // update facet count and allocate more memory
    this->stl.stats.number_of_facets = number_of_facets + mesh->stl.stats.number_of_facets;
    this->stl.stats.original_num_facets = this->stl.stats.number_of_facets;
    stl_reallocate(&this->stl);
    
    // copy facets
    for (int i = 0; i < mesh->stl.stats.number_of_facets; i++) {
        this->stl.facet_start[number_of_facets + i] = mesh->stl.facet_start[i];
    }
    
    // update size
    stl_get_size(&this->stl);
}

#ifdef SLIC3RXS
SV*
TriangleMesh::to_SV() {
    SV* sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::TriangleMesh", (void*)this );
    return sv;
}

void TriangleMesh::ReadFromPerl(SV* vertices, SV* facets)
{
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
#endif

}
